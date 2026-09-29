#include "backend/llvm/emitter.h"
#include "cpu/cpu.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

namespace dolllvm {

using namespace llvm;

void FunctionEmitter::chargeCycles(u32 cycles) {
  chargeCycles(ConstantInt::get(Type::getInt64Ty(context_), cycles));
}

void FunctionEmitter::chargeCycles(Value *cycles) {
  Value *old = builder_.CreateLoad(Type::getInt64Ty(context_), cycles_);
  Value *next = builder_.CreateAdd(old, cycles);
  builder_.CreateStore(next, cycles_);
}

Value *FunctionEmitter::emitTimebaseRead() {
  if (!modern_runtime_)
    return stateValue(DOLIR_STATE_TIMEBASE);

  Type *i32 = Type::getInt32Ty(context_);
  Type *i64 = Type::getInt64Ty(context_);
  Type *pointer = PointerType::getUnqual(context_);
  StructType *servicesType =
      StructType::get(context_, {i32, i32, pointer, pointer, pointer, pointer});
  Value *services = runtimeField(11);
  Value *function = builder_.CreateLoad(
      pointer, builder_.CreateStructGEP(servicesType, services, 5));
  Value *serviceContext = builder_.CreateLoad(
      pointer, builder_.CreateStructGEP(servicesType, services, 2));
  Value *elapsed = builder_.CreateAdd(
      builder_.CreateLoad(i64, guard_cycles_local_),
      builder_.CreateLoad(i64, cycles_));
  CallInst *result = builder_.CreateCall(
      FunctionType::get(i64, {pointer, i64}, false), function,
      {serviceContext, elapsed});
  result->addFnAttr(Attribute::NoUnwind);
  return result;
}

void FunctionEmitter::emitTimebaseWrite(const DolIRInstruction &inst) {
  if (!modern_runtime_) {
    builder_.CreateStore(operand(inst, 0), state_[DOLIR_STATE_TIMEBASE]);
    noteStateWrite(DOLIR_STATE_TIMEBASE, operand(inst, 0));
    return;
  }

  BasicBlock *resume =
      BasicBlock::Create(context_, "timebase_write_resume", function_);
  sideExit(inst.guest_pc, 2);
  builder_.SetInsertPoint(resume);
}

void FunctionEmitter::settleCycles() {
  if (modern_runtime_) {
    Value *cycles = builder_.CreateLoad(Type::getInt64Ty(context_), cycles_);
    builder_.CreateStore(cycles, pending_cycles_);
    builder_.CreateStore(builder_.getInt64(0), cycles_);
    return;
  }
  Value *downcount =
      loadOffset(Type::getInt64Ty(context_), offsetof(CPUState, downcount));
  Value *cycles = builder_.CreateLoad(Type::getInt64Ty(context_), cycles_);
  builder_.CreateStore(builder_.CreateSub(downcount, cycles),
                       bytePtr(offsetof(CPUState, downcount)));
  Value *guard =
      builder_.CreateLoad(Type::getInt64Ty(context_), guard_cycles_local_);
  builder_.CreateStore(builder_.CreateAdd(guard, cycles), guard_cycles_local_);
  builder_.CreateStore(builder_.getInt64(0), cycles_);
  builder_.CreateStore(builder_.getInt64(0), pending_cycles_);
}

void FunctionEmitter::flushCallCounters(bool forceCycles) {
  if (forceCycles || !nativeCyclesInResult(abi_range_))
    builder_.CreateStore(
        builder_.CreateLoad(Type::getInt64Ty(context_), cycles_),
        pending_cycles_);
  if (!native_abi_ || !cold_escapes_)
    builder_.CreateStore(
        builder_.CreateLoad(Type::getInt64Ty(context_), guard_cycles_local_),
        guard_cycles_);
}

void FunctionEmitter::reloadCallCounters() {
  builder_.CreateStore(
      builder_.CreateLoad(Type::getInt64Ty(context_), pending_cycles_),
      cycles_);
  if (!native_abi_ || !cold_escapes_)
    builder_.CreateStore(
        builder_.CreateLoad(Type::getInt64Ty(context_), guard_cycles_),
        guard_cycles_local_);
}

void FunctionEmitter::emitBudgetGuard(u32 pc) {
  Type *i32 = Type::getInt32Ty(context_);
  Type *i64 = Type::getInt64Ty(context_);
  Type *pointer = PointerType::getUnqual(context_);
  Value *cycles = builder_.CreateAdd(
      builder_.CreateLoad(i64, guard_cycles_local_),
      builder_.CreateLoad(i64, cycles_));
  Value *overCycles = builder_.CreateICmpUGE(
      cycles, modern_runtime_ ? builder_.CreateLoad(i64,
                                                    builder_.CreateStructGEP(
                                                        chainType(), chain_, 4))
                              : static_cast<Value *>(ConstantInt::get(i64, 256)));
  Value *exhausted = overCycles;
  if (!native_abi_ || !cold_escapes_) {
    Value *steps = builder_.CreateLoad(i64, guard_steps_);
    Value *nextSteps = builder_.CreateAdd(steps, ConstantInt::get(i64, 1));
    builder_.CreateStore(nextSteps, guard_steps_);
    Value *overSteps = builder_.CreateICmpUGE(
        nextSteps, ConstantInt::get(i64, 2048));
    exhausted = builder_.CreateOr(overCycles, overSteps);
  }

  BasicBlock *run = BasicBlock::Create(context_, "budget_run", function_);
  BasicBlock *exit = BasicBlock::Create(context_, "budget_exit", function_);

  if (modern_runtime_ && native_abi_ && cold_escapes_) {
    BasicBlock *checkpoint =
        BasicBlock::Create(context_, "budget_checkpoint", function_);
    builder_.CreateCondBr(exhausted, checkpoint, run);
    builder_.SetInsertPoint(checkpoint);

    Value *services = runtimeField(11);
    BasicBlock *probe = BasicBlock::Create(context_, "budget_probe", function_);
    builder_.CreateCondBr(builder_.CreateIsNotNull(services), probe, exit);
    builder_.SetInsertPoint(probe);

    StructType *servicesType = StructType::get(
        context_, {i32, i32, pointer, pointer, pointer, pointer, pointer,
                   pointer, pointer, pointer});
    Value *structSize = builder_.CreateLoad(
        i32, builder_.CreateStructGEP(servicesType, services, 1));
    const DataLayout &layout = module_.getDataLayout();
    const u64 callbackEnd =
        layout.getStructLayout(servicesType)->getElementOffset(9) +
        layout.getPointerSize();
    BasicBlock *loadCallback =
        BasicBlock::Create(context_, "budget_load_callback", function_);
    builder_.CreateCondBr(
        builder_.CreateICmpUGE(structSize, builder_.getInt32(callbackEnd)),
        loadCallback, exit);
    builder_.SetInsertPoint(loadCallback);

    Value *callback = builder_.CreateLoad(
        pointer, builder_.CreateStructGEP(servicesType, services, 9));
    BasicBlock *invoke =
        BasicBlock::Create(context_, "budget_continue_invoke", function_);
    builder_.CreateCondBr(builder_.CreateIsNotNull(callback), invoke, exit);
    builder_.SetInsertPoint(invoke);

    Value *serviceContext = builder_.CreateLoad(
        pointer, builder_.CreateStructGEP(servicesType, services, 2));
    CallInst *remaining = builder_.CreateCall(
        FunctionType::get(i32, {pointer, i64, i32}, false), callback,
        {serviceContext, cycles, builder_.getInt32(pc)});
    remaining->addFnAttr(Attribute::NoUnwind);
    BasicBlock *renewed =
        BasicBlock::Create(context_, "budget_renewed", function_);
    builder_.CreateCondBr(
        builder_.CreateICmpNE(remaining, builder_.getInt32(0)), renewed, exit);

    builder_.SetInsertPoint(renewed);
    Value *newBudget =
        builder_.CreateAdd(cycles, builder_.CreateZExt(remaining, i64));
    builder_.CreateStore(newBudget,
                         builder_.CreateStructGEP(chainType(), chain_, 4));
    builder_.CreateBr(run);
  } else {
    builder_.CreateCondBr(exhausted, exit, run);
  }

  builder_.SetInsertPoint(exit);
  sideExit(pc);
  builder_.SetInsertPoint(run);
}

} // namespace dolllvm
