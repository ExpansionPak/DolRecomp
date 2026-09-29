#include "backend/llvm/emitter.h"
#include "backend/llvm/native_abi.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>

namespace dolllvm {

using namespace llvm;

bool FunctionEmitter::emitRegion(u32 index, raw_ostream &diagnostics) {
  resetFPRepresentations();
  known_state_.fill(nullptr);
  psq_direct_proven_ = false;
  psq_indexed_proven_ = false;
  fp_available_checked_ = false;
  pending_fprf_ = nullptr;
  builder_.SetInsertPoint(blocks_[index]);
  for (u32 current = index; current < source_.block_count; current++) {
    const DolIRBlock &block = source_.blocks[current];
    service_yield_used_ = false;
    if (modern_runtime_)
      builder_.CreateStore(builder_.getFalse(), service_yield_);
    if (current != index && region_leaders_[current]) {
      materializeFPRF();
      builder_.CreateBr(blocks_[current]);
      return true;
    }
    const bool useService =
        modern_runtime_ && native_abi_ && needsInterpreter(block);
    if (loop_headers_[current])
      emitBudgetGuard(block.guest_address);
    chargeCycles(block.cycle_cost);
    if (useService) {
      emitInstructionService(block.guest_address);
    } else {
      values_.assign(source_.value_count, nullptr);
      for (u32 i = 0; i < block.instruction_count; i++) {
        if (!emitInstruction(block.instructions[i], diagnostics))
          return false;
      }
    }
    if (service_yield_used_) {
      BasicBlock *resume =
          BasicBlock::Create(context_, "memory_service_continue", function_);
      BasicBlock *yield =
          BasicBlock::Create(context_, "memory_service_yield", function_);
      Value *serviceYield = builder_.CreateLoad(Type::getInt1Ty(context_),
                                                service_yield_);
      builder_.CreateCondBr(serviceYield, yield, resume,
                            MDBuilder(context_).createBranchWeights(1, 2000));
      builder_.SetInsertPoint(yield);
      sideExit(block.guest_address + 4u);
      builder_.SetInsertPoint(resume);
    }
    if (block.terminator.kind == DOLIR_TERM_FALLTHROUGH) {
      u32 next = block.terminator.targets[0];
      if (next != DOLIR_NO_BLOCK && next == current + 1u &&
          next < source_.block_count && !region_leaders_[next])
        continue;
    }
    materializeFPRF();
    return emitTerminator(block.terminator, diagnostics);
  }
  diagnostics << "dolllvm: unterminated native region at 0x"
              << format_hex_no_prefix(source_.blocks[index].guest_address, 8)
              << "\n";
  return false;
}

} // namespace dolllvm
