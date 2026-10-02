#include "backend/llvm/emitter.h"
#include "cpu/cpu.h"
#include <cstdio>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>

namespace dolllvm {
using namespace llvm;
BasicBlock *FunctionEmitter::directDestination(const DolIRTerminator &term,
                                               u32 slot) {
  if (term.targets[slot] != DOLIR_NO_BLOCK) {
    if (source_.blocks[term.targets[slot]].terminator.kind ==
        DOLIR_TERM_FALLBACK)
      return fallbackEdge(term.target_addresses[slot]);
    return blocks_[term.targets[slot]];
  }
  return externalDestination(term, slot);
}
const DolLLVMFunctionRange *FunctionEmitter::rangeFor(u32 address) const {
  u32 first = 0;
  u32 last = range_count_;
  while (first < last) {
    const u32 middle = first + (last - first) / 2u;
    const DolLLVMFunctionRange &range = ranges_[middle];
    if (address < range.start)
      last = middle;
    else if (address >= range.end)
      first = middle + 1u;
    else
      return &range;
  }
  return nullptr;
}
const DolLLVMCallEdge *FunctionEmitter::callEdge(const DolIRTerminator &term,
                                                u32 slot) const {
  for (u32 index = 0; index < call_edge_count_; index++) {
    const DolLLVMCallEdge &edge = call_edges_[index];
    if (edge.caller_start == source_.guest_start &&
        edge.callsite_pc == term.guest_pc &&
        edge.callee_address == term.target_addresses[slot])
      return &edge;
  }
  return nullptr;
}
BasicBlock *FunctionEmitter::externalDestination(const DolIRTerminator &term,
                                                 u32 slot) {
  u32 target = term.target_addresses[slot];
  const DolLLVMFunctionRange *range = rangeFor(target);
  if (!range)
    return nullptr;
  const bool nativeTarget =
      (range->abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE) != 0;
  if (native_abi_ && !nativeTarget)
    return nullptr;
  if (!native_abi_ && nativeTarget && cold_escapes_)
    return nullptr;
  if (nativeTarget) {
    for (u32 state = 0; state < DOLIR_STATE_COUNT; state++) {
      auto stateSlot = static_cast<DolIRStateSlot>(state);
      if (stateInput(range, stateSlot) && !state_[state])
        return nullptr;
    }
  }
  BasicBlock *callBlock = BasicBlock::Create(
      context_, term.linked ? "direct_call" : "direct_tail", function_);
  IRBuilderBase::InsertPoint saved = builder_.saveIP();
  builder_.SetInsertPoint(callBlock);
  emitBudgetGuard(target);
  if (nativeTarget && !modern_runtime_) {
    Type *pointer = PointerType::getUnqual(context_);
    Value *hostCall = loadOffset(pointer, offsetof(CPUState, host_call));
    BasicBlock *invoke =
        BasicBlock::Create(context_, "native_call_invoke", function_);
    BasicBlock *query =
        BasicBlock::Create(context_, "native_call_query", function_);
    builder_.CreateCondBr(builder_.CreateIsNull(hostCall), invoke, query);
    builder_.SetInsertPoint(query);
    FunctionCallee available = module_.getOrInsertFunction(
        "ppc_native_region_available",
        FunctionType::get(Type::getInt1Ty(context_),
                          {pointer, Type::getInt32Ty(context_),
                           Type::getInt32Ty(context_)},
                          false));
    Value *canEnter = builder_.CreateCall(
        available, {ctx_, builder_.getInt32(range->start),
                    builder_.getInt32(range->end)});
    BasicBlock *blocked =
        BasicBlock::Create(context_, "native_call_blocked", function_);
    builder_.CreateCondBr(canEnter, invoke, blocked);
    builder_.SetInsertPoint(blocked);
    sideExit(target);
    builder_.SetInsertPoint(invoke);
  }
  char name[64];
  snprintf(name, sizeof(name), "func_%08X_budget", range->start);
  const std::string targetName = symbolName(name);
  if (!nativeTarget)
    syncDirtyState();
  Value *callDepth = nullptr;
  if (modern_runtime_ && nativeTarget) {
    callDepth = builder_.CreateLoad(Type::getInt64Ty(context_), guard_steps_);
    BasicBlock *invoke = BasicBlock::Create(context_, "call_depth_ok", function_);
    BasicBlock *yield = BasicBlock::Create(context_, "call_depth_exit", function_);
    builder_.CreateCondBr(builder_.CreateICmpULT(callDepth, builder_.getInt64(64)),
                          invoke, yield);
    builder_.SetInsertPoint(yield);
    sideExit(target);
    builder_.SetInsertPoint(invoke);
    builder_.CreateStore(builder_.CreateAdd(callDepth, builder_.getInt64(1)), guard_steps_);
  }
  const bool cyclesInResult = nativeTarget && nativeCyclesInResult(range);
  if (!cyclesInResult)
    flushCallCounters(true);
  auto callee = module_.getOrInsertFunction(targetName, bodyFunctionType(range));
  if (auto *calleeFunction = dyn_cast<Function>(callee.getCallee())) {
    calleeFunction->setCallingConv(bodyCallingConvention());
    calleeFunction->setVisibility(modern_runtime_ ? GlobalValue::DefaultVisibility
                                                  : GlobalValue::HiddenVisibility);
    calleeFunction->setDSOLocal(!modern_runtime_);
  }
  Value *calleeReturnPC =
      term.linked ? static_cast<Value *>(builder_.getInt32(term.guest_pc + 4u))
                  : return_pc_;
  Value *control = builder_.CreateOr(
      builder_.CreateZExt(builder_.getInt32(target),
                          Type::getInt64Ty(context_)),
      builder_.CreateShl(
          builder_.CreateZExt(calleeReturnPC, Type::getInt64Ty(context_)),
          builder_.getInt64(32)));
  SmallVector<Value *, 32> arguments = {ctx_};
  if (modern_runtime_)
    arguments.push_back(state_interface_);
  arguments.push_back(chain_);
  arguments.push_back(control);
  if (nativeTarget) {
    if (cyclesInResult)
      arguments.push_back(
          builder_.CreateLoad(Type::getInt64Ty(context_), cycles_));
    for (u32 state = 0; state < DOLIR_STATE_COUNT; state++) {
      auto stateSlot = static_cast<DolIRStateSlot>(state);
      if (stateInput(range, stateSlot))
        arguments.push_back(stateValue(stateSlot));
    }
  }
  if (modern_runtime_ && nativeTarget) {
    const DolLLVMCallEdge *edge = callEdge(term, slot);
    if (edge)
      stageStateMask(edge->may_dirty_before);
    else
      syncDirtyState();
  }
  CallInst *nativeCall = builder_.CreateCall(callee, arguments);
  nativeCall->setCallingConv(bodyCallingConvention());
  if (nativeTarget)
    nativeCall->addFnAttr(Attribute::NoInline);
  if (nativeTarget) {
    if (modern_runtime_ && cold_escapes_) {
      BasicBlock *structuredExit = BasicBlock::Create(context_, "native_call_structured_exit", function_);
      BasicBlock *continued = BasicBlock::Create(context_, "native_call_continue", function_);
      builder_.CreateCondBr(structuredExitPending(), structuredExit, continued);
      builder_.SetInsertPoint(structuredExit);
      returnStructuredExit();
      builder_.SetInsertPoint(continued);
    }
    if (callDepth)
      builder_.CreateStore(callDepth, guard_steps_);
    if (!cyclesInResult)
      reloadCallCounters();
    acceptNativeResult(nativeCall, range);
    if (cold_escapes_) {
      if (!term.linked) {
        if (native_abi_) {
          returnNative(return_pc_);
        } else {
          materialize(return_pc_);
          returnFromBody();
        }
        builder_.restoreIP(saved);
        return callBlock;
      }
      const u32 continuation = term.guest_pc + 4u;
      const bool local = continuation >= source_.guest_start &&
                         continuation < source_.guest_end &&
                         ((continuation - source_.guest_start) & 3u) == 0;
      const u32 continuationBlock =
          local ? (continuation - source_.guest_start) / 4u : 0u;
      if (!local || continuationBlock >= blocks_.size()) {
        sideExit(continuation);
      } else {
        builder_.CreateBr(blocks_[continuationBlock]);
      }
      builder_.restoreIP(saved);
      return callBlock;
    }
    Value *returnedPC = nativeResultPC(nativeCall);
    Value *continues = nativeResultContinues(nativeCall);
    if (!term.linked) {
      if (native_abi_) {
        BasicBlock *forward =
            BasicBlock::Create(context_, "tail_forward", function_);
        BasicBlock *stopped =
            BasicBlock::Create(context_, "tail_materialize", function_);
        builder_.CreateCondBr(continues, forward, stopped);
        builder_.SetInsertPoint(forward);
        returnNative(returnedPC);
        builder_.SetInsertPoint(stopped);
        materialize(returnedPC);
        returnFromBody();
      } else {
        materialize(returnedPC);
        returnFromBody();
      }
      builder_.restoreIP(saved);
      return callBlock;
    }
    const u32 continuation = term.guest_pc + 4u;
    const bool local = continuation >= source_.guest_start &&
                       continuation < source_.guest_end &&
                       ((continuation - source_.guest_start) & 3u) == 0;
    const u32 continuationBlock =
        local ? (continuation - source_.guest_start) / 4u : 0u;
    BasicBlock *resume = BasicBlock::Create(context_, "call_resume", function_);
    BasicBlock *mismatch = BasicBlock::Create(context_, "call_mismatch", function_);
    Value *matches = builder_.CreateAnd(
        continues,
        builder_.CreateICmpEQ(returnedPC, builder_.getInt32(continuation)));
    if (!local || continuationBlock >= blocks_.size())
      matches = builder_.getFalse();
    builder_.CreateCondBr(matches, resume, mismatch);
    builder_.SetInsertPoint(mismatch);
    materialize(returnedPC);
    returnFromBody();
    builder_.SetInsertPoint(resume);
    builder_.CreateBr(blocks_[continuationBlock]);
    builder_.restoreIP(saved);
    return callBlock;
  }
  if (!term.linked) {
    nativeCall->setTailCallKind(CallInst::TCK_MustTail);
    builder_.CreateRetVoid();
    builder_.restoreIP(saved);
    return callBlock;
  }
  u32 continuation = term.guest_pc + 4u;
  u32 continuationBlock = 0;
  bool local = continuation >= source_.guest_start &&
               continuation < source_.guest_end &&
               ((continuation - source_.guest_start) & 3u) == 0;
  if (local)
    continuationBlock = (continuation - source_.guest_start) / 4u;
  BasicBlock *resume = BasicBlock::Create(context_, "call_resume", function_);
  BasicBlock *mismatch = BasicBlock::Create(context_, "call_mismatch", function_);
  Value *returnedPC =
      loadOffset(Type::getInt32Ty(context_), offsetof(CPUState, pc));
  builder_.CreateCondBr(
      builder_.CreateICmpEQ(returnedPC, builder_.getInt32(continuation)),
      resume, mismatch);
  builder_.SetInsertPoint(mismatch);
  builder_.CreateRetVoid();
  builder_.SetInsertPoint(resume);
  if (!local || continuationBlock >= blocks_.size()) {
    builder_.CreateRetVoid();
  } else {
    reloadCallCounters();
    reloadUsedState();
    builder_.CreateBr(blocks_[continuationBlock]);
  }
  builder_.restoreIP(saved);
  return callBlock;
}

BasicBlock *FunctionEmitter::exitDestination(u32 pc) {
  BasicBlock *exit = BasicBlock::Create(context_, "side_exit", function_);
  IRBuilderBase::InsertPoint saved = builder_.saveIP();
  builder_.SetInsertPoint(exit);
  sideExit(pc);
  builder_.restoreIP(saved);
  return exit;
}

BasicBlock *FunctionEmitter::fallbackDestination(
    const DolIRTerminator &terminator) {
  return fallbackEdge(terminator.guest_pc);
}

BasicBlock *FunctionEmitter::fallbackEdge(u32 pc) {
  BasicBlock *edge = BasicBlock::Create(context_, "fallback_edge", function_);
  IRBuilderBase::InsertPoint saved = builder_.saveIP();
  builder_.SetInsertPoint(edge);
  fallback_pc_->addIncoming(builder_.getInt32(pc), edge);
  builder_.CreateBr(fallback_block_);
  builder_.restoreIP(saved);
  return edge;
}

} // namespace dolllvm
