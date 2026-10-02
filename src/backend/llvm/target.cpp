#include "backend/llvm/target.h"

#include <array>
#include <cstring>

#include <llvm/MC/TargetRegistry.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

namespace dolllvm {

using namespace llvm;

struct ProfileDefinition {
  DolLLVMTargetProfile id;
  const char *name;
  const char *suffix;
  const char *triple;
  const char *cpu;
  const char *features;
  bool aarch64;
};

static constexpr std::array<ProfileDefinition, 5> definitions{{
    {DOLLLVM_TARGET_HOST, "host", "host", nullptr, "generic", "", false},
    {DOLLLVM_TARGET_X86_64_V2, "x86-64-v2", "x86_64_v2",
     "x86_64-unknown-linux-gnu", "x86-64-v2", "", false},
    {DOLLLVM_TARGET_X86_64_V3, "x86-64-v3", "x86_64_v3",
     "x86_64-unknown-linux-gnu", "x86-64-v3", "", false},
    {DOLLLVM_TARGET_AARCH64_GENERIC, "aarch64", "aarch64",
     "aarch64-unknown-linux-gnu", "generic", "+v8a", true},
    {DOLLLVM_TARGET_AARCH64_A57, "aarch64-a57", "aarch64_a57",
     "aarch64-unknown-linux-gnu", "cortex-a57", "+v8a,+crc", true},
}};

static const ProfileDefinition *definition(DolLLVMTargetProfile id) {
  for (const auto &candidate : definitions)
    if (candidate.id == id)
      return &candidate;
  return nullptr;
}

void initializeTargets() {
  static const bool once = [] {
  // Only the targets DolRecomp can emit, and only those the host LLVM
  // actually ships. InitializeAll* expands to every target this LLVM was
  // configured with, which forces the link to carry backends the emitter
  // never uses; naming X86 and AArch64 unconditionally instead would fail to
  // compile against an LLVM built without one of them. CMake derives these
  // two macros from LLVM_TARGETS_TO_BUILD, so this tracks the host build.
#if defined(DOLLLVM_HAVE_X86_TARGET)
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();
    LLVMInitializeX86AsmParser();
#endif
#if defined(DOLLLVM_HAVE_AARCH64_TARGET)
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();
    LLVMInitializeAArch64AsmParser();
#endif
    return true;
  }();
  (void)once;
}

bool resolveTargetProfile(const DolLLVMOptions *options, TargetProfile &result,
                          std::string &error) {
  DolLLVMTargetProfile id =
      options ? options->target_profile : DOLLLVM_TARGET_HOST;
  const ProfileDefinition *selected = definition(id);
  if (!selected) {
    error = "unknown target profile";
    return false;
  }
  result.name = selected->name;
  result.suffix = selected->suffix;
  result.cpu = selected->cpu;
  result.features = selected->features;
  result.aarch64 = selected->aarch64;
  const std::string hostTriple = sys::getDefaultTargetTriple();
  const Triple host(hostTriple);
  if (options && options->target_triple && options->target_triple[0])
    result.triple = options->target_triple;
  else if ((selected->aarch64 && host.isAArch64()) ||
           (!selected->aarch64 && id != DOLLLVM_TARGET_HOST &&
            host.getArch() == Triple::x86_64))
    result.triple = hostTriple;
  else if (selected->triple)
    result.triple = selected->triple;
  else
    result.triple = hostTriple;
  Triple triple(result.triple);
  if (id == DOLLLVM_TARGET_HOST) {
    result.aarch64 = triple.isAArch64();
    if (!triple.isX86() && !triple.isAArch64()) {
      error = "host profile requires x86-64 or AArch64";
      return false;
    }
  } else if (result.aarch64 != triple.isAArch64() ||
             (!result.aarch64 && triple.getArch() != Triple::x86_64)) {
    error = "target profile and triple architectures disagree";
    return false;
  }
  return true;
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

int defaultCodegenLevel(int ir_optimization_level) {
  if (ir_optimization_level <= 0)
    return 0;
  return 2;
}

int fastIterationCodegenLevel(int ir_optimization_level,
                              uint64_t module_instruction_count) {
  if (module_instruction_count >= kFastIterationCodegenInstructionThreshold)
    return 0;
  return defaultCodegenLevel(ir_optimization_level);
}

uint64_t moduleInstructionCount(const Module &module) {
  uint64_t instructions = 0;
  for (const Function &function : module)
    for (const BasicBlock &block : function)
      instructions += block.size();
  return instructions;
}

int emissionCodegenLevel(int ir_optimization_level, bool fast_iteration,
                         const Module &module) {
  if (!fast_iteration)
    return defaultCodegenLevel(ir_optimization_level);
  return fastIterationCodegenLevel(ir_optimization_level,
                                   moduleInstructionCount(module));
}

std::unique_ptr<TargetMachine> createTargetMachine(const TargetProfile &profile,
                                                   int optimization_level,
                                                   DolLLVMSemantics semantics,
                                                   std::string &error) {
  initializeTargets();
  const Target *target = TargetRegistry::lookupTarget(profile.triple, error);
  if (!target)
    return nullptr;
  TargetOptions options;
  options.AllowFPOpFusion = semantics == DOLLLVM_SEMANTICS_FAST
                                ? FPOpFusion::Fast
                                : FPOpFusion::Strict;
  return std::unique_ptr<TargetMachine>(target->createTargetMachine(
      profile.triple, profile.cpu, profile.features, options, Reloc::PIC_,
      std::nullopt, codegenLevel(optimization_level)));
}

bool objectMatchesProfile(const char *path, const TargetProfile &profile) {
  FILE *file = fopen(path, "rb");
  if (!file)
    return false;
  unsigned char bytes[20]{};
  size_t count = fread(bytes, 1, sizeof(bytes), file);
  fclose(file);
  Triple triple(profile.triple);
  if (triple.isOSBinFormatCOFF())
    return count >= 2 && bytes[0] == 0x64 && bytes[1] == 0x86;
  if (triple.isOSBinFormatMachO())
    return count >= 4 && bytes[0] == 0xcf && bytes[1] == 0xfa &&
           bytes[2] == 0xed && bytes[3] == 0xfe;
  if (count < 20 || std::memcmp(bytes,
                                "\x7f"
                                "ELF",
                                4) != 0)
    return false;
  const unsigned machine = unsigned(bytes[18]) | (unsigned(bytes[19]) << 8);
  return machine == (profile.aarch64 ? 183u : 62u);
}

} // namespace dolllvm

extern "C" bool dolllvm_parse_target_profile(const char *name,
                                             DolLLVMTargetProfile *profile) {
  if (!name || !profile)
    return false;
  for (const auto &candidate : dolllvm::definitions) {
    if (!std::strcmp(name, candidate.name)) {
      *profile = candidate.id;
      return true;
    }
  }
  return false;
}

extern "C" const char *
dolllvm_target_profile_name(DolLLVMTargetProfile profile) {
  const auto *selected = dolllvm::definition(profile);
  return selected ? selected->name : "invalid";
}

extern "C" const char *
dolllvm_target_profile_suffix(DolLLVMTargetProfile profile) {
  const auto *selected = dolllvm::definition(profile);
  return selected ? selected->suffix : "invalid";
}
