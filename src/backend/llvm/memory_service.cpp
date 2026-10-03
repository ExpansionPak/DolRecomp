#include "backend/llvm/emitter.h"
#include "cpu/cpu.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Module.h>

namespace dolllvm {

using namespace llvm;

namespace {

constexpr u32 ServiceContinue = 0;
constexpr u32 ServiceException = 1;
constexpr u32 ServiceYield = 2;
constexpr u32 ServiceFallback = 3;
constexpr u32 ServiceStop = 4;
constexpr u32 ExitException = 1;
constexpr u32 ExitFallback = 2;
constexpr u32 ExitStop = 5;

} // namespace

Value *FunctionEmitter::externalRead(Value *address, u32 width, Value **yielded) {
  *yielded = builder_.getFalse();
  if (modern_runtime_) {
    Type *i32 = Type::getInt32Ty(context_);
    Type *i64 = Type::getInt64Ty(context_);
    Type *pointer = PointerType::getUnqual(context_);
    const u32 blockIndex = (current_pc_ - source_.guest_start) / 4u;
    Value *elapsed = builder_.CreateSub(
        builder_.CreateAdd(builder_.CreateLoad(i64, guard_cycles_local_),
                           builder_.CreateLoad(i64, cycles_)),
        builder_.getInt64(source_.blocks[blockIndex].cycle_cost));
    materializeMemoryService(current_pc_);
    AllocaInst *valueOut = temporary(i64, "read_service_value");
    AllocaInst *remainingOut = temporary(i32, "read_service_remaining");
    FunctionCallee service = module_.getOrInsertFunction(
        "moderngekko_read_memory",
        FunctionType::get(i32, {pointer, i32, i32, i32, i64, pointer, pointer},
                          false));
    CallInst *status = builder_.CreateCall(
        service, {ctx_, builder_.getInt32(current_pc_), address,
                  builder_.getInt32(width), elapsed, valueOut, remainingOut});
    status->addFnAttr(Attribute::NoUnwind);
    Value *isYield =
        builder_.CreateICmpEQ(status, builder_.getInt32(ServiceYield));
    Value *completed = builder_.CreateOr(
        builder_.CreateICmpEQ(status, builder_.getInt32(ServiceContinue)), isYield);
    BasicBlock *resume =
        BasicBlock::Create(context_, "read_service_resume", function_);
    BasicBlock *failed =
        BasicBlock::Create(context_, "read_service_exit", function_);
    builder_.CreateCondBr(completed, resume, failed,
                          MDBuilder(context_).createBranchWeights(2000, 1));

    builder_.SetInsertPoint(failed);
    Value *reason = builder_.CreateSelect(
        builder_.CreateICmpEQ(status, builder_.getInt32(ServiceException)),
        builder_.getInt32(ExitException),
        builder_.CreateSelect(
            builder_.CreateICmpEQ(status, builder_.getInt32(ServiceStop)),
            builder_.getInt32(ExitStop), builder_.getInt32(ExitFallback)));
    branchMemoryServiceFailure(reason, elapsed);

    builder_.SetInsertPoint(resume);
    reloadCallCounters();
    Value *remaining = builder_.CreateLoad(i32, remainingOut);
    Value *budget =
        builder_.CreateAdd(elapsed, builder_.CreateZExt(remaining, i64));
    builder_.CreateStore(budget, builder_.CreateStructGEP(chainType(), chain_, 4));
    Value *completedCycles = builder_.CreateAdd(
        builder_.CreateLoad(i64, guard_cycles_local_),
        builder_.CreateLoad(i64, cycles_));
    *yielded = builder_.CreateOr(
        isYield, builder_.CreateICmpUGE(completedCycles, budget));
    return builder_.CreateLoad(i64, valueOut);
  }

  Type *pointer = PointerType::getUnqual(context_);
  Value *function = loadOffset(pointer, offsetof(CPUState, external_read));
  BasicBlock *call = BasicBlock::Create(context_, "read_external", function_);
  BasicBlock *zero = BasicBlock::Create(context_, "read_unmapped", function_);
  BasicBlock *join = BasicBlock::Create(context_, "read_slow_join", function_);
  builder_.CreateCondBr(builder_.CreateIsNotNull(function), call, zero,
                        MDBuilder(context_).createBranchWeights(2000, 1));
  builder_.SetInsertPoint(call);
  materialize(current_pc_);
  auto *functionType = FunctionType::get(
      Type::getInt64Ty(context_),
      {pointer, Type::getInt32Ty(context_), Type::getInt8Ty(context_)}, false);
  Value *called = builder_.CreateCall(
      functionType, function, {ctx_, address, builder_.getInt8(width)});
  Value *exception =
      loadOffset(Type::getInt32Ty(context_), offsetof(CPUState, exception));
  BasicBlock *resume =
      BasicBlock::Create(context_, "read_slow_resume", function_);
  BasicBlock *failed =
      BasicBlock::Create(context_, "read_slow_exit", function_);
  builder_.CreateCondBr(builder_.CreateICmpEQ(exception, builder_.getInt32(0)),
                        resume, failed);
  builder_.SetInsertPoint(failed);
  returnFromBody();
  builder_.SetInsertPoint(resume);
  reloadUsedState();
  builder_.CreateBr(join);
  BasicBlock *calledEnd = builder_.GetInsertBlock();
  builder_.SetInsertPoint(zero);
  Value *empty = builder_.getInt64(0);
  builder_.CreateBr(join);
  builder_.SetInsertPoint(join);
  PHINode *phi = builder_.CreatePHI(Type::getInt64Ty(context_), 2);
  phi->addIncoming(called, calledEnd);
  phi->addIncoming(empty, zero);
  return phi;
}

void FunctionEmitter::branchMemoryServiceFailure(Value *reason, Value *elapsed) {
  if (!memory_service_failure_) {
    memory_service_failure_ =
        BasicBlock::Create(context_, "memory_service_failure", function_);
    IRBuilder<> failureBuilder(memory_service_failure_);
    memory_service_failure_reason_ = failureBuilder.CreatePHI(
        Type::getInt32Ty(context_), 0, "memory_service_failure_reason");
    memory_service_failure_elapsed_ = failureBuilder.CreatePHI(
        Type::getInt64Ty(context_), 0, "memory_service_failure_elapsed");
  }
  BasicBlock *from = builder_.GetInsertBlock();
  memory_service_failure_reason_->addIncoming(reason, from);
  memory_service_failure_elapsed_->addIncoming(elapsed, from);
  builder_.CreateBr(memory_service_failure_);
}

void FunctionEmitter::emitMemoryServiceFailure() {
  if (!memory_service_failure_)
    return;

  IRBuilderBase::InsertPoint saved = builder_.saveIP();
  builder_.SetInsertPoint(memory_service_failure_);
  builder_.CreateStore(memory_service_failure_reason_,
                       builder_.CreateStructGEP(chainType(), chain_, 7));
  Value *localElapsed = builder_.CreateSub(
      memory_service_failure_elapsed_,
      builder_.CreateLoad(Type::getInt64Ty(context_), guard_cycles_local_));
  builder_.CreateStore(localElapsed, pending_cycles_);

  u64 registerDirty[DOLIR_STATE_MASK_WORDS]{};
  for (u32 slot = DOLIR_STATE_GPR0; slot <= DOLIR_STATE_PS1_31; slot++) {
    if (dirty_[slot])
      registerDirty[slot / 64u] |= u64(1) << (slot & 63u);
  }
  stageStateMask(registerDirty);
  commitModernState();
  returnFromBody();
  builder_.restoreIP(saved);
}

Value *FunctionEmitter::externalWrite(Value *address, Value *value, u32 width) {
  if (modern_runtime_) {
    Type *i32 = Type::getInt32Ty(context_);
    Type *i64 = Type::getInt64Ty(context_);
    Type *pointer = PointerType::getUnqual(context_);
    const u32 blockIndex = (current_pc_ - source_.guest_start) / 4u;
    Value *elapsed = builder_.CreateSub(
        builder_.CreateAdd(builder_.CreateLoad(i64, guard_cycles_local_),
                           builder_.CreateLoad(i64, cycles_)),
        builder_.getInt64(source_.blocks[blockIndex].cycle_cost));
    materializeMemoryService(current_pc_);
    AllocaInst *remainingOut = temporary(i32, "write_service_remaining");
    FunctionCallee service = module_.getOrInsertFunction(
        "moderngekko_write_memory",
        FunctionType::get(i32, {pointer, i32, i32, i64, i32, i64, pointer},
                          false));
    CallInst *status = builder_.CreateCall(
        service, {ctx_, builder_.getInt32(current_pc_), address,
                  builder_.CreateZExtOrTrunc(value, i64), builder_.getInt32(width),
                  elapsed, remainingOut});
    status->addFnAttr(Attribute::NoUnwind);
    Value *isYield =
        builder_.CreateICmpEQ(status, builder_.getInt32(ServiceYield));
    Value *completed = builder_.CreateOr(
        builder_.CreateICmpEQ(status, builder_.getInt32(ServiceContinue)), isYield);
    BasicBlock *resume =
        BasicBlock::Create(context_, "write_service_resume", function_);
    BasicBlock *failed =
        BasicBlock::Create(context_, "write_service_exit", function_);
    builder_.CreateCondBr(completed, resume, failed,
                          MDBuilder(context_).createBranchWeights(2000, 1));

    builder_.SetInsertPoint(failed);
    Value *reason = builder_.CreateSelect(
        builder_.CreateICmpEQ(status, builder_.getInt32(ServiceException)),
        builder_.getInt32(ExitException),
        builder_.CreateSelect(
            builder_.CreateICmpEQ(status, builder_.getInt32(ServiceStop)),
            builder_.getInt32(ExitStop), builder_.getInt32(ExitFallback)));
    branchMemoryServiceFailure(reason, elapsed);

    builder_.SetInsertPoint(resume);
    reloadCallCounters();
    Value *remaining = builder_.CreateLoad(i32, remainingOut);
    Value *budget =
        builder_.CreateAdd(elapsed, builder_.CreateZExt(remaining, i64));
    builder_.CreateStore(budget, builder_.CreateStructGEP(chainType(), chain_, 4));
    Value *completedCycles = builder_.CreateAdd(
        builder_.CreateLoad(i64, guard_cycles_local_),
        builder_.CreateLoad(i64, cycles_));
    return builder_.CreateOr(
        isYield, builder_.CreateICmpUGE(completedCycles, budget));
  }

  Type *pointer = PointerType::getUnqual(context_);
  Value *function = loadOffset(pointer, offsetof(CPUState, external_write));
  BasicBlock *call = BasicBlock::Create(context_, "write_external", function_);
  BasicBlock *done =
      BasicBlock::Create(context_, "write_slow_done", function_);
  builder_.CreateCondBr(builder_.CreateIsNotNull(function), call, done);
  builder_.SetInsertPoint(call);
  materialize(current_pc_);
  auto *functionType = FunctionType::get(
      Type::getVoidTy(context_),
      {pointer, Type::getInt32Ty(context_), Type::getInt64Ty(context_),
       Type::getInt8Ty(context_)},
      false);
  builder_.CreateCall(
      functionType, function,
      {ctx_, address,
       builder_.CreateZExtOrTrunc(value, Type::getInt64Ty(context_)),
       builder_.getInt8(width)});
  Value *exception =
      loadOffset(Type::getInt32Ty(context_), offsetof(CPUState, exception));
  BasicBlock *resume =
      BasicBlock::Create(context_, "write_slow_resume", function_);
  BasicBlock *failed =
      BasicBlock::Create(context_, "write_slow_exit", function_);
  builder_.CreateCondBr(builder_.CreateICmpEQ(exception, builder_.getInt32(0)),
                        resume, failed);
  builder_.SetInsertPoint(failed);
  returnFromBody();
  builder_.SetInsertPoint(resume);
  reloadUsedState();
  builder_.CreateBr(done);
  builder_.SetInsertPoint(done);
  return builder_.getFalse();
}

} // namespace dolllvm
