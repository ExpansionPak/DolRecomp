#include "backend/llvm/emitter.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Alignment.h>

namespace dolllvm {

using namespace llvm;

void FunctionEmitter::clearReservation(Value *address) {
  Value *valid = builder_.CreateLoad(Type::getInt1Ty(context_),
                                     state_[DOLIR_STATE_RESERVE_VALID]);
  Value *reserved = builder_.CreateLoad(Type::getInt32Ty(context_),
                                        state_[DOLIR_STATE_RESERVE_ADDR]);
  Value *differentLine = builder_.CreateICmpNE(
      builder_.CreateAnd(builder_.CreateXor(reserved, address),
                         builder_.getInt32(~31u)),
      builder_.getInt32(0));
  builder_.CreateStore(builder_.CreateAnd(valid, differentLine),
                       state_[DOLIR_STATE_RESERVE_VALID]);
}

void FunctionEmitter::journal(Value *offset, u32 width) {
  if (!write_journal_)
    return;
  Type *ptr = PointerType::getUnqual(context_);
  GlobalVariable *journal = cast<GlobalVariable>(
      module_.getOrInsertGlobal("g_mem_write_journal", ptr));
  GlobalVariable *user = cast<GlobalVariable>(
      module_.getOrInsertGlobal("g_mem_write_journal_user", ptr));
  Value *fn = builder_.CreateLoad(ptr, journal);
  BasicBlock *call = BasicBlock::Create(context_, "journal", function_);
  BasicBlock *done = BasicBlock::Create(context_, "journal_done", function_);
  builder_.CreateCondBr(builder_.CreateIsNotNull(fn), call, done,
                        MDBuilder(context_).createBranchWeights(2000, 1));
  builder_.SetInsertPoint(call);
  auto *functionType = FunctionType::get(
      Type::getVoidTy(context_),
      {Type::getInt32Ty(context_), Type::getInt32Ty(context_), ptr}, false);
  builder_.CreateCall(
      functionType, fn,
      {offset, builder_.getInt32(width), builder_.CreateLoad(ptr, user)});
  builder_.CreateBr(done);
  builder_.SetInsertPoint(done);
}

void FunctionEmitter::endianStore(Value *pointer, Value *value, u32 width) {
  Type *integerType = IntegerType::get(context_, width * 8u);
  Value *narrowed = value;
  if (value->getType() != integerType)
    narrowed = builder_.CreateZExtOrTrunc(value, integerType);
  StoreInst *store = builder_.CreateStore(bswap(narrowed), pointer);
  store->setAlignment(Align(1));
}

} // namespace dolllvm
