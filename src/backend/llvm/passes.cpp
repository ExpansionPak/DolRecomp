#include "backend/llvm/passes.h"

#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/IR/Module.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/Target/TargetMachine.h>

namespace dolllvm {

using namespace llvm;

static OptimizationLevel optimizationLevel(int level) {
  if (level <= 0)
    return OptimizationLevel::O0;
  if (level == 1)
    return OptimizationLevel::O1;
  if (level == 2)
    return OptimizationLevel::O2;
  return OptimizationLevel::O3;
}

bool optimizeModule(Module &module, TargetMachine &machine,
                    const DolLLVMOptions &options, raw_ostream &diagnostics) {
  if (options.optimization_level <= 0)
    return true;
  LoopAnalysisManager loops;
  FunctionAnalysisManager functions;
  CGSCCAnalysisManager call_graph;
  ModuleAnalysisManager modules;
  PipelineTuningOptions tuning;
  tuning.LoopVectorization = true;
  tuning.SLPVectorization = true;
  std::optional<PGOOptions> pgo;
  if (options.profile_generate_path && options.profile_generate_path[0]) {
    pgo.emplace(options.profile_generate_path, "", "", "",
                vfs::getRealFileSystem(), PGOOptions::IRInstr);
  } else if (options.profile_use_path && options.profile_use_path[0]) {
    pgo.emplace(options.profile_use_path, "", "", "", vfs::getRealFileSystem(),
                PGOOptions::IRUse);
  }
  PassBuilder builder(&machine, tuning, pgo);
  builder.registerModuleAnalyses(modules);
  builder.registerCGSCCAnalyses(call_graph);
  builder.registerFunctionAnalyses(functions);
  builder.registerLoopAnalyses(loops);
  builder.crossRegisterProxies(loops, functions, call_graph, modules);
  if (options.fast_iteration) {
    ModulePassManager pipeline;
    constexpr const char *fastPipeline =
        "function(mem2reg,early-cse<memssa>,"
        "instcombine<no-verify-fixpoint>,simplifycfg,sccp,gvn,dse,adce),"
        "globaldce";
    if (Error error = builder.parsePassPipeline(pipeline, fastPipeline)) {
      diagnostics << "dolllvm: cannot construct fast optimization pipeline: "
                  << toString(std::move(error)) << '\n';
      return false;
    }
    pipeline.run(module, modules);
    return true;
  }
  ModulePassManager pipeline = builder.buildPerModuleDefaultPipeline(
      optimizationLevel(options.optimization_level));
  pipeline.run(module, modules);
  return true;
}

} // namespace dolllvm
