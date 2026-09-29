#include "backend/llvm/native_abi.h"

#include "cpu/cpu.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace {

bool nativeMemoryAccess(const DolIRInstruction &instruction) {
  if (instruction.address_domain != DOLIR_ADDRESS_MEM1 &&
      instruction.address_domain != DOLIR_ADDRESS_MEM2)
    return false;
  const u32 width = instruction.aux & 0xffu;
  const u32 lower = instruction.address_lower & ~0x40000000u;
  const u32 upper = instruction.address_upper & ~0x40000000u;
  const bool mem2 = instruction.address_domain == DOLIR_ADDRESS_MEM2;
  const u32 base = mem2 ? WII_MEM2_BASE : GC_RAM_BASE;
  const u32 size = mem2 ? WII_MEM2_SIZE : GC_MAIN_RAM_SIZE;
  return width && width <= size && lower >= base && upper >= lower &&
         upper - base <= size - width;
}

bool nativeHelper(const DolIRInstruction &instruction) {
  if (instruction.aux == DOLIR_HELPER_MEMORY_FENCE ||
      instruction.aux == DOLIR_HELPER_FP_AVAILABLE ||
      instruction.aux == DOLIR_HELPER_PROGRAM_EXCEPTION ||
      instruction.aux == DOLIR_HELPER_CACHE_CONTROL ||
      instruction.aux == DOLIR_HELPER_TIMEBASE_READ ||
      instruction.aux == DOLIR_HELPER_TIMEBASE_WRITE ||
      instruction.aux == DOLIR_HELPER_PSQ_LOAD ||
      instruction.aux == DOLIR_HELPER_PSQ_STORE ||
      instruction.aux == DOLIR_HELPER_EXACT_FLOAT ||
      instruction.aux == DOLIR_HELPER_EXACT_PAIRED)
    return true;
  return false;
}

u32 nativeABIFlags(const DolIRFunction &function, u32 *blockers) {
  bool memory = false;
  *blockers = 0;
  for (u32 blockIndex = 0; blockIndex < function.block_count; blockIndex++) {
    const DolIRBlock &block = function.blocks[blockIndex];
    if (!block.cycle_cost)
      *blockers |= DOLLLVM_ABI_BLOCK_CONTROL;
    if (block.terminator.kind == DOLIR_TERM_FALLBACK) {
      *blockers |= DOLLLVM_ABI_BLOCK_FALLBACK;
      if (!dolllvm::canInlineFallback(block))
        *blockers |= DOLLLVM_ABI_BLOCK_CONTROL;
    }
    if (block.terminator.kind == DOLIR_TERM_SYSTEM_CALL)
      *blockers |= DOLLLVM_ABI_BLOCK_EXCEPTION;
    if (block.terminator.kind == DOLIR_TERM_RFI)
      *blockers |= DOLLLVM_ABI_BLOCK_RFI;
    for (u32 index = 0; index < block.instruction_count; index++) {
      const DolIRInstruction &instruction = block.instructions[index];
      if (instruction.op == DOLIR_OP_GUEST_LOAD ||
          instruction.op == DOLIR_OP_GUEST_STORE) {
        if (!nativeMemoryAccess(instruction))
          *blockers |= DOLLLVM_ABI_BLOCK_MEMORY_SERVICE;
        memory = true;
        continue;
      }
      if (instruction.op == DOLIR_OP_HELPER_CALL && !nativeHelper(instruction))
        *blockers |= DOLLLVM_ABI_BLOCK_HELPER;
    }
  }
  if (*blockers)
    return 0;
  return static_cast<u32>(DOLLLVM_FUNCTION_ABI_NATIVE) |
         (memory ? static_cast<u32>(DOLLLVM_FUNCTION_ABI_NATIVE_MEMORY) : 0u);
}

u32 targetCount(const DolIRTerminator &term) {
  return term.kind == DOLIR_TERM_COND_BRANCH ? 2u
         : term.kind == DOLIR_TERM_FALLTHROUGH || term.kind == DOLIR_TERM_BRANCH
             ? 1u
         : term.kind == DOLIR_TERM_INDIRECT ? 2u
                                            : 0u;
}

} // namespace

extern "C" bool dolllvm_analyze_function_abi(const DolIRFunction *function,
                                             DolLLVMFunctionRange *range) {
  if (!function || !range || function->guest_start != range->start ||
      function->guest_end != range->end)
    return false;
  memset(range->semantic_input_state, 0, sizeof(range->semantic_input_state));
  memset(range->may_def_state, 0, sizeof(range->may_def_state));
  memset(range->must_def_state, 0, sizeof(range->must_def_state));
  memset(range->callsite_live_after, 0, sizeof(range->callsite_live_after));
  memset(range->semantic_output_state, 0, sizeof(range->semantic_output_state));
  memset(range->input_state, 0, sizeof(range->input_state));
  memset(range->output_state, 0, sizeof(range->output_state));
  memset(range->escape_state, 0, sizeof(range->escape_state));
  range->direct_memory_accesses = 0;
  range->generic_memory_accesses = 0;
  range->helper_calls = 0;
  range->native_call_targets = 0;
  range->native_call_depth = 0;
  range->abi_blockers = 0;

  const u32 blockCount = function->block_count;
  using Mask = std::array<u64, DOLIR_STATE_MASK_WORDS>;
  std::vector<std::vector<u32>> predecessors(blockCount);
  std::vector<std::vector<u32>> successors(blockCount);
  std::vector<Mask> blockDefs(blockCount);
  std::vector<Mask> definiteIn(blockCount);
  std::vector<Mask> definiteOut(blockCount);

  for (u32 blockIndex = 0; blockIndex < blockCount; blockIndex++) {
    const DolIRBlock &block = function->blocks[blockIndex];
    for (u32 index = 0; index < block.instruction_count; index++) {
      const DolIRInstruction &instruction = block.instructions[index];
      for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
        blockDefs[blockIndex][word] |= instruction.state_defs[word];
        range->may_def_state[word] |= instruction.state_defs[word];
      }
      if (instruction.op == DOLIR_OP_GUEST_LOAD ||
          instruction.op == DOLIR_OP_GUEST_STORE) {
        if (nativeMemoryAccess(instruction))
          range->direct_memory_accesses++;
        else
          range->generic_memory_accesses++;
      } else if (instruction.op == DOLIR_OP_HELPER_CALL) {
        range->helper_calls++;
      }
    }

    const DolIRTerminator &term = block.terminator;
    for (u32 slot = 0; slot < targetCount(term); slot++) {
      const u32 target = term.targets[slot];
      if (target != DOLIR_NO_BLOCK && target < blockCount)
        successors[blockIndex].push_back(target);
    }
    if (term.linked) {
      const u32 continuation = term.guest_pc + 4u;
      if (continuation >= function->guest_start &&
          continuation < function->guest_end &&
          ((continuation - function->guest_start) & 3u) == 0) {
        const u32 target = (continuation - function->guest_start) / 4u;
        if (target < blockCount &&
            std::find(successors[blockIndex].begin(),
                      successors[blockIndex].end(),
                      target) == successors[blockIndex].end())
          successors[blockIndex].push_back(target);
      }
    }
    for (u32 target : successors[blockIndex])
      predecessors[target].push_back(blockIndex);
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (u32 blockIndex = 0; blockIndex < blockCount; blockIndex++) {
      Mask incoming{};
      if (blockIndex != 0 && !predecessors[blockIndex].empty()) {
        incoming.fill(std::numeric_limits<u64>::max());
        for (u32 predecessor : predecessors[blockIndex])
          for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
            incoming[word] &= definiteOut[predecessor][word];
      }
      Mask outgoing{};
      for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
        outgoing[word] = incoming[word] | blockDefs[blockIndex][word];
      if (incoming != definiteIn[blockIndex] ||
          outgoing != definiteOut[blockIndex]) {
        definiteIn[blockIndex] = incoming;
        definiteOut[blockIndex] = outgoing;
        changed = true;
      }
    }
  }

  bool hasExit = false;
  Mask exitDefs;
  exitDefs.fill(std::numeric_limits<u64>::max());
  for (u32 blockIndex = 0; blockIndex < blockCount; blockIndex++) {
    Mask written = definiteIn[blockIndex];
    const DolIRBlock &block = function->blocks[blockIndex];
    for (u32 index = 0; index < block.instruction_count; index++) {
      const DolIRInstruction &instruction = block.instructions[index];
      for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
        range->semantic_input_state[word] |=
            instruction.state_uses[word] & ~written[word];
        written[word] |= instruction.state_defs[word];
      }
    }
    if (successors[blockIndex].empty()) {
      hasExit = true;
      for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
        exitDefs[word] &= definiteOut[blockIndex][word];
    }
  }
  for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
    range->must_def_state[word] = hasExit ? exitDefs[word] : 0u;
    range->input_state[word] =
        range->semantic_input_state[word] |
        (range->may_def_state[word] & ~range->must_def_state[word]);
    range->output_state[word] = range->may_def_state[word];
    range->callsite_live_after[word] = 0;
    range->semantic_output_state[word] = 0;
  }
  range->abi_flags = nativeABIFlags(*function, &range->abi_blockers);
  return true;
}

extern "C" void dolllvm_enable_native_services(DolLLVMFunctionRange *ranges,
                                               u32 rangeCount) {
  if (!ranges)
    return;
  for (u32 index = 0; index < rangeCount; index++) {
    DolLLVMFunctionRange &range = ranges[index];
    range.abi_blockers &=
        ~(DOLLLVM_ABI_BLOCK_EXCEPTION | DOLLLVM_ABI_BLOCK_MEMORY_SERVICE |
          DOLLLVM_ABI_BLOCK_FALLBACK);
    if (range.abi_blockers)
      continue;
    range.abi_flags |= DOLLLVM_FUNCTION_ABI_NATIVE;
    if (range.direct_memory_accesses || range.generic_memory_accesses)
      range.abi_flags |= DOLLLVM_FUNCTION_ABI_NATIVE_MEMORY;
  }
}
