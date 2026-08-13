#ifndef DOLRECOMP_LLVM_BACKEND_H
#define DOLRECOMP_LLVM_BACKEND_H

#include "ir/dolir.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u32 start;
    u32 end;
} DolLLVMFunctionRange;

typedef struct {
    const char* target_triple;
    int optimization_level;
    int verify;
    int emit_ir;
    const char* ir_path;
    /* ThinLTO needs bitcode carrying a module summary index, not plain bitcode:
       the summary is what lets the thin link decide which callees to import
       without reading every module's body.

       NOT YET IMPLEMENTED -- these fields are accepted and ignored. See the
       note in dolllvm_emit_object() for what the first attempt got wrong. */
    int emit_bitcode;
    const char* bitcode_path;
    const DolLLVMFunctionRange* function_ranges;
    u32 function_range_count;
} DolLLVMOptions;

bool dolllvm_emit_object(const DolIRModule* module, const char* object_path,
                         const DolLLVMOptions* options, FILE* diagnostics);

// Resolve an optional target to the triple used for emission.
bool dolllvm_effective_triple(const char* requested, char* out, size_t size);

// Validate an object's magic against the effective target triple.
bool dolllvm_object_matches_triple(const char* path, const char* requested);

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
bool dolllvm_codegen_fingerprint(char* out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
