#include "ir/dolir.h"

#include <cstdio>
#include <memory>
#include <vector>

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
  llvm::Function *callee = module->getFunction("func_80003600_budget");
  llvm::Function *mtmsr = module->getFunction("func_80003D20_budget");
  llvm::Function *timebase = module->getFunction("func_80003D30_budget");
  llvm::Function *wideWrapper = module->getFunction("func_80003D40");
  llvm::Function *wide = module->getFunction("func_80003D40_budget");
  llvm::Function *resume = module->getFunction("func_80003D60_budget");
  llvm::Function *exactService = module->getFunction("func_80003D80_budget");
  llvm::Function *inlineFallback = module->getFunction("func_80003DA0_budget");
  llvm::Function *controlFallback = module->getFunction("func_80003DB0_budget");
  llvm::Function *structuredOuter = module->getFunction("func_80004A00_budget");
  llvm::Function *structuredMiddle = module->getFunction("func_80004B00_budget");
  llvm::Function *memoryService = module->getFunction("func_80003100_budget");
  llvm::Function *fifoService = module->getFunction("func_80003140_budget");
  llvm::Function *psqService = module->getFunction("func_80003A60_budget");
  llvm::Function *cache = module->getFunction("func_80002400_budget");
  llvm::Function *systemCall = module->getFunction("func_80002600_budget");
  llvm::Function *rfi = module->getFunction("func_80002700_budget");
  CHECK(wrapper != nullptr && body != nullptr && callee != nullptr);
  CHECK(wrapper->getVisibility() == llvm::GlobalValue::DefaultVisibility);
  CHECK(body->getVisibility() == llvm::GlobalValue::DefaultVisibility);
  CHECK(callee->getVisibility() == llvm::GlobalValue::DefaultVisibility);
  CHECK(!wrapper->isDSOLocal());
  CHECK(!body->isDSOLocal());
  CHECK(!callee->isDSOLocal());
  CHECK(body->arg_size() == 8);
  CHECK(callee->arg_size() == 7);
  for (const llvm::GlobalVariable &global : module->globals())
    CHECK(!global.getName().ends_with(".dirty"));

  llvm::AllocaInst *chainAlloca = nullptr;
  for (llvm::Instruction &instruction : wrapper->getEntryBlock())
    if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(&instruction))
      if (alloca->getName() == "chain")
        chainAlloca = alloca;
  CHECK(chainAlloca != nullptr);
  auto *chainStruct =
      llvm::dyn_cast<llvm::StructType>(chainAlloca->getAllocatedType());
  CHECK(chainStruct != nullptr && chainStruct->getNumElements() == 11);
  const llvm::StructLayout *chainLayout =
      module->getDataLayout().getStructLayout(chainStruct);
  const uint64_t budgetOffset = chainLayout->getElementOffset(4);
  const uint64_t pcOffset = chainLayout->getElementOffset(5);
  const uint64_t npcOffset = chainLayout->getElementOffset(6);
  const uint64_t reasonOffset = chainLayout->getElementOffset(7);
  CHECK(chainStruct->getElementType(8)->isPointerTy());
  CHECK(chainStruct->getElementType(9)->isPointerTy());
  CHECK(chainStruct->getElementType(10)->isPointerTy());
  CHECK(mtmsr != nullptr);
  const llvm::BasicBlock *mtmsrService = nullptr;
  for (const llvm::BasicBlock &block : *mtmsr)
    if (block.getName() == "msr_ee_service")
      mtmsrService = &block;
  CHECK(mtmsrService != nullptr);
  bool mtmsrConditionalService = false;
  for (const llvm::BasicBlock &block : *mtmsr)
  {
    const auto *branch = llvm::dyn_cast<llvm::BranchInst>(block.getTerminator());
    if (!branch || !branch->isConditional())
      continue;
    for (unsigned successor = 0; successor < branch->getNumSuccessors(); ++successor)
      mtmsrConditionalService |= branch->getSuccessor(successor) == mtmsrService;
  }
  CHECK(mtmsrConditionalService);
  CHECK(timebase != nullptr);
  CHECK(wideWrapper != nullptr && wide != nullptr);
  CHECK(resume != nullptr);
  CHECK(exactService != nullptr);
  CHECK(inlineFallback != nullptr);
  CHECK(controlFallback != nullptr);
  CHECK(structuredOuter != nullptr && structuredMiddle != nullptr);
  CHECK(memoryService != nullptr);
  CHECK(fifoService != nullptr);
  CHECK(psqService != nullptr);
  CHECK(cache != nullptr);
  CHECK(systemCall != nullptr);
  CHECK(rfi == nullptr);
  CHECK(wrapper->arg_size() == 9);
  CHECK(wrapper->getArg(3)->getName() == "state_values");
  CHECK(wrapper->getArg(4)->getName() == "dirty_mask");
  CHECK(wrapper->getArg(5)->getName() == "valid_mask");
  CHECK(wrapper->getArg(8)->getName() == "cycle_base");
  CHECK(wrapper->getReturnType()->isVoidTy());
  CHECK(wrapper->getArg(0)->hasStructRetAttr());
  auto *wrapperExitType = llvm::dyn_cast<llvm::StructType>(
      wrapper->getParamStructRetType(0));
  CHECK(wrapperExitType != nullptr && wrapperExitType->getNumElements() == 7);
  CHECK(body->arg_size() >= 5);
  CHECK(body->getReturnType()->isStructTy());
  for (llvm::Type *field :
       llvm::cast<llvm::StructType>(body->getReturnType())->elements())
    CHECK(field->isIntegerTy(64));
  CHECK(wide->getReturnType()->isStructTy());
  CHECK(llvm::cast<llvm::StructType>(wide->getReturnType())->getNumElements() ==
        3);
  CHECK(module->getFunction("_setjmp") == nullptr);
  CHECK(module->getFunction("_longjmp") == nullptr);
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
  bool fallback_service_dispatch = false;
  bool fallback_service_call = false;
  bool fallback_service_yield = false;
  bool fallback_service_continue = false;
  bool control_fallback_service = false;
  bool wrapper_reload_guard = false;
  bool wrapper_dirty_stage = false;
  bool wrapper_valid_stage = false;
  bool memory_read_bridge = false;
  bool memory_write_bridge = false;
  bool memory_budget_compare = false;
  bool memory_budget_add = false;
  bool memory_yield_pc = false;
  bool memory_yield_npc = false;
  bool memory_yield_reason = false;
  bool fifo_try_path = false;
  bool fifo_partial_path = false;
  bool fifo_full_bridge = false;
  bool wide_wrapper_reload = false;
  bool wide_wrapper_commit = false;
  bool structured_budget_marker = false;
  bool structured_budget_longjmp = false;
  bool native_structured_propagation = false;
  bool wrapper_structured_return = false;
  bool wrapper_structured_commit = false;
  bool cold_service_longjmp = false;
  bool structured_return_mismatch = false;
  unsigned wide_wrapper_indirect_calls = 0;
  const llvm::PHINode *memorySideExitPC = nullptr;
  std::vector<const llvm::Value *> memoryBudgets;
  unsigned body_indirect_calls = 0;
  for (llvm::Function &function : *module) {
    if (function.isDeclaration())
      continue;
    for (llvm::BasicBlock &block : function) {
      for (llvm::Instruction &instruction : block) {
        if (&function == wrapper &&
            block.getName().starts_with("structured_return"))
          wrapper_structured_return = true;
        if (&function == wrapper &&
            block.getName().starts_with("reload_persistent_state"))
          wrapper_reload_guard = true;
        if (&function == structuredOuter &&
            block.getName().starts_with("native_call_structured_exit"))
          native_structured_propagation = true;
        if (&function == fifoService) {
          fifo_try_path |= block.getName().starts_with("fifo_try_probe");
          fifo_partial_path |= block.getName().starts_with("fifo_partial_probe");
        }
        if (&function == memoryService) {
          if (auto *phi = llvm::dyn_cast<llvm::PHINode>(&instruction)) {
            if (phi->getName() == "side_exit_pc") {
              for (const llvm::Value *incoming : phi->incoming_values()) {
                auto *constant = llvm::dyn_cast<llvm::ConstantInt>(incoming);
                if (constant && constant->getZExtValue() == 0x80003104u) {
                  memorySideExitPC = phi;
                  memory_yield_pc = true;
                }
              }
            }
          }
        }
        if (&function == exactService) {
          exact_service_resume |=
              block.getName().starts_with("guest_80003D84");
          bool serviceCallBlock = false;
          if (const auto *branch =
                  llvm::dyn_cast<llvm::BranchInst>(block.getTerminator())) {
            for (unsigned successor = 0;
                 successor < branch->getNumSuccessors(); ++successor)
              serviceCallBlock |=
                  branch->getSuccessor(successor)->getName().starts_with(
                      "instruction_service_resume");
          }
          if (serviceCallBlock)
            if (auto *call = llvm::dyn_cast<llvm::CallBase>(&instruction))
              exact_service_call |= call->getCalledFunction() == nullptr;
        }
        if (&function == inlineFallback) {
          fallback_service_dispatch |=
              block.getName().starts_with("fallback_service");
          fallback_service_yield |=
              block.getName().starts_with("instruction_service_yield");
          fallback_service_continue |=
              block.getName().starts_with("guest_80003DA4");
          if (block.getName().starts_with("fallback_service_invoke"))
            if (auto *call = llvm::dyn_cast<llvm::CallBase>(&instruction))
              fallback_service_call |= call->getCalledFunction() == nullptr;
        }
        if (&function == controlFallback)
          control_fallback_service |=
              block.getName().starts_with("fallback_service");
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
        if (block.getName().starts_with("return_escape")) {
          if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&instruction)) {
            llvm::APInt offset(64, 0);
            const llvm::Value *base =
                store->getPointerOperand()->stripAndAccumulateConstantOffsets(
                    module->getDataLayout(), offset, false);
            if (base == function.getArg(2) &&
                offset.getZExtValue() == reasonOffset) {
              if (auto *value =
                      llvm::dyn_cast<llvm::BinaryOperator>(store->getValueOperand()))
                structured_return_mismatch |=
                    value->getOpcode() == llvm::Instruction::Or;
            }
          }
        }
        auto *call = llvm::dyn_cast<llvm::CallBase>(&instruction);
        if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&instruction)) {
          llvm::APInt offset(64, 0);
          const llvm::Value *base =
              store->getPointerOperand()->stripAndAccumulateConstantOffsets(
                  module->getDataLayout(), offset, false);
          if (&function == wrapper) {
            wrapper_dirty_stage |= base == wrapper->getArg(4);
            wrapper_valid_stage |= base == wrapper->getArg(5);
          }
          if (&function == body && base == body->getArg(2)) {
            const uint64_t raw = offset.getZExtValue();
            auto *constant =
                llvm::dyn_cast<llvm::ConstantInt>(store->getValueOperand());
            structured_budget_marker |=
                raw == reasonOffset && constant &&
                constant->getZExtValue() == 0x80000000u;
          }
          if (&function == memoryService && base == memoryService->getArg(2)) {
            const uint64_t raw = offset.getZExtValue();
            if (raw == budgetOffset) {
              memoryBudgets.push_back(store->getValueOperand());
              memory_budget_add |=
                  llvm::isa<llvm::BinaryOperator>(store->getValueOperand()) &&
                  llvm::cast<llvm::BinaryOperator>(store->getValueOperand())
                          ->getOpcode() == llvm::Instruction::Add;
            }
            auto *constant =
                llvm::dyn_cast<llvm::ConstantInt>(store->getValueOperand());
            if (block.getName().starts_with("memory_service_yield") && constant) {
              memory_yield_pc |=
                  raw == pcOffset && constant->getZExtValue() == 0x80003104u;
              memory_yield_npc |=
                  raw == npcOffset && constant->getZExtValue() == 0x80003108u;
              memory_yield_reason |=
                  raw == reasonOffset && constant->isZero();
            }
            if (block.getName().starts_with("shared_side_exit") && constant)
              memory_yield_reason |=
                  raw == reasonOffset &&
                  constant->getZExtValue() == 0x80000000u;
            if (raw == pcOffset && store->getValueOperand() == memorySideExitPC)
              memory_yield_pc = true;
            if (raw == npcOffset) {
              if (auto *add = llvm::dyn_cast<llvm::BinaryOperator>(
                      store->getValueOperand())) {
                if (add->getOpcode() == llvm::Instruction::Add) {
                  const llvm::Value *other = nullptr;
                  if (add->getOperand(0) == memorySideExitPC)
                    other = add->getOperand(1);
                  else if (add->getOperand(1) == memorySideExitPC)
                    other = add->getOperand(0);
                  auto *four = llvm::dyn_cast_or_null<llvm::ConstantInt>(other);
                  memory_yield_npc |= four && four->getZExtValue() == 4u;
                }
              }
            }
          }
        }
        if (&function == memoryService)
          if (auto *compare = llvm::dyn_cast<llvm::ICmpInst>(&instruction))
            if (compare->getPredicate() == llvm::ICmpInst::ICMP_UGE)
              for (const llvm::Value *budget : memoryBudgets)
                memory_budget_compare |= compare->getOperand(0) == budget ||
                                         compare->getOperand(1) == budget;
        if (call && call->getCalledFunction() &&
            call->getCalledFunction()->getName() == "moderngekko_commit_state") {
          wrapper_structured_commit |=
              &function == wrapper && block.getName() == "structured_return";
        }
        if (call && !call->getCalledFunction()) {
          state_callback = true;
          if (&function == body)
            ++body_indirect_calls;
          if (&function == wideWrapper)
            ++wide_wrapper_indirect_calls;
          if (&function == timebase)
            timebase_callback = true;
          if (&function == cache)
            cache_callback = true;
        }
        if (&function == memoryService && call && call->getCalledFunction()) {
          memory_read_bridge |=
              call->getCalledFunction()->getName() == "moderngekko_read_memory";
          memory_write_bridge |=
              call->getCalledFunction()->getName() == "moderngekko_write_memory";
        }
        if (&function == fifoService && call && call->getCalledFunction())
          fifo_full_bridge |=
              call->getCalledFunction()->getName() == "moderngekko_write_memory";
        if (&function == wideWrapper && call && call->getCalledFunction()) {
          wide_wrapper_reload |=
              call->getCalledFunction()->getName() == "moderngekko_reload_state";
          wide_wrapper_commit |=
              call->getCalledFunction()->getName() == "moderngekko_commit_state";
        }
        if (call && call->getCalledFunction() &&
            call->getCalledFunction()->getName() == "func_80003600_budget") {
          native_call = true;
          native_call_noinline |= call->hasFnAttr(llvm::Attribute::NoInline);
          if (auto *branch = llvm::dyn_cast<llvm::BranchInst>(block.getTerminator())) {
            if (branch->isConditional()) {
              for (unsigned successor = 0; successor < 2; successor++)
                native_structured_propagation |=
                    branch->getSuccessor(successor)->getName() ==
                    "native_call_continue";
            }
          }
        }
        if (call && call->getCalledFunction() &&
            call->getCalledFunction()->getName() == "_longjmp") {
          cold_escape = true;
          structured_budget_longjmp |=
              &function == body &&
              block.getName().starts_with("shared_side_exit");
          cold_service_longjmp |=
              &function == memoryService &&
              block.getName().starts_with("memory_service_failure");
        }
        if (&function == wrapper &&
            block.getName().starts_with("structured_return") && call &&
            call->getCalledFunction() &&
            call->getCalledFunction()->getName() == "moderngekko_commit_state")
          wrapper_structured_commit = true;
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
  CHECK(!cold_escape);
  CHECK(state_commit);
  CHECK(invalidated_exit);
  CHECK(timebase_callback);
  CHECK(cache_callback);
  CHECK(!wide_cycle_reset);
  CHECK(!resume_state_corrupted);
  CHECK(exact_service_call);
  CHECK(exact_service_resume);
  CHECK(fallback_service_dispatch);
  CHECK(fallback_service_call);
  CHECK(fallback_service_yield);
  CHECK(fallback_service_continue);
  CHECK(!control_fallback_service);
  CHECK(!wrapper_reload_guard);
  CHECK(wrapper_dirty_stage);
  CHECK(wrapper_valid_stage);
  CHECK(memory_read_bridge);
  CHECK(memory_write_bridge);
  CHECK(memory_budget_add);
  CHECK(memory_budget_compare);
  CHECK(memory_yield_pc);
  CHECK(memory_yield_npc);
  CHECK(memory_yield_reason);
  CHECK(fifo_try_path);
  CHECK(fifo_partial_path);
  CHECK(fifo_full_bridge);
  CHECK(!wide_wrapper_reload);
  CHECK(!wide_wrapper_commit);
  CHECK(structured_budget_marker);
  CHECK(!structured_budget_longjmp);
  CHECK(native_structured_propagation);
  CHECK(wrapper_structured_return);
  CHECK(!wrapper_structured_commit);
  CHECK(!cold_service_longjmp);
  CHECK(structured_return_mismatch);
  CHECK(wide_wrapper_indirect_calls == 1);
  return 0;
}
