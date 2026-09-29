#include "backend/llvm/emitter.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>

namespace dolllvm {

using namespace llvm;

void FunctionEmitter::stageStateMask(const u64 *stateMask) {
  if (!modern_runtime_ || !stateMask)
    return;
  Type *i64 = Type::getInt64Ty(context_);
  ArrayType *valuesType = ArrayType::get(i64, DOLIR_STATE_COUNT);
  ArrayType *maskType = ArrayType::get(i64, DOLIR_STATE_MASK_WORDS);
  Value *values = builder_.CreateStructGEP(chainType(), chain_, 8);
  Value *dirtyMask = builder_.CreateStructGEP(chainType(), chain_, 9);
  u64 stagedWords[DOLIR_STATE_MASK_WORDS]{};
  auto stage = [&](DolIRStateSlot slot, Value *value) {
    if (value->getType()->isDoubleTy())
      value = builder_.CreateBitCast(value, i64);
    else
      value = builder_.CreateZExtOrTrunc(value, i64);
    builder_.CreateStore(
        value, builder_.CreateInBoundsGEP(
                   valuesType, values,
                   {builder_.getInt64(0), builder_.getInt64(slot)}));
    const u32 rawSlot = static_cast<u32>(slot);
    stagedWords[rawSlot / 64u] |= u64(1) << (rawSlot & 63u);
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
    Value *slot = builder_.CreateInBoundsGEP(
        maskType, dirtyMask,
        {builder_.getInt64(0), builder_.getInt64(word)});
    Value *old = builder_.CreateLoad(i64, slot);
    builder_.CreateStore(builder_.CreateOr(old, builder_.getInt64(stagedWords[word])),
                         slot);
  }
}

void FunctionEmitter::commitModernState() {
  if (!modern_runtime_)
    return;
  Type *i64 = Type::getInt64Ty(context_);
  ArrayType *maskType = ArrayType::get(i64, DOLIR_STATE_MASK_WORDS);
  Type *pointer = PointerType::getUnqual(context_);
  FunctionCallee commit = module_.getOrInsertFunction(
      "moderngekko_commit_state",
      FunctionType::get(Type::getVoidTy(context_),
                        {pointer, pointer, pointer}, false));
  if (auto *function = dyn_cast<Function>(commit.getCallee())) {
    function->addFnAttr(Attribute::Cold);
    function->addFnAttr(Attribute::NoUnwind);
  }
  Value *values = builder_.CreateStructGEP(chainType(), chain_, 8);
  Value *dirtyMask = builder_.CreateStructGEP(chainType(), chain_, 9);
  CallInst *call =
      builder_.CreateCall(commit, {state_interface_, values, dirtyMask});
  call->addFnAttr(Attribute::Cold);
  call->addFnAttr(Attribute::NoUnwind);
  for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
    Value *slot = builder_.CreateInBoundsGEP(
        maskType, dirtyMask,
        {builder_.getInt64(0), builder_.getInt64(word)});
    builder_.CreateStore(builder_.getInt64(0), slot);
  }
}

} // namespace dolllvm
