#include "backend/llvm/emitter.h"
#include "cpu/cpu.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>

namespace dolllvm {

using namespace llvm;

namespace {

constexpr u32 StructuredExitFlag = 0x80000000u;

} // namespace

void FunctionEmitter::emitEntry() {
  builder_.SetInsertPoint(entry_);
  StructType *chainTy = chainType();
  guard_cycles_ = builder_.CreateStructGEP(chainTy, chain_, 1);
  guard_steps_ = builder_.CreateStructGEP(chainTy, chain_, 2);
  pending_cycles_ = builder_.CreateStructGEP(chainTy, chain_, 3);
  entry_pc_ = builder_.CreateTrunc(control_pc_, Type::getInt32Ty(context_));
  return_pc_ = builder_.CreateTrunc(
      builder_.CreateLShr(control_pc_, builder_.getInt64(32)),
      Type::getInt32Ty(context_));
  Type *pointer = PointerType::getUnqual(context_);
  cycles_ =
      builder_.CreateAlloca(Type::getInt64Ty(context_), nullptr, "cycles");
  guard_cycles_local_ = builder_.CreateAlloca(Type::getInt64Ty(context_),
                                              nullptr, "guard_cycles_local");
  Value *initialCycles =
      initial_cycles_
          ? initial_cycles_
          : builder_.CreateLoad(Type::getInt64Ty(context_), pending_cycles_);
  builder_.CreateStore(initialCycles, cycles_);
  Value *initialGuard =
      native_abi_ && cold_escapes_
          ? static_cast<Value *>(builder_.getInt64(0))
          : builder_.CreateLoad(Type::getInt64Ty(context_), guard_cycles_);
  builder_.CreateStore(initialGuard, guard_cycles_local_);
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    if (!used_[slot])
      continue;
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (slotInMemory(stateSlot)) {
      state_[slot] = bytePtr(stateOffset(stateSlot));
      continue;
    }
    state_[slot] = builder_.CreateAlloca(type(dolir_state_type(stateSlot)),
                                         nullptr, "state");
  }
  Type *pairF32 = FixedVectorType::get(Type::getFloatTy(context_), 2);
  Type *pairF64 = FixedVectorType::get(Type::getDoubleTy(context_), 2);
  for (u32 reg = 0; reg < 32; reg++) {
    pair_f32_[reg] = builder_.CreateAlloca(pairF32, nullptr, "pair.f32");
    pair_f64_[reg] = builder_.CreateAlloca(pairF64, nullptr, "pair.f64");
  }
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    if (!used_[slot])
      continue;
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (slotInMemory(stateSlot))
      continue;
    Value *initial = native_abi_ && native_inputs_[slot]
                         ? static_cast<Value *>(native_inputs_[slot])
                         : loadContext(stateSlot);
    if (modern_runtime_ && native_abi_ && !native_inputs_[slot] &&
        stateSlot != DOLIR_STATE_PC && stateSlot != DOLIR_STATE_TIMEBASE &&
        stateSlot != DOLIR_STATE_PROGRAM_EXCEPTION &&
        stateSlot != DOLIR_STATE_DOWNCOUNT) {
      Type *i64 = Type::getInt64Ty(context_);
      ArrayType *valuesType = ArrayType::get(i64, DOLIR_STATE_COUNT);
      ArrayType *maskType = ArrayType::get(i64, DOLIR_STATE_MASK_WORDS);
      Value *values = builder_.CreateStructGEP(chainTy, chain_, 8);
      Value *dirtyMask = builder_.CreateStructGEP(chainTy, chain_, 9);
      const u32 word = slot / 64u;
      const u32 bit = slot & 63u;
      Value *maskWord = builder_.CreateLoad(
          i64, builder_.CreateInBoundsGEP(
                   maskType, dirtyMask,
                   {builder_.getInt64(0), builder_.getInt64(word)}));
      Value *dirty = builder_.CreateICmpNE(
          builder_.CreateAnd(maskWord, builder_.getInt64(u64(1) << bit)),
          builder_.getInt64(0));
      Value *chained = builder_.CreateLoad(
          i64, builder_.CreateInBoundsGEP(
                   valuesType, values,
                   {builder_.getInt64(0), builder_.getInt64(slot)}));
      Type *target = type(dolir_state_type(stateSlot));
      chained = target->isDoubleTy() ? builder_.CreateBitCast(chained, target)
                                     : builder_.CreateZExtOrTrunc(chained, target);
      initial = builder_.CreateSelect(dirty, chained, initial);
    }
    builder_.CreateStore(initial, state_[slot]);
  }
  BasicBlock *nativeEntry =
      BasicBlock::Create(context_, "native_entry", function_);
  BasicBlock *query =
      BasicBlock::Create(context_, "interception_query", function_);
  Function *expect = Intrinsic::getDeclaration(&module_, Intrinsic::expect,
                                               {Type::getInt1Ty(context_)});
  if (modern_runtime_) {
    builder_.CreateBr(query);
  } else {
    Value *hostCall = loadOffset(pointer, offsetof(CPUState, host_call));
    Value *noInterception = builder_.CreateCall(
        expect, {builder_.CreateIsNull(hostCall), builder_.getTrue()});
    builder_.CreateCondBr(noInterception, nativeEntry, query);
  }
  builder_.SetInsertPoint(query);
  FunctionCallee available;
  Value *canEnter = nullptr;
  if (modern_runtime_) {
    available = module_.getOrInsertFunction(
        "moderngekko_native_region_available",
        FunctionType::get(Type::getInt1Ty(context_),
            {pointer, Type::getInt32Ty(context_), Type::getInt32Ty(context_)},
            false));
    canEnter = builder_.CreateCall(available, {ctx_,
                                        builder_.getInt32(source_.guest_start),
                                        builder_.getInt32(source_.guest_end)});
  } else {
    available = module_.getOrInsertFunction(
        "ppc_native_region_available",
        FunctionType::get(
            Type::getInt1Ty(context_),
            {pointer, Type::getInt32Ty(context_), Type::getInt32Ty(context_)},
            false));
    canEnter = builder_.CreateCall(
        available, {ctx_, builder_.getInt32(source_.guest_start),
                    builder_.getInt32(source_.guest_end)});
  }
  canEnter = builder_.CreateCall(expect, {canEnter, builder_.getTrue()});
  BasicBlock *intercept =
      BasicBlock::Create(context_, "interception_exit", function_);
  builder_.CreateCondBr(canEnter, nativeEntry, intercept);
  builder_.SetInsertPoint(intercept);
  if (modern_runtime_)
    builder_.CreateStore(builder_.getInt32(4),
                         builder_.CreateStructGEP(chainType(), chain_, 7));
  materialize(entry_pc_);
  returnFromBody();

  builder_.SetInsertPoint(nativeEntry);
  if (modern_runtime_) {
    ram_ = runtimeField(3);
    ram_size_ = runtimeField(4);
    mem2_ = runtimeField(6);
    mem2_size_ = runtimeField(7);
  } else {
    ram_ = loadOffset(pointer, offsetof(CPUState, ram));
    ram_size_ =
        loadOffset(Type::getInt32Ty(context_), offsetof(CPUState, ram_size));
    mem2_ = loadOffset(pointer, offsetof(CPUState, exram));
    mem2_size_ =
        loadOffset(Type::getInt32Ty(context_), offsetof(CPUState, exram_size));
  }
  Function *assume = Intrinsic::getDeclaration(&module_, Intrinsic::assume);
  Value *ramSizeValid =
      fixed_memory_layout_
          ? builder_.CreateICmpEQ(ram_size_,
                                  builder_.getInt32(expected_ram_size_))
          : builder_.CreateICmpULE(ram_size_,
                                   builder_.getInt32(GC_MAIN_RAM_SIZE));
  builder_.CreateCall(assume, {ramSizeValid});
  builder_.CreateCall(
      assume,
      {builder_.CreateICmpULE(mem2_size_, builder_.getInt32(WII_MEM2_SIZE))});
  if (fixed_memory_layout_ && expected_mem2_size_) {
    Value *mem2SizeValid = builder_.CreateOr(
        builder_.CreateIsNull(mem2_),
        builder_.CreateICmpEQ(mem2_size_,
                              builder_.getInt32(expected_mem2_size_)));
    builder_.CreateCall(assume, {mem2SizeValid});
  }
  initializeEntryControls();
  BasicBlock *bad = BasicBlock::Create(context_, "entry_miss", function_);
  u32 nativeEntries = 0;
  for (u32 i = 0; i < source_.block_count; i++)
    nativeEntries += region_leaders_[i] &&
                     source_.blocks[i].terminator.kind != DOLIR_TERM_FALLBACK;
  auto *dispatch = builder_.CreateSwitch(entry_pc_, bad, nativeEntries);
  for (u32 i = 0; i < source_.block_count; i++) {
    if (!region_leaders_[i] ||
        source_.blocks[i].terminator.kind == DOLIR_TERM_FALLBACK)
      continue;
    dispatch->addCase(ConstantInt::get(Type::getInt32Ty(context_),
                                       source_.blocks[i].guest_address),
                      blocks_[i]);
  }
  emitColdEntry(bad);
}

void FunctionEmitter::syncDirtyState() {
  if (modern_runtime_) {
    u64 localDirty[DOLIR_STATE_MASK_WORDS]{};
    for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
      if (!dirty_[slot])
        continue;
      localDirty[slot / 64u] |= u64(1) << (slot & 63u);
    }
    stageStateMask(localDirty);
    return;
  }
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    if (!dirty_[slot] || slot == DOLIR_STATE_FPSCR)
      continue;
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (slotInMemory(stateSlot))
      continue;
    storeContext(
        stateSlot,
        builder_.CreateLoad(type(dolir_state_type(stateSlot)), state_[slot]));
  }
  materializeFPRF();
  if (dirty_[DOLIR_STATE_FPSCR])
    storeContext(DOLIR_STATE_FPSCR, stateValue(DOLIR_STATE_FPSCR));
}

void FunctionEmitter::returnFromBody() {
  if (modern_runtime_ && native_abi_ && cold_escapes_) {
    Value *reasonPtr =
        builder_.CreateStructGEP(chainType(), chain_, 7);
    Value *reason =
        builder_.CreateLoad(Type::getInt32Ty(context_), reasonPtr);
    builder_.CreateStore(
        builder_.CreateOr(reason, builder_.getInt32(StructuredExitFlag)),
        reasonPtr);
    returnStructuredExit();
    return;
  }
  if (!cold_escapes_)
    flushCallCounters();
  if (native_abi_) {
    if (cold_escapes_) {
      Value *buffer = builder_.CreateStructGEP(chainType(), chain_, 0);
      if (intrinsic_escapes_) {
        builder_.CreateCall(
            Intrinsic::getDeclaration(&module_, Intrinsic::eh_sjlj_longjmp),
            {buffer});
      } else {
        auto longjmp = module_.getOrInsertFunction(
            "_longjmp", FunctionType::get(Type::getVoidTy(context_),
                                          {PointerType::getUnqual(context_),
                                           Type::getInt32Ty(context_)},
                                          false));
        if (auto *longjmpFunction = dyn_cast<Function>(longjmp.getCallee())) {
          longjmpFunction->addFnAttr(Attribute::NoReturn);
          longjmpFunction->addFnAttr(Attribute::NoUnwind);
        }
        builder_.CreateCall(longjmp, {buffer, builder_.getInt32(1)});
      }
      builder_.CreateUnreachable();
      return;
    }
    Value *pc = loadOffset(Type::getInt32Ty(context_), offsetof(CPUState, pc));
    builder_.CreateRet(nativeResult(pc, false, true));
  } else {
    builder_.CreateRetVoid();
  }
}

Value *FunctionEmitter::structuredExitPending() {
  if (!modern_runtime_ || !native_abi_ || !cold_escapes_)
    return builder_.getFalse();
  Value *reason = builder_.CreateLoad(
      Type::getInt32Ty(context_), builder_.CreateStructGEP(chainType(), chain_, 7));
  return builder_.CreateICmpNE(
      builder_.CreateAnd(reason, builder_.getInt32(StructuredExitFlag)),
      builder_.getInt32(0));
}

void FunctionEmitter::returnStructuredExit() {
  if (!modern_runtime_ || !native_abi_ || !cold_escapes_) {
    returnFromBody();
    return;
  }
  Type *resultType = nativeResultType(abi_range_);
  if (!resultType->isVoidTy())
    builder_.CreateRet(UndefValue::get(resultType));
  else
    builder_.CreateRetVoid();
}

} // namespace dolllvm
