#include "backend/llvm/llvm_function_emitter.h"
#include "cpu/cpu.h"

#include <cstdio>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>

namespace dolllvm {

using namespace llvm;

BasicBlock *FunctionEmitter::directDestination(const DolIRTerminator &term,
                                               u32 slot) {
  if (term.targets[slot] != DOLIR_NO_BLOCK)
    return blocks_[term.targets[slot]];
  return externalDestination(term, slot);
}

// Ranges are sorted by start address and do not overlap, so this is a binary
// search rather than the scan it used to be.
//
// The scan was tolerable while a range was a fixed chunk and there were a few
// thousand of them. Planned regions produce one range per contiguous run, which
// is several times as many, and this is called for every external destination
// in every block -- so the cost is (ranges x edges) and it dominated emission:
// region objects came out at roughly a ninth the rate of fixed chunks until
// this changed.
//
// The caller guarantees the sort. Both producers in pipeline.c emit ranges in
// ascending address order.
const DolLLVMFunctionRange *FunctionEmitter::rangeFor(u32 address) const {
  u32 low = 0;
  u32 high = range_count_;
  while (low < high) {
    u32 mid = low + (high - low) / 2;
    if (address < ranges_[mid].start) {
      high = mid;
    } else if (address >= ranges_[mid].end) {
      low = mid + 1;
    } else {
      return &ranges_[mid];
    }
  }
  return nullptr;
}

BasicBlock *FunctionEmitter::externalDestination(const DolIRTerminator &term,
                                                 u32 slot) {
  u32 target = term.target_addresses[slot];
  const DolLLVMFunctionRange *range = rangeFor(target);
  if (!range)
    return nullptr;
  BasicBlock *callBlock = BasicBlock::Create(
      context_, term.linked ? "direct_call" : "direct_tail", function_);
  IRBuilderBase::InsertPoint saved = builder_.saveIP();
  builder_.SetInsertPoint(callBlock);
  emitBudgetGuard(target);
  materialize(target);
  char name[64];
  snprintf(name, sizeof(name), "func_%08X_budget", range->start);
  auto callee = module_.getOrInsertFunction(
      name, FunctionType::get(Type::getVoidTy(context_),
                              {PointerType::getUnqual(context_),
                               PointerType::getUnqual(context_),
                               PointerType::getUnqual(context_)}, false));
  if (auto *calleeFunction = dyn_cast<Function>(callee.getCallee())) {
    calleeFunction->setVisibility(GlobalValue::HiddenVisibility);
    calleeFunction->setDSOLocal(true);
  }
  builder_.CreateCall(callee, {ctx_, guard_cycles_, guard_steps_});
  if (!term.linked) {
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
  BasicBlock *mismatch =
      BasicBlock::Create(context_, "call_mismatch", function_);
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
    // Only what the continuation actually needs. This used to restore every
    // slot the function touches anywhere, which on a merged region meant tens
    // of loads per call for a continuation that reads a handful.
    reloadLiveState(continuationBlock);
    builder_.CreateStore(builder_.getInt64(0), cycles_);
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

bool FunctionEmitter::emitTerminator(const DolIRTerminator &term,
                                     raw_ostream &diagnostics) {
  switch (term.kind) {
  case DOLIR_TERM_BRANCH: {
    BasicBlock *destination = directDestination(term, 0);
    builder_.CreateBr(destination ? destination
                                  : exitDestination(term.target_addresses[0]));
    return true;
  }
  case DOLIR_TERM_COND_BRANCH: {
    BasicBlock *yes = directDestination(term, 0);
    BasicBlock *no = directDestination(term, 1);
    if (!yes)
      yes = exitDestination(term.target_addresses[0]);
    if (!no)
      no = exitDestination(term.target_addresses[1]);
    builder_.CreateCondBr(values_[term.condition], yes, no);
    return true;
  }
  case DOLIR_TERM_INDIRECT: {
    BasicBlock *taken =
        BasicBlock::Create(context_, "indirect_taken", function_);
    BasicBlock *fallthrough = directDestination(term, 1);
    if (!fallthrough)
      fallthrough = exitDestination(term.target_addresses[1]);
    builder_.CreateCondBr(values_[term.condition], taken, fallthrough);
    builder_.SetInsertPoint(taken);
    Value *target = values_[term.target_value];
    if (!continuations_.empty()) {
      BasicBlock *unknown =
          BasicBlock::Create(context_, "indirect_exit", function_);
      auto *dispatch =
          builder_.CreateSwitch(target, unknown, continuations_.size());
      for (u32 block : continuations_)
        dispatch->addCase(
            builder_.getInt32(source_.blocks[block].guest_address),
            blocks_[block]);
      builder_.SetInsertPoint(unknown);
    }
    for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
      if (dirty_[slot]) {
        auto stateSlot = static_cast<DolIRStateSlot>(slot);
        storeContext(stateSlot,
                     builder_.CreateLoad(type(dolir_state_type(stateSlot)),
                                         state_[slot]));
      }
    }
    storeContext(DOLIR_STATE_PC, target);
    Value *downcount =
        loadOffset(Type::getInt64Ty(context_), offsetof(CPUState, downcount));
    Value *cycles = builder_.CreateLoad(Type::getInt64Ty(context_), cycles_);
    builder_.CreateStore(builder_.CreateSub(downcount, cycles),
                         bytePtr(offsetof(CPUState, downcount)));
    builder_.CreateRetVoid();
    return true;
  }
  case DOLIR_TERM_SIDE_EXIT:
    sideExit(term.target_addresses[0]);
    return true;
  case DOLIR_TERM_FALLBACK: {
    materialize(term.guest_pc);
    auto callee = module_.getOrInsertFunction(
        "ppc_fallback_instruction",
        FunctionType::get(Type::getVoidTy(context_),
                          {PointerType::getUnqual(context_),
                           Type::getInt32Ty(context_),
                           Type::getInt32Ty(context_)},
                          false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt32(term.raw),
                                 builder_.getInt32(term.guest_pc)});
    u32 next = term.guest_pc + 4u;
    u32 nextBlock = 0;
    bool local = next >= source_.guest_start && next < source_.guest_end &&
                 ((next - source_.guest_start) & 3u) == 0;
    if (local)
      nextBlock = (next - source_.guest_start) / 4u;
    if (!local || nextBlock >= blocks_.size()) {
      builder_.CreateRetVoid();
      return true;
    }
    Value *returnedPC =
        loadOffset(Type::getInt32Ty(context_), offsetof(CPUState, pc));
    Value *exception =
        loadOffset(Type::getInt32Ty(context_), offsetof(CPUState, exception));
    Value *resume = builder_.CreateAnd(
        builder_.CreateICmpEQ(returnedPC, builder_.getInt32(next)),
        builder_.CreateICmpEQ(exception, builder_.getInt32(0)));
    BasicBlock *reload =
        BasicBlock::Create(context_, "fallback_resume", function_);
    BasicBlock *done = BasicBlock::Create(context_, "fallback_exit", function_);
    builder_.CreateCondBr(resume, reload, done);
    builder_.SetInsertPoint(done);
    builder_.CreateRetVoid();
    builder_.SetInsertPoint(reload);
    for (u32 state = 0; state < DOLIR_STATE_COUNT; state++) {
      if (!used_[state])
        continue;
      auto stateSlot = static_cast<DolIRStateSlot>(state);
      builder_.CreateStore(loadContext(stateSlot), state_[state]);
    }
    builder_.CreateStore(builder_.getInt64(0), cycles_);
    builder_.CreateBr(blocks_[nextBlock]);
    return true;
  }
  case DOLIR_TERM_RETURN:
    materialize(term.target_addresses[0]);
    builder_.CreateRetVoid();
    return true;
  case DOLIR_TERM_SYSTEM_CALL: {
    materialize(term.guest_pc);
    auto callee = module_.getOrInsertFunction(
        "ppc_system_call_exception",
        FunctionType::get(
            Type::getVoidTy(context_),
            {PointerType::getUnqual(context_), Type::getInt32Ty(context_)},
            false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt32(term.guest_pc)});
    builder_.CreateRetVoid();
    return true;
  }
  case DOLIR_TERM_RFI: {
    materialize(term.guest_pc);
    auto callee = module_.getOrInsertFunction(
        "ppc_rfi", FunctionType::get(Type::getVoidTy(context_),
                                     {PointerType::getUnqual(context_),
                                      Type::getInt32Ty(context_)},
                                     false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt32(term.guest_pc)});
    builder_.CreateRetVoid();
    return true;
  }
  default:
    diagnostics << "dolllvm: missing terminator at 0x"
                << format_hex_no_prefix(term.guest_pc, 8) << "\n";
    return false;
  }
}

} // namespace dolllvm
