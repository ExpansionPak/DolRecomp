#include "backend/llvm/emitter.h"
#include "backend/llvm/native_abi.h"
#include "cpu/cpu.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>

namespace dolllvm {

using namespace llvm;

void FunctionEmitter::emitColdEntry(BasicBlock *entryMiss) {
  builder_.SetInsertPoint(entryMiss);
  Value *offset = builder_.CreateSub(entry_pc_, builder_.getInt32(source_.guest_start));
  Value *inRange = builder_.CreateICmpULT(
      offset, builder_.getInt32(source_.guest_end - source_.guest_start));
  Value *aligned = builder_.CreateICmpEQ(
      builder_.CreateAnd(offset, builder_.getInt32(3)), builder_.getInt32(0));
  BasicBlock *valid = BasicBlock::Create(context_, "cold_entry", function_);
  BasicBlock *invalid = BasicBlock::Create(context_, "invalid_entry", function_);
  builder_.CreateCondBr(builder_.CreateAnd(inRange, aligned), valid, invalid);

  builder_.SetInsertPoint(invalid);
  if (modern_runtime_) {
    builder_.CreateStore(builder_.getInt32(2),
                         builder_.CreateStructGEP(chainType(), chain_, 7));
    materialize(entry_pc_);
    returnFromBody();
  } else {
    materialize(entry_pc_);
    returnFromBody();
  }

  builder_.SetInsertPoint(valid);
  fallback_pc_->addIncoming(entry_pc_, valid);
  builder_.CreateBr(fallback_block_);
}

void FunctionEmitter::emitFallbackHandler() {
  resetFPRepresentations();
  known_state_.fill(nullptr);
  pending_fprf_ = nullptr;
  fp_available_checked_ = false;
  builder_.SetInsertPoint(fallback_block_);
  if (modern_runtime_) {
    BasicBlock *fallbackExit = BasicBlock::Create(context_, "fallback_exit", function_);
    u32 serviceable = 0;
    for (u32 i = 0; i < source_.block_count; i++)
      serviceable += canInlineFallback(source_.blocks[i]);
    auto *dispatch = builder_.CreateSwitch(fallback_pc_, fallbackExit, serviceable);
    for (u32 i = 0; i < source_.block_count; i++) {
      const DolIRBlock &block = source_.blocks[i];
      if (!canInlineFallback(block))
        continue;

      BasicBlock *service = BasicBlock::Create(context_, "fallback_service", function_);
      dispatch->addCase(builder_.getInt32(block.guest_address), service);
      builder_.SetInsertPoint(service);

      Value *msr = state_[DOLIR_STATE_MSR]
                       ? stateValue(DOLIR_STATE_MSR)
                       : loadContext(DOLIR_STATE_MSR);
      Value *fpEnabled = builder_.CreateICmpNE(
          builder_.CreateAnd(msr, builder_.getInt32(1u << 13)),
          builder_.getInt32(0));
      BasicBlock *invoke = BasicBlock::Create(context_, "fallback_service_invoke", function_);
      builder_.CreateCondBr(fpEnabled, invoke, fallbackExit,
                            MDBuilder(context_).createBranchWeights(2000, 1));
      builder_.SetInsertPoint(invoke);
      if (loop_headers_[i])
        emitBudgetGuard(block.guest_address);
      emitInstructionService(block.guest_address, block.cycle_cost);
      if (i + 1u < source_.block_count)
        builder_.CreateBr(blocks_[i + 1u]);
      else
        sideExit(block.guest_address + 4u);
    }

    builder_.SetInsertPoint(fallbackExit);
    builder_.CreateStore(builder_.getInt32(2),
                         builder_.CreateStructGEP(chainType(), chain_, 7));
    materialize(fallback_pc_);
    returnFromBody();
    return;
  }
  std::vector<Constant *> rawValues;
  std::vector<Constant *> cycleValues;
  rawValues.reserve(source_.block_count);
  cycleValues.reserve(source_.block_count);
  for (u32 i = 0; i < source_.block_count; i++) {
    const DolIRBlock &block = source_.blocks[i];
    rawValues.push_back(builder_.getInt32(block.raw));
    cycleValues.push_back(builder_.getInt32(block.cycle_cost));
  }
  ArrayType *rawType = ArrayType::get(Type::getInt32Ty(context_), source_.block_count);
  auto *rawTable =
      new GlobalVariable(module_, rawType, true, GlobalValue::PrivateLinkage,
                         ConstantArray::get(rawType, rawValues),
                         std::string(source_.name) + "_fallback_raw");
  ArrayType *cycleType = ArrayType::get(Type::getInt32Ty(context_), source_.block_count);
  auto *cycleTable =
      new GlobalVariable(module_, cycleType, true, GlobalValue::PrivateLinkage,
                         ConstantArray::get(cycleType, cycleValues),
                         std::string(source_.name) + "_fallback_cycles");
  Value *index = builder_.CreateZExt(
      builder_.CreateLShr(
          builder_.CreateSub(fallback_pc_,
                             builder_.getInt32(source_.guest_start)),
          builder_.getInt32(2)),
      Type::getInt64Ty(context_));
  Value *raw = builder_.CreateLoad(
      Type::getInt32Ty(context_),
      builder_.CreateInBoundsGEP(rawType, rawTable,
                                 {builder_.getInt64(0), index}));
  Value *cycles = builder_.CreateLoad(
      Type::getInt32Ty(context_),
      builder_.CreateInBoundsGEP(cycleType, cycleTable,
                                 {builder_.getInt64(0), index}));
  chargeCycles(builder_.CreateZExt(cycles, Type::getInt64Ty(context_)));
  materialize(fallback_pc_);
  auto callee = module_.getOrInsertFunction(
      "ppc_fallback_instruction",
      FunctionType::get(Type::getVoidTy(context_),
                        {PointerType::getUnqual(context_),
                         Type::getInt32Ty(context_),
                         Type::getInt32Ty(context_)},
                        false));
  if (auto *function = dyn_cast<Function>(callee.getCallee()))
    function->addFnAttr(Attribute::Cold);
  builder_.CreateCall(callee, {ctx_, raw, fallback_pc_});
  Value *exception =
      loadOffset(Type::getInt32Ty(context_), offsetof(CPUState, exception));
  BasicBlock *resume = BasicBlock::Create(context_, "fallback_resume", function_);
  BasicBlock *done = BasicBlock::Create(context_, "fallback_exit", function_);
  builder_.CreateCondBr(builder_.CreateICmpEQ(exception, builder_.getInt32(0)),
                        resume, done);
  builder_.SetInsertPoint(done);
  returnFromBody();

  builder_.SetInsertPoint(resume);
  reloadUsedState();
  Value *returnedPC =
      loadOffset(Type::getInt32Ty(context_), offsetof(CPUState, pc));
  BasicBlock *repeatCheck =
      BasicBlock::Create(context_, "fallback_repeat_check", function_);
  u32 nativeEntries = 0;
  for (u32 i = 0; i < source_.block_count; i++) {
    nativeEntries += region_leaders_[i] &&
                     source_.blocks[i].terminator.kind != DOLIR_TERM_FALLBACK;
  }
  auto *dispatch =
      builder_.CreateSwitch(returnedPC, repeatCheck, nativeEntries);
  for (u32 i = 0; i < source_.block_count; i++) {
    if (!region_leaders_[i] ||
        source_.blocks[i].terminator.kind == DOLIR_TERM_FALLBACK)
      continue;
    dispatch->addCase(builder_.getInt32(source_.blocks[i].guest_address),
                      blocks_[i]);
  }

  builder_.SetInsertPoint(repeatCheck);
  Value *offset =
      builder_.CreateSub(returnedPC, builder_.getInt32(source_.guest_start));
  Value *inRange = builder_.CreateICmpULT(
      offset, builder_.getInt32(source_.guest_end - source_.guest_start));
  Value *aligned = builder_.CreateICmpEQ(
      builder_.CreateAnd(offset, builder_.getInt32(3)), builder_.getInt32(0));
  fallback_pc_->addIncoming(returnedPC, repeatCheck);
  builder_.CreateCondBr(builder_.CreateAnd(inRange, aligned), fallback_block_,
                        done);
}

bool FunctionEmitter::emitTerminator(const DolIRTerminator &term,
                                     raw_ostream &diagnostics) {
  switch (term.kind) {
  case DOLIR_TERM_FALLTHROUGH:
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
    if (native_abi_) {
      if (!cold_escapes_)
        settleCycles();
      returnNative(target);
    } else {
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
      builder_.CreateStore(builder_.getInt64(0), cycles_);
      builder_.CreateStore(builder_.getInt64(0), pending_cycles_);
      returnFromBody();
    }
    return true;
  }
  case DOLIR_TERM_SIDE_EXIT:
    sideExit(term.target_addresses[0]);
    return true;
  case DOLIR_TERM_FALLBACK: {
    builder_.CreateBr(fallbackDestination(term));
    return true;
  }
  case DOLIR_TERM_RETURN:
    if (native_abi_) {
      if (!cold_escapes_)
        settleCycles();
      returnNative(builder_.getInt32(term.target_addresses[0]));
    } else {
      materialize(term.target_addresses[0]);
      returnFromBody();
    }
    return true;
  case DOLIR_TERM_SYSTEM_CALL: {
    if (modern_runtime_) {
      sideExit(term.guest_pc, 2);
      return true;
    }
    materialize(term.guest_pc);
    auto callee = module_.getOrInsertFunction(
        "ppc_system_call_exception",
        FunctionType::get(
            Type::getVoidTy(context_),
            {PointerType::getUnqual(context_), Type::getInt32Ty(context_)},
            false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt32(term.guest_pc)});
    returnFromBody();
    return true;
  }
  case DOLIR_TERM_RFI: {
    if (modern_runtime_) {
      sideExit(term.guest_pc, 2);
      return true;
    }
    materialize(term.guest_pc);
    auto callee = module_.getOrInsertFunction(
        "ppc_rfi", FunctionType::get(Type::getVoidTy(context_),
                                     {PointerType::getUnqual(context_),
                                      Type::getInt32Ty(context_)},
                                     false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt32(term.guest_pc)});
    returnFromBody();
    return true;
  }
  default:
    diagnostics << "dolllvm: missing terminator at 0x"
                << format_hex_no_prefix(term.guest_pc, 8) << "\n";
    return false;
  }
}

} // namespace dolllvm
