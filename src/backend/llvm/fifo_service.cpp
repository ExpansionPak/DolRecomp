#include "backend/llvm/emitter.h"

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

Value *FunctionEmitter::externalFifoWrite(Value *address, Value *value,
                                          u32 width) {
  Type *i32 = Type::getInt32Ty(context_);
  Type *i64 = Type::getInt64Ty(context_);
  Type *pointer = PointerType::getUnqual(context_);
  Value *services = runtimeField(11);
  Value *elapsed = nullptr;
  const u32 blockIndex = (current_pc_ - source_.guest_start) / 4u;
  elapsed = builder_.CreateSub(
      builder_.CreateAdd(builder_.CreateLoad(i64, guard_cycles_local_),
                         builder_.CreateLoad(i64, cycles_)),
      builder_.getInt64(source_.blocks[blockIndex].cycle_cost));
  AllocaInst *remainingOut = temporary(i32, "fifo_service_remaining");

  SmallVector<Type *, 14> serviceFields = {i32, i32};
  serviceFields.append(12, pointer);
  StructType *servicesType = StructType::get(context_, serviceFields);
  const DataLayout &layout = module_.getDataLayout();
  const StructLayout *serviceLayout = layout.getStructLayout(servicesType);
  const u64 tryFifoEnd =
      serviceLayout->getElementOffset(8) + layout.getPointerSize();
  const u64 partialFifoEnd =
      serviceLayout->getElementOffset(13) + layout.getPointerSize();

  BasicBlock *tryProbe =
      BasicBlock::Create(context_, "fifo_try_probe", function_);
  BasicBlock *tryLoad = BasicBlock::Create(context_, "fifo_try_load", function_);
  BasicBlock *tryInvoke =
      BasicBlock::Create(context_, "fifo_try_invoke", function_);
  BasicBlock *partialProbe =
      BasicBlock::Create(context_, "fifo_partial_probe", function_);
  BasicBlock *partialLoad =
      BasicBlock::Create(context_, "fifo_partial_load", function_);
  BasicBlock *partialPrep =
      BasicBlock::Create(context_, "fifo_partial_prep", function_);
  BasicBlock *partialInvoke =
      BasicBlock::Create(context_, "fifo_partial_invoke", function_);
  BasicBlock *partialClassify =
      BasicBlock::Create(context_, "fifo_partial_classify", function_);
  BasicBlock *partialCompleted =
      BasicBlock::Create(context_, "fifo_partial_completed", function_);
  BasicBlock *partialFailed =
      BasicBlock::Create(context_, "fifo_partial_failed", function_);
  BasicBlock *fullPrep =
      BasicBlock::Create(context_, "fifo_full_prep", function_);
  BasicBlock *fullInvoke =
      BasicBlock::Create(context_, "fifo_full_invoke", function_);
  BasicBlock *fullCompleted =
      BasicBlock::Create(context_, "fifo_full_completed", function_);
  BasicBlock *fullFailed =
      BasicBlock::Create(context_, "fifo_full_failed", function_);
  BasicBlock *timedResume =
      BasicBlock::Create(context_, "fifo_timed_resume", function_);
  BasicBlock *done = BasicBlock::Create(context_, "fifo_done", function_);

  builder_.CreateCondBr(builder_.CreateIsNotNull(services), tryProbe, fullPrep);

  builder_.SetInsertPoint(tryProbe);
  Value *structSize = builder_.CreateLoad(
      i32, builder_.CreateStructGEP(servicesType, services, 1));
  builder_.CreateCondBr(
      builder_.CreateICmpUGE(structSize, builder_.getInt32(tryFifoEnd)), tryLoad,
      fullPrep);

  builder_.SetInsertPoint(tryLoad);
  Value *tryCallback = builder_.CreateLoad(
      pointer, builder_.CreateStructGEP(servicesType, services, 8));
  builder_.CreateCondBr(builder_.CreateIsNotNull(tryCallback), tryInvoke,
                        partialProbe);

  builder_.SetInsertPoint(tryInvoke);
  Value *serviceContext = builder_.CreateLoad(
      pointer, builder_.CreateStructGEP(servicesType, services, 2));
  CallInst *buffered = builder_.CreateCall(
      FunctionType::get(i32, {pointer, i64, i32}, false), tryCallback,
      {serviceContext, builder_.CreateZExtOrTrunc(value, i64),
       builder_.getInt32(width)});
  buffered->addFnAttr(Attribute::NoUnwind);
  BasicBlock *tryBuffered =
      BasicBlock::Create(context_, "fifo_try_buffered", function_);
  builder_.CreateCondBr(builder_.CreateICmpNE(buffered, builder_.getInt32(0)),
                        tryBuffered, partialProbe,
                        MDBuilder(context_).createBranchWeights(2000, 1));
  builder_.SetInsertPoint(tryBuffered);
  builder_.CreateBr(done);

  builder_.SetInsertPoint(partialProbe);
  // services is known non-null on every edge reaching this block.
  Value *partialStructSize = builder_.CreateLoad(
      i32, builder_.CreateStructGEP(servicesType, services, 1));
  builder_.CreateCondBr(
      builder_.CreateICmpUGE(partialStructSize,
                             builder_.getInt32(partialFifoEnd)),
      partialLoad, fullPrep);

  builder_.SetInsertPoint(partialLoad);
  Value *partialCallback = builder_.CreateLoad(
      pointer, builder_.CreateStructGEP(servicesType, services, 13));
  builder_.CreateCondBr(builder_.CreateIsNotNull(partialCallback), partialPrep,
                        fullPrep);

  builder_.SetInsertPoint(partialPrep);
  materializeFifoService(current_pc_);
  builder_.CreateBr(partialInvoke);

  builder_.SetInsertPoint(partialInvoke);
  Value *partialContext = builder_.CreateLoad(
      pointer, builder_.CreateStructGEP(servicesType, services, 2));
  StructType *resultType = StructType::get(context_, {i64, i32, i32});
  CallInst *partialResult = builder_.CreateCall(
      FunctionType::get(resultType, {pointer, i32, i32, i64, i32, i64}, false),
      partialCallback,
      {partialContext, builder_.getInt32(current_pc_), address,
       builder_.CreateZExtOrTrunc(value, i64), builder_.getInt32(width), elapsed});
  partialResult->addFnAttr(Attribute::NoUnwind);
  Value *partialStatus = builder_.CreateExtractValue(partialResult, 1);
  Value *partialRemaining = builder_.CreateExtractValue(partialResult, 2);
  builder_.CreateStore(partialRemaining, remainingOut);
  builder_.CreateCondBr(
      builder_.CreateICmpEQ(partialStatus, builder_.getInt32(ServiceFallback)),
      fullInvoke, partialClassify, MDBuilder(context_).createBranchWeights(1, 2000));

  builder_.SetInsertPoint(partialClassify);
  Value *partialIsYield =
      builder_.CreateICmpEQ(partialStatus, builder_.getInt32(ServiceYield));
  Value *partialSuccess = builder_.CreateOr(
      builder_.CreateICmpEQ(partialStatus, builder_.getInt32(ServiceContinue)),
      partialIsYield);
  builder_.CreateCondBr(partialSuccess, partialCompleted, partialFailed,
                        MDBuilder(context_).createBranchWeights(2000, 1));

  builder_.SetInsertPoint(partialCompleted);
  if (used_[DOLIR_STATE_EXCEPTION])
    reloadState(DOLIR_STATE_EXCEPTION);
  builder_.CreateBr(timedResume);

  builder_.SetInsertPoint(partialFailed);
  Value *partialReason = builder_.CreateSelect(
      builder_.CreateICmpEQ(partialStatus, builder_.getInt32(ServiceException)),
      builder_.getInt32(ExitException),
      builder_.CreateSelect(
          builder_.CreateICmpEQ(partialStatus, builder_.getInt32(ServiceStop)),
          builder_.getInt32(ExitStop), builder_.getInt32(ExitFallback)));
  branchMemoryServiceFailure(partialReason, elapsed);

  builder_.SetInsertPoint(fullPrep);
  materializeMemoryService(current_pc_);
  builder_.CreateBr(fullInvoke);

  builder_.SetInsertPoint(fullInvoke);
  FunctionCallee fullService = module_.getOrInsertFunction(
      "moderngekko_write_memory",
      FunctionType::get(i32, {pointer, i32, i32, i64, i32, i64, pointer}, false));
  CallInst *fullStatus = builder_.CreateCall(
      fullService,
      {ctx_, builder_.getInt32(current_pc_), address,
       builder_.CreateZExtOrTrunc(value, i64), builder_.getInt32(width), elapsed,
       remainingOut});
  fullStatus->addFnAttr(Attribute::NoUnwind);
  Value *fullIsYield =
      builder_.CreateICmpEQ(fullStatus, builder_.getInt32(ServiceYield));
  Value *fullSuccess = builder_.CreateOr(
      builder_.CreateICmpEQ(fullStatus, builder_.getInt32(ServiceContinue)),
      fullIsYield);
  builder_.CreateCondBr(fullSuccess, fullCompleted, fullFailed,
                        MDBuilder(context_).createBranchWeights(2000, 1));

  builder_.SetInsertPoint(fullCompleted);
  builder_.CreateBr(timedResume);

  builder_.SetInsertPoint(fullFailed);
  Value *fullReason = builder_.CreateSelect(
      builder_.CreateICmpEQ(fullStatus, builder_.getInt32(ServiceException)),
      builder_.getInt32(ExitException),
      builder_.CreateSelect(
          builder_.CreateICmpEQ(fullStatus, builder_.getInt32(ServiceStop)),
          builder_.getInt32(ExitStop), builder_.getInt32(ExitFallback)));
  branchMemoryServiceFailure(fullReason, elapsed);

  builder_.SetInsertPoint(timedResume);
  PHINode *timedStatus = builder_.CreatePHI(i32, 2, "fifo_service_status");
  timedStatus->addIncoming(partialStatus, partialCompleted);
  timedStatus->addIncoming(fullStatus, fullCompleted);
  reloadCallCounters();
  Value *remaining = builder_.CreateLoad(i32, remainingOut);
  Value *budget =
      builder_.CreateAdd(elapsed, builder_.CreateZExt(remaining, i64));
  builder_.CreateStore(budget, builder_.CreateStructGEP(chainType(), chain_, 4));
  Value *completedCycles = builder_.CreateAdd(
      builder_.CreateLoad(i64, guard_cycles_local_),
      builder_.CreateLoad(i64, cycles_));
  Value *timedYield = builder_.CreateOr(
      builder_.CreateICmpEQ(timedStatus, builder_.getInt32(ServiceYield)),
      builder_.CreateICmpUGE(completedCycles, budget));
  builder_.CreateBr(done);
  BasicBlock *timedEnd = builder_.GetInsertBlock();

  builder_.SetInsertPoint(done);
  PHINode *yielded = builder_.CreatePHI(Type::getInt1Ty(context_), 2);
  yielded->addIncoming(builder_.getFalse(), tryBuffered);
  yielded->addIncoming(timedYield, timedEnd);
  return yielded;
}

} // namespace dolllvm
