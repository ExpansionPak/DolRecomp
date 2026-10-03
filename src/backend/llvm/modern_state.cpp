#include "backend/llvm/emitter.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>

namespace dolllvm {

using namespace llvm;

bool FunctionEmitter::modernStateCacheable(DolIRStateSlot slot) const {
  return slot >= DOLIR_STATE_GPR0 && slot <= DOLIR_STATE_PS1_31;
}

Value *FunctionEmitter::modernStateValues() {
  Type *pointer = PointerType::getUnqual(context_);
  return builder_.CreateLoad(
      pointer, builder_.CreateStructGEP(chainType(), chain_, 8));
}

Value *FunctionEmitter::modernStateDirtyMask() {
  Type *pointer = PointerType::getUnqual(context_);
  return builder_.CreateLoad(
      pointer, builder_.CreateStructGEP(chainType(), chain_, 9));
}

Value *FunctionEmitter::modernStateValidMask() {
  Type *pointer = PointerType::getUnqual(context_);
  return builder_.CreateLoad(
      pointer, builder_.CreateStructGEP(chainType(), chain_, 10));
}

void FunctionEmitter::invalidateModernStateCache() {
  if (!modern_runtime_)
    return;
  Type *i64 = Type::getInt64Ty(context_);
  Value *validMask = modernStateValidMask();
  for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
    builder_.CreateStore(
        builder_.getInt64(0),
        builder_.CreateInBoundsGEP(i64, validMask, builder_.getInt64(word)));
}

void FunctionEmitter::retainModernPersistentDirty() {
  if (!modern_runtime_)
    return;
  Type *i64 = Type::getInt64Ty(context_);
  Value *dirtyMask = modernStateDirtyMask();
  for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
    u64 keep = 0;
    for (u32 bit = 0; bit < 64; bit++) {
      const u32 rawSlot = word * 64u + bit;
      if (rawSlot < DOLIR_STATE_COUNT &&
          modernStateCacheable(static_cast<DolIRStateSlot>(rawSlot)))
        keep |= u64(1) << bit;
    }
    Value *slot =
        builder_.CreateInBoundsGEP(i64, dirtyMask, builder_.getInt64(word));
    if (!keep) {
      builder_.CreateStore(builder_.getInt64(0), slot);
      continue;
    }
    Value *old = builder_.CreateLoad(i64, slot);
    builder_.CreateStore(builder_.CreateAnd(old, builder_.getInt64(keep)), slot);
  }
}

void FunctionEmitter::stageStateMask(const u64 *stateMask) {
  if (!modern_runtime_ || !stateMask)
    return;
  Type *i64 = Type::getInt64Ty(context_);
  Value *values = modernStateValues();
  Value *dirtyMask = modernStateDirtyMask();
  Value *validMask = modernStateValidMask();
  u64 stagedWords[DOLIR_STATE_MASK_WORDS]{};
  u64 validWords[DOLIR_STATE_MASK_WORDS]{};
  auto stage = [&](DolIRStateSlot slot, Value *value) {
    if (value->getType()->isDoubleTy())
      value = builder_.CreateBitCast(value, i64);
    else
      value = builder_.CreateZExtOrTrunc(value, i64);
    builder_.CreateStore(
        value,
        builder_.CreateInBoundsGEP(i64, values, builder_.getInt64(slot)));
    const u32 rawSlot = static_cast<u32>(slot);
    stagedWords[rawSlot / 64u] |= u64(1) << (rawSlot & 63u);
    if (modernStateCacheable(slot))
      validWords[rawSlot / 64u] |= u64(1) << (rawSlot & 63u);
  };
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!dolir_state_mask_test(stateMask, stateSlot) || !state_[slot] ||
        stateSlot == DOLIR_STATE_FPSCR)
      continue;
    stage(stateSlot, stateValue(stateSlot));
  }
  if (dolir_state_mask_test(stateMask, DOLIR_STATE_FPSCR) &&
      state_[DOLIR_STATE_FPSCR]) {
    materializeFPRF();
    stage(DOLIR_STATE_FPSCR, stateValue(DOLIR_STATE_FPSCR));
  }
  for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
    if (!stagedWords[word])
      continue;
    Value *slot =
        builder_.CreateInBoundsGEP(i64, dirtyMask, builder_.getInt64(word));
    Value *old = builder_.CreateLoad(i64, slot);
    builder_.CreateStore(builder_.CreateOr(old, builder_.getInt64(stagedWords[word])),
                         slot);
    if (validWords[word]) {
      Value *validSlot =
          builder_.CreateInBoundsGEP(i64, validMask, builder_.getInt64(word));
      Value *oldValid = builder_.CreateLoad(i64, validSlot);
      builder_.CreateStore(
          builder_.CreateOr(oldValid, builder_.getInt64(validWords[word])),
          validSlot);
    }
  }
}

void FunctionEmitter::commitModernState() {
  if (!modern_runtime_)
    return;
  Type *i64 = Type::getInt64Ty(context_);
  Type *pointer = PointerType::getUnqual(context_);
  FunctionCallee commit = module_.getOrInsertFunction(
      "moderngekko_commit_state",
      FunctionType::get(Type::getVoidTy(context_),
                        {pointer, pointer, pointer}, false));
  if (auto *function = dyn_cast<Function>(commit.getCallee())) {
    function->addFnAttr(Attribute::Cold);
    function->addFnAttr(Attribute::NoUnwind);
  }
  Value *values = modernStateValues();
  Value *dirtyMask = modernStateDirtyMask();
  CallInst *call =
      builder_.CreateCall(commit, {state_interface_, values, dirtyMask});
  call->addFnAttr(Attribute::Cold);
  call->addFnAttr(Attribute::NoUnwind);
  for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
    Value *slot =
        builder_.CreateInBoundsGEP(i64, dirtyMask, builder_.getInt64(word));
    builder_.CreateStore(builder_.getInt64(0), slot);
  }
  invalidateModernStateCache();
}

} // namespace dolllvm
