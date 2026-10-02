#include "backend/llvm/target.h"

#include <cstdio>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__,    \
                   #x);                                                        \
      return 1;                                                                \
    }                                                                          \
  } while (0)

int main() {
  CHECK(dolllvm::defaultCodegenLevel(0) == 0);
  CHECK(dolllvm::defaultCodegenLevel(1) == 2);
  CHECK(dolllvm::defaultCodegenLevel(2) == 2);
  CHECK(dolllvm::defaultCodegenLevel(3) == 2);
  CHECK(dolllvm::fastIterationCodegenLevel(
            3, dolllvm::kFastIterationCodegenInstructionThreshold - 1) == 2);
  CHECK(dolllvm::fastIterationCodegenLevel(
            3, dolllvm::kFastIterationCodegenInstructionThreshold) == 0);
  CHECK(dolllvm::fastIterationCodegenLevel(
            3, dolllvm::kFastIterationCodegenInstructionThreshold + 1) == 0);

  llvm::LLVMContext context;
  llvm::Module module("policy", context);
  auto *type = llvm::FunctionType::get(llvm::Type::getVoidTy(context), false);
  auto *function = llvm::Function::Create(type, llvm::GlobalValue::ExternalLinkage,
                                          "f", module);
  llvm::BasicBlock::Create(context, "entry", function);
  CHECK(dolllvm::emissionCodegenLevel(3, false, module) == 2);
  CHECK(dolllvm::emissionCodegenLevel(3, true, module) == 2);
  return 0;
}
