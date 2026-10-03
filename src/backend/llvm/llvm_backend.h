#ifndef DOLRECOMP_LLVM_BACKEND_H
#define DOLRECOMP_LLVM_BACKEND_H

#include "ir/dolir.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  DOLLLVM_FUNCTION_ABI_NATIVE = 1u << 0,
  DOLLLVM_FUNCTION_ABI_NATIVE_MEMORY = 1u << 1,
} DolLLVMFunctionABIFlag;

typedef enum {
  DOLLLVM_ABI_BLOCK_CONTROL = 1u << 0,
  DOLLLVM_ABI_BLOCK_EXCEPTION = 1u << 1,
  DOLLLVM_ABI_BLOCK_HELPER = 1u << 2,
  DOLLLVM_ABI_BLOCK_MEMORY_SERVICE = 1u << 3,
  DOLLLVM_ABI_BLOCK_RFI = 1u << 4,
  DOLLLVM_ABI_BLOCK_FALLBACK = 1u << 5,
} DolLLVMFunctionABIBlocker;

#define DOLLLVM_NATIVE_ABI_VERSION 5u

typedef struct {
  u32 start;
  u32 end;
  u64 semantic_input_state[DOLIR_STATE_MASK_WORDS];
  u64 may_def_state[DOLIR_STATE_MASK_WORDS];
  u64 must_def_state[DOLIR_STATE_MASK_WORDS];
  u64 callsite_live_after[DOLIR_STATE_MASK_WORDS];
  u64 semantic_output_state[DOLIR_STATE_MASK_WORDS];
  u64 input_state[DOLIR_STATE_MASK_WORDS];
  u64 output_state[DOLIR_STATE_MASK_WORDS];
  u64 escape_state[DOLIR_STATE_MASK_WORDS];
  u32 direct_memory_accesses;
  u32 generic_memory_accesses;
  u32 helper_calls;
  u32 native_call_targets;
  u32 native_call_depth;
  u32 abi_blockers;
  u32 abi_flags;
} DolLLVMFunctionRange;

typedef struct {
  u32 caller_start;
  u32 callsite_pc;
  u32 callee_address;
  u64 live_after[DOLIR_STATE_MASK_WORDS];
  u64 defined_before[DOLIR_STATE_MASK_WORDS];
  u64 may_dirty_before[DOLIR_STATE_MASK_WORDS];
} DolLLVMCallEdge;

typedef struct {
  u32 start;
  u32 end;
  const char *symbol;
} DolLLVMPatch;

typedef enum {
  DOLLLVM_TARGET_HOST,
  DOLLLVM_TARGET_X86_64_V2,
  DOLLLVM_TARGET_X86_64_V3,
  DOLLLVM_TARGET_AARCH64_GENERIC,
  DOLLLVM_TARGET_AARCH64_A57,
} DolLLVMTargetProfile;

typedef enum {
  DOLLLVM_SEMANTICS_EXACT,
  DOLLLVM_SEMANTICS_FAST,
} DolLLVMSemantics;

typedef enum {
  DOLLLVM_INSTRUMENTATION_NONE,
  DOLLLVM_INSTRUMENTATION_LOCKSTEP,
} DolLLVMInstrumentation;

typedef enum {
  DOLLLVM_NATIVE_ABI_UNRESTRICTED,
  DOLLLVM_NATIVE_ABI_COMPACT,
  DOLLLVM_NATIVE_ABI_DISABLED,
} DolLLVMNativeABIPolicy;

typedef enum {
  DOLLLVM_RUNTIME_RECOMPCORE,
  DOLLLVM_RUNTIME_MODERNGEKKO,
} DolLLVMRuntime;

typedef struct {
  const char *target_triple;
  DolLLVMTargetProfile target_profile;
  DolLLVMSemantics semantics;
  DolLLVMInstrumentation instrumentation;
  int optimization_level;
  int fast_iteration;
  int verify;
  int emit_ir;
  const char *ir_path;
  const char *symbol_suffix;
  const char *profile_generate_path;
  const char *profile_use_path;
  const char *thinlto_path;
  int emit_thinlto;
  DolLLVMNativeABIPolicy native_abi_policy;
  DolLLVMRuntime runtime;
  int fixed_memory_layout;
  u32 ram_size;
  u32 mem2_size;
  u64 partition_seed;
  int state_in_memory;
  int function_ranges_prepared;
  const DolLLVMFunctionRange *function_ranges;
  u32 function_range_count;
  const DolLLVMCallEdge *call_edges;
  u32 call_edge_count;
  const u32 *entry_points;
  u32 entry_point_count;
  const DolLLVMPatch *patches;
  u32 patch_count;
} DolLLVMOptions;

bool dolllvm_emit_object(const DolIRModule *module, const char *object_path,
                         const DolLLVMOptions *options, FILE *diagnostics);
bool dolllvm_effective_triple(const DolLLVMOptions *options, char *out,
                              size_t size);
bool dolllvm_object_matches_options(const char *path,
                                    const DolLLVMOptions *options);
u32 dolllvm_collect_native_entries(const DolIRFunction *function,
                                   const DolLLVMOptions *options,
                                   u32 *entries, u32 capacity);
bool dolllvm_parse_target_profile(const char *name,
                                  DolLLVMTargetProfile *profile);
const char *dolllvm_target_profile_name(DolLLVMTargetProfile profile);
const char *dolllvm_target_profile_suffix(DolLLVMTargetProfile profile);

bool dolllvm_analyze_function_abi(const DolIRFunction *function,
                                  DolLLVMFunctionRange *range);
bool dolllvm_analyze_callsite_state(const DolIRFunction *function,
                                    u32 block_index,
                                    const u64 *function_outputs,
                                    const u64 *post_call_defs,
                                    u64 *live_after, u64 *defined_before,
                                    u64 *may_dirty_before);
bool dolllvm_analyze_callsite_states(const DolIRFunction *function,
                                     const u64 *function_outputs,
                                     const u64 *post_call_defs,
                                     u64 *live_after, u64 *defined_before,
                                     u64 *may_dirty_before);
bool dolllvm_analyze_callsite_dirty(const DolIRFunction *function,
                                    const u64 *post_call_defs,
                                    u64 *may_dirty_before);
bool dolllvm_propagate_function_abis(DolLLVMFunctionRange *ranges,
                                     u32 range_count,
                                     const DolLLVMCallEdge *edges,
                                     u32 edge_count);
void dolllvm_enable_native_services(DolLLVMFunctionRange *ranges,
                                    u32 range_count);
void dolllvm_apply_native_abi_policy(DolLLVMFunctionRange *ranges,
                                     u32 range_count,
                                     DolLLVMNativeABIPolicy policy);
void dolllvm_report_abi_stats(const DolLLVMFunctionRange *ranges,
                              u32 range_count, const char *target_triple,
                              FILE *output);

// Profile-guided optimization for the LLVM backend.
//
// The C backend gets PGO for free: its chunks are C source, so clang's own
// -fprofile-generate / -fprofile-use reach them through CFLAGS. Nothing reaches
// these objects the same way, because this backend emits IR and codegens it
// in-process -- clang never sees a translation unit. Sample/AutoFDO is
// genuinely unavailable (the emitter attaches no DILocation, so a sample
// profile has nothing to bind to), but IR-level *instrumentation* PGO needs no
// debug info: it is two LLVM passes run over the module the backend builds.
//
//   DOLRECOMP_LLVM_PGO=gen              instrument
//   DOLRECOMP_LLVM_PGO=use              apply DOLRECOMP_LLVM_PROFILE
//   DOLRECOMP_LLVM_PROFILE=<file>       merged .profdata (use mode only)
//
// Both are placed at the very front of the pipeline, on the raw emitter output,
// so the CFG hashes PGOInstrumentationUse matches against are computed on
// exactly the IR PGOInstrumentationGen saw. The lowering pass runs last, as
// clang does it, so the counter intrinsics stay opaque to the optimizer and
// cannot be sunk, merged or eliminated into wrong counts.
//
// -fprofile-generate must still reach the module link line, which is where the
// profiling runtime comes from. A C half of the same module instrumented by
// clang through CFLAGS writes IR-level counters into the same profile, so the
// two merge without special handling.
typedef enum {
  DOLLLVM_PGO_OFF = 0,
  DOLLLVM_PGO_GEN = 1,
  DOLLLVM_PGO_USE = 2
} DolLLVMPGOMode;

int dolllvm_pgo_mode(void);

// The profile-vs-DOL staleness gate.
//
// A profile that no longer describes the emitted CFG does not fail. It
// degrades: PGOInstrumentationUse rejects the mismatched records one function
// at a time and leaves those functions unprofiled, so the build succeeds, the
// module looks trained, and the measurement is quietly against something
// between a profiled and an unprofiled arm. Clang's own warning for the C half
// (-Wprofile-instr-out-of-date) was suppressed on this project, and the LLVM
// half never had one at all.
//
// The gate is a POSITIVE check rather than a warning scrape: after
// PGOInstrumentationUse has run -- observed through a pass-instrumentation
// callback, so nothing about the pipeline moves -- every defined function that
// matched its profile record carries entry-count metadata, and every function
// that did not carries none. On a profile collected from this same module that
// second set is empty, because IR instrumentation records EVERY function at
// gen time, whether or not the scene ever executed it. So a non-zero count
// means the profile and the DOL have diverged, not that the scene was narrow.
//
//   DOLRECOMP_LLVM_PGO_STALE=error   fail the emit (the default)
//   DOLRECOMP_LLVM_PGO_STALE=warn    report and keep building
//   DOLRECOMP_LLVM_PGO_STALE=off     no check
typedef enum {
  DOLLLVM_PGO_STALE_OFF = 0,
  DOLLLVM_PGO_STALE_WARN = 1,
  DOLLLVM_PGO_STALE_ERROR = 2
} DolLLVMPGOStalePolicy;

int dolllvm_pgo_stale_policy(void);

// Every codegen-affecting input that is not already in the object cache key:
// LLVM version, target CPU and feature string, relocation and code model, and
// the pass pipeline. Hash this alongside the instruction words, or a codegen
// change silently reuses objects built with the old settings.
bool dolllvm_codegen_fingerprint(const DolLLVMOptions *options, char *out,
                                 size_t size);

#ifdef __cplusplus
}
#endif

#endif
