#include "backend/llvm/emitter.h"
#include "backend/llvm/native_abi.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>

namespace dolllvm {

using namespace llvm;

void collectRegionLeaders(const DolIRFunction &function, bool modernRuntime,
                          bool nativeABI, const u32 *entryPoints,
                          u32 entryPointCount, std::vector<bool> &leaders) {
  leaders.assign(function.block_count, false);
  if (!function.block_count)
    return;
  leaders[0] = true;
  for (u32 i = 0; i < function.block_count; i++) {
    if (modernRuntime && nativeABI && needsInterpreter(function.blocks[i])) {
      leaders[i] = true;
      if (i + 1u < function.block_count)
        leaders[i + 1u] = true;
    }
    const DolIRTerminator &term = function.blocks[i].terminator;
    if (term.kind == DOLIR_TERM_FALLBACK)
      leaders[i] = true;
    if (i + 1u < function.block_count && term.kind != DOLIR_TERM_FALLTHROUGH)
      leaders[i + 1u] = true;
    if (i + 1u < function.block_count) {
      const DolIRBlock &body = function.blocks[i];
      for (u32 n = 0; n < body.instruction_count; n++) {
        if (dolir_state_mask_test(body.instructions[n].state_defs,
                                  DOLIR_STATE_MSR)) {
          leaders[i + 1u] = true;
          break;
        }
      }
    }
    u32 count = term.kind == DOLIR_TERM_COND_BRANCH ? 2u
                : term.kind == DOLIR_TERM_BRANCH    ? 1u
                : term.kind == DOLIR_TERM_INDIRECT  ? 2u
                                                    : 0u;
    for (u32 edge = 0; edge < count; edge++) {
      if (term.targets[edge] != DOLIR_NO_BLOCK)
        leaders[term.targets[edge]] = true;
    }
  }
  for (u32 i = 0; i < entryPointCount; i++) {
    u32 address = entryPoints[i];
    if (address < function.guest_start || address >= function.guest_end ||
        ((address - function.guest_start) & 3u) != 0)
      continue;
    leaders[(address - function.guest_start) / 4u] = true;
  }
}

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
