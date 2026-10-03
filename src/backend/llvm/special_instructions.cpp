#include "backend/llvm/emitter.h"
#include "cpu/cpu.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>

namespace dolllvm {

using namespace llvm;

void FunctionEmitter::emitStateWrite(const DolIRInstruction &inst) {
  auto slot = static_cast<DolIRStateSlot>(inst.aux);
  Value *value = operand(inst, 0);
  if (slot != DOLIR_STATE_MSR) {
    builder_.CreateStore(value, state_[inst.aux]);
    noteStateWrite(slot, value);
    return;
  }

  Value *old =
      builder_.CreateLoad(Type::getInt32Ty(context_), state_[inst.aux]);
  builder_.CreateStore(value, state_[inst.aux]);
  noteStateWrite(slot, value);
  Value *enabled = builder_.CreateAnd(builder_.CreateNot(old), value);
  enabled = builder_.CreateICmpNE(
      builder_.CreateAnd(enabled, builder_.getInt32(0x8000)),
      builder_.getInt32(0));
  BasicBlock *exit = BasicBlock::Create(context_, "msr_ee_exit", function_);
  BasicBlock *resume = BasicBlock::Create(context_, "msr_ee_resume", function_);
  if (modern_runtime_) {
    Value *eeChanged = builder_.CreateICmpNE(
        builder_.CreateAnd(builder_.CreateXor(old, value),
                           builder_.getInt32(0x8000)),
        builder_.getInt32(0));
    BasicBlock *serviceBlock =
        BasicBlock::Create(context_, "msr_ee_service", function_);
    builder_.CreateCondBr(eeChanged, serviceBlock, resume);
    builder_.SetInsertPoint(serviceBlock);

    Type *i32 = Type::getInt32Ty(context_);
    Type *i64 = Type::getInt64Ty(context_);
    Type *pointer = PointerType::getUnqual(context_);
    FunctionCallee service = module_.getOrInsertFunction(
        "moderngekko_try_write_msr",
        FunctionType::get(i32, {pointer, i32, i32, i64}, false));
    Value *elapsed = builder_.CreateAdd(
        builder_.CreateLoad(i64, guard_cycles_local_),
        builder_.CreateLoad(i64, cycles_));
    CallInst *accepted = builder_.CreateCall(service, {ctx_, old, value, elapsed});
    accepted->addFnAttr(Attribute::NoUnwind);
    Value *serviced =
        builder_.CreateICmpNE(accepted, builder_.getInt32(0));
    Value *mustExit = builder_.CreateAnd(enabled, builder_.CreateNot(serviced));
    builder_.CreateCondBr(mustExit, exit, resume);
  } else {
    builder_.CreateCondBr(enabled, exit, resume);
  }
  builder_.SetInsertPoint(exit);
  sideExit(modern_runtime_ ? inst.guest_pc : inst.guest_pc + 4u,
           modern_runtime_ ? 6u : 0u);
  builder_.SetInsertPoint(resume);
  if (modern_runtime_)
    emitBudgetGuard(inst.guest_pc + 4u);
}

void FunctionEmitter::emitStoreConditional(const DolIRInstruction &inst) {
  materialize(inst.guest_pc);
  Type *ptr = PointerType::getUnqual(context_);
  auto callee = module_.getOrInsertFunction(
      "ppc_stwcx_op", FunctionType::get(Type::getVoidTy(context_),
                                        {ptr, Type::getInt8Ty(context_),
                                         Type::getInt32Ty(context_),
                                         Type::getInt32Ty(context_)},
                                        false));
  builder_.CreateCall(callee,
                      {ctx_, builder_.getInt8(inst.immediate & 0xFFu),
                       operand(inst, 0), builder_.getInt32(inst.guest_pc)});
  Value *exception =
      loadOffset(Type::getInt32Ty(context_), offsetof(CPUState, exception));
  BasicBlock *resume = BasicBlock::Create(context_, "stwcx_resume", function_);
  BasicBlock *failed = BasicBlock::Create(context_, "stwcx_exit", function_);
  builder_.CreateCondBr(builder_.CreateICmpEQ(exception, builder_.getInt32(0)),
                        resume, failed);
  builder_.SetInsertPoint(failed);
  returnFromBody();
  builder_.SetInsertPoint(resume);
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    if (used_[slot])
      reloadState(static_cast<DolIRStateSlot>(slot));
  }
}

Value *FunctionEmitter::emitFPAvailable(u32 pc) {
  if (fp_available_checked_)
    return ConstantInt::getTrue(context_);
  Value *msr =
      builder_.CreateLoad(Type::getInt32Ty(context_), state_[DOLIR_STATE_MSR]);
  Value *enabled = builder_.CreateICmpNE(
      builder_.CreateAnd(msr, builder_.getInt32(1u << 13)),
      builder_.getInt32(0));
  BasicBlock *fast = builder_.GetInsertBlock();
  BasicBlock *good = BasicBlock::Create(context_, "fp_ok", function_);
  BasicBlock *cold = BasicBlock::Create(context_, "fp_check", function_);
  builder_.CreateCondBr(enabled, good, cold);
  builder_.SetInsertPoint(cold);
  if (modern_runtime_) {
    Value *exceptions = builder_.CreateLoad(Type::getInt32Ty(context_),
                                            state_[DOLIR_STATE_EXCEPTION]);
    builder_.CreateStore(builder_.CreateOr(exceptions, builder_.getInt32(0x40)),
                         state_[DOLIR_STATE_EXCEPTION]);
    sideExit(pc, 1);
    builder_.SetInsertPoint(good);
    fp_available_checked_ = true;
    return ConstantInt::getTrue(context_);
  }
  materialize(pc);
  auto callee = module_.getOrInsertFunction(
      "ppc_fp_available", FunctionType::get(Type::getInt1Ty(context_),
                                            {PointerType::getUnqual(context_),
                                             Type::getInt32Ty(context_)},
                                            false));
  Value *available = builder_.CreateCall(callee, {ctx_, builder_.getInt32(pc)});
  BasicBlock *reload = BasicBlock::Create(context_, "fp_reload", function_);
  BasicBlock *bad = BasicBlock::Create(context_, "fp_exit", function_);
  builder_.CreateCondBr(available, reload, bad);
  builder_.SetInsertPoint(bad);
  returnFromBody();
  builder_.SetInsertPoint(reload);
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    if (used_[slot]) {
      auto stateSlot = static_cast<DolIRStateSlot>(slot);
      Value *reloaded = loadContext(stateSlot);
      builder_.CreateStore(reloaded, state_[slot]);
    }
  }
  builder_.CreateBr(good);
  builder_.SetInsertPoint(good);
  PHINode *checked = builder_.CreatePHI(Type::getInt1Ty(context_), 2);
  checked->addIncoming(ConstantInt::getTrue(context_), fast);
  checked->addIncoming(available, reload);
  fp_available_checked_ = true;
  return checked;
}

} // namespace dolllvm
