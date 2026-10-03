#include "backend/llvm/emitter.h"
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
namespace dolllvm {
using namespace llvm;
bool FunctionEmitter::emitModernWrapper(raw_ostream &diagnostics) {
  Type *pointer = PointerType::getUnqual(context_);
  Type *i32 = Type::getInt32Ty(context_);
  Type *i64 = Type::getInt64Ty(context_);
  StructType *exitType =
      StructType::get(context_, {i32, i32, i32, i32, i32, i32, i32});
  FunctionType *wrapperType = FunctionType::get(
      Type::getVoidTy(context_),
      {pointer, pointer, pointer, pointer, pointer, pointer, i32, i32, i32},
      false);
  const std::string wrapperName = symbolName(source_.name);
  Function *wrapper = module_.getFunction(wrapperName);
  if (!wrapper)
    wrapper = Function::Create(wrapperType, GlobalValue::ExternalLinkage, wrapperName,
                               module_);
  if (wrapper->getFunctionType() != wrapperType || !wrapper->empty()) {
    diagnostics << "dolllvm: conflicting native entry " << source_.name << "\n";
    return false;
  }
  wrapper->setCallingConv(CallingConv::C);
  wrapper->setVisibility(GlobalValue::DefaultVisibility);
  wrapper->setDSOLocal(false);
  wrapper->getArg(0)->setName("result");
  wrapper->getArg(0)->addAttr(Attribute::getWithStructRetType(context_, exitType));
  wrapper->getArg(0)->addAttr(Attribute::NoAlias);
  wrapper->getArg(1)->setName("runtime");
  wrapper->getArg(2)->setName("state");
  wrapper->getArg(3)->setName("state_values");
  wrapper->getArg(4)->setName("dirty_mask");
  wrapper->getArg(5)->setName("valid_mask");
  wrapper->getArg(6)->setName("entry_pc");
  wrapper->getArg(7)->setName("cycle_budget");
  wrapper->getArg(8)->setName("cycle_base");
  wrapper->getArg(1)->addAttr(Attribute::NonNull);
  wrapper->getArg(2)->addAttr(Attribute::NonNull);
  wrapper->getArg(3)->addAttr(Attribute::NonNull);
  wrapper->getArg(4)->addAttr(Attribute::NonNull);
  wrapper->getArg(5)->addAttr(Attribute::NonNull);
  BasicBlock *entry = BasicBlock::Create(context_, "entry", wrapper);
  IRBuilderBase::InsertPoint saved = builder_.saveIP();
  Argument *savedContext = ctx_;
  Argument *savedStateInterface = state_interface_;
  Value *savedEntryPC = entry_pc_;
  builder_.SetInsertPoint(entry);
  Value *result = wrapper->getArg(0);
  ctx_ = wrapper->getArg(1);
  state_interface_ = wrapper->getArg(2);
  entry_pc_ = wrapper->getArg(6);
  StructType *chainTy = chainType();
  AllocaInst *chain = builder_.CreateAlloca(chainTy, nullptr, "chain");
  chain->setAlignment(Align(16));
  builder_.CreateStore(
      builder_.CreateZExt(wrapper->getArg(8), i64),
      builder_.CreateStructGEP(chainTy, chain, 1));
  builder_.CreateStore(builder_.getInt64(0),
                       builder_.CreateStructGEP(chainTy, chain, 2));
  builder_.CreateStore(builder_.getInt64(0),
                       builder_.CreateStructGEP(chainTy, chain, 3));
  builder_.CreateStore(
      builder_.CreateAdd(
          builder_.CreateZExt(wrapper->getArg(8), i64),
          builder_.CreateZExt(wrapper->getArg(7), i64)),
      builder_.CreateStructGEP(chainTy, chain, 4));
  builder_.CreateStore(wrapper->getArg(6),
                       builder_.CreateStructGEP(chainTy, chain, 5));
  builder_.CreateStore(
      builder_.CreateAdd(wrapper->getArg(6), builder_.getInt32(4)),
      builder_.CreateStructGEP(chainTy, chain, 6));
  builder_.CreateStore(builder_.getInt32(0),
                       builder_.CreateStructGEP(chainTy, chain, 7));
  Value *stateValues = wrapper->getArg(3);
  Value *dirtyMask = wrapper->getArg(4);
  Value *validMask = wrapper->getArg(5);
  builder_.CreateStore(stateValues, builder_.CreateStructGEP(chainTy, chain, 8));
  builder_.CreateStore(dirtyMask, builder_.CreateStructGEP(chainTy, chain, 9));
  builder_.CreateStore(validMask, builder_.CreateStructGEP(chainTy, chain, 10));
  Value *returnAddress = loadContext(DOLIR_STATE_LR);
  Value *returnPC =
      builder_.CreateAnd(returnAddress, builder_.getInt32(~3u));
  Value *control = builder_.CreateOr(
      builder_.CreateZExt(wrapper->getArg(6), i64),
      builder_.CreateShl(
          builder_.CreateZExt(returnPC, i64),
          builder_.getInt64(32)));
  SmallVector<Value *, 32> arguments = {wrapper->getArg(1), wrapper->getArg(2),
                                        chain, control};
  if (nativeCyclesInResult(abi_range_))
    arguments.push_back(builder_.getInt64(0));
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!stateInput(abi_range_, stateSlot))
      continue;
    if (modernStateCacheable(stateSlot)) {
      Value *value = builder_.CreateLoad(
          i64, builder_.CreateInBoundsGEP(i64, stateValues,
                                          builder_.getInt64(slot)));
      Type *target = type(dolir_state_type(stateSlot));
      value = target->isDoubleTy() ? builder_.CreateBitCast(value, target)
                                   : builder_.CreateZExtOrTrunc(value, target);
      arguments.push_back(value);
    } else if (stateSlot == DOLIR_STATE_LR) {
      arguments.push_back(returnAddress);
    } else {
      arguments.push_back(loadContext(stateSlot));
    }
  }
  CallInst *body = builder_.CreateCall(function_, arguments);
  body->setCallingConv(bodyCallingConvention());
  body->addFnAttr(Attribute::NoInline);
  Value *rawReason =
      builder_.CreateLoad(i32, builder_.CreateStructGEP(chainTy, chain, 7));
  Value *structuredExit = builder_.CreateICmpNE(
      builder_.CreateAnd(rawReason, builder_.getInt32(0x80000000u)),
      builder_.getInt32(0));
  BasicBlock *structuredReturn =
      BasicBlock::Create(context_, "structured_return", wrapper);
  BasicBlock *normalReturn =
      BasicBlock::Create(context_, "normal_return", wrapper);
  builder_.CreateCondBr(structuredExit, structuredReturn, normalReturn);
  builder_.SetInsertPoint(structuredReturn);
  builder_.CreateStore(
      builder_.CreateAnd(rawReason, builder_.getInt32(0x7fffffffu)),
      builder_.CreateStructGEP(exitType, result, 0));
  builder_.CreateStore(
      builder_.CreateLoad(i32, builder_.CreateStructGEP(chainTy, chain, 5)),
      builder_.CreateStructGEP(exitType, result, 1));
  builder_.CreateStore(
      builder_.CreateLoad(i32, builder_.CreateStructGEP(chainTy, chain, 6)),
      builder_.CreateStructGEP(exitType, result, 2));
  builder_.CreateStore(
      builder_.CreateTrunc(
          builder_.CreateLoad(Type::getInt64Ty(context_),
                              builder_.CreateStructGEP(chainTy, chain, 3)),
          i32),
      builder_.CreateStructGEP(exitType, result, 3));
  for (unsigned field = 4; field < 7; field++)
    builder_.CreateStore(builder_.getInt32(0),
                         builder_.CreateStructGEP(exitType, result, field));
  builder_.CreateRetVoid();
  builder_.SetInsertPoint(normalReturn);
  u32 resultField = 2;
  u64 outputMask[DOLIR_STATE_MASK_WORDS]{};
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!stateOutput(abi_range_, stateSlot))
      continue;
    Value *value = cold_escapes_
                       ? nativeOutputValue(body, abi_range_, stateSlot)
                       : builder_.CreateExtractValue(body, resultField++);
    if (modernStateCacheable(stateSlot)) {
      Value *bits =
          value->getType()->isDoubleTy() ? builder_.CreateBitCast(value, i64)
                                         : builder_.CreateZExtOrTrunc(value, i64);
      builder_.CreateStore(
          bits, builder_.CreateInBoundsGEP(i64, stateValues,
                                           builder_.getInt64(slot)));
      outputMask[slot / 64u] |= u64(1) << (slot & 63u);
    } else {
      storeContext(stateSlot, value);
    }
  }
  for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
    if (!outputMask[word])
      continue;
    Value *dirtySlot = builder_.CreateInBoundsGEP(
        i64, dirtyMask, builder_.getInt64(word));
    Value *dirty = builder_.CreateLoad(i64, dirtySlot);
    builder_.CreateStore(
        builder_.CreateOr(dirty, builder_.getInt64(outputMask[word])),
        dirtySlot);
    Value *validSlot = builder_.CreateInBoundsGEP(
        i64, validMask, builder_.getInt64(word));
    Value *valid = builder_.CreateLoad(i64, validSlot);
    builder_.CreateStore(
        builder_.CreateOr(valid, builder_.getInt64(outputMask[word])),
        validSlot);
  }
  Value *normalCycles =
      nativeCyclesInResult(abi_range_)
          ? nativeCycleValue(body, abi_range_)
          : builder_.CreateLoad(Type::getInt64Ty(context_),
                                builder_.CreateStructGEP(chainTy, chain, 3));
  builder_.CreateStore(builder_.getInt32(0),
                       builder_.CreateStructGEP(exitType, result, 0));
  builder_.CreateStore(returnPC, builder_.CreateStructGEP(exitType, result, 1));
  builder_.CreateStore(builder_.CreateAdd(returnPC, builder_.getInt32(4)),
                       builder_.CreateStructGEP(exitType, result, 2));
  builder_.CreateStore(
      builder_.CreateTrunc(normalCycles, Type::getInt32Ty(context_)),
      builder_.CreateStructGEP(exitType, result, 3));
  for (unsigned field = 4; field < 7; field++)
    builder_.CreateStore(builder_.getInt32(0),
                         builder_.CreateStructGEP(exitType, result, field));
  builder_.CreateRetVoid();
  ctx_ = savedContext;
  state_interface_ = savedStateInterface;
  entry_pc_ = savedEntryPC;
  builder_.restoreIP(saved);
  return !verifyFunction(*wrapper, &diagnostics);
}
} // namespace dolllvm
