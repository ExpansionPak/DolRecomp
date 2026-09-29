#include "backend/llvm/emitter.h"
#include "cpu/cpu.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Alignment.h>

namespace dolllvm {

using namespace llvm;

Value *FunctionEmitter::normalizeAddress(Value *address) {
  return builder_.CreateAnd(address, builder_.getInt32(~0x40000000u));
}

Value *FunctionEmitter::provenMemoryPointer(const DolIRInstruction &instruction,
                                            Value *address, u32 width,
                                            Value **offset,
                                            Value **available) {
  if (!fixed_memory_layout_ ||
      (instruction.address_domain != DOLIR_ADDRESS_MEM1 &&
       instruction.address_domain != DOLIR_ADDRESS_MEM2))
    return nullptr;
  const u32 lower = instruction.address_lower & ~0x40000000u;
  const u32 upper = instruction.address_upper & ~0x40000000u;
  const bool mem2 = instruction.address_domain == DOLIR_ADDRESS_MEM2;
  const u32 base = mem2 ? WII_MEM2_BASE : GC_RAM_BASE;
  const u32 expectedSize = mem2 ? expected_mem2_size_ : expected_ram_size_;
  if (expectedSize < width || lower < base || upper < lower ||
      upper - base > expectedSize - width)
    return nullptr;
  Value *normalized = normalizeAddress(address);
  *offset = builder_.CreateSub(normalized, builder_.getInt32(base));
  *available = mem2 ? builder_.CreateIsNotNull(mem2_) : builder_.getTrue();
  function_->addFnAttr("dolrecomp-native-memory", mem2 ? "mem2" : "mem1");
  Value *memory = mem2 ? mem2_ : ram_;
  return mem2 ? builder_.CreateGEP(Type::getInt8Ty(context_), memory, *offset)
              : builder_.CreateInBoundsGEP(Type::getInt8Ty(context_), memory,
                                           *offset);
}

Value *FunctionEmitter::rangeCheck(Value *normalized, u32 base, Value *size,
                                   u32 width) {
  Value *offset = builder_.CreateSub(normalized, builder_.getInt32(base));
  Value *largeEnough = builder_.CreateICmpUGE(size, builder_.getInt32(width));
  Value *last = builder_.CreateSub(size, builder_.getInt32(width));
  return builder_.CreateAnd(largeEnough, builder_.CreateICmpULE(offset, last));
}

Value *FunctionEmitter::endianLoad(Value *pointer, Type *resultType,
                                   u32 width) {
  Type *integerType = IntegerType::get(context_, width * 8u);
  LoadInst *load = builder_.CreateLoad(integerType, pointer, "native.load");
  load->setAlignment(Align(1));
  Value *loaded = load;
  loaded = bswap(loaded);
  if (resultType != integerType)
    loaded = builder_.CreateZExtOrTrunc(loaded, resultType);
  return loaded;
}

Value *FunctionEmitter::emitGuestLoad(const DolIRInstruction &instruction,
                                      Value *address, Type *resultType,
                                      u32 width, bool sign) {
  Value *directOffset = nullptr;
  Value *directAvailable = nullptr;
  if (Value *pointer = provenMemoryPointer(instruction, address, width,
                                           &directOffset, &directAvailable)) {
    if (!isa<ConstantInt>(directAvailable)) {
      materializeFPRF();
      BasicBlock *directBlock =
          BasicBlock::Create(context_, "load_proven_mem2", function_);
      BasicBlock *slowBlock =
          BasicBlock::Create(context_, "load_proven_mem2_slow", function_);
      BasicBlock *join =
          BasicBlock::Create(context_, "load_proven_mem2_join", function_);
      builder_.CreateCondBr(directAvailable, directBlock, slowBlock,
                            MDBuilder(context_).createBranchWeights(2000, 1));

      builder_.SetInsertPoint(directBlock);
      Value *directValue = endianLoad(pointer, resultType, width);
      builder_.CreateBr(join);

      builder_.SetInsertPoint(slowBlock);
      Value *slowYield = nullptr;
      Value *slow64 = externalRead(address, width, &slowYield);
      Value *slowValue = builder_.CreateZExtOrTrunc(slow64, resultType);
      BasicBlock *slowEnd = builder_.GetInsertBlock();
      builder_.CreateBr(join);

      builder_.SetInsertPoint(join);
      PHINode *phi = builder_.CreatePHI(resultType, 2);
      phi->addIncoming(directValue, directBlock);
      phi->addIncoming(slowValue, slowEnd);
      if (modern_runtime_) {
        PHINode *yield = builder_.CreatePHI(Type::getInt1Ty(context_), 2);
        yield->addIncoming(builder_.getFalse(), directBlock);
        yield->addIncoming(slowYield, slowEnd);
        Value *pending =
            builder_.CreateLoad(Type::getInt1Ty(context_), service_yield_);
        builder_.CreateStore(builder_.CreateOr(pending, yield), service_yield_);
        service_yield_used_ = true;
      }
      if (sign && width * 8u < resultType->getIntegerBitWidth()) {
        Value *narrow = builder_.CreateTrunc(
            phi, IntegerType::get(context_, width * 8u));
        return builder_.CreateSExt(narrow, resultType);
      }
      return phi;
    }
    Value *loaded = endianLoad(pointer, resultType, width);
    if (sign && width * 8u < resultType->getIntegerBitWidth()) {
      Value *narrow =
          builder_.CreateTrunc(loaded, IntegerType::get(context_, width * 8u));
      return builder_.CreateSExt(narrow, resultType);
    }
    return loaded;
  }
  materializeFPRF();
  Value *normalized = normalizeAddress(address);
  Value *mem1 = rangeCheck(normalized, GC_RAM_BASE, ram_size_, width);
  BasicBlock *mem1Block = BasicBlock::Create(context_, "load_mem1", function_);
  BasicBlock *checkMem2 =
      BasicBlock::Create(context_, "load_check_mem2", function_);
  BasicBlock *mem2Block = BasicBlock::Create(context_, "load_mem2", function_);
  BasicBlock *slowBlock = BasicBlock::Create(context_, "load_slow", function_);
  BasicBlock *join = BasicBlock::Create(context_, "load_join", function_);
  builder_.CreateCondBr(mem1, mem1Block, checkMem2,
                        MDBuilder(context_).createBranchWeights(2000, 1));

  builder_.SetInsertPoint(mem1Block);
  Value *mem1Offset =
      builder_.CreateSub(normalized, builder_.getInt32(GC_RAM_BASE));
  Value *mem1Ptr =
      builder_.CreateInBoundsGEP(Type::getInt8Ty(context_), ram_, mem1Offset);
  Value *mem1Value = endianLoad(mem1Ptr, resultType, width);
  builder_.CreateBr(join);

  builder_.SetInsertPoint(checkMem2);
  Value *inMem2 = builder_.CreateAnd(
      builder_.CreateIsNotNull(mem2_),
      rangeCheck(normalized, WII_MEM2_BASE, mem2_size_, width));
  builder_.CreateCondBr(inMem2, mem2Block, slowBlock,
                        MDBuilder(context_).createBranchWeights(2000, 1));

  builder_.SetInsertPoint(mem2Block);
  Value *mem2Offset =
      builder_.CreateSub(normalized, builder_.getInt32(WII_MEM2_BASE));
  Value *mem2Ptr =
      builder_.CreateInBoundsGEP(Type::getInt8Ty(context_), mem2_, mem2Offset);
  Value *mem2Value = endianLoad(mem2Ptr, resultType, width);
  builder_.CreateBr(join);

  builder_.SetInsertPoint(slowBlock);
  Value *slowYield = nullptr;
  Value *slow64 = externalRead(address, width, &slowYield);
  Value *slowValue = builder_.CreateZExtOrTrunc(slow64, resultType);
  BasicBlock *slowEnd = builder_.GetInsertBlock();
  builder_.CreateBr(join);

  builder_.SetInsertPoint(join);
  PHINode *phi = builder_.CreatePHI(resultType, 3);
  phi->addIncoming(mem1Value, mem1Block);
  phi->addIncoming(mem2Value, mem2Block);
  phi->addIncoming(slowValue, slowEnd);
  if (modern_runtime_) {
    PHINode *yield = builder_.CreatePHI(Type::getInt1Ty(context_), 3);
    yield->addIncoming(builder_.getFalse(), mem1Block);
    yield->addIncoming(builder_.getFalse(), mem2Block);
    yield->addIncoming(slowYield, slowEnd);
    Value *pending =
        builder_.CreateLoad(Type::getInt1Ty(context_), service_yield_);
    builder_.CreateStore(builder_.CreateOr(pending, yield), service_yield_);
    service_yield_used_ = true;
  }
  if (sign && width * 8u < resultType->getIntegerBitWidth()) {
    Value *narrow =
        builder_.CreateTrunc(phi, IntegerType::get(context_, width * 8u));
    return builder_.CreateSExt(narrow, resultType);
  }
  return phi;
}

void FunctionEmitter::emitGuestStore(const DolIRInstruction &instruction,
                                     Value *address, Value *value, u32 width) {
  Value *directOffset = nullptr;
  Value *directAvailable = nullptr;
  if (Value *pointer = provenMemoryPointer(instruction, address, width,
                                           &directOffset, &directAvailable)) {
    clearReservation(address);
    if (instruction.address_domain == DOLIR_ADDRESS_MEM1)
      journal(directOffset, width);
    if (!isa<ConstantInt>(directAvailable)) {
      materializeFPRF();
      BasicBlock *directBlock =
          BasicBlock::Create(context_, "store_proven_mem2", function_);
      BasicBlock *slowBlock =
          BasicBlock::Create(context_, "store_proven_mem2_slow", function_);
      BasicBlock *join =
          BasicBlock::Create(context_, "store_proven_mem2_join", function_);
      builder_.CreateCondBr(directAvailable, directBlock, slowBlock,
                            MDBuilder(context_).createBranchWeights(2000, 1));

      builder_.SetInsertPoint(directBlock);
      endianStore(pointer, value, width);
      builder_.CreateBr(join);

      builder_.SetInsertPoint(slowBlock);
      Value *slowYield = externalWrite(address, value, width);
      BasicBlock *slowEnd = builder_.GetInsertBlock();
      builder_.CreateBr(join);

      builder_.SetInsertPoint(join);
      if (modern_runtime_) {
        PHINode *yield = builder_.CreatePHI(Type::getInt1Ty(context_), 2);
        yield->addIncoming(builder_.getFalse(), directBlock);
        yield->addIncoming(slowYield, slowEnd);
        Value *pending =
            builder_.CreateLoad(Type::getInt1Ty(context_), service_yield_);
        builder_.CreateStore(builder_.CreateOr(pending, yield), service_yield_);
        service_yield_used_ = true;
      }
      return;
    }
    endianStore(pointer, value, width);
    return;
  }
  materializeFPRF();
  clearReservation(address);
  if (modern_runtime_ && instruction.address_domain == DOLIR_ADDRESS_FIFO) {
    Value *yield = externalFifoWrite(address, value, width);
    Value *pending =
        builder_.CreateLoad(Type::getInt1Ty(context_), service_yield_);
    builder_.CreateStore(builder_.CreateOr(pending, yield), service_yield_);
    service_yield_used_ = true;
    return;
  }
  Value *normalized = normalizeAddress(address);
  BasicBlock *mem1Block = BasicBlock::Create(context_, "store_mem1", function_);
  BasicBlock *checkMem2 =
      BasicBlock::Create(context_, "store_check_mem2", function_);
  BasicBlock *mem2Block = BasicBlock::Create(context_, "store_mem2", function_);
  BasicBlock *slowBlock = BasicBlock::Create(context_, "store_slow", function_);
  BasicBlock *join = BasicBlock::Create(context_, "store_join", function_);
  builder_.CreateCondBr(rangeCheck(normalized, GC_RAM_BASE, ram_size_, width),
                        mem1Block, checkMem2,
                        MDBuilder(context_).createBranchWeights(2000, 1));

  builder_.SetInsertPoint(mem1Block);
  Value *mem1Offset =
      builder_.CreateSub(normalized, builder_.getInt32(GC_RAM_BASE));
  journal(mem1Offset, width);
  Value *mem1Ptr =
      builder_.CreateInBoundsGEP(Type::getInt8Ty(context_), ram_, mem1Offset);
  endianStore(mem1Ptr, value, width);
  builder_.CreateBr(join);

  builder_.SetInsertPoint(checkMem2);
  Value *inMem2 = builder_.CreateAnd(
      builder_.CreateIsNotNull(mem2_),
      rangeCheck(normalized, WII_MEM2_BASE, mem2_size_, width));
  builder_.CreateCondBr(inMem2, mem2Block, slowBlock,
                        MDBuilder(context_).createBranchWeights(2000, 1));

  builder_.SetInsertPoint(mem2Block);
  Value *mem2Offset =
      builder_.CreateSub(normalized, builder_.getInt32(WII_MEM2_BASE));
  Value *mem2Ptr =
      builder_.CreateInBoundsGEP(Type::getInt8Ty(context_), mem2_, mem2Offset);
  endianStore(mem2Ptr, value, width);
  builder_.CreateBr(join);

  builder_.SetInsertPoint(slowBlock);
  Value *slowYield = externalWrite(address, value, width);
  builder_.CreateBr(join);
  BasicBlock *slowEnd = builder_.GetInsertBlock();
  builder_.SetInsertPoint(join);
  if (modern_runtime_) {
    PHINode *yield = builder_.CreatePHI(Type::getInt1Ty(context_), 3);
    yield->addIncoming(builder_.getFalse(), mem1Block);
    yield->addIncoming(builder_.getFalse(), mem2Block);
    yield->addIncoming(slowYield, slowEnd);
    Value *pending =
        builder_.CreateLoad(Type::getInt1Ty(context_), service_yield_);
    builder_.CreateStore(builder_.CreateOr(pending, yield), service_yield_);
    service_yield_used_ = true;
  }
}

} // namespace dolllvm
