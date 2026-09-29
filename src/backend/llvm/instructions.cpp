#include "backend/llvm/emitter.h"
#include "backend/llvm/native_abi.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>

namespace dolllvm {

using namespace llvm;

Value *FunctionEmitter::operand(const DolIRInstruction &inst, u32 index) {
  return values_[inst.operands[index]];
}

Value *FunctionEmitter::castValue(DolIROp op, Type *resultType, Value *value) {
  switch (op) {
  case DOLIR_OP_TRUNC:
    return builder_.CreateTrunc(value, resultType);
  case DOLIR_OP_ZEXT:
    return builder_.CreateZExt(value, resultType);
  case DOLIR_OP_SEXT:
    return builder_.CreateSExt(value, resultType);
  case DOLIR_OP_BITCAST:
    return builder_.CreateBitCast(value, resultType);
  case DOLIR_OP_FPTRUNC:
    return builder_.CreateFPTrunc(value, resultType);
  case DOLIR_OP_FPEXT:
    return builder_.CreateFPExt(value, resultType);
  default:
    return nullptr;
  }
}

Value *FunctionEmitter::bswap(Value *value) {
  auto *integer = cast<IntegerType>(value->getType());
  if (integer->getBitWidth() == 8)
    return value;
  Function *intrinsic =
      Intrinsic::getDeclaration(&module_, Intrinsic::bswap, {value->getType()});
  return builder_.CreateCall(intrinsic, {value});
}

bool FunctionEmitter::emitInstruction(const DolIRInstruction &inst,
                                      raw_ostream &diagnostics) {
  current_pc_ = inst.guest_pc;
  Value *result = nullptr;
  Type *resultType = type(inst.type);
  switch (inst.op) {
  case DOLIR_OP_CONSTANT:
    if (inst.type == DOLIR_TYPE_F32)
      result = ConstantFP::get(
          context_, APFloat(APFloat::IEEEsingle(), APInt(32, inst.immediate)));
    else if (inst.type == DOLIR_TYPE_F64)
      result = ConstantFP::get(
          context_, APFloat(APFloat::IEEEdouble(), APInt(64, inst.immediate)));
    else
      result = ConstantInt::get(resultType, inst.immediate);
    break;
  case DOLIR_OP_STATE_READ:
    result = stateValue(static_cast<DolIRStateSlot>(inst.aux));
    break;
  case DOLIR_OP_STATE_WRITE:
    emitStateWrite(inst);
    break;
  case DOLIR_OP_ADD:
    result = builder_.CreateAdd(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_SUB:
    result = builder_.CreateSub(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_MUL:
    result = builder_.CreateMul(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_UDIV:
    result = builder_.CreateUDiv(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_SDIV:
    result = builder_.CreateSDiv(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_AND:
    result = builder_.CreateAnd(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_OR:
    result = builder_.CreateOr(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_XOR:
    result = builder_.CreateXor(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_NOT:
    result = builder_.CreateNot(operand(inst, 0));
    break;
  case DOLIR_OP_SHL:
    result = builder_.CreateShl(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_LSHR:
    result = builder_.CreateLShr(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_ASHR:
    result = builder_.CreateAShr(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_ROTL: {
    Function *intrinsic =
        Intrinsic::getDeclaration(&module_, Intrinsic::fshl, {resultType});
    result = builder_.CreateCall(
        intrinsic, {operand(inst, 0), operand(inst, 0), operand(inst, 1)});
    break;
  }
  case DOLIR_OP_CLZ: {
    Function *intrinsic =
        Intrinsic::getDeclaration(&module_, Intrinsic::ctlz, {resultType});
    result = builder_.CreateCall(
        intrinsic, {operand(inst, 0), ConstantInt::getFalse(context_)});
    break;
  }
  case DOLIR_OP_BSWAP:
    result = bswap(operand(inst, 0));
    break;
  case DOLIR_OP_TRUNC:
  case DOLIR_OP_ZEXT:
  case DOLIR_OP_SEXT:
  case DOLIR_OP_BITCAST:
  case DOLIR_OP_FPTRUNC:
  case DOLIR_OP_FPEXT:
    result = castValue(inst.op, resultType, operand(inst, 0));
    break;
  case DOLIR_OP_ICMP_EQ:
    result = builder_.CreateICmpEQ(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_ICMP_NE:
    result = builder_.CreateICmpNE(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_ICMP_ULT:
    result = builder_.CreateICmpULT(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_ICMP_ULE:
    result = builder_.CreateICmpULE(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_ICMP_SLT:
    result = builder_.CreateICmpSLT(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_ICMP_SLE:
    result = builder_.CreateICmpSLE(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_FCMP_OEQ:
    result = builder_.CreateFCmpOEQ(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_FCMP_OLT:
    result = builder_.CreateFCmpOLT(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_FCMP_OGE:
    result = builder_.CreateFCmpOGE(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_SELECT:
    result = builder_.CreateSelect(operand(inst, 0), operand(inst, 1),
                                   operand(inst, 2));
    break;
  case DOLIR_OP_FADD:
    result = builder_.CreateFAdd(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_FSUB:
    result = builder_.CreateFSub(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_FMUL:
    result = builder_.CreateFMul(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_FDIV:
    result = builder_.CreateFDiv(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_FNEG:
    result = builder_.CreateFNeg(operand(inst, 0));
    break;
  case DOLIR_OP_FABS: {
    Function *intrinsic =
        Intrinsic::getDeclaration(&module_, Intrinsic::fabs, {resultType});
    result = builder_.CreateCall(intrinsic, {operand(inst, 0)});
    break;
  }
  case DOLIR_OP_VECTOR_BUILD: {
    result = PoisonValue::get(resultType);
    result =
        builder_.CreateInsertElement(result, operand(inst, 0), uint64_t{0});
    result = builder_.CreateInsertElement(result, operand(inst, 1), 1u);
    break;
  }
  case DOLIR_OP_VECTOR_EXTRACT:
    result = builder_.CreateExtractElement(operand(inst, 0), inst.aux);
    break;
  case DOLIR_OP_VECTOR_SHUFFLE:
    result = builder_.CreateShuffleVector(
        operand(inst, 0), operand(inst, 1),
        {static_cast<int>(inst.aux & 0xFFu),
         static_cast<int>((inst.aux >> 8) & 0xFFu)});
    break;
  case DOLIR_OP_GUEST_LOAD:
    result = emitGuestLoad(inst, operand(inst, 0), resultType, inst.aux & 0xffu,
                           (inst.aux & 0x100u) != 0);
    break;
  case DOLIR_OP_GUEST_STORE:
    emitGuestStore(inst, operand(inst, 0), operand(inst, 1), inst.aux & 0xffu);
    break;
  case DOLIR_OP_HELPER_CALL:
    if (inst.aux == DOLIR_HELPER_FP_AVAILABLE)
      result = emitFPAvailable(inst.guest_pc);
    else if (inst.aux == DOLIR_HELPER_MEMORY_FENCE)
      builder_.CreateFence(AtomicOrdering::SequentiallyConsistent);
    else if (inst.aux == DOLIR_HELPER_EXACT_FLOAT)
      emitExactFloat(inst.immediate);
    else if (inst.aux == DOLIR_HELPER_EXACT_PAIRED)
      emitExactPaired(inst.immediate);
    else if (inst.aux == DOLIR_HELPER_PSQ_LOAD ||
             inst.aux == DOLIR_HELPER_PSQ_STORE)
      result = emitPSQ(inst);
    else if (inst.aux == DOLIR_HELPER_STORE_CONDITIONAL)
      emitStoreConditional(inst);
    else if (inst.aux == DOLIR_HELPER_FPSCR_UPDATED)
      emitFPSCRUpdated();
    else if (inst.aux == DOLIR_HELPER_FPSCR_BIT)
      emitFPSCRBit(inst.immediate);
    else if (inst.aux == DOLIR_HELPER_PROGRAM_EXCEPTION)
      emitProgramException(inst);
    else if (inst.aux == DOLIR_HELPER_SPR_READ)
      result = emitSPRRead(inst);
    else if (inst.aux == DOLIR_HELPER_SPR_WRITE)
      emitSPRWrite(inst);
    else if (inst.aux == DOLIR_HELPER_TIMEBASE_READ)
      result = emitTimebaseRead();
    else if (inst.aux == DOLIR_HELPER_TIMEBASE_WRITE)
      emitTimebaseWrite(inst);
    else if (inst.aux == DOLIR_HELPER_LSWX)
      emitLSWX(inst);
    else if (inst.aux == DOLIR_HELPER_DCBZ_L ||
             inst.aux == DOLIR_HELPER_ECIWX || inst.aux == DOLIR_HELPER_ECOWX ||
             inst.aux == DOLIR_HELPER_TLBIE ||
             inst.aux == DOLIR_HELPER_CACHE_CONTROL)
      result = emitRuntimeBoundary(inst);
    else {
      diagnostics << "dolllvm: unsupported helper " << inst.aux << " at 0x"
                  << format_hex_no_prefix(inst.guest_pc, 8) << "\n";
      return false;
    }
    break;
  default:
    diagnostics << "dolllvm: unsupported DolIR op " << unsigned(inst.op)
                << " at 0x" << format_hex_no_prefix(inst.guest_pc, 8) << "\n";
    return false;
  }
  if (inst.result)
    values_[inst.result] = result;
  return inst.type == DOLIR_TYPE_VOID || result != nullptr;
}
} // namespace dolllvm
