#pragma once

#include "backend/llvm/llvm_backend.h"

#include <string>

namespace llvm {
class Module;
}

namespace dolllvm {

struct TargetProfile;

bool tryReusePreoptObject(llvm::Module &module, const TargetProfile &profile,
                          const DolLLVMOptions &options,
                          const char *objectPath, std::string &cachePath);
void storePreoptObject(const std::string &cachePath, const char *objectPath);

} // namespace dolllvm
