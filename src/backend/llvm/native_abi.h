#ifndef DOLRECOMP_LLVM_NATIVE_ABI_H
#define DOLRECOMP_LLVM_NATIVE_ABI_H

#include "backend/llvm/llvm_backend.h"

#include <vector>

namespace dolllvm {

inline bool canInlineFallback(const DolIRBlock &block) {
  if (block.terminator.kind != DOLIR_TERM_FALLBACK || !block.cycle_cost)
    return false;

  switch (block.terminator.raw >> 26) {
  case 16:
  case 17:
  case 18:
  case 19:
    return false;
  default:
    return true;
  }
}

void prepareModuleABIs(const DolIRModule &source,
                       std::vector<DolLLVMFunctionRange> &ranges,
                       DolLLVMRuntime runtime,
                       std::vector<DolLLVMCallEdge> *callEdges = nullptr);
bool collectPreparedModuleCallEdges(const DolIRModule &source,
                                    const DolLLVMFunctionRange *ranges,
                                    u32 rangeCount,
                                    std::vector<DolLLVMCallEdge> &callEdges);
bool needsInterpreter(const DolIRBlock &block);
void collectRegionLeaders(const DolIRFunction &function, bool modernRuntime,
                          bool nativeABI, const u32 *entryPoints,
                          u32 entryPointCount, std::vector<bool> &leaders);

} // namespace dolllvm

#endif
