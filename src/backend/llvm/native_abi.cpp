#include "backend/llvm/native_abi.h"
#include "backend/llvm/emitter.h"
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
namespace {
DolLLVMFunctionRange *exactRange(std::vector<DolLLVMFunctionRange> &ranges,
                                 u32 start) {
  auto it = std::lower_bound(ranges.begin(), ranges.end(), start,
                             [](const DolLLVMFunctionRange &range, u32 value) {
                               return range.start < value;
                             });
  if (it != ranges.end() && it->start == start)
    return &*it;
  for (DolLLVMFunctionRange &range : ranges)
    if (range.start == start)
      return &range;
  return nullptr;
}
const DolLLVMFunctionRange *
addressRange(const std::vector<DolLLVMFunctionRange> &ranges, u32 address) {
  auto it = std::upper_bound(ranges.begin(), ranges.end(), address,
                             [](u32 value, const DolLLVMFunctionRange &range) {
                               return value < range.start;
                             });
  if (it != ranges.begin()) {
    --it;
    if (address < it->end)
      return &*it;
  }
  for (const DolLLVMFunctionRange &range : ranges)
    if (address >= range.start && address < range.end)
      return &range;
  return nullptr;
}
bool emptyABI(const DolLLVMFunctionRange &range) {
  if (range.abi_flags || range.abi_blockers || range.direct_memory_accesses ||
      range.generic_memory_accesses || range.helper_calls ||
      range.native_call_targets || range.native_call_depth)
    return false;
  for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
    if (range.semantic_input_state[word] || range.may_def_state[word] ||
        range.must_def_state[word] || range.callsite_live_after[word] ||
        range.semantic_output_state[word] || range.input_state[word] ||
        range.output_state[word] || range.escape_state[word])
      return false;
  return true;
}
u32 targetCount(const DolIRTerminator &term) {
  return term.kind == DOLIR_TERM_COND_BRANCH ? 2u
         : term.kind == DOLIR_TERM_FALLTHROUGH || term.kind == DOLIR_TERM_BRANCH
             ? 1u
         : term.kind == DOLIR_TERM_INDIRECT ? 2u
                                            : 0u;
}
void buildPostCallDefs(const DolIRFunction &function,
                       const std::vector<DolLLVMFunctionRange> &ranges,
                       std::vector<u64> &postCallDefs) {
  postCallDefs.assign(
      static_cast<size_t>(function.block_count) * DOLIR_STATE_MASK_WORDS, 0u);
  for (u32 blockIndex = 0; blockIndex < function.block_count; blockIndex++) {
    const DolIRTerminator &term = function.blocks[blockIndex].terminator;
    if (!term.linked)
      continue;
    for (u32 slot = 0; slot < targetCount(term); slot++) {
      const DolLLVMFunctionRange *target =
          addressRange(ranges, term.target_addresses[slot]);
      if (!target || target->start == function.guest_start ||
          !(target->abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE))
        continue;
      for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
        postCallDefs[static_cast<size_t>(blockIndex) *
                         DOLIR_STATE_MASK_WORDS +
                     word] |= target->output_state[word];
    }
  }
}

bool collectCallEdges(const DolIRModule &source,
                      const std::vector<DolLLVMFunctionRange> &ranges,
                      bool includePostCallDefs,
                      std::vector<DolLLVMCallEdge> &edges, bool updateOnly,
                      bool dirtyOnly = false) {
  size_t edgeIndex = 0;
  for (u32 index = 0; index < source.function_count; index++) {
    const DolIRFunction &function = source.functions[index];
    const DolLLVMFunctionRange *range = addressRange(ranges, function.guest_start);
    if (!range || range->start != function.guest_start)
      continue;
    std::vector<u64> postCallDefs;
    const u64 *postDefs = nullptr;
    if (includePostCallDefs) {
      buildPostCallDefs(function, ranges, postCallDefs);
      postDefs = postCallDefs.data();
    }
    const size_t stateWords =
        static_cast<size_t>(function.block_count) * DOLIR_STATE_MASK_WORDS;
    std::vector<u64> allLive, allDefined, allDirty(stateWords);
    if (dirtyOnly || updateOnly) {
      if (!dolllvm_analyze_callsite_dirty(&function, postDefs, allDirty.data()))
        return false;
    } else {
      allLive.resize(stateWords);
      allDefined.resize(stateWords);
      if (!dolllvm_analyze_callsite_states(
              &function, range->may_def_state, postDefs, allLive.data(),
              allDefined.data(), allDirty.data()))
        return false;
    }
    for (u32 blockIndex = 0; blockIndex < function.block_count; blockIndex++) {
      const DolIRTerminator &term = function.blocks[blockIndex].terminator;
      const size_t offset =
          static_cast<size_t>(blockIndex) * DOLIR_STATE_MASK_WORDS;
      const u64 *mayDirtyBefore = allDirty.data() + offset;
      for (u32 slot = 0; slot < targetCount(term); slot++) {
        const DolLLVMFunctionRange *target =
            addressRange(ranges, term.target_addresses[slot]);
        if (!target || target->start == range->start)
          continue;
        if (updateOnly) {
          if (edgeIndex >= edges.size() ||
              edges[edgeIndex].caller_start != range->start ||
              edges[edgeIndex].callsite_pc != term.guest_pc ||
              edges[edgeIndex].callee_address != term.target_addresses[slot])
            return false;
          std::copy(mayDirtyBefore,
                    mayDirtyBefore + DOLIR_STATE_MASK_WORDS,
                    edges[edgeIndex].may_dirty_before);
        } else {
          DolLLVMCallEdge edge{};
          edge.caller_start = range->start;
          edge.callsite_pc = term.guest_pc;
          edge.callee_address = term.target_addresses[slot];
          if (!dirtyOnly) {
            std::copy(allLive.data() + offset,
                      allLive.data() + offset + DOLIR_STATE_MASK_WORDS,
                      edge.live_after);
            std::copy(allDefined.data() + offset,
                      allDefined.data() + offset + DOLIR_STATE_MASK_WORDS,
                      edge.defined_before);
          }
          std::copy(mayDirtyBefore,
                    mayDirtyBefore + DOLIR_STATE_MASK_WORDS,
                    edge.may_dirty_before);
          edges.push_back(edge);
        }
        edgeIndex++;
      }
    }
  }
  return !updateOnly || edgeIndex == edges.size();
}

} // namespace

namespace dolllvm {

bool needsInterpreter(const DolIRBlock &block) {
  for (u32 index = 0; index < block.instruction_count; index++) {
    const DolIRInstruction &instruction = block.instructions[index];
    if (instruction.op != DOLIR_OP_HELPER_CALL)
      continue;
    if (instruction.aux == DOLIR_HELPER_EXACT_FLOAT)
      return true;
    if (instruction.aux == DOLIR_HELPER_EXACT_PAIRED &&
        (instruction.immediate & 0xffu) > DOLIR_EXACT_PS_MULS1)
      return true;
  }
  return false;
}

void prepareModuleABIs(const DolIRModule &source,
                       std::vector<DolLLVMFunctionRange> &ranges,
                       DolLLVMRuntime runtime,
                       std::vector<DolLLVMCallEdge> *callEdges) {
  for (u32 index = 0; index < source.function_count; index++) {
    const DolIRFunction &function = source.functions[index];
    DolLLVMFunctionRange *range = exactRange(ranges, function.guest_start);
    if (range && emptyABI(*range))
      dolllvm_analyze_function_abi(&function, range);
  }
  if (runtime == DOLLLVM_RUNTIME_MODERNGEKKO)
    dolllvm_enable_native_services(ranges.data(),
                                   static_cast<u32>(ranges.size()));

  std::vector<DolLLVMCallEdge> edges;
  if (!collectCallEdges(source, ranges, false, edges, false))
    return;
  if (!dolllvm_propagate_function_abis(ranges.data(),
                                       static_cast<u32>(ranges.size()),
                                       edges.data(),
                                       static_cast<u32>(edges.size())))
    return;

  if (!collectCallEdges(source, ranges, true, edges, true))
    return;
  if (!dolllvm_propagate_function_abis(ranges.data(),
                                       static_cast<u32>(ranges.size()),
                                       edges.data(),
                                       static_cast<u32>(edges.size())))
    return;
  if (runtime == DOLLLVM_RUNTIME_MODERNGEKKO)
    for (DolLLVMFunctionRange &range : ranges)
      std::fill(std::begin(range.escape_state), std::end(range.escape_state), 0u);
  if (callEdges)
    *callEdges = std::move(edges);
}

bool collectPreparedModuleCallEdges(const DolIRModule &source,
                                    const DolLLVMFunctionRange *ranges,
                                    u32 rangeCount,
                                    std::vector<DolLLVMCallEdge> &callEdges) {
  if (!ranges && rangeCount) return false;
  std::vector<DolLLVMFunctionRange> prepared(ranges, ranges + rangeCount);
  callEdges.clear();
  return collectCallEdges(source, prepared, true, callEdges, false, true);
}

bool FunctionEmitter::stateInput(const DolLLVMFunctionRange *range,
                                 DolIRStateSlot slot) const {
  return range &&
         (dolir_state_mask_test(range->input_state, slot) ||
          (!modern_runtime_ && dolir_state_mask_test(range->escape_state, slot)));
}

bool FunctionEmitter::stateOutput(const DolLLVMFunctionRange *range,
                                  DolIRStateSlot slot) const {
  return range && dolir_state_mask_test(range->output_state, slot);
}

llvm::Type *
FunctionEmitter::nativeResultType(const DolLLVMFunctionRange *range) {
  llvm::SmallVector<llvm::Type *, 32> fields;
  if (!cold_escapes_) {
    fields.push_back(llvm::Type::getInt32Ty(context_));
    fields.push_back(llvm::Type::getInt1Ty(context_));
  }
  if (cold_escapes_) {
    const u32 lanes = nativeResultLaneCount(range);
    if (!lanes)
      return llvm::Type::getVoidTy(context_);
    if (lanes == 1)
      return llvm::Type::getInt64Ty(context_);
    fields.assign(lanes, llvm::Type::getInt64Ty(context_));
  } else {
    for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
      auto stateSlot = static_cast<DolIRStateSlot>(slot);
      if (stateOutput(range, stateSlot))
        fields.push_back(type(dolir_state_type(stateSlot)));
    }
  }
  return llvm::StructType::get(context_, fields);
}

llvm::StructType *FunctionEmitter::chainType() {
  llvm::Type *pointer = llvm::PointerType::getUnqual(context_);
  const u32 bufferWords = intrinsic_escapes_ ? 5u : 64u;
  return llvm::StructType::get(
      context_,
      {llvm::ArrayType::get(pointer, bufferWords),
       llvm::Type::getInt64Ty(context_), llvm::Type::getInt64Ty(context_),
       llvm::Type::getInt64Ty(context_), llvm::Type::getInt64Ty(context_),
       llvm::Type::getInt32Ty(context_), llvm::Type::getInt32Ty(context_),
       llvm::Type::getInt32Ty(context_),
       llvm::ArrayType::get(llvm::Type::getInt64Ty(context_),
                            DOLIR_STATE_COUNT),
       llvm::ArrayType::get(llvm::Type::getInt64Ty(context_),
                            DOLIR_STATE_MASK_WORDS)});
}

llvm::FunctionType *
FunctionEmitter::bodyFunctionType(const DolLLVMFunctionRange *range) {
  llvm::Type *pointer = llvm::PointerType::getUnqual(context_);
  llvm::SmallVector<llvm::Type *, 32> arguments = {pointer};
  if (modern_runtime_)
    arguments.push_back(pointer);
  arguments.push_back(pointer);
  arguments.push_back(llvm::Type::getInt64Ty(context_));
  const bool native =
      range && (range->abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE) != 0;
  if (native) {
    if (nativeCyclesInResult(range))
      arguments.push_back(llvm::Type::getInt64Ty(context_));
    for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
      auto stateSlot = static_cast<DolIRStateSlot>(slot);
      if (stateInput(range, stateSlot))
        arguments.push_back(type(dolir_state_type(stateSlot)));
    }
  }
  llvm::Type *result = native
                           ? static_cast<llvm::Type *>(nativeResultType(range))
                           : llvm::Type::getVoidTy(context_);
  return llvm::FunctionType::get(result, arguments, false);
}

llvm::CallingConv::ID FunctionEmitter::bodyCallingConvention() const {
  return llvm::CallingConv::Fast;
}

} // namespace dolllvm
