#include "backend/llvm/llvm_function_emitter.h"
#include "common/options.h"
#include "cpu/cpu.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

namespace dolllvm {

using namespace llvm;

AllocaInst *FunctionEmitter::temporary(Type *valueType, StringRef name) {
  IRBuilder<> allocations(entry_->getTerminator());
  return allocations.CreateAlloca(valueType, nullptr, name);
}

Value *FunctionEmitter::stateValue(DolIRStateSlot slot) {
  return builder_.CreateLoad(type(dolir_state_type(slot)), state_[slot]);
}

void FunctionEmitter::syncState(DolIRStateSlot slot) {
  storeContext(slot, stateValue(slot));
}

void FunctionEmitter::reloadState(DolIRStateSlot slot) {
  // Under DOLRECOMP_STATE_MEMORY the slot IS the CPUState field, so this would
  // be a load of a location stored straight back to itself.
  if (state_in_memory())
    return;
  builder_.CreateStore(loadContext(slot), state_[slot]);
}

void FunctionEmitter::reloadUsedState() {
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    if (used_[slot])
      reloadState(static_cast<DolIRStateSlot>(slot));
  }
  // The cycle counter is emitter bookkeeping rather than guest state, so it is
  // reset in both modes.
  builder_.CreateStore(builder_.getInt64(0), cycles_);
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
  builder_.CreateRetVoid();
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
}

void FunctionEmitter::emitProgramException(const DolIRInstruction &inst) {
  BasicBlock *taken = BasicBlock::Create(context_, "trap_taken", function_);
  BasicBlock *resume = BasicBlock::Create(context_, "trap_resume", function_);
  builder_.CreateCondBr(operand(inst, 0), taken, resume);
  builder_.SetInsertPoint(taken);
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
  builder_.CreateRetVoid();
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

void FunctionEmitter::emitExactFloat(u64 descriptor) {
  auto op = static_cast<DolIRExactFloat>(descriptor & 0xFFu);
  u32 d = (descriptor >> 8) & 0xFFu;
  u32 a = (descriptor >> 16) & 0xFFu;
  u32 b = (descriptor >> 24) & 0xFFu;
  u32 c = (descriptor >> 32) & 0xFFu;
  u32 crfd = (descriptor >> 40) & 0xFFu;
  auto fprSlot = [](u32 reg) {
    return static_cast<DolIRStateSlot>(DOLIR_STATE_FPR0 + reg);
  };
  auto ps1Slot = [](u32 reg) {
    return static_cast<DolIRStateSlot>(DOLIR_STATE_PS1_0 + reg);
  };
  Type *ptr = PointerType::getUnqual(context_);
  Type *f64 = Type::getDoubleTy(context_);
  syncState(DOLIR_STATE_FPSCR);

  if (op == DOLIR_EXACT_FCMPU || op == DOLIR_EXACT_FCMPO) {
    syncState(DOLIR_STATE_CR);
    auto callee = module_.getOrInsertFunction(
        "ppc_fcmp", FunctionType::get(Type::getVoidTy(context_),
                                      {ptr, Type::getInt8Ty(context_), f64, f64,
                                       Type::getInt1Ty(context_)},
                                      false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt8(crfd),
                                 stateValue(fprSlot(a)), stateValue(fprSlot(b)),
                                 builder_.getInt1(op == DOLIR_EXACT_FCMPO)});
    reloadState(DOLIR_STATE_CR);
    reloadState(DOLIR_STATE_FPSCR);
    return;
  }

  DolIRStateSlot destination = fprSlot(d);
  Value *old = stateValue(destination);
  if (op >= DOLIR_EXACT_FADDS && op <= DOLIR_EXACT_FRSP) {
    const char *name = nullptr;
    switch (op) {
    case DOLIR_EXACT_FADDS:
      name = "ppc_fadds";
      break;
    case DOLIR_EXACT_FSUBS:
      name = "ppc_fsubs";
      break;
    case DOLIR_EXACT_FMULS:
      name = "ppc_fmuls";
      break;
    case DOLIR_EXACT_FDIVS:
      name = "ppc_fdivs";
      break;
    case DOLIR_EXACT_FADD:
      name = "ppc_fadd";
      break;
    case DOLIR_EXACT_FSUB:
      name = "ppc_fsub";
      break;
    case DOLIR_EXACT_FMUL:
      name = "ppc_fmul";
      break;
    case DOLIR_EXACT_FDIV:
      name = "ppc_fdiv";
      break;
    case DOLIR_EXACT_FRSP:
      name = "ppc_frsp";
      break;
    default:
      break;
    }
    syncState(destination);
    bool single = op <= DOLIR_EXACT_FDIVS || op == DOLIR_EXACT_FRSP;
    if (single)
      syncState(ps1Slot(d));
    syncState(fprSlot(op == DOLIR_EXACT_FRSP ? b : a));
    if (op != DOLIR_EXACT_FRSP)
      syncState(
          fprSlot(op == DOLIR_EXACT_FMULS || op == DOLIR_EXACT_FMUL ? c : b));
    if (op == DOLIR_EXACT_FRSP) {
      auto callee = module_.getOrInsertFunction(
          name, FunctionType::get(
                    Type::getVoidTy(context_),
                    {ptr, Type::getInt8Ty(context_), Type::getInt8Ty(context_)},
                    false));
      builder_.CreateCall(callee,
                          {ctx_, builder_.getInt8(d), builder_.getInt8(b)});
    } else {
      auto callee = module_.getOrInsertFunction(
          name, FunctionType::get(Type::getVoidTy(context_),
                                  {ptr, Type::getInt8Ty(context_),
                                   Type::getInt8Ty(context_),
                                   Type::getInt8Ty(context_)},
                                  false));
      builder_.CreateCall(
          callee,
          {ctx_, builder_.getInt8(d), builder_.getInt8(a),
           builder_.getInt8(
               op == DOLIR_EXACT_FMULS || op == DOLIR_EXACT_FMUL ? c : b)});
    }
    reloadState(destination);
    if (single)
      reloadState(ps1Slot(d));
  } else if (op == DOLIR_EXACT_FCTIW || op == DOLIR_EXACT_FCTIWZ) {
    AllocaInst *output = temporary(Type::getInt64Ty(context_), "fctiw.result");
    builder_.CreateStore(
        builder_.CreateBitCast(old, Type::getInt64Ty(context_)), output);
    auto callee = module_.getOrInsertFunction(
        "ppc_fctiw",
        FunctionType::get(Type::getInt1Ty(context_),
                          {ptr, f64, Type::getInt1Ty(context_), ptr}, false));
    Value *success = builder_.CreateCall(
        callee, {ctx_, stateValue(fprSlot(b)),
                 builder_.getInt1(op == DOLIR_EXACT_FCTIWZ), output});
    Value *converted = builder_.CreateBitCast(
        builder_.CreateLoad(Type::getInt64Ty(context_), output), f64);
    builder_.CreateStore(builder_.CreateSelect(success, converted, old),
                         state_[destination]);
  } else if (op == DOLIR_EXACT_FRES || op == DOLIR_EXACT_FRSQRTE) {
    AllocaInst *output = temporary(f64, "estimate.result");
    builder_.CreateStore(old, output);
    const char *name = op == DOLIR_EXACT_FRES ? "ppc_fres" : "ppc_frsqrte";
    auto callee = module_.getOrInsertFunction(
        name,
        FunctionType::get(Type::getInt1Ty(context_), {ptr, f64, ptr}, false));
    Value *success =
        builder_.CreateCall(callee, {ctx_, stateValue(fprSlot(b)), output});
    Value *estimate = builder_.CreateLoad(f64, output);
    builder_.CreateStore(builder_.CreateSelect(success, estimate, old),
                         state_[destination]);
    if (op == DOLIR_EXACT_FRES) {
      DolIRStateSlot ps1 = ps1Slot(d);
      Value *oldPs1 = stateValue(ps1);
      builder_.CreateStore(builder_.CreateSelect(success, estimate, oldPs1),
                           state_[ps1]);
    }
  } else {
    bool single = op >= DOLIR_EXACT_FMADDS && op <= DOLIR_EXACT_FNMSUBS;
    bool subtract = op == DOLIR_EXACT_FMSUB || op == DOLIR_EXACT_FNMSUB ||
                    op == DOLIR_EXACT_FMSUBS || op == DOLIR_EXACT_FNMSUBS;
    bool negative = op == DOLIR_EXACT_FNMADD || op == DOLIR_EXACT_FNMSUB ||
                    op == DOLIR_EXACT_FNMADDS || op == DOLIR_EXACT_FNMSUBS;
    AllocaInst *output = temporary(f64, "fma.result");
    builder_.CreateStore(old, output);
    auto callee = module_.getOrInsertFunction(
        "ppc_fma",
        FunctionType::get(Type::getInt1Ty(context_),
                          {ptr, f64, f64, f64, Type::getInt1Ty(context_),
                           Type::getInt1Ty(context_), Type::getInt1Ty(context_),
                           ptr},
                          false));
    Value *success = builder_.CreateCall(
        callee,
        {ctx_, stateValue(fprSlot(a)), stateValue(fprSlot(c)),
         stateValue(fprSlot(b)), builder_.getInt1(single),
         builder_.getInt1(subtract), builder_.getInt1(negative), output});
    Value *fused = builder_.CreateLoad(f64, output);
    builder_.CreateStore(builder_.CreateSelect(success, fused, old),
                         state_[destination]);
    if (single) {
      DolIRStateSlot ps1 = ps1Slot(d);
      Value *oldPs1 = stateValue(ps1);
      builder_.CreateStore(builder_.CreateSelect(success, fused, oldPs1),
                           state_[ps1]);
    }
  }
  reloadState(DOLIR_STATE_FPSCR);
}

void FunctionEmitter::emitExactPaired(u64 descriptor) {
  auto op = static_cast<DolIRExactPaired>(descriptor & 0xFFu);
  u32 d = (descriptor >> 8) & 0xFFu;
  u32 a = (descriptor >> 16) & 0xFFu;
  u32 b = (descriptor >> 24) & 0xFFu;
  u32 c = (descriptor >> 32) & 0xFFu;
  u32 crfd = (descriptor >> 40) & 0xFFu;
  auto fprSlot = [](u32 reg) {
    return static_cast<DolIRStateSlot>(DOLIR_STATE_FPR0 + reg);
  };
  auto ps1Slot = [](u32 reg) {
    return static_cast<DolIRStateSlot>(DOLIR_STATE_PS1_0 + reg);
  };
  auto syncPair = [this, &fprSlot, &ps1Slot](u32 reg) {
    syncState(fprSlot(reg));
    syncState(ps1Slot(reg));
  };
  auto reloadPair = [this, &fprSlot, &ps1Slot](u32 reg) {
    reloadState(fprSlot(reg));
    reloadState(ps1Slot(reg));
  };
  Type *ptr = PointerType::getUnqual(context_);
  Type *i8 = Type::getInt8Ty(context_);
  Type *f64 = Type::getDoubleTy(context_);
  syncState(DOLIR_STATE_FPSCR);

  if (op >= DOLIR_EXACT_PS_CMPU0) {
    bool lane1 = op == DOLIR_EXACT_PS_CMPU1 || op == DOLIR_EXACT_PS_CMPO1;
    bool ordered = op == DOLIR_EXACT_PS_CMPO0 || op == DOLIR_EXACT_PS_CMPO1;
    syncState(DOLIR_STATE_CR);
    syncPair(a);
    syncPair(b);
    auto callee = module_.getOrInsertFunction(
        "ppc_fcmp", FunctionType::get(
                        Type::getVoidTy(context_),
                        {ptr, i8, f64, f64, Type::getInt1Ty(context_)}, false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt8(crfd),
                                 stateValue(lane1 ? ps1Slot(a) : fprSlot(a)),
                                 stateValue(lane1 ? ps1Slot(b) : fprSlot(b)),
                                 builder_.getInt1(ordered)});
    reloadState(DOLIR_STATE_CR);
    reloadState(DOLIR_STATE_FPSCR);
    return;
  }

  syncPair(d);
  if (op == DOLIR_EXACT_PS_RES || op == DOLIR_EXACT_PS_RSQRTE) {
    syncPair(b);
    const char *name =
        op == DOLIR_EXACT_PS_RES ? "ppc_ps_res_op" : "ppc_ps_rsqrte_op";
    auto callee = module_.getOrInsertFunction(
        name,
        FunctionType::get(Type::getVoidTy(context_), {ptr, i8, i8}, false));
    builder_.CreateCall(callee,
                        {ctx_, builder_.getInt8(d), builder_.getInt8(b)});
  } else if (op == DOLIR_EXACT_PS_MADD || op == DOLIR_EXACT_PS_MSUB ||
             op == DOLIR_EXACT_PS_NMADD || op == DOLIR_EXACT_PS_NMSUB) {
    syncPair(a);
    syncPair(b);
    syncPair(c);
    bool subtract = op == DOLIR_EXACT_PS_MSUB || op == DOLIR_EXACT_PS_NMSUB;
    bool negative = op == DOLIR_EXACT_PS_NMADD || op == DOLIR_EXACT_PS_NMSUB;
    auto callee = module_.getOrInsertFunction(
        "ppc_ps_madd_op",
        FunctionType::get(Type::getVoidTy(context_),
                          {ptr, i8, i8, i8, i8, Type::getInt1Ty(context_),
                           Type::getInt1Ty(context_)},
                          false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt8(d), builder_.getInt8(a),
                                 builder_.getInt8(c), builder_.getInt8(b),
                                 builder_.getInt1(subtract),
                                 builder_.getInt1(negative)});
  } else if (op == DOLIR_EXACT_PS_MADDS0 || op == DOLIR_EXACT_PS_MADDS1 ||
             op == DOLIR_EXACT_PS_SUM0 || op == DOLIR_EXACT_PS_SUM1) {
    syncPair(a);
    syncPair(b);
    syncPair(c);
    const char *name = op == DOLIR_EXACT_PS_MADDS0   ? "ppc_ps_madds0"
                       : op == DOLIR_EXACT_PS_MADDS1 ? "ppc_ps_madds1"
                       : op == DOLIR_EXACT_PS_SUM0   ? "ppc_ps_sum0"
                                                     : "ppc_ps_sum1";
    auto callee = module_.getOrInsertFunction(
        name, FunctionType::get(Type::getVoidTy(context_),
                                {ptr, i8, i8, i8, i8}, false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt8(d), builder_.getInt8(a),
                                 builder_.getInt8(c), builder_.getInt8(b)});
  } else if (op == DOLIR_EXACT_PS_MULS0 || op == DOLIR_EXACT_PS_MULS1) {
    syncPair(a);
    syncPair(c);
    const char *name =
        op == DOLIR_EXACT_PS_MULS0 ? "ppc_ps_muls0" : "ppc_ps_muls1";
    auto callee = module_.getOrInsertFunction(
        name,
        FunctionType::get(Type::getVoidTy(context_), {ptr, i8, i8, i8}, false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt8(d), builder_.getInt8(a),
                                 builder_.getInt8(c)});
  } else {
    syncPair(a);
    u32 rhs = op == DOLIR_EXACT_PS_MUL ? c : b;
    syncPair(rhs);
    const char *name = op == DOLIR_EXACT_PS_ADD   ? "ppc_ps_add_op"
                       : op == DOLIR_EXACT_PS_SUB ? "ppc_ps_sub_op"
                       : op == DOLIR_EXACT_PS_MUL ? "ppc_ps_mul_op"
                                                  : "ppc_ps_div_op";
    auto callee = module_.getOrInsertFunction(
        name,
        FunctionType::get(Type::getVoidTy(context_), {ptr, i8, i8, i8}, false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt8(d), builder_.getInt8(a),
                                 builder_.getInt8(rhs)});
  }
  reloadPair(d);
  reloadState(DOLIR_STATE_FPSCR);
}

Value *FunctionEmitter::emitPSQ(const DolIRInstruction &inst) {
  materialize(inst.guest_pc);
  u32 reg = inst.immediate & 0xFFu;
  bool w = ((inst.immediate >> 8) & 1u) != 0;
  u32 gqr = (inst.immediate >> 9) & 7u;
  bool indexed = ((inst.immediate >> 12) & 1u) != 0;
  bool load = inst.aux == DOLIR_HELPER_PSQ_LOAD;
  Type *ptr = PointerType::getUnqual(context_);
  auto callee = module_.getOrInsertFunction(
      load ? "ppc_psq_load" : "ppc_psq_store",
      FunctionType::get(Type::getInt1Ty(context_),
                        {ptr, Type::getInt8Ty(context_),
                         Type::getInt32Ty(context_), Type::getInt1Ty(context_),
                         Type::getInt8Ty(context_), Type::getInt1Ty(context_),
                         Type::getInt32Ty(context_)},
                        false));
  Value *success = builder_.CreateCall(
      callee, {ctx_, builder_.getInt8(reg), operand(inst, 0),
               builder_.getInt1(w), builder_.getInt8(gqr),
               builder_.getInt1(indexed), builder_.getInt32(inst.guest_pc)});
  BasicBlock *resume = BasicBlock::Create(context_, "psq_resume", function_);
  BasicBlock *failed = BasicBlock::Create(context_, "psq_exit", function_);
  builder_.CreateCondBr(success, resume, failed);
  builder_.SetInsertPoint(failed);
  builder_.CreateRetVoid();
  builder_.SetInsertPoint(resume);
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    if (used_[slot])
      reloadState(static_cast<DolIRStateSlot>(slot));
  }
  builder_.CreateStore(builder_.getInt64(0), cycles_);
  return ConstantInt::getTrue(context_);
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
  builder_.CreateRetVoid();
  builder_.SetInsertPoint(resume);
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    if (used_[slot])
      reloadState(static_cast<DolIRStateSlot>(slot));
  }
  builder_.CreateStore(builder_.getInt64(0), cycles_);
}

Value *FunctionEmitter::emitFPAvailable(u32 pc) {
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
  builder_.CreateRetVoid();
  builder_.SetInsertPoint(reload);
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    if (used_[slot]) {
      auto stateSlot = static_cast<DolIRStateSlot>(slot);
      Value *reloaded = loadContext(stateSlot);
      builder_.CreateStore(reloaded, state_[slot]);
    }
  }
  builder_.CreateStore(builder_.getInt64(0), cycles_);
  builder_.CreateBr(good);
  builder_.SetInsertPoint(good);
  PHINode *checked = builder_.CreatePHI(Type::getInt1Ty(context_), 2);
  checked->addIncoming(ConstantInt::getTrue(context_), fast);
  checked->addIncoming(available, reload);
  return checked;
}

} // namespace dolllvm
