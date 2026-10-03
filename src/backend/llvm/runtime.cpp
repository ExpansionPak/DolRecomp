#include "backend/llvm/emitter.h"
#include "cpu/cpu.h"
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
namespace dolllvm {
using namespace llvm;
AllocaInst *FunctionEmitter::temporary(Type *valueType, StringRef name) {
  IRBuilder<> allocations(entry_->getTerminator());
  return allocations.CreateAlloca(valueType, nullptr, name);
}
Value *FunctionEmitter::stateValue(DolIRStateSlot slot) {
  if (slot == DOLIR_STATE_FPSCR)
    materializeFPRF();
  if (known_state_[slot])
    return known_state_[slot];
  return builder_.CreateLoad(type(dolir_state_type(slot)), state_[slot]);
}
void FunctionEmitter::syncState(DolIRStateSlot slot) {
  storeContext(slot, stateValue(slot));
}
void FunctionEmitter::reloadState(DolIRStateSlot slot) {
  known_state_[slot] = nullptr;
  if (slot == DOLIR_STATE_FPSCR)
    pending_fprf_ = nullptr;
  if (slotInMemory(slot))
    return;
  builder_.CreateStore(loadContext(slot), state_[slot]);
}
void FunctionEmitter::reloadModernState() {
  Type *i64 = Type::getInt64Ty(context_);
  ArrayType *maskType =
      ArrayType::get(i64, DOLIR_STATE_MASK_WORDS);
  const std::string name = source_.name + std::string(".used");
  GlobalVariable *mask = module_.getGlobalVariable(name, true);
  if (!mask) {
    SmallVector<Constant *, DOLIR_STATE_MASK_WORDS> words;
    for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
      u64 bits = 0;
      for (u32 bit = 0; bit < 64; bit++) {
        const u32 slot = word * 64u + bit;
        if (slot < DOLIR_STATE_COUNT && used_[slot])
          bits |= 1ull << bit;
      }
      words.push_back(builder_.getInt64(bits));
    }
    mask = new GlobalVariable(module_, maskType, true,
                              GlobalValue::PrivateLinkage,
                              ConstantArray::get(maskType, words), name);
  }
  Type *pointer = PointerType::getUnqual(context_);
  FunctionCallee reload = module_.getOrInsertFunction(
      "moderngekko_reload_state",
      FunctionType::get(Type::getVoidTy(context_),
                        {pointer, pointer, pointer}, false));
  if (auto *function = dyn_cast<Function>(reload.getCallee())) {
    function->addFnAttr(Attribute::Cold);
    function->addFnAttr(Attribute::NoUnwind);
  }
  Value *values = modernStateValues();
  CallInst *call = builder_.CreateCall(reload, {state_interface_, values, mask});
  call->addFnAttr(Attribute::Cold);
  call->addFnAttr(Attribute::NoUnwind);
  Value *validMask = modernStateValidMask();
  for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
    u64 bits = 0;
    for (u32 bit = 0; bit < 64; bit++) {
      const u32 slot = word * 64u + bit;
      if (slot < DOLIR_STATE_COUNT && used_[slot] &&
          modernStateCacheable(static_cast<DolIRStateSlot>(slot)))
        bits |= u64(1) << bit;
    }
    if (!bits)
      continue;
    Value *validSlot =
        builder_.CreateInBoundsGEP(i64, validMask, builder_.getInt64(word));
    Value *old = builder_.CreateLoad(i64, validSlot);
    builder_.CreateStore(builder_.CreateOr(old, builder_.getInt64(bits)),
                         validSlot);
  }
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    if (!used_[slot])
      continue;
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (slotInMemory(stateSlot))
      continue;
    Value *value = nullptr;
    if (stateSlot == DOLIR_STATE_PC || stateSlot == DOLIR_STATE_TIMEBASE ||
        stateSlot == DOLIR_STATE_PROGRAM_EXCEPTION ||
        stateSlot == DOLIR_STATE_DOWNCOUNT) {
      value = loadContext(stateSlot);
    } else {
      value = builder_.CreateLoad(
          i64, builder_.CreateInBoundsGEP(i64, values, builder_.getInt64(slot)));
      Type *target = type(dolir_state_type(stateSlot));
      value = target->isDoubleTy() ? builder_.CreateBitCast(value, target)
                                  : builder_.CreateZExtOrTrunc(value, target);
    }
    builder_.CreateStore(value, state_[slot]);
  }
}
void FunctionEmitter::reloadUsedState() {
  invalidateFPRepresentations();
  fp_available_checked_ = false;
  pending_fprf_ = nullptr;
  known_state_.fill(nullptr);
  psq_direct_proven_ = false;
  psq_indexed_proven_ = false;
  if (modern_runtime_) {
    reloadModernState();
    return;
  }
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    if (used_[slot])
      reloadState(static_cast<DolIRStateSlot>(slot));
  }
}
void FunctionEmitter::continueAfterRuntimeBoundary(StringRef prefix) {
  Value *exception =
      loadOffset(Type::getInt32Ty(context_), offsetof(CPUState, exception));
  BasicBlock *resume =
      BasicBlock::Create(context_, prefix + "_resume", function_);
  BasicBlock *failed =
      BasicBlock::Create(context_, prefix + "_exit", function_);
  builder_.CreateCondBr(builder_.CreateICmpEQ(exception, builder_.getInt32(0)),
                        resume, failed);
  builder_.SetInsertPoint(failed);
  returnFromBody();
  builder_.SetInsertPoint(resume);
  reloadUsedState();
}
void FunctionEmitter::emitFPSCRUpdated() {
  syncState(DOLIR_STATE_FPSCR);
  auto callee = module_.getOrInsertFunction(
      "ppc_fpscr_control_updated",
      FunctionType::get(Type::getVoidTy(context_),
                        {PointerType::getUnqual(context_)}, false));
  builder_.CreateCall(callee, {ctx_});
  reloadState(DOLIR_STATE_FPSCR);
  fp_denormal_safe_.fill(false);
}
void FunctionEmitter::emitFPSCRBit(u64 descriptor) {
  syncState(DOLIR_STATE_FPSCR);
  const char *name =
      ((descriptor >> 8) & 1u) ? "ppc_mtfsb1_op" : "ppc_mtfsb0_op";
  auto callee = module_.getOrInsertFunction(
      name, FunctionType::get(
                Type::getVoidTy(context_),
                {PointerType::getUnqual(context_), Type::getInt8Ty(context_)},
                false));
  builder_.CreateCall(callee, {ctx_, builder_.getInt8(descriptor & 0xFFu)});
  reloadState(DOLIR_STATE_FPSCR);
  fp_denormal_safe_.fill(false);
}
void FunctionEmitter::emitProgramException(const DolIRInstruction &inst) {
  BasicBlock *taken = BasicBlock::Create(context_, "trap_taken", function_);
  BasicBlock *resume = BasicBlock::Create(context_, "trap_resume", function_);
  builder_.CreateCondBr(operand(inst, 0), taken, resume);
  builder_.SetInsertPoint(taken);
  if (modern_runtime_) {
    Value *exceptions = builder_.CreateLoad(Type::getInt32Ty(context_),
                                            state_[DOLIR_STATE_EXCEPTION]);
    Value *updated =
        builder_.CreateOr(exceptions, builder_.getInt32(0x80));
    builder_.CreateStore(updated, state_[DOLIR_STATE_EXCEPTION]);
    noteStateWrite(DOLIR_STATE_EXCEPTION, updated);
    Value *cause = builder_.getInt32(inst.immediate);
    builder_.CreateStore(cause, state_[DOLIR_STATE_SRR1]);
    noteStateWrite(DOLIR_STATE_SRR1, cause);
    sideExit(inst.guest_pc, 1);
    builder_.SetInsertPoint(resume);
    return;
  }
  materialize(inst.guest_pc);
  auto callee = module_.getOrInsertFunction(
      "ppc_program_exception",
      FunctionType::get(Type::getVoidTy(context_),
                        {PointerType::getUnqual(context_),
                         Type::getInt32Ty(context_),
                         Type::getInt32Ty(context_)},
                        false));
  builder_.CreateCall(callee, {ctx_, builder_.getInt32(inst.immediate),
                               builder_.getInt32(inst.guest_pc)});
  returnFromBody();
  builder_.SetInsertPoint(resume);
}
Value *FunctionEmitter::emitSPRRead(const DolIRInstruction &inst) {
  materialize(inst.guest_pc);
  auto callee = module_.getOrInsertFunction(
      "ppc_mfspr", FunctionType::get(Type::getInt32Ty(context_),
                                     {PointerType::getUnqual(context_),
                                      Type::getInt16Ty(context_),
                                      Type::getInt32Ty(context_)},
                                     false));
  Value *result =
      builder_.CreateCall(callee, {ctx_, builder_.getInt16(inst.immediate),
                                   builder_.getInt32(inst.guest_pc)});
  continueAfterRuntimeBoundary("mfspr");
  return result;
}
void FunctionEmitter::emitSPRWrite(const DolIRInstruction &inst) {
  materialize(inst.guest_pc);
  auto callee = module_.getOrInsertFunction(
      "ppc_mtspr",
      FunctionType::get(Type::getVoidTy(context_),
                        {PointerType::getUnqual(context_),
                         Type::getInt16Ty(context_), Type::getInt32Ty(context_),
                         Type::getInt32Ty(context_)},
                        false));
  builder_.CreateCall(callee,
                      {ctx_, builder_.getInt16(inst.immediate),
                       operand(inst, 0), builder_.getInt32(inst.guest_pc)});
  continueAfterRuntimeBoundary("mtspr");
}
void FunctionEmitter::emitLSWX(const DolIRInstruction &inst) {
  materialize(inst.guest_pc);
  auto callee = module_.getOrInsertFunction(
      "ppc_lswx",
      FunctionType::get(Type::getVoidTy(context_),
                        {PointerType::getUnqual(context_),
                         Type::getInt8Ty(context_), Type::getInt8Ty(context_),
                         Type::getInt8Ty(context_), Type::getInt32Ty(context_)},
                        false));
  builder_.CreateCall(callee, {ctx_, builder_.getInt8(inst.immediate & 0xFFu),
                               builder_.getInt8((inst.immediate >> 8) & 0xFFu),
                               builder_.getInt8((inst.immediate >> 16) & 0xFFu),
                               builder_.getInt32(inst.guest_pc)});
  continueAfterRuntimeBoundary("lswx");
}
Value *FunctionEmitter::emitRuntimeBoundary(const DolIRInstruction &inst) {
  if (modern_runtime_ && inst.aux == DOLIR_HELPER_CACHE_CONTROL) {
    emitCacheControl(inst);
    return nullptr;
  }
  materialize(inst.guest_pc);
  Type *ptr = PointerType::getUnqual(context_);
  Value *result = nullptr;
  StringRef prefix;
  if (inst.aux == DOLIR_HELPER_DCBZ_L) {
    prefix = "dcbz_l";
    auto callee = module_.getOrInsertFunction(
        "ppc_dcbz_l", FunctionType::get(Type::getVoidTy(context_),
                                        {ptr, Type::getInt32Ty(context_),
                                         Type::getInt32Ty(context_)},
                                        false));
    builder_.CreateCall(
        callee, {ctx_, operand(inst, 0), builder_.getInt32(inst.guest_pc)});
  } else if (inst.aux == DOLIR_HELPER_ECIWX) {
    prefix = "eciwx";
    auto callee = module_.getOrInsertFunction(
        "ppc_eciwx", FunctionType::get(Type::getInt32Ty(context_),
                                       {ptr, Type::getInt32Ty(context_),
                                        Type::getInt32Ty(context_)},
                                       false));
    result = builder_.CreateCall(
        callee, {ctx_, operand(inst, 0), builder_.getInt32(inst.guest_pc)});
  } else if (inst.aux == DOLIR_HELPER_ECOWX) {
    prefix = "ecowx";
    auto callee = module_.getOrInsertFunction(
        "ppc_ecowx", FunctionType::get(Type::getVoidTy(context_),
                                       {ptr, Type::getInt32Ty(context_),
                                        Type::getInt32Ty(context_),
                                        Type::getInt32Ty(context_)},
                                       false));
    builder_.CreateCall(callee, {ctx_, operand(inst, 0), operand(inst, 1),
                                 builder_.getInt32(inst.guest_pc)});
  } else if (inst.aux == DOLIR_HELPER_TLBIE) {
    prefix = "tlbie";
    auto callee = module_.getOrInsertFunction(
        "ppc_tlbie", FunctionType::get(Type::getVoidTy(context_),
                                       {ptr, Type::getInt32Ty(context_),
                                        Type::getInt32Ty(context_)},
                                       false));
    builder_.CreateCall(
        callee, {ctx_, operand(inst, 0), builder_.getInt32(inst.guest_pc)});
  } else {
    prefix = "cache";
    auto callee = module_.getOrInsertFunction(
        "ppc_cache_control", FunctionType::get(Type::getVoidTy(context_),
                                               {ptr, Type::getInt8Ty(context_),
                                                Type::getInt32Ty(context_),
                                                Type::getInt32Ty(context_)},
                                               false));
    builder_.CreateCall(callee,
                        {ctx_, builder_.getInt8(inst.immediate),
                         operand(inst, 0), builder_.getInt32(inst.guest_pc)});
  }
  continueAfterRuntimeBoundary(prefix);
  return result;
}
} // namespace dolllvm
