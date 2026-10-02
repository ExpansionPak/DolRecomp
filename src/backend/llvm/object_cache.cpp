#include "backend/llvm/object_cache.h"

#include "backend/llvm/llvm_backend.h"
#include "backend/llvm/target.h"

#include <array>
#include <cstdlib>
#include <string>
#include <string_view>

#include <llvm/ADT/SmallVector.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/SHA256.h>
#include <llvm/Support/raw_ostream.h>

namespace dolllvm {

using namespace llvm;

namespace {

std::string hexDigest(const std::array<uint8_t, 32> &digest) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result(64, '0');
  for (size_t index = 0; index < digest.size(); index++) {
    result[index * 2] = digits[digest[index] >> 4];
    result[index * 2 + 1] = digits[digest[index] & 15];
  }
  return result;
}

void addNumber(SHA256 &hash, uint64_t value) {
  hash.update(StringRef(reinterpret_cast<const char *>(&value), sizeof(value)));
}

bool cacheEnabled(const DolLLVMOptions &options, const char **root) {
  *root = std::getenv("DOLRECOMP_LLVM_PREOPT_OBJECT_CACHE");
  if (!*root || !(*root)[0] || std::string_view(*root) == "off")
    return false;
  if (options.emit_ir || options.emit_thinlto || options.profile_generate_path ||
      options.profile_use_path)
    return false;
  return true;
}

std::string cacheKey(Module &module, const TargetProfile &profile,
                     const DolLLVMOptions &options) {
  SmallVector<char, 0> bitcode;
  raw_svector_ostream bitcodeStream(bitcode);
  WriteBitcodeToFile(module, bitcodeStream);

  SHA256 hash;
  hash.update("dolrecomp-preopt-object-v1");
  hash.update(LLVM_VERSION_STRING);
#ifdef DOLRECOMP_PREOPT_POLICY_HASH
  hash.update(DOLRECOMP_PREOPT_POLICY_HASH);
#endif
#ifdef DOLRECOMP_LLVM_POSTOPT_SOURCE_HASH
  hash.update(DOLRECOMP_LLVM_POSTOPT_SOURCE_HASH);
#endif
  hash.update(StringRef(bitcode.data(), bitcode.size()));
  hash.update(profile.triple);
  hash.update(profile.cpu);
  hash.update(profile.features);
  addNumber(hash, static_cast<uint64_t>(options.optimization_level));
  addNumber(hash, static_cast<uint64_t>(options.fast_iteration));
  addNumber(hash, static_cast<uint64_t>(options.semantics));
  const char *configured = std::getenv("DOLRECOMP_LLVM_CODEGEN_LEVEL");
  hash.update(configured && configured[0]
                  ? configured
                  : (options.fast_iteration ? "fast-adaptive-codegen-v1"
                                            : "default-o2-v1"));
  return hexDigest(hash.final());
}

} // namespace

bool tryReusePreoptObject(Module &module, const TargetProfile &profile,
                          const DolLLVMOptions &options,
                          const char *objectPath, std::string &cachePath) {
  const char *root = nullptr;
  if (!cacheEnabled(options, &root))
    return false;
  if (std::error_code error = sys::fs::create_directories(root))
    return false;
  cachePath = std::string(root) + "/" + cacheKey(module, profile, options) + ".o";
  if (!sys::fs::exists(cachePath) || !objectMatchesProfile(cachePath.c_str(), profile))
    return false;
  if (std::error_code error = sys::fs::copy_file(cachePath, objectPath))
    return false;
  return true;
}

void storePreoptObject(const std::string &cachePath, const char *objectPath) {
  if (cachePath.empty() || sys::fs::exists(cachePath))
    return;
  std::string temporary = cachePath + ".tmp." +
                          std::to_string(sys::Process::getProcessId());
  sys::fs::remove(temporary);
  if (sys::fs::copy_file(objectPath, temporary))
    return;
  if (sys::fs::rename(temporary, cachePath))
    sys::fs::remove(temporary);
}

} // namespace dolllvm
