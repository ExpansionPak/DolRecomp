#include "backend/llvm/llvm_backend.h"

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace {

using Mask = std::array<u64, DOLIR_STATE_MASK_WORDS>;

u32 targetCount(const DolIRTerminator &term) {
  return term.kind == DOLIR_TERM_COND_BRANCH ? 2u
         : term.kind == DOLIR_TERM_FALLTHROUGH || term.kind == DOLIR_TERM_BRANCH
             ? 1u
         : term.kind == DOLIR_TERM_INDIRECT ? 2u
                                            : 0u;
}

void addSuccessors(const DolIRFunction &function, u32 blockIndex,
                   std::vector<u32> &successors) {
  const DolIRTerminator &term = function.blocks[blockIndex].terminator;
  for (u32 slot = 0; slot < targetCount(term); slot++) {
    const u32 target = term.targets[slot];
    if (target != DOLIR_NO_BLOCK && target < function.block_count)
      successors.push_back(target);
  }
  if (!term.linked)
    return;
  const u32 continuation = term.guest_pc + 4u;
  if (continuation < function.guest_start ||
      continuation >= function.guest_end ||
      ((continuation - function.guest_start) & 3u) != 0)
    return;
  const u32 target = (continuation - function.guest_start) / 4u;
  if (target < function.block_count &&
      std::find(successors.begin(), successors.end(), target) ==
          successors.end())
    successors.push_back(target);
}

} // namespace

extern "C" bool dolllvm_analyze_callsite_state(const DolIRFunction *function,
                                               u32 blockIndex,
                                               const u64 *functionOutputs,
                                               const u64 *postCallDefs,
                                               u64 *liveAfter,
                                               u64 *definedBefore,
                                               u64 *mayDirtyBefore) {
  if (!function || blockIndex >= function->block_count || !functionOutputs ||
      !liveAfter || !definedBefore || !mayDirtyBefore)
    return false;
  const u32 count = function->block_count;
  std::vector<std::vector<u32>> successors(count);
  std::vector<std::vector<u32>> predecessors(count);
  std::vector<Mask> uses(count);
  std::vector<Mask> defs(count);
  for (u32 block = 0; block < count; block++) {
    Mask written{};
    const DolIRBlock &source = function->blocks[block];
    for (u32 index = 0; index < source.instruction_count; index++) {
      const DolIRInstruction &instruction = source.instructions[index];
      for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
        uses[block][word] |= instruction.state_uses[word] & ~written[word];
        written[word] |= instruction.state_defs[word];
        defs[block][word] |= instruction.state_defs[word];
      }
    }
    addSuccessors(*function, block, successors[block]);
    for (u32 target : successors[block])
      predecessors[target].push_back(block);
  }

  std::vector<Mask> liveIn(count);
  std::vector<Mask> liveOut(count);
  bool changed = true;
  while (changed) {
    changed = false;
    for (u32 position = count; position != 0; position--) {
      const u32 block = position - 1u;
      Mask outgoing{};
      if (successors[block].empty())
        std::copy(functionOutputs, functionOutputs + DOLIR_STATE_MASK_WORDS,
                  outgoing.begin());
      else
        for (u32 target : successors[block])
          for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
            outgoing[word] |= liveIn[target][word];
      Mask incoming{};
      for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
        incoming[word] =
            uses[block][word] | (outgoing[word] & ~defs[block][word]);
      if (incoming != liveIn[block] || outgoing != liveOut[block]) {
        liveIn[block] = incoming;
        liveOut[block] = outgoing;
        changed = true;
      }
    }
  }

  std::vector<Mask> definiteIn(count);
  std::vector<Mask> definiteOut(count);
  changed = true;
  while (changed) {
    changed = false;
    for (u32 block = 0; block < count; block++) {
      Mask incoming{};
      if (block != 0 && !predecessors[block].empty()) {
        incoming.fill(std::numeric_limits<u64>::max());
        for (u32 predecessor : predecessors[block])
          for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
            incoming[word] &= definiteOut[predecessor][word];
      }
      Mask outgoing{};
      for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
        outgoing[word] = incoming[word] | defs[block][word];
      if (incoming != definiteIn[block] || outgoing != definiteOut[block]) {
        definiteIn[block] = incoming;
        definiteOut[block] = outgoing;
        changed = true;
      }
    }
  }

  std::vector<Mask> mayDirtyIn(count);
  std::vector<Mask> mayDirtyOut(count);
  changed = true;
  while (changed) {
    changed = false;
    for (u32 block = 0; block < count; block++) {
      Mask incoming{};
      for (u32 predecessor : predecessors[block])
        for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
          incoming[word] |= mayDirtyOut[predecessor][word];
      Mask outgoing{};
      for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
        outgoing[word] = incoming[word] | defs[block][word];
        if (postCallDefs)
          outgoing[word] |=
              postCallDefs[(size_t)block * DOLIR_STATE_MASK_WORDS + word];
      }
      if (incoming != mayDirtyIn[block] || outgoing != mayDirtyOut[block]) {
        mayDirtyIn[block] = incoming;
        mayDirtyOut[block] = outgoing;
        changed = true;
      }
    }
  }

  const DolIRTerminator &term = function->blocks[blockIndex].terminator;
  const u32 continuation = term.guest_pc + 4u;
  const bool localContinuation =
      term.linked && continuation >= function->guest_start &&
      continuation < function->guest_end &&
      ((continuation - function->guest_start) & 3u) == 0;
  const u32 continuationBlock =
      localContinuation ? (continuation - function->guest_start) / 4u : 0u;
  const Mask &after = localContinuation && continuationBlock < count
                          ? liveIn[continuationBlock]
                          : liveOut[blockIndex];
  for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
    liveAfter[word] = after[word];
    definedBefore[word] = definiteOut[blockIndex][word];
    mayDirtyBefore[word] =
        mayDirtyIn[blockIndex][word] | defs[blockIndex][word];
  }
  return true;
}
