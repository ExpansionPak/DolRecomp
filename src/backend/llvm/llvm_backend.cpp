#include "backend/llvm/llvm_backend.h"
#include "common/options.h"
#include "backend/llvm/llvm_function_emitter.h"

#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Transforms/IPO/ThinLTOBitcodeWriter.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/IR/PassInstrumentation.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Transforms/Instrumentation/InstrProfiling.h>
#include <llvm/Transforms/Instrumentation/PGOInstrumentation.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/Config/llvm-config.h>

#include <atomic>
#include <cstdlib>
#include <cstring>

namespace {

using namespace llvm;

// Shared by emission, cache hashing and resume validation.
static std::string resolveTriple(const char *requested) {
  return requested && requested[0] ? std::string(requested)
                                   : llvm::sys::getDefaultTargetTriple();
}

// Everything below feeds both codegen and the object cache key. Changing any of
// it must invalidate cached objects, so keep it reachable from one place: the
// fingerprint is built from these same accessors, and an edit that misses the
// key produces a build that silently reuses objects from the old settings and
// reports them as a result.
//
// DOLRECOMP_LLVM_CPU and DOLRECOMP_LLVM_FEATURES override the target machine's
// CPU and feature string; "native" resolves to the host. The default stays
// "generic" with no features, which is the portable x86-64 baseline -- a
// released module must run on any x86-64 host, so raising the target has to
// stay opt-in.
//
// That baseline already includes SSE2, which is not incidental here: E001
// measured the vectorizers as worth 1.37x precisely because Gekko
// paired-singles are 2-wide f32 pairs that map onto SSE2. So a wider target is
// a live hypothesis rather than a long shot.
static constexpr const char *kDefaultTargetCPU = "generic";
static constexpr const char *kDefaultTargetFeatures = "";

static std::string targetCPU() {
  const char *cpu = getenv("DOLRECOMP_LLVM_CPU");
  if (!cpu || !cpu[0])
    return kDefaultTargetCPU;
  // createTargetMachine does not expand "native" itself.
  if (!strcmp(cpu, "native"))
    return llvm::sys::getHostCPUName().str();
  return cpu;
}

static std::string targetFeatures() {
  const char *features = getenv("DOLRECOMP_LLVM_FEATURES");
  return features ? features : kDefaultTargetFeatures;
}

// instcombine's fixpoint check is a self-diagnostic for the pass, not a
// correctness property of the IR. Recompiled Gekko functions contain long
// straight-line integer and condition-flag sequences that can still be changing
// after one iteration, which makes the pass call report_fatal_error and take the
// whole recompilation down. Suppressing the check leaves the optimization
// itself intact.
//
// Do not remove the vectorizers. Measured (LLVM-EXPERIMENTS E001): dropping
// loop-vectorize, slp-vectorizer and vector-combine costs **-27%** throughput
// and makes the module 4.6% *larger*. The intuition that they cannot pay off
// against CPU "generic" with an empty feature string is wrong -- SSE2 is part
// of the x86-64 baseline, and Gekko paired-singles are inherently 2-wide f32
// pairs, so SLP has real work to do at that baseline.
// The hand-rolled pipeline below is one shot over each function, and it is
// weaker than what the C backend's chunks get: clang -O3 iterates function
// simplification, interleaves inlining with cleanup inside the CGSCC walk, and
// runs SROA, loop unrolling and several more rounds of instcombine. Here
// cgscc(inline) runs *last*, followed only by ipsccp and globaldce, so nothing
// ever simplifies inlined code.
//
// DOLRECOMP_LLVM_PIPELINE=o3 swaps it for LLVM's own -O3 module pipeline, the
// same one clang builds, and raises codegen from Default (O2) to Aggressive.
// Measuring that is the first step in explaining why compiled C beats this
// backend by 60-70% on the same DolIR (AOT-PERFORMANCE-RESULTS.md 5t).
static bool defaultO3Pipeline() {
  static const bool enabled = [] {
    const char *value = std::getenv("DOLRECOMP_LLVM_PIPELINE");
    return value && value[0] == 'o' && value[1] == '3';
  }();
  return enabled;
}

static constexpr const char *kPassPipeline =
    "function(mem2reg,early-cse<memssa>,instcombine<no-verify-fixpoint>,"
    "simplifycfg,sccp,"
    "correlated-propagation,jump-threading,gvn,dse,adce,loop-simplify,"
    "loop-rotate,loop-mssa(licm),loop-vectorize,slp-vectorizer,vector-"
    "combine,"
    "tailcallelim),cgscc(inline),ipsccp,globaldce";

// The profile path, and a content hash of it, resolved once. The hash is what
// goes in the cache key: two different profiles written to the same path must
// not share objects, and a profile regenerated in place by a collection script
// makes the path alone a non-identity -- exactly the silent stale-reuse the
// fingerprint exists to prevent.
static const std::string &pgoProfilePath() {
  static const std::string path = [] {
    const char *file = getenv("DOLRECOMP_LLVM_PROFILE");
    return std::string(file ? file : "");
  }();
  return path;
}

static const std::string &pgoProfileFingerprint() {
  static const std::string fingerprint = [] {
    if (pgoProfilePath().empty())
      return std::string("none");
    FILE *file = fopen(pgoProfilePath().c_str(), "rb");
    if (!file)
      return std::string("missing");
    unsigned long long hash = 1469598103934665603ull;
    unsigned long long size = 0;
    unsigned char buffer[65536];
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
      size += read;
      for (size_t i = 0; i < read; i++) {
        hash ^= buffer[i];
        hash *= 1099511628211ull;
      }
    }
    fclose(file);
    char text[64];
    snprintf(text, sizeof(text), "%016llx/%llu", hash, size);
    return std::string(text);
  }();
  return fingerprint;
}

static void reportPgoStaleSummary();

// Off by default, so the default module stays byte-identical to an unprofiled
// build and the existing object cache keeps its meaning. "use" without a
// readable DOLRECOMP_LLVM_PROFILE is refused rather than silently degraded to
// an unprofiled build -- an untrained PGO build that looks trained is the one
// failure mode that corrupts a measurement instead of stopping it.
extern "C" int dolllvm_pgo_mode(void) {
  static const int mode = [] {
    const char *requested = getenv("DOLRECOMP_LLVM_PGO");
    if (!requested || !requested[0] || !strcmp(requested, "0") ||
        !strcmp(requested, "off"))
      return (int)DOLLLVM_PGO_OFF;
    if (!strcmp(requested, "gen"))
      return (int)DOLLLVM_PGO_GEN;
    if (strcmp(requested, "use")) {
      fprintf(stderr, "dolllvm: DOLRECOMP_LLVM_PGO must be gen, use or off\n");
      abort();
    }
    if (pgoProfilePath().empty() || pgoProfileFingerprint() == "missing") {
      fprintf(stderr,
              "dolllvm: DOLRECOMP_LLVM_PGO=use needs a readable "
              "DOLRECOMP_LLVM_PROFILE (.profdata)\n");
      abort();
    }
    fprintf(stderr, "dolllvm: PGO use, profile %s (%s)\n",
            pgoProfilePath().c_str(), pgoProfileFingerprint().c_str());
    fflush(stderr);
    // One summary per process, at exit, so a `warn`-policy build ends with a
    // total rather than with thousands of individually ignorable lines.
    atexit(reportPgoStaleSummary);
    return (int)DOLLLVM_PGO_USE;
  }();
  return mode;
}

// Process-wide tallies for the staleness gate. The job runner is threads in one
// process (run_parallel_jobs), so these are atomic and the summary is printed
// once, from an atexit hook registered when use mode is first resolved. Without
// the summary a stale profile under the `warn` policy is thousands of
// individually ignorable lines and no total.
static std::atomic<unsigned long long> pgoMatchedFunctions{0};
static std::atomic<unsigned long long> pgoUnmatchedFunctions{0};
static std::atomic<unsigned long long> pgoStaleChunks{0};

static void reportPgoStaleSummary() {
  const unsigned long long matched = pgoMatchedFunctions.load();
  const unsigned long long unmatched = pgoUnmatchedFunctions.load();
  const unsigned long long total = matched + unmatched;
  if (total == 0)
    return;
  fprintf(stderr,
          "dolllvm: PGO profile match: %llu/%llu functions matched, %llu "
          "unmatched across %llu stale chunks\n",
          matched, total, unmatched, pgoStaleChunks.load());
  if (unmatched != 0)
    fprintf(stderr,
            "dolllvm: PROFILE IS STALE against this DOL -- %.4f%% of emitted "
            "functions carry no profile record. Re-collect the profile against "
            "this binary.\n",
            100.0 * (double)unmatched / (double)total);
  fflush(stderr);
}

static CodeGenOptLevel codegenLevel(int level) {
  if (level <= 0)
    return CodeGenOptLevel::None;
  if (level == 1)
    return CodeGenOptLevel::Less;
  if (level == 2)
    return CodeGenOptLevel::Default;
  return CodeGenOptLevel::Aggressive;
}

static TargetMachine *targetMachine(const Target *target,
                                    const std::string &tripleName, int opt) {
  static thread_local std::string cachedTriple;
  static thread_local int cachedOpt = -1;
  static thread_local std::unique_ptr<TargetMachine> cachedMachine;
  if (!cachedMachine || cachedTriple != tripleName || cachedOpt != opt) {
    TargetOptions options;
    cachedMachine.reset(target->createTargetMachine(
        tripleName, targetCPU(), targetFeatures(), options, Reloc::PIC_,
        std::nullopt, codegenLevel(opt)));
    cachedTriple = tripleName;
    cachedOpt = opt;
  }
  return cachedMachine.get();
}

} // namespace

extern "C" bool dolllvm_emit_object(const DolIRModule *source,
                                    const char *object_path,
                                    const DolLLVMOptions *options,
                                    FILE *diagnostics) {
  if (!source || !object_path || !diagnostics)
    return false;
  static bool initialized = [] {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    return true;
  }();
  (void)initialized;

  int opt = options ? options->optimization_level : 2;
  if (defaultO3Pipeline())
    opt = 3;
  std::string tripleName =
      resolveTriple(options ? options->target_triple : nullptr);
  const llvm::Triple triple(tripleName);
  if (triple.getArch() != llvm::Triple::x86_64 ||
      (!triple.isOSLinux() && !triple.isOSWindows())) {
    fprintf(
        diagnostics,
        "dolllvm: supported production targets are x86-64 Linux and Windows\n");
    return false;
  }

  std::string error;
  const llvm::Target *target =
      llvm::TargetRegistry::lookupTarget(tripleName, error);
  if (!target) {
    fprintf(diagnostics, "dolllvm: %s\n", error.c_str());
    return false;
  }
  llvm::TargetMachine *machine = targetMachine(target, tripleName, opt);
  if (!machine) {
    fprintf(diagnostics, "dolllvm: failed to create target machine\n");
    return false;
  }

  llvm::LLVMContext context;
  llvm::Module module("dolrecomp_native", context);
  module.setTargetTriple(tripleName);
  module.setDataLayout(machine->createDataLayout());
  std::string diagnosticText;
  llvm::raw_string_ostream diagnosticStream(diagnosticText);
  for (u32 i = 0; i < source->function_count; i++) {
    dolllvm::FunctionEmitter emitter(
        context, module, source->functions[i],
        options ? options->function_ranges : nullptr,
        options ? options->function_range_count : 0);
    if (!emitter.emit(diagnosticStream)) {
      diagnosticStream.flush();
      fprintf(diagnostics, "%s", diagnosticText.c_str());
      return false;
    }
  }
  if (llvm::verifyModule(module, &diagnosticStream)) {
    diagnosticStream.flush();
    fprintf(diagnostics, "%s", diagnosticText.c_str());
    return false;
  }

  if (opt > 0) {
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;

    // DOLRECOMP_LLVM_TRACE_PASSES names the last pass and IR unit to start.
    // Without it an optimizer that fails to converge is indistinguishable from
    // one that is merely slow: the historical instcombine hang on this title
    // spun for 49 minutes at 1.00 core with nothing identifying the function.
    // Each line is flushed, so the last line printed is where it stopped.
    llvm::PassInstrumentationCallbacks callbacks;
    const int pgo = dolllvm_pgo_mode();
    const bool tracePasses = getenv("DOLRECOMP_LLVM_TRACE_PASSES") != nullptr;
    // P002. Counted here, acted on after the pipeline has run. The callback is
    // observational -- pass instrumentation cannot change what the passes do --
    // so the gate costs the emitted objects nothing and the fingerprint does
    // not move.
    unsigned long long matchedHere = 0;
    unsigned long long unmatchedHere = 0;
    const bool gatePgo =
        pgo == DOLLLVM_PGO_USE &&
        dolllvm_pgo_stale_policy() != DOLLLVM_PGO_STALE_OFF;
    if (gatePgo) {
      callbacks.registerAfterPassCallback(
          [&matchedHere, &unmatchedHere](llvm::StringRef pass, llvm::Any ir,
                                         const llvm::PreservedAnalyses &) {
            if (pass != "PGOInstrumentationUse")
              return;
            const llvm::Module *const *mod =
                llvm::any_cast<const llvm::Module *>(&ir);
            if (!mod || !*mod)
              return;
            // Immediately after the Use pass, so the count reflects what the
            // profile matched and not what later passes went on to create.
            for (const llvm::Function &function : **mod) {
              if (function.isDeclaration())
                continue;
              if (function.getEntryCount())
                matchedHere++;
              else
                unmatchedHere++;
            }
          });
    }
    if (tracePasses) {
      callbacks.registerBeforeNonSkippedPassCallback(
          [](llvm::StringRef pass, llvm::Any ir) {
            // Any holds a pointer to the IR unit, so any_cast<const T *> on
            // the Any* yields const T *const *.
            std::string unit = "<unknown>";
            const llvm::Function *const *fn =
                llvm::any_cast<const llvm::Function *>(&ir);
            const llvm::Module *const *mod =
                llvm::any_cast<const llvm::Module *>(&ir);
            if (fn && *fn)
              unit = (*fn)->getName().str();
            else if (mod && *mod)
              unit = (*mod)->getName().str();
            fprintf(stderr, "dolllvm: pass %s on %s\n", pass.str().c_str(),
                    unit.c_str());
            fflush(stderr);
          });
    }
    llvm::PassBuilder passBuilder(machine, llvm::PipelineTuningOptions(),
                                  std::nullopt,
                                  (tracePasses || gatePgo) ? &callbacks
                                                           : nullptr);
    passBuilder.registerModuleAnalyses(mam);
    passBuilder.registerCGSCCAnalyses(cgam);
    passBuilder.registerFunctionAnalyses(fam);
    passBuilder.registerLoopAnalyses(lam);
    passBuilder.crossRegisterProxies(lam, fam, cgam, mam);
    llvm::ModulePassManager passes;
    // P001. Front of the pipeline, before anything has touched the emitter's
    // output. Gen and Use must observe byte-identical IR or the CFG hashes they
    // key on disagree and every function silently goes unprofiled; running both
    // here makes that identity structural rather than a property of the passes
    // in between, which are free to change without invalidating a profile.
    if (pgo == DOLLLVM_PGO_GEN) {
#if LLVM_VERSION_MAJOR >= 20
      passes.addPass(llvm::PGOInstrumentationGen(
          llvm::PGOInstrumentationType::FDO));
#else
      passes.addPass(llvm::PGOInstrumentationGen(/*IsCS=*/false));
#endif
    } else if (pgo == DOLLLVM_PGO_USE) {
      passes.addPass(llvm::PGOInstrumentationUse(pgoProfilePath()));
    }
    if (llvm::Error error =
            passBuilder.parsePassPipeline(passes, kPassPipeline)) {
      fprintf(diagnostics,
              "dolllvm: cannot construct optimization pipeline: %s\n",
              llvm::toString(std::move(error)).c_str());
      return false;
    }
    // The counter intrinsics have to survive the optimizer as intrinsics: once
    // lowered they are a load, an add and a store on a global, and GVN or DSE
    // will happily fold two iterations of a loop into one increment. Lower them
    // last, which is where clang lowers them and for the same reason.
    if (pgo == DOLLLVM_PGO_GEN)
      passes.addPass(llvm::InstrProfilingLoweringPass(llvm::InstrProfOptions(),
                                                      /*IsCS=*/false));
    passes.run(module, mam);

    // ThinLTO bitcode, emitted from a CLONE of the optimised module.
    //
    // ThinLTOBitcodeWriterPass is not a pure writer: it splits the module into
    // the part that can be thin-imported and the part that cannot, and it does
    // that in place. Running it on `module` would hand a mutated module to the
    // object emission below, so the object and the bitcode would describe
    // different programs -- with the object being the wrong one.
    //
    // The clone costs memory proportional to one region, which is bounded by
    // the region size cap, and only when bitcode is asked for.
    //
    // Letting the pass build the summary is the point: a first attempt called
    // buildModuleSummaryIndex(module, nullptr, nullptr) by hand and segfaulted,
    // because that signature wants a BlockFrequencyInfo callback and a real
    // ProfileSummaryInfo rather than nulls. This is how clang does it.
    if (options && options->emit_bitcode && options->bitcode_path) {
      std::error_code bitcodeError;
      llvm::raw_fd_ostream bitcodeFile(options->bitcode_path, bitcodeError,
                                       llvm::sys::fs::OF_None);
      if (bitcodeError) {
        fprintf(diagnostics, "dolllvm: cannot write bitcode: %s\n",
                bitcodeError.message().c_str());
        return false;
      }
      std::unique_ptr<llvm::Module> clone = llvm::CloneModule(module);
      llvm::LoopAnalysisManager cloneLam;
      llvm::FunctionAnalysisManager cloneFam;
      llvm::CGSCCAnalysisManager cloneCgam;
      llvm::ModuleAnalysisManager cloneMam;
      llvm::PassBuilder clonePassBuilder(machine);
      clonePassBuilder.registerModuleAnalyses(cloneMam);
      clonePassBuilder.registerCGSCCAnalyses(cloneCgam);
      clonePassBuilder.registerFunctionAnalyses(cloneFam);
      clonePassBuilder.registerLoopAnalyses(cloneLam);
      clonePassBuilder.crossRegisterProxies(cloneLam, cloneFam, cloneCgam,
                                            cloneMam);
      llvm::ModulePassManager bitcodePasses;
      bitcodePasses.addPass(
          llvm::ThinLTOBitcodeWriterPass(bitcodeFile, /*ThinLinkOS=*/nullptr));
      bitcodePasses.run(*clone, cloneMam);
      bitcodeFile.flush();
    }

    // P002. The verdict. Under `error` a stale profile stops the build here,
    // which is the point: the failure this gate exists for is a build that
    // SUCCEEDS while training on records that no longer describe it, and every
    // downstream number then belongs to a module nobody meant to measure.
    if (gatePgo) {
      pgoMatchedFunctions += matchedHere;
      pgoUnmatchedFunctions += unmatchedHere;
      if (unmatchedHere != 0) {
        pgoStaleChunks++;
        fprintf(diagnostics,
                "dolllvm: PGO profile stale for %s: %llu of %llu functions "
                "have no profile record\n",
                object_path ? object_path : "<chunk>", unmatchedHere,
                matchedHere + unmatchedHere);
        fflush(diagnostics);
        if (dolllvm_pgo_stale_policy() == DOLLLVM_PGO_STALE_ERROR) {
          fprintf(diagnostics,
                  "dolllvm: refusing to emit against a stale profile. Re-collect "
                  "it, or set DOLRECOMP_LLVM_PGO_STALE=warn to build anyway.\n");
          fflush(diagnostics);
          return false;
        }
      }
    }
  }
  if (llvm::verifyModule(module, &diagnosticStream)) {
    diagnosticStream.flush();
    fprintf(diagnostics, "%s", diagnosticText.c_str());
    return false;
  }

  if (options && options->emit_ir && options->ir_path) {
    std::error_code irError;
    llvm::raw_fd_ostream irFile(options->ir_path, irError,
                                llvm::sys::fs::OF_Text);
    if (irError) {
      fprintf(diagnostics, "dolllvm: cannot write IR: %s\n",
              irError.message().c_str());
      return false;
    }
    module.print(irFile, nullptr);
  }

  std::error_code objectError;
  llvm::raw_fd_ostream objectFile(object_path, objectError,
                                  llvm::sys::fs::OF_None);
  if (objectError) {
    fprintf(diagnostics, "dolllvm: cannot write object: %s\n",
            objectError.message().c_str());
    return false;
  }
  llvm::legacy::PassManager codegen;
  if (machine->addPassesToEmitFile(codegen, objectFile, nullptr,
                                   llvm::CodeGenFileType::ObjectFile)) {
    fprintf(diagnostics, "dolllvm: target cannot emit objects\n");
    return false;
  }
  codegen.run(module);
  objectFile.flush();
  return true;
}

extern "C" bool dolllvm_effective_triple(const char *requested, char *out,
                                         size_t size) {
  if (!out || size == 0)
    return false;
  const std::string triple = resolveTriple(requested);
  if (triple.size() + 1 > size)
    return false;
  memcpy(out, triple.c_str(), triple.size() + 1);
  return true;
}

extern "C" bool dolllvm_object_matches_triple(const char *path,
                                              const char *requested) {
  FILE *file = fopen(path, "rb");
  if (!file)
    return false;
  unsigned char magic[4] = {0, 0, 0, 0};
  const size_t read = fread(magic, 1, sizeof(magic), file);
  fclose(file);
  if (read != sizeof(magic))
    return false;

  const llvm::Triple triple(resolveTriple(requested));
  if (triple.isOSBinFormatCOFF())
    // IMAGE_FILE_MACHINE_AMD64, little-endian, at offset 0 of a COFF object.
    return magic[0] == 0x64 && magic[1] == 0x86;
  if (triple.isOSBinFormatMachO())
    return magic[0] == 0xCF && magic[1] == 0xFA && magic[2] == 0xED &&
           magic[3] == 0xFE;
  return magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' &&
         magic[3] == 'F';
}

// Default `error`: a stale profile is a wrong measurement, not a slow one, and
// a profile that looks applied and is not costs a whole build cycle.
extern "C" int dolllvm_pgo_stale_policy(void) {
  static const int policy = [] {
    const char *requested = getenv("DOLRECOMP_LLVM_PGO_STALE");
    if (!requested || !requested[0] || !strcmp(requested, "error"))
      return (int)DOLLLVM_PGO_STALE_ERROR;
    if (!strcmp(requested, "warn"))
      return (int)DOLLLVM_PGO_STALE_WARN;
    if (!strcmp(requested, "off") || !strcmp(requested, "0"))
      return (int)DOLLLVM_PGO_STALE_OFF;
    fprintf(stderr,
            "dolllvm: DOLRECOMP_LLVM_PGO_STALE must be error, warn or off\n");
    abort();
    return (int)DOLLLVM_PGO_STALE_ERROR;
  }();
  return policy;
}

extern "C" bool dolllvm_codegen_fingerprint(char *out, size_t size) {
  if (!out || size == 0)
    return false;
  // Every codegen-affecting input the object cache key would otherwise miss.
  // The triple is hashed separately by the caller, which already had it.
  // Keyed to the PGO mode, and in use mode to the profile's CONTENT -- a
  // profile rewritten in place by a collection script makes its path a
  // non-identity. Absent entirely when PGO is off, which is what keeps the
  // default objects byte-identical and the existing cache valid.
  const std::string fingerprint =
      std::string(LLVM_VERSION_STRING) + "|" + targetCPU() + "|" +
      targetFeatures() + "|" + "pic|small|" + kPassPipeline +
      (dolllvm_pgo_mode() == DOLLLVM_PGO_GEN ? "|pgo=gen" : "") +
      (dolllvm_pgo_mode() == DOLLLVM_PGO_USE
           ? "|pgo=use:" + pgoProfileFingerprint()
           : "") +
      // Emitter behaviour flags. These change generated code, and the cache key
      // does not hash the emitter's source -- so leaving them out means flipping
      // one silently reuses objects built with the other setting and the
      // measurement compares nothing. That has happened three times in this
      // project; absent when off, so default objects stay byte-identical.
      (std::getenv("DOLRECOMP_NARROW_BARRIERS") &&
               std::getenv("DOLRECOMP_NARROW_BARRIERS")[0] == '1'
           ? "|narrow=1" : "") +
      (std::getenv("DOLRECOMP_INLINE_REGIONS") &&
               std::getenv("DOLRECOMP_INLINE_REGIONS")[0] == '1'
           ? "|inline=1" : "") +
      // --memory-mode changes the body of every load and store, so a cached
      // object from one mode is not a valid answer for a build in the other.
      // Both modes are marked, unlike the other flags here: objects predating
      // this option were emitted in safe mode and carried no marker, so
      // leaving the new default unmarked would let them satisfy a fast-mode
      // build. Marking both invalidates those once, which is the point.
      (memory_mode_is_fast() ? "|mem=fast" : "|mem=safe") +
      // Suppresses every direct call, so it changes far more emitted code than
      // any other flag here.
      (replacements_enabled() ? "|repl=1" : "") +
      // Changes the signature of every internal region body, so a cached object
      // from the other setting is not merely slower, it is incompatible.
      (reg_args_enabled() ? "|regargs=1" : "") +
      // A different optimization pipeline entirely.
      (defaultO3Pipeline() ? "|pipeline=o3" : "") +
      // Changes where every guest state access points. Nothing about a cached
      // object from the other mode is reusable.
      (state_in_memory() ? "|state=mem" : "");
  if (fingerprint.size() + 1 > size)
    return false;
  memcpy(out, fingerprint.c_str(), fingerprint.size() + 1);
  return true;
}
