#include "backend/llvm/emitter.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>

namespace dolllvm {

using namespace llvm;

namespace {

constexpr u32 StructuredExitFlag = 0x80000000u;

} // namespace

void FunctionEmitter::materialize(u32 pc) {
  materialize(ConstantInt::get(Type::getInt32Ty(context_), pc));
}

void FunctionEmitter::materialize(Value *pc) {
  syncDirtyState();
  if (modern_runtime_) {
    builder_.CreateStore(pc, builder_.CreateStructGEP(chainType(), chain_, 5));
    builder_.CreateStore(builder_.CreateAdd(pc, builder_.getInt32(4)),
                         builder_.CreateStructGEP(chainType(), chain_, 6));
  } else {
    storeContext(DOLIR_STATE_PC, pc);
  }
  settleCycles();
  commitModernState();
}

void FunctionEmitter::materializeMemoryService(u32 pc) {
  if (!modern_runtime_) {
    materialize(pc);
    return;
  }

  u64 serviceDirty[DOLIR_STATE_MASK_WORDS]{};
  auto publishControl = [&](DolIRStateSlot slot) {
    const u32 raw = static_cast<u32>(slot);
    if (dirty_[raw])
      serviceDirty[raw / 64u] |= u64(1) << (raw & 63u);
  };
  publishControl(DOLIR_STATE_MSR);
  for (u32 slot = DOLIR_STATE_SR0; slot <= DOLIR_STATE_SR15; slot++)
    publishControl(static_cast<DolIRStateSlot>(slot));
  publishControl(DOLIR_STATE_EXCEPTION);
  stageStateMask(serviceDirty);
  builder_.CreateStore(builder_.getInt32(pc),
                       builder_.CreateStructGEP(chainType(), chain_, 5));
  builder_.CreateStore(builder_.getInt32(pc + 4u),
                       builder_.CreateStructGEP(chainType(), chain_, 6));
  settleCycles();
  commitModernState();
}

void FunctionEmitter::materializeFifoService(u32 pc) {
  if (!modern_runtime_) {
    materialize(pc);
    return;
  }

  u64 serviceDirty[DOLIR_STATE_MASK_WORDS]{};
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    if (!dirty_[slot] ||
        (slot >= DOLIR_STATE_GPR0 && slot <= DOLIR_STATE_PS1_31))
      continue;
    serviceDirty[slot / 64u] |= u64(1) << (slot & 63u);
  }
  stageStateMask(serviceDirty);
  builder_.CreateStore(builder_.getInt32(pc),
                       builder_.CreateStructGEP(chainType(), chain_, 5));
  builder_.CreateStore(builder_.getInt32(pc + 4u),
                       builder_.CreateStructGEP(chainType(), chain_, 6));
  settleCycles();
  commitModernState();
}

void FunctionEmitter::sideExit(u32 pc, u32 reason) {
  if (!modern_runtime_) {
    materialize(pc);
    returnFromBody();
    return;
  }

  if (!shared_side_exit_) {
    shared_side_exit_ = BasicBlock::Create(context_, "shared_side_exit", function_);
    IRBuilder<> exitBuilder(shared_side_exit_);
    shared_side_exit_pc_ =
        exitBuilder.CreatePHI(Type::getInt32Ty(context_), 0, "side_exit_pc");
    shared_side_exit_reason_ =
        exitBuilder.CreatePHI(Type::getInt32Ty(context_), 0, "side_exit_reason");
  }
  BasicBlock *from = builder_.GetInsertBlock();
  shared_side_exit_pc_->addIncoming(builder_.getInt32(pc), from);
  shared_side_exit_reason_->addIncoming(builder_.getInt32(reason), from);
  builder_.CreateBr(shared_side_exit_);
}

void FunctionEmitter::emitSharedSideExit() {
  if (!shared_side_exit_)
    return;
  IRBuilderBase::InsertPoint saved = builder_.saveIP();
  builder_.SetInsertPoint(shared_side_exit_);
  Value *structured = builder_.CreateICmpEQ(shared_side_exit_reason_,
                                            builder_.getInt32(0));
  Value *publishedReason = builder_.CreateSelect(
      structured, builder_.getInt32(StructuredExitFlag), shared_side_exit_reason_);
  builder_.CreateStore(publishedReason,
                       builder_.CreateStructGEP(chainType(), chain_, 7));
  materialize(shared_side_exit_pc_);
  BasicBlock *structuredReturn =
      BasicBlock::Create(context_, "structured_side_exit", function_);
  BasicBlock *coldEscape =
      BasicBlock::Create(context_, "cold_side_exit", function_);
  builder_.CreateCondBr(structured, structuredReturn, coldEscape);
  builder_.SetInsertPoint(structuredReturn);
  returnStructuredExit();
  builder_.SetInsertPoint(coldEscape);
  returnFromBody();
  builder_.restoreIP(saved);
}

} // namespace dolllvm
