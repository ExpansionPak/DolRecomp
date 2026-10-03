#include "backend/llvm/emitter.h"
#include "backend/llvm/state_access.h"
#include "cpu/cpu.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Constants.h>

namespace dolllvm {

using namespace llvm;

StructType *FunctionEmitter::runtimeType() {
  Type *i32 = Type::getInt32Ty(context_);
  Type *pointer = PointerType::getUnqual(context_);
  return StructType::get(context_,
                         {i32, i32, pointer, pointer, i32, i32, pointer, i32,
                          i32, pointer, i32, pointer, pointer});
}

StructType *FunctionEmitter::stateInterfaceType() {
  Type *i32 = Type::getInt32Ty(context_);
  Type *pointer = PointerType::getUnqual(context_);
  SmallVector<Type *, 25> fields = {i32, i32};
  fields.append(23, pointer);
  return StructType::get(context_, fields);
}

Value *FunctionEmitter::runtimeField(u32 field) {
  return builder_.CreateLoad(
      runtimeType()->getElementType(field),
      builder_.CreateStructGEP(runtimeType(), ctx_, field));
}

Value *FunctionEmitter::stateField(u32 field) {
  Type *pointer = PointerType::getUnqual(context_);
  return builder_.CreateLoad(
      pointer,
      builder_.CreateStructGEP(stateInterfaceType(), state_interface_, field));
}

Value *FunctionEmitter::callStateRead(u32 field, ArrayRef<Value *> arguments,
                                      Type *resultType) {
  Type *pointer = PointerType::getUnqual(context_);
  Value *stateContext = builder_.CreateLoad(
      pointer, builder_.CreateStructGEP(stateInterfaceType(), state_interface_,
                                        StateContext));
  SmallVector<Value *, 4> callArguments = {stateContext};
  callArguments.append(arguments.begin(), arguments.end());
  SmallVector<Type *, 4> argumentTypes;
  for (Value *argument : callArguments)
    argumentTypes.push_back(argument->getType());
  CallInst *call =
      builder_.CreateCall(FunctionType::get(resultType, argumentTypes, false),
                          stateField(field), callArguments);
  call->addFnAttr(Attribute::NoUnwind);
  return call;
}

void FunctionEmitter::callStateWrite(u32 field, ArrayRef<Value *> arguments) {
  callStateRead(field, arguments, Type::getVoidTy(context_));
}

Value *FunctionEmitter::loadContext(DolIRStateSlot slot) {
  if (modern_runtime_) {
    Type *i32 = Type::getInt32Ty(context_);
    if (slot >= DOLIR_STATE_GPR0 && slot <= DOLIR_STATE_GPR31)
      return callStateRead(ReadGPR,
                           {builder_.getInt32(slot - DOLIR_STATE_GPR0)}, i32);
    if (slot >= DOLIR_STATE_FPR0 && slot <= DOLIR_STATE_FPR31) {
      Value *bits = callStateRead(
          ReadFPR,
          {builder_.getInt32(slot - DOLIR_STATE_FPR0), builder_.getInt32(0)},
          Type::getInt64Ty(context_));
      return builder_.CreateBitCast(bits, Type::getDoubleTy(context_));
    }
    if (slot >= DOLIR_STATE_PS1_0 && slot <= DOLIR_STATE_PS1_31) {
      Value *bits = callStateRead(
          ReadFPR,
          {builder_.getInt32(slot - DOLIR_STATE_PS1_0), builder_.getInt32(1)},
          Type::getInt64Ty(context_));
      return builder_.CreateBitCast(bits, Type::getDoubleTy(context_));
    }
    if (slot >= DOLIR_STATE_CR0 && slot <= DOLIR_STATE_CR7) {
      Value *packed = callStateRead(ReadCR, {}, i32);
      const u32 shift = 28u - 4u * (slot - DOLIR_STATE_CR0);
      return builder_.CreateAnd(builder_.CreateLShr(packed, shift),
                                builder_.getInt32(0xf));
    }
    if (slot >= DOLIR_STATE_XER_CA && slot <= DOLIR_STATE_XER_SO) {
      Value *packed = callStateRead(ReadXER, {}, i32);
      const u32 shift = 29u + (slot - DOLIR_STATE_XER_CA);
      return builder_.CreateTrunc(builder_.CreateLShr(packed, shift),
                                  Type::getInt1Ty(context_));
    }
    if (slot >= DOLIR_STATE_SR0 && slot <= DOLIR_STATE_SR15)
      return callStateRead(ReadSR, {builder_.getInt32(slot - DOLIR_STATE_SR0)},
                           i32);
    if (slot >= DOLIR_STATE_GQR0 && slot <= DOLIR_STATE_GQR7)
      return callStateRead(
          ReadSPR, {builder_.getInt32(SPR_GQR0 + slot - DOLIR_STATE_GQR0)},
          i32);
    switch (slot) {
    case DOLIR_STATE_LR:
      return callStateRead(ReadSPR, {builder_.getInt32(SPR_LR)}, i32);
    case DOLIR_STATE_CTR:
      return callStateRead(ReadSPR, {builder_.getInt32(SPR_CTR)}, i32);
    case DOLIR_STATE_CR:
      return callStateRead(ReadCR, {}, i32);
    case DOLIR_STATE_XER:
      return callStateRead(ReadXER, {}, i32);
    case DOLIR_STATE_FPSCR:
      return callStateRead(ReadFPSCR, {}, i32);
    case DOLIR_STATE_MSR:
      return callStateRead(ReadMSR, {}, i32);
    case DOLIR_STATE_SRR0:
      return callStateRead(ReadSPR, {builder_.getInt32(SPR_SRR0)}, i32);
    case DOLIR_STATE_SRR1:
      return callStateRead(ReadSPR, {builder_.getInt32(SPR_SRR1)}, i32);
    case DOLIR_STATE_DAR:
      return callStateRead(ReadSPR, {builder_.getInt32(SPR_DAR)}, i32);
    case DOLIR_STATE_DSISR:
      return callStateRead(ReadSPR, {builder_.getInt32(SPR_DSISR)}, i32);
    case DOLIR_STATE_EAR:
      return callStateRead(ReadSPR, {builder_.getInt32(SPR_EAR)}, i32);
    case DOLIR_STATE_HID2:
      return callStateRead(ReadSPR, {builder_.getInt32(SPR_HID2)}, i32);
    case DOLIR_STATE_EXCEPTION:
      return callStateRead(ReadExceptions, {}, i32);
    case DOLIR_STATE_RESERVE_ADDR:
      return callStateRead(ReadReserveAddress, {}, i32);
    case DOLIR_STATE_RESERVE_VALID:
      return builder_.CreateICmpNE(callStateRead(ReadReserveValid, {}, i32),
                                   builder_.getInt32(0));
    case DOLIR_STATE_PC:
      return entry_pc_ ? entry_pc_ : builder_.getInt32(source_.guest_start);
    default:
      return Constant::getNullValue(type(dolir_state_type(slot)));
    }
  }
  if (slot >= DOLIR_STATE_CR0 && slot <= DOLIR_STATE_CR7) {
    Value *packed = builder_.CreateLoad(Type::getInt32Ty(context_),
                                        bytePtr(offsetof(CPUState, cr)));
    const u32 shift = 28u - 4u * (slot - DOLIR_STATE_CR0);
    return builder_.CreateAnd(builder_.CreateLShr(packed, shift),
                              builder_.getInt32(0xFu));
  }
  if (slot >= DOLIR_STATE_XER_CA && slot <= DOLIR_STATE_XER_SO) {
    Value *packed = builder_.CreateLoad(Type::getInt32Ty(context_),
                                        bytePtr(offsetof(CPUState, xer)));
    const u32 shift = 29u + (slot - DOLIR_STATE_XER_CA);
    return builder_.CreateTrunc(builder_.CreateLShr(packed, shift),
                                Type::getInt1Ty(context_));
  }
  return builder_.CreateLoad(type(dolir_state_type(slot)),
                             bytePtr(stateOffset(slot)));
}

} // namespace dolllvm
