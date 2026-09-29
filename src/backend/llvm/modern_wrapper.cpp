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
  StructType *exitType =
      StructType::get(context_, {i32, i32, i32, i32, i32, i32, i32});
  FunctionType *wrapperType = FunctionType::get(
      Type::getVoidTy(context_), {pointer, pointer, pointer, i32, i32}, false);
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
  wrapper->setVisibility(GlobalValue::HiddenVisibility);
  wrapper->setDSOLocal(true);
  wrapper->getArg(0)->setName("result");
  wrapper->getArg(0)->addAttr(Attribute::getWithStructRetType(context_, exitType));
  wrapper->getArg(0)->addAttr(Attribute::NoAlias);
  wrapper->getArg(1)->setName("runtime");
  wrapper->getArg(2)->setName("state");
  wrapper->getArg(3)->setName("entry_pc");
  wrapper->getArg(4)->setName("cycle_budget");
  wrapper->getArg(1)->addAttr(Attribute::NonNull);
  wrapper->getArg(2)->addAttr(Attribute::NonNull);

  BasicBlock *entry = BasicBlock::Create(context_, "entry", wrapper);
  IRBuilderBase::InsertPoint saved = builder_.saveIP();
  Argument *savedContext = ctx_;
  Argument *savedStateInterface = state_interface_;
  Value *savedEntryPC = entry_pc_;
  builder_.SetInsertPoint(entry);
  Value *result = wrapper->getArg(0);
  ctx_ = wrapper->getArg(1);
  state_interface_ = wrapper->getArg(2);
  entry_pc_ = wrapper->getArg(3);

  StructType *chainTy = chainType();
  AllocaInst *chain = builder_.CreateAlloca(chainTy, nullptr, "chain");
  chain->setAlignment(Align(16));
  builder_.CreateStore(builder_.getInt64(0),
                       builder_.CreateStructGEP(chainTy, chain, 1));
  builder_.CreateStore(builder_.getInt64(0),
                       builder_.CreateStructGEP(chainTy, chain, 2));
  builder_.CreateStore(builder_.getInt64(0),
                       builder_.CreateStructGEP(chainTy, chain, 3));
  builder_.CreateStore(
      builder_.CreateZExt(wrapper->getArg(4), Type::getInt64Ty(context_)),
      builder_.CreateStructGEP(chainTy, chain, 4));
  builder_.CreateStore(wrapper->getArg(3),
                       builder_.CreateStructGEP(chainTy, chain, 5));
  builder_.CreateStore(
      builder_.CreateAdd(wrapper->getArg(3), builder_.getInt32(4)),
      builder_.CreateStructGEP(chainTy, chain, 6));
  builder_.CreateStore(builder_.getInt32(0),
                       builder_.CreateStructGEP(chainTy, chain, 7));
  ArrayType *dirtyMaskTy =
      ArrayType::get(Type::getInt64Ty(context_), DOLIR_STATE_MASK_WORDS);
  ArrayType *stateValuesTy =
      ArrayType::get(Type::getInt64Ty(context_), DOLIR_STATE_COUNT);
  Value *stateValues = builder_.CreateStructGEP(chainTy, chain, 8);
  Value *dirtyMask = builder_.CreateStructGEP(chainTy, chain, 9);
  for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
    builder_.CreateStore(
        builder_.getInt64(0),
        builder_.CreateInBoundsGEP(
            dirtyMaskTy, dirtyMask,
            {builder_.getInt64(0), builder_.getInt64(word)}));

  Value *returnAddress = loadContext(DOLIR_STATE_LR);
  Value *returnPC =
      builder_.CreateAnd(returnAddress, builder_.getInt32(~3u));
  u64 registerInputMask[DOLIR_STATE_MASK_WORDS]{};
  bool hasRegisterInputs = false;
  for (u32 slot = DOLIR_STATE_GPR0; slot <= DOLIR_STATE_PS1_31; slot++) {
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!stateInput(abi_range_, stateSlot))
      continue;
    registerInputMask[slot / 64u] |= u64(1) << (slot & 63u);
    hasRegisterInputs = true;
  }
  if (hasRegisterInputs) {
    for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
      builder_.CreateStore(
          builder_.getInt64(registerInputMask[word]),
          builder_.CreateInBoundsGEP(
              dirtyMaskTy, dirtyMask,
              {builder_.getInt64(0), builder_.getInt64(word)}));
    FunctionCallee reload = module_.getOrInsertFunction(
        "moderngekko_reload_state",
        FunctionType::get(Type::getVoidTy(context_),
                          {pointer, pointer, pointer}, false));
    if (auto *function = dyn_cast<Function>(reload.getCallee())) {
      function->addFnAttr(Attribute::Cold);
      function->addFnAttr(Attribute::NoUnwind);
    }
    CallInst *reloadCall =
        builder_.CreateCall(reload, {state_interface_, stateValues, dirtyMask});
    reloadCall->addFnAttr(Attribute::Cold);
    reloadCall->addFnAttr(Attribute::NoUnwind);
    for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
      builder_.CreateStore(
          builder_.getInt64(0),
          builder_.CreateInBoundsGEP(
              dirtyMaskTy, dirtyMask,
              {builder_.getInt64(0), builder_.getInt64(word)}));
  }

  Value *control = builder_.CreateOr(
      builder_.CreateZExt(wrapper->getArg(3), Type::getInt64Ty(context_)),
      builder_.CreateShl(
          builder_.CreateZExt(returnPC, Type::getInt64Ty(context_)),
          builder_.getInt64(32)));
  SmallVector<Value *, 32> arguments = {wrapper->getArg(1), wrapper->getArg(2),
                                        chain, control};
  if (nativeCyclesInResult(abi_range_))
    arguments.push_back(builder_.getInt64(0));
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!stateInput(abi_range_, stateSlot))
      continue;
    if (slot <= DOLIR_STATE_PS1_31) {
      Value *value = builder_.CreateLoad(
          Type::getInt64Ty(context_),
          builder_.CreateInBoundsGEP(
              stateValuesTy, stateValues,
              {builder_.getInt64(0), builder_.getInt64(slot)}));
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
  u64 registerOutputMask[DOLIR_STATE_MASK_WORDS]{};
  bool hasRegisterOutputs = false;
  for (u32 slot = DOLIR_STATE_GPR0; slot <= DOLIR_STATE_PS1_31; slot++) {
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!stateOutput(abi_range_, stateSlot))
      continue;
    Value *value = cold_escapes_
                       ? nativeOutputValue(body, abi_range_, stateSlot)
                       : builder_.CreateExtractValue(body, resultField++);
    if (value->getType()->isDoubleTy())
      value = builder_.CreateBitCast(value, Type::getInt64Ty(context_));
    else
      value = builder_.CreateZExtOrTrunc(value, Type::getInt64Ty(context_));
    builder_.CreateStore(
        value, builder_.CreateInBoundsGEP(
                   stateValuesTy, stateValues,
                   {builder_.getInt64(0), builder_.getInt64(slot)}));
    registerOutputMask[slot / 64u] |= u64(1) << (slot & 63u);
    hasRegisterOutputs = true;
  }
  if (hasRegisterOutputs) {
    for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
      builder_.CreateStore(
          builder_.getInt64(registerOutputMask[word]),
          builder_.CreateInBoundsGEP(
              dirtyMaskTy, dirtyMask,
              {builder_.getInt64(0), builder_.getInt64(word)}));
    FunctionCallee commit = module_.getOrInsertFunction(
        "moderngekko_commit_state",
        FunctionType::get(Type::getVoidTy(context_),
                          {pointer, pointer, pointer}, false));
    if (auto *function = dyn_cast<Function>(commit.getCallee())) {
      function->addFnAttr(Attribute::Cold);
      function->addFnAttr(Attribute::NoUnwind);
    }
    CallInst *commitCall =
        builder_.CreateCall(commit, {state_interface_, stateValues, dirtyMask});
    commitCall->addFnAttr(Attribute::Cold);
    commitCall->addFnAttr(Attribute::NoUnwind);
  }
  for (u32 slot = DOLIR_STATE_PS1_31 + 1; slot < DOLIR_STATE_COUNT; slot++) {
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!stateOutput(abi_range_, stateSlot))
      continue;
    Value *value = cold_escapes_
                       ? nativeOutputValue(body, abi_range_, stateSlot)
                       : builder_.CreateExtractValue(body, resultField++);
    storeContext(stateSlot, value);
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
