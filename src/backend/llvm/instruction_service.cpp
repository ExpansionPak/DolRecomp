#include "backend/llvm/emitter.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/MDBuilder.h>

namespace dolllvm {

using namespace llvm;

void FunctionEmitter::emitInstructionService(u32 pc, u32 fallbackCycleCost) {
  constexpr u32 ServiceContinue = 0;
  constexpr u32 ServiceException = 1;
  constexpr u32 ServiceYield = 2;
  constexpr u32 ServiceFallback = 3;
  constexpr u32 ServiceStop = 4;
  constexpr u32 ExitException = 1;
  constexpr u32 ExitFallback = 2;
  constexpr u32 ExitStop = 5;
  Type *i32 = Type::getInt32Ty(context_);
  Type *pointer = PointerType::getUnqual(context_);
  StructType *servicesType = StructType::get(
      context_, {i32, i32, pointer, pointer, pointer, pointer, pointer,
                 pointer});
  Value *services = runtimeField(11);
  Value *function = builder_.CreateLoad(
      pointer, builder_.CreateStructGEP(servicesType, services, 7));
  Value *serviceContext = builder_.CreateLoad(
      pointer, builder_.CreateStructGEP(servicesType, services, 2));

  materialize(pc);
  CallInst *status = builder_.CreateCall(
      FunctionType::get(i32, {pointer, i32}, false), function,
      {serviceContext, builder_.getInt32(pc)});
  status->addFnAttr(Attribute::NoUnwind);

  if (fallbackCycleCost) {
    Value *pending = builder_.CreateLoad(Type::getInt64Ty(context_),
                                         pending_cycles_);
    Value *charged = builder_.CreateAdd(
        pending, builder_.getInt64(fallbackCycleCost));
    Value *executed = builder_.CreateICmpNE(
        status, builder_.getInt32(ServiceFallback));
    builder_.CreateStore(builder_.CreateSelect(executed, charged, pending),
                         pending_cycles_);
  }

  BasicBlock *resume =
      BasicBlock::Create(context_, "instruction_service_resume", function_);
  BasicBlock *failed =
      BasicBlock::Create(context_, "instruction_service_exit", function_);
  if (fallbackCycleCost) {
    BasicBlock *classify = BasicBlock::Create(
        context_, "instruction_service_classify", function_);
    BasicBlock *yield =
        BasicBlock::Create(context_, "instruction_service_yield", function_);
    builder_.CreateCondBr(
        builder_.CreateICmpEQ(status, builder_.getInt32(ServiceContinue)),
        resume, classify, MDBuilder(context_).createBranchWeights(2000, 1));
    builder_.SetInsertPoint(classify);
    builder_.CreateCondBr(
        builder_.CreateICmpEQ(status, builder_.getInt32(ServiceYield)), yield,
        failed, MDBuilder(context_).createBranchWeights(1, 2000));
    builder_.SetInsertPoint(yield);
    reloadCallCounters();
    reloadUsedState();
    sideExit(pc + 4u);
  } else {
    builder_.CreateCondBr(
        builder_.CreateICmpEQ(status, builder_.getInt32(ServiceContinue)),
        resume, failed, MDBuilder(context_).createBranchWeights(2000, 1));
  }

  builder_.SetInsertPoint(failed);
  Value *reason = builder_.CreateSelect(
      builder_.CreateICmpEQ(status, builder_.getInt32(ServiceException)),
      builder_.getInt32(ExitException),
      builder_.CreateSelect(
          builder_.CreateICmpEQ(status, builder_.getInt32(ServiceStop)),
          builder_.getInt32(ExitStop), builder_.getInt32(ExitFallback)));
  builder_.CreateStore(reason,
                       builder_.CreateStructGEP(chainType(), chain_, 7));
  returnFromBody();

  builder_.SetInsertPoint(resume);
  reloadCallCounters();
  reloadUsedState();
}

} // namespace dolllvm
