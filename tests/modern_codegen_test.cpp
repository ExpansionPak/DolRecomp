#include <cstdio>
#include <memory>

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__,    \
                   #x);                                                        \
      return 1;                                                                \
    }                                                                          \
  } while (0)

int main(int argc, char **argv) {
  CHECK(argc == 2);
  llvm::LLVMContext context;
  llvm::SMDiagnostic diagnostic;
  std::unique_ptr<llvm::Module> module =
      llvm::parseIRFile(argv[1], diagnostic, context);
  if (!module) {
    diagnostic.print(argv[0], llvm::errs());
    return 1;
  }

  llvm::Function *wrapper = module->getFunction("func_80003500");
  llvm::Function *body = module->getFunction("func_80003500_budget");
  llvm::Function *mtmsr = module->getFunction("func_80003D20_budget");
  llvm::Function *timebase = module->getFunction("func_80003D30_budget");
  llvm::Function *wide = module->getFunction("func_80003D40_budget");
  llvm::Function *resume = module->getFunction("func_80003D60_budget");
  llvm::Function *exactService = module->getFunction("func_80003D80_budget");
  llvm::Function *cache = module->getFunction("func_80002400_budget");
  llvm::Function *systemCall = module->getFunction("func_80002600_budget");
  llvm::Function *rfi = module->getFunction("func_80002700_budget");
  CHECK(wrapper != nullptr && body != nullptr);
  CHECK(mtmsr != nullptr);
  CHECK(timebase != nullptr);
  CHECK(wide != nullptr);
  CHECK(resume != nullptr);
  CHECK(exactService != nullptr);
  CHECK(cache != nullptr);
  CHECK(systemCall != nullptr);
  CHECK(rfi == nullptr);
  CHECK(wrapper->arg_size() == 4);
  CHECK(wrapper->getReturnType()->isStructTy());
  CHECK(llvm::cast<llvm::StructType>(wrapper->getReturnType())
            ->getNumElements() == 7);
  CHECK(body->arg_size() >= 5);
  CHECK(body->getReturnType()->isStructTy());
  for (llvm::Type *field :
       llvm::cast<llvm::StructType>(body->getReturnType())->elements())
    CHECK(field->isIntegerTy(64));
  CHECK(wide->getReturnType()->isStructTy());
  CHECK(llvm::cast<llvm::StructType>(wide->getReturnType())->getNumElements() ==
        3);
  CHECK(module->getFunction("_setjmp") != nullptr);
  CHECK(module->getFunction("_longjmp") != nullptr);
  llvm::Function *regionAvailable =
      module->getFunction("moderngekko_native_region_available");
  CHECK(regionAvailable != nullptr && regionAvailable->arg_size() == 3);

  bool state_callback = false;
  bool native_call = false;
  bool native_call_noinline = false;
  bool direct_memory = false;
  bool cold_escape = false;
  bool state_commit = false;
  bool invalidated_exit = false;
  bool timebase_callback = false;
  bool cache_callback = false;
  bool wide_cycle_reset = false;
  bool resume_state_corrupted = false;
  bool exact_service_call = false;
  bool exact_service_resume = false;
  unsigned body_indirect_calls = 0;
  for (llvm::Function &function : *module) {
    if (function.isDeclaration())
      continue;
    for (llvm::BasicBlock &block : function) {
      for (llvm::Instruction &instruction : block) {
        if (&function == exactService) {
          exact_service_resume |=
              block.getName().starts_with("guest_80003D84");
          if (block.getName().starts_with("guest_80003D80"))
            if (auto *call = llvm::dyn_cast<llvm::CallBase>(&instruction))
              exact_service_call |= call->getCalledFunction() == nullptr;
        }
        if (&function == wide) {
          if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&instruction)) {
            llvm::APInt offset(64, 0);
            const llvm::Value *base =
                store->getPointerOperand()->stripAndAccumulateConstantOffsets(
                    module->getDataLayout(), offset, false);
            auto *constant =
                llvm::dyn_cast<llvm::ConstantInt>(store->getValueOperand());
            wide_cycle_reset |= base == wide->getArg(2) && offset == 528 &&
                                constant && constant->isZero();
          }
        }
        if (&function == resume && block.getName() == "cold_entry") {
          if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&instruction)) {
            llvm::APInt offset(64, 0);
            const llvm::Value *base =
                store->getPointerOperand()->stripAndAccumulateConstantOffsets(
                    module->getDataLayout(), offset, false);
            auto *constant =
                llvm::dyn_cast<llvm::ConstantInt>(store->getValueOperand());
            resume_state_corrupted |= base == resume->getArg(2) &&
                                      offset == 608 && constant &&
                                      constant->getZExtValue() == 13;
          }
        }
        if (&function == body && block.getName() == "interception_exit") {
          if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&instruction)) {
            if (auto *value =
                    llvm::dyn_cast<llvm::ConstantInt>(store->getValueOperand()))
              invalidated_exit |= value->getZExtValue() == 4;
          }
        }
        auto *call = llvm::dyn_cast<llvm::CallBase>(&instruction);
        if (call && !call->getCalledFunction()) {
          state_callback = true;
          if (&function == body)
            ++body_indirect_calls;
          if (&function == timebase)
            timebase_callback = true;
          if (&function == cache)
            cache_callback = true;
        }
        if (call && call->getCalledFunction() &&
            call->getCalledFunction()->getName() == "func_80003600_budget") {
          native_call = true;
          native_call_noinline |= call->hasFnAttr(llvm::Attribute::NoInline);
        }
        if (call && call->getCalledFunction() &&
            call->getCalledFunction()->getName() == "_longjmp")
          cold_escape = true;
        if (call && call->getCalledFunction() &&
            call->getCalledFunction()->getName() == "moderngekko_commit_state")
          state_commit = true;
        if (instruction.getName() == "native.load")
          direct_memory = true;
        if (call && call->getCalledFunction())
          CHECK(!call->getCalledFunction()->getName().starts_with("ppc_"));
      }
    }
  }
  CHECK(state_callback);
  CHECK(native_call);
  CHECK(native_call_noinline);
  CHECK(direct_memory);
  CHECK(cold_escape);
  CHECK(state_commit);
  CHECK(invalidated_exit);
  CHECK(timebase_callback);
  CHECK(cache_callback);
  CHECK(!wide_cycle_reset);
  CHECK(!resume_state_corrupted);
  CHECK(exact_service_call);
  CHECK(exact_service_resume);
  CHECK(body_indirect_calls <= 2);
  return 0;
}
