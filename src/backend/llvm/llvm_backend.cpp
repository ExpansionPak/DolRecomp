#include "backend/llvm/llvm_backend.h"

#include "backend/llvm/emitter.h"
#include "backend/llvm/native_abi.h"
#include "backend/llvm/passes.h"
#include "backend/llvm/target.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <llvm/Analysis/ModuleSummaryAnalysis.h>
#include <llvm/Analysis/ProfileSummaryInfo.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>

namespace {

using namespace llvm;

int selectedCodegenLevel(int ir_level) {
  const char *configured = std::getenv("DOLRECOMP_LLVM_CODEGEN_LEVEL");
  if (!configured || !configured[0])
    return ir_level > 0 ? 2 : 0;
  char *end = nullptr;
  long level = std::strtol(configured, &end, 10);
  return end && !*end && level >= 0 && level <= 3 ? static_cast<int>(level)
                                                  : (ir_level > 0 ? 2 : 0);
}

bool readableProfile(const char *path, FILE *diagnostics) {
  if (!path || !path[0])
    return true;
  FILE *file = fopen(path, "rb");
  if (!file) {
    fprintf(diagnostics, "dolllvm: cannot read profile '%s'\n", path);
    return false;
  }
  fclose(file);
  return true;
}

void applyTargetAttributes(Module &module,
                           const dolllvm::TargetProfile &profile,
                           const DolLLVMOptions &options) {
  for (Function &function : module) {
    if (function.isDeclaration())
      continue;
    function.addFnAttr("target-cpu", profile.cpu);
    if (!profile.features.empty())
      function.addFnAttr("target-features", profile.features);
    function.addFnAttr(Attribute::NoUnwind);
    if (options.semantics == DOLLLVM_SEMANTICS_EXACT)
      function.addFnAttr("strictfp");
  }
}

bool writeIR(Module &module, const DolLLVMOptions &options, FILE *diagnostics) {
  if (!options.emit_ir || !options.ir_path)
    return true;
  std::error_code error;
  raw_fd_ostream file(options.ir_path, error, sys::fs::OF_Text);
  if (error) {
    fprintf(diagnostics, "dolllvm: cannot write IR: %s\n",
            error.message().c_str());
    return false;
  }
  module.print(file, nullptr);
  return true;
}

bool writeObject(Module &module, TargetMachine &machine, const char *path,
                 FILE *diagnostics) {
  std::error_code error;
  raw_fd_ostream file(path, error, sys::fs::OF_None);
  if (error) {
    fprintf(diagnostics, "dolllvm: cannot write object: %s\n",
            error.message().c_str());
    return false;
  }
  legacy::PassManager passes;
  if (machine.addPassesToEmitFile(passes, file, nullptr,
                                  CodeGenFileType::ObjectFile)) {
    fprintf(diagnostics, "dolllvm: target cannot emit objects\n");
    return false;
  }
  passes.run(module);
  file.flush();
  return true;
}

bool writeThinLTO(Module &module, const DolLLVMOptions &options,
                  FILE *diagnostics) {
  if (!options.emit_thinlto || !options.thinlto_path)
    return true;
  std::error_code error;
  raw_fd_ostream file(options.thinlto_path, error, sys::fs::OF_None);
  if (error) {
    fprintf(diagnostics, "dolllvm: cannot write ThinLTO bitcode: %s\n",
            error.message().c_str());
    return false;
  }
  ProfileSummaryInfo profile(module);
  ModuleSummaryIndex index = buildModuleSummaryIndex(module, {}, &profile);
  WriteBitcodeToFile(module, file, false, &index, true);
  file.flush();
  return true;
}

} // namespace

extern "C" bool dolllvm_emit_object(const DolIRModule *source,
                                    const char *object_path,
                                    const DolLLVMOptions *given,
                                    FILE *diagnostics) {
  if (!source || !object_path || !diagnostics)
    return false;
  DolLLVMOptions options{};
  if (given)
    options = *given;
  else
    options.optimization_level = 3;
  std::vector<DolLLVMFunctionRange> ranges;
  std::vector<DolLLVMCallEdge> callEdges;
  if (options.function_ranges && options.function_range_count) {
    ranges.assign(options.function_ranges,
                  options.function_ranges + options.function_range_count);
    dolllvm::prepareModuleABIs(*source, ranges, options.runtime, &callEdges);
    dolllvm_apply_native_abi_policy(ranges.data(),
                                    static_cast<u32>(ranges.size()),
                                    options.native_abi_policy);
    options.function_ranges = ranges.data();
    options.call_edges = callEdges.data();
    options.call_edge_count = static_cast<u32>(callEdges.size());
  }
  if (!readableProfile(options.profile_use_path, diagnostics))
    return false;

  dolllvm::TargetProfile profile;
  std::string error;
  if (!dolllvm::resolveTargetProfile(&options, profile, error)) {
    fprintf(diagnostics, "dolllvm: %s\n", error.c_str());
    return false;
  }
  std::unique_ptr<TargetMachine> machine = dolllvm::createTargetMachine(
      profile, selectedCodegenLevel(options.optimization_level),
      options.semantics, error);
  if (!machine) {
    fprintf(diagnostics, "dolllvm: %s\n", error.c_str());
    return false;
  }

  LLVMContext context;
  Module module("dolrecomp_native", context);
  module.setTargetTriple(profile.triple);
  module.setDataLayout(machine->createDataLayout());
  module.setSourceFileName("dolrecomp://deterministic/module");
  std::string text;
  raw_string_ostream stream(text);
  const bool timings = std::getenv("DOLRECOMP_LLVM_TIMINGS") != nullptr;
  auto checkpoint = std::chrono::steady_clock::now();
  auto report = [&](const char *stage) {
    if (!timings)
      return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - checkpoint);
    fprintf(diagnostics, "dolllvm timing %s %s: %lld ms\n", object_path, stage,
            static_cast<long long>(elapsed.count()));
    checkpoint = std::chrono::steady_clock::now();
  };

  for (u32 index = 0; index < source->function_count; index++) {
    dolllvm::FunctionEmitter emitter(context, module, source->functions[index],
                                     options);
    if (!emitter.emit(stream)) {
      stream.flush();
      fprintf(diagnostics, "%s", text.c_str());
      return false;
    }
  }
  applyTargetAttributes(module, profile, options);
  report("lower");
  if (verifyModule(module, &stream) ||
      !dolllvm::optimizeModule(module, *machine, options, stream) ||
      verifyModule(module, &stream)) {
    stream.flush();
    fprintf(diagnostics, "%s", text.c_str());
    return false;
  }
  report("optimize");
  if (!writeIR(module, options, diagnostics) ||
      !writeThinLTO(module, options, diagnostics) ||
      !writeObject(module, *machine, object_path, diagnostics))
    return false;
  report("codegen");
  return true;
}

extern "C" bool dolllvm_effective_triple(const DolLLVMOptions *options,
                                         char *out, size_t size) {
  if (!out || !size)
    return false;
  dolllvm::TargetProfile profile;
  std::string error;
  if (!dolllvm::resolveTargetProfile(options, profile, error) ||
      profile.triple.size() + 1 > size)
    return false;
  memcpy(out, profile.triple.c_str(), profile.triple.size() + 1);
  return true;
}

extern "C" bool dolllvm_object_matches_options(const char *path,
                                               const DolLLVMOptions *options) {
  dolllvm::TargetProfile profile;
  std::string error;
  return dolllvm::resolveTargetProfile(options, profile, error) &&
         dolllvm::objectMatchesProfile(path, profile);
}

extern "C" u32 dolllvm_collect_native_entries(const DolIRFunction *function,
                                               const DolLLVMOptions *options,
                                               u32 *entries, u32 capacity) {
  if (!function || !options)
    return 0;

  bool nativeABI = false;
  for (u32 index = 0; index < options->function_range_count; index++) {
    const DolLLVMFunctionRange &range = options->function_ranges[index];
    if (range.start != function->guest_start)
      continue;
    nativeABI = (range.abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE) != 0;
    break;
  }
  if (options->runtime == DOLLLVM_RUNTIME_MODERNGEKKO && !nativeABI)
    return 0;

  std::vector<bool> leaders;
  dolllvm::collectRegionLeaders(
      *function, options->runtime == DOLLLVM_RUNTIME_MODERNGEKKO, nativeABI,
      options->entry_points, options->entry_point_count, leaders);
  u32 count = 0;
  for (u32 index = 0; index < function->block_count; index++) {
    if (!leaders[index] ||
        function->blocks[index].terminator.kind == DOLIR_TERM_FALLBACK)
      continue;
    if (entries && count < capacity)
      entries[count] = function->blocks[index].guest_address;
    count++;
  }
  return count;
}

extern "C" bool dolllvm_codegen_fingerprint(const DolLLVMOptions *options,
                                            char *out, size_t size) {
  if (!out || !size)
    return false;
  dolllvm::TargetProfile profile;
  std::string error;
  if (!dolllvm::resolveTargetProfile(options, profile, error))
    return false;
  const int written = std::snprintf(
      out, size,
      "llvm=%s|triple=%s|cpu=%s|features=%s|native-abi=%u|native-policy=%u|"
      "runtime=%u|"
      "mask-words=%u|"
      "state-count=%u|calling=fastcc|control=pc32x2|return=i64-lanes|"
      "escape=structured-cold-v2|memory=proven-ram-domain-v2|cycles=return-or-chain|"
      "x86-return-registers=3|"
      "aarch64-return-registers=8|reloc=pic|pipeline=default-per-module",
      LLVM_VERSION_STRING, profile.triple.c_str(), profile.cpu.c_str(),
      profile.features.c_str(), DOLLLVM_NATIVE_ABI_VERSION,
      static_cast<unsigned>(options ? options->native_abi_policy
                                    : DOLLLVM_NATIVE_ABI_UNRESTRICTED),
      static_cast<unsigned>(options ? options->runtime
                                    : DOLLLVM_RUNTIME_RECOMPCORE),
      DOLIR_STATE_MASK_WORDS, DOLIR_STATE_COUNT);
  return written >= 0 && static_cast<size_t>(written) < size;
}
