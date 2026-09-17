#include "backend/llvm/llvm_backend.h"

#include <algorithm>
#include <bitset>
#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>

namespace {

u32 stateCount(const u64 *first, const u64 *second = nullptr) {
  u32 count = 0;
  for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
    count += static_cast<u32>(
        std::bitset<64>(first[word] | (second ? second[word] : 0u)).count());
  return count;
}

u32 packedReturnLanes(const u64 *mask) {
  u32 lanes = 0;
  u32 bits = 0;
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    const auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!dolir_state_mask_test(mask, stateSlot))
      continue;
    u32 width = 32;
    if (dolir_state_type(stateSlot) == DOLIR_TYPE_I1)
      width = 1;
    else if (dolir_state_type(stateSlot) == DOLIR_TYPE_I64 ||
             dolir_state_type(stateSlot) == DOLIR_TYPE_F64)
      width = 64;
    if (bits && bits + width > 64) {
      lanes++;
      bits = 0;
    }
    bits += width;
    if (bits == 64) {
      lanes++;
      bits = 0;
    }
  }
  return lanes + static_cast<u32>(bits != 0);
}

} // namespace

extern "C" bool dolllvm_propagate_function_abis(DolLLVMFunctionRange *ranges,
                                                u32 rangeCount,
                                                const DolLLVMCallEdge *edges,
                                                u32 edgeCount) {
  if ((!ranges && rangeCount) || (!edges && edgeCount))
    return false;
  std::unordered_map<u32, u32> starts;
  std::vector<u32> order(rangeCount);
  for (u32 index = 0; index < rangeCount; index++) {
    starts.emplace(ranges[index].start, index);
    order[index] = index;
  }
  std::sort(order.begin(), order.end(), [&](u32 left, u32 right) {
    return ranges[left].start < ranges[right].start;
  });
  auto containing = [&](u32 address) -> u32 {
    auto position = std::upper_bound(
        order.begin(), order.end(), address,
        [&](u32 value, u32 index) { return value < ranges[index].start; });
    if (position == order.begin())
      return rangeCount;
    const u32 index = *--position;
    return address < ranges[index].end ? index : rangeCount;
  };
  struct Relation {
    u32 caller;
    u32 callee;
    const DolLLVMCallEdge *edge;
  };
  std::vector<Relation> relations;
  std::vector<std::vector<u32>> callers(rangeCount);
  std::vector<std::vector<u32>> callees(rangeCount);
  for (u32 index = 0; index < rangeCount; index++) {
    ranges[index].native_call_targets = 0;
    ranges[index].native_call_depth = 0;
  }
  for (u32 edgeIndex = 0; edgeIndex < edgeCount; edgeIndex++) {
    auto caller = starts.find(edges[edgeIndex].caller_start);
    const u32 callee = containing(edges[edgeIndex].callee_address);
    if (caller == starts.end() || callee == rangeCount ||
        !(ranges[callee].abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE))
      continue;
    const u32 relation = static_cast<u32>(relations.size());
    relations.push_back({caller->second, callee, &edges[edgeIndex]});
    callers[callee].push_back(relation);
    if (ranges[caller->second].abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE) {
      callees[caller->second].push_back(relation);
      ranges[caller->second].native_call_targets++;
    }
    for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
      ranges[callee].callsite_live_after[word] |=
          edges[edgeIndex].live_after[word];
  }
  for (u32 index = 0; index < rangeCount; index++)
    for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
      ranges[index].semantic_output_state[word] =
          ranges[index].may_def_state[word] &
          ranges[index].callsite_live_after[word];

  std::deque<u32> work;
  std::vector<bool> queued(rangeCount, true);
  for (u32 index = 0; index < rangeCount; index++)
    work.push_back(index);
  while (!work.empty()) {
    const u32 callee = work.front();
    work.pop_front();
    queued[callee] = false;
    for (u32 relationIndex : callers[callee]) {
      const Relation &relation = relations[relationIndex];
      const u32 caller = relation.caller;
      bool changed = false;
      for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
        const u64 oldSemanticInput = ranges[caller].semantic_input_state[word];
        const u64 oldMayDef = ranges[caller].may_def_state[word];
        const u64 oldInput = ranges[caller].input_state[word];
        const u64 oldOutput = ranges[caller].output_state[word];
        ranges[caller].semantic_input_state[word] |=
            ranges[callee].semantic_input_state[word] &
            ~relation.edge->defined_before[word];
        ranges[caller].may_def_state[word] |=
            ranges[callee].may_def_state[word];
        ranges[caller].input_state[word] |=
            ranges[callee].input_state[word] &
            ~relation.edge->defined_before[word];
        ranges[caller].output_state[word] |= ranges[callee].output_state[word];
        changed |=
            oldSemanticInput != ranges[caller].semantic_input_state[word] ||
            oldMayDef != ranges[caller].may_def_state[word] ||
            oldInput != ranges[caller].input_state[word] ||
            oldOutput != ranges[caller].output_state[word];
      }
      if (changed && !queued[caller]) {
        queued[caller] = true;
        work.push_back(caller);
      }
    }
  }

  for (u32 index = 0; index < rangeCount; index++)
    for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
      ranges[index].semantic_output_state[word] =
          ranges[index].may_def_state[word] &
          ranges[index].callsite_live_after[word];

  queued.assign(rangeCount, true);
  for (u32 index = 0; index < rangeCount; index++)
    work.push_back(index);
  while (!work.empty()) {
    const u32 caller = work.front();
    work.pop_front();
    queued[caller] = false;
    for (u32 relationIndex : callees[caller]) {
      const u32 callee = relations[relationIndex].callee;
      bool changed = false;
      for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
        const u64 oldEscape = ranges[callee].escape_state[word];
        ranges[callee].escape_state[word] |= ranges[caller].escape_state[word] |
                                             ranges[caller].output_state[word];
        changed |= oldEscape != ranges[callee].escape_state[word];
      }
      if (changed && !queued[callee]) {
        queued[callee] = true;
        work.push_back(callee);
      }
    }
  }

  std::vector<u8> visit(rangeCount);
  std::function<u32(u32)> callDepth = [&](u32 caller) {
    if (!(ranges[caller].abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE))
      return 0u;
    if (visit[caller] == 2u)
      return ranges[caller].native_call_depth;
    if (visit[caller] == 1u)
      return 0u;
    visit[caller] = 1u;
    u32 depth = 1u;
    for (u32 relationIndex : callees[caller])
      depth = std::max(depth, 1u + callDepth(relations[relationIndex].callee));
    visit[caller] = 2u;
    ranges[caller].native_call_depth = depth;
    return depth;
  };
  for (u32 index = 0; index < rangeCount; index++)
    callDepth(index);
  return true;
}

extern "C" void dolllvm_apply_native_abi_policy(DolLLVMFunctionRange *ranges,
                                                u32 rangeCount,
                                                DolLLVMNativeABIPolicy policy) {
  if (!ranges)
    return;
  for (u32 index = 0; index < rangeCount; index++) {
    DolLLVMFunctionRange &range = ranges[index];
    if (!(range.abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE))
      continue;
    const bool compact =
        stateCount(range.input_state, range.escape_state) <= 4u &&
        packedReturnLanes(range.output_state) <= 2u;
    if (policy == DOLLLVM_NATIVE_ABI_DISABLED ||
        (policy == DOLLLVM_NATIVE_ABI_COMPACT && !compact))
      range.abi_flags &=
          ~(DOLLLVM_FUNCTION_ABI_NATIVE | DOLLLVM_FUNCTION_ABI_NATIVE_MEMORY);
  }
}
