#ifndef DOLRECOMP_PERF_H
#define DOLRECOMP_PERF_H

/* Phase 0 instrumentation.
 *
 * Two populations of numbers are reported through one mechanism:
 *
 *   COMPILE counters are raised by dolrecomp itself while it plans regions and
 *   drives LLVM. They are always collected -- they cost a handful of adds per
 *   region against a backend that is already running an optimizer -- but are
 *   only *written out* when --perf-report is given.
 *
 *   RUNTIME counters are raised by generated code and by the hosting runtime
 *   (ModernGekko). Those cannot be unconditionally live: a counter on the guest
 *   memory fast path would be a store per guest load. They are emitted into the
 *   generated output behind DOLRECOMP_PERF and compile to nothing unless the
 *   module is deliberately built with it, which is what "nearly zero-overhead
 *   when disabled" means here.
 *
 * The X-macro below is the single source of truth. Adding a counter to it
 * extends the struct, the JSON object, the console table and the reset path at
 * once, so those three cannot drift apart.
 *
 * Determinism: every counter is a plain u64 incremented from the CPU thread
 * that executes guest code. DolRecomp generates a single guest CPU thread, so
 * no atomics are needed and repeated runs of the same workload produce the same
 * counts. A host that drives generated code from several threads at once must
 * define DOLRECOMP_PERF_ATOMIC (see the generated header) or accept lost
 * updates -- it is a measurement build either way, never a shipping one.
 */

#include "common/types.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DOLRECOMP_PERF_COUNTERS(C)
 *   C(field, "json.name", "Console label", GROUP)
 *
 * GROUP drives the console summary's section headings only.
 */
#define DOLRECOMP_PERF_GROUP_DISPATCH "Dispatch and linking"
#define DOLRECOMP_PERF_GROUP_STATE    "Guest state traffic"
#define DOLRECOMP_PERF_GROUP_INDIRECT "Indirect control flow"
#define DOLRECOMP_PERF_GROUP_MEMORY   "Guest memory"
#define DOLRECOMP_PERF_GROUP_EXEC     "Execution"
#define DOLRECOMP_PERF_GROUP_COMPILE  "Compilation"

#define DOLRECOMP_PERF_RUNTIME_COUNTERS(C)                                     \
    C(dispatcher_entries, "dispatcher_entries",                                \
      "Dispatcher entries", DOLRECOMP_PERF_GROUP_DISPATCH)                     \
    C(region_transfers_direct, "region_transfers_direct",                      \
      "Direct region-to-region transfers", DOLRECOMP_PERF_GROUP_DISPATCH)      \
    C(guest_calls_direct, "guest_calls_direct",                                \
      "Direct native guest calls", DOLRECOMP_PERF_GROUP_DISPATCH)              \
    C(tail_transfers, "tail_transfers",                                        \
      "Native tail transfers", DOLRECOMP_PERF_GROUP_DISPATCH)                  \
    C(calls_via_thunk, "calls_via_thunk",                                      \
      "Calls still using public thunks", DOLRECOMP_PERF_GROUP_DISPATCH)        \
    C(calls_via_dispatch, "calls_via_dispatch",                                \
      "Calls still returning through dispatch", DOLRECOMP_PERF_GROUP_DISPATCH) \
                                                                               \
    C(state_materializations_full, "state_materializations_full",              \
      "Full CPUState materializations", DOLRECOMP_PERF_GROUP_STATE)            \
    C(state_syncs_partial, "state_syncs_partial",                              \
      "Partial state synchronizations", DOLRECOMP_PERF_GROUP_STATE)            \
    C(state_reloads_after_boundary, "state_reloads_after_boundary",            \
      "State reloads after runtime boundaries", DOLRECOMP_PERF_GROUP_STATE)    \
                                                                               \
    C(indirect_branches, "indirect_branches",                                  \
      "Indirect branch executions", DOLRECOMP_PERF_GROUP_INDIRECT)             \
    C(indirect_cache_hits, "indirect_cache_hits",                              \
      "Indirect target-cache hits", DOLRECOMP_PERF_GROUP_INDIRECT)             \
    C(indirect_cache_misses, "indirect_cache_misses",                          \
      "Indirect target-cache misses", DOLRECOMP_PERF_GROUP_INDIRECT)           \
    C(blr_prediction_hits, "blr_prediction_hits",                              \
      "BLR prediction hits", DOLRECOMP_PERF_GROUP_INDIRECT)                    \
    C(blr_prediction_misses, "blr_prediction_misses",                          \
      "BLR prediction misses", DOLRECOMP_PERF_GROUP_INDIRECT)                  \
                                                                               \
    C(mem1_fast_reads, "mem1_fast_reads",                                      \
      "MEM1 fast-path reads", DOLRECOMP_PERF_GROUP_MEMORY)                     \
    C(mem1_fast_writes, "mem1_fast_writes",                                    \
      "MEM1 fast-path writes", DOLRECOMP_PERF_GROUP_MEMORY)                    \
    C(mem2_fast_reads, "mem2_fast_reads",                                      \
      "MEM2 fast-path reads", DOLRECOMP_PERF_GROUP_MEMORY)                     \
    C(mem2_fast_writes, "mem2_fast_writes",                                    \
      "MEM2 fast-path writes", DOLRECOMP_PERF_GROUP_MEMORY)                    \
    C(const_ram_accesses, "const_ram_accesses",                                \
      "Constant-address RAM accesses", DOLRECOMP_PERF_GROUP_MEMORY)            \
    C(const_mmio_accesses, "const_mmio_accesses",                              \
      "Constant-address MMIO accesses", DOLRECOMP_PERF_GROUP_MEMORY)           \
    C(slow_reads, "slow_reads",                                                \
      "Generic slow memory reads", DOLRECOMP_PERF_GROUP_MEMORY)                \
    C(slow_writes, "slow_writes",                                              \
      "Generic slow memory writes", DOLRECOMP_PERF_GROUP_MEMORY)               \
    C(fastmem_faults, "fastmem_faults",                                        \
      "Fault-assisted fastmem faults", DOLRECOMP_PERF_GROUP_MEMORY)            \
                                                                               \
    C(exception_exits, "exception_exits",                                      \
      "Exception exits", DOLRECOMP_PERF_GROUP_EXEC)                            \
    C(runtime_helper_calls, "runtime_helper_calls",                            \
      "Runtime helper calls", DOLRECOMP_PERF_GROUP_EXEC)                       \
    C(fallback_instructions, "fallback_instructions",                          \
      "Fallback-instruction executions", DOLRECOMP_PERF_GROUP_EXEC)            \
    C(region_executions, "region_executions",                                  \
      "Region executions", DOLRECOMP_PERF_GROUP_EXEC)                          \
    C(hot_region_executions, "hot_region_executions",                          \
      "Hot-region executions", DOLRECOMP_PERF_GROUP_EXEC)                      \
    C(cycles_charged, "cycles_charged",                                        \
      "Cycles charged", DOLRECOMP_PERF_GROUP_EXEC)

#define DOLRECOMP_PERF_COMPILE_COUNTERS(C)                                     \
    C(regions_planned, "regions_planned",                                      \
      "Regions planned", DOLRECOMP_PERF_GROUP_COMPILE)                         \
    C(region_guest_instructions, "region_guest_instructions",                  \
      "Guest instructions in regions", DOLRECOMP_PERF_GROUP_COMPILE)           \
    C(region_ir_instructions, "region_ir_instructions",                        \
      "DolIR instructions emitted", DOLRECOMP_PERF_GROUP_COMPILE)              \
    C(region_code_bytes, "region_code_bytes",                                  \
      "Generated code bytes", DOLRECOMP_PERF_GROUP_COMPILE)                    \
    C(llvm_optimize_ns, "llvm_optimize_ns",                                    \
      "LLVM optimization time (ns)", DOLRECOMP_PERF_GROUP_COMPILE)             \
    C(llvm_codegen_ns, "llvm_codegen_ns",                                      \
      "LLVM code-generation time (ns)", DOLRECOMP_PERF_GROUP_COMPILE)          \
    C(artifact_cache_hits, "artifact_cache_hits",                              \
      "Object/bitcode cache hits", DOLRECOMP_PERF_GROUP_COMPILE)               \
    C(artifact_cache_misses, "artifact_cache_misses",                          \
      "Object/bitcode cache misses", DOLRECOMP_PERF_GROUP_COMPILE)

#define DOLRECOMP_PERF_COUNTERS(C)                                             \
    DOLRECOMP_PERF_RUNTIME_COUNTERS(C)                                         \
    DOLRECOMP_PERF_COMPILE_COUNTERS(C)

typedef struct {
#define DOLRECOMP_PERF_FIELD(field, json, label, group) u64 field;
    DOLRECOMP_PERF_COUNTERS(DOLRECOMP_PERF_FIELD)
#undef DOLRECOMP_PERF_FIELD
} DolPerfCounters;

/* Per-region compilation detail, kept alongside the aggregate counters so the
 * report can show where compile time and code size actually went. Phase 1 fills
 * these in from the planner; the fixed-chunk path records one entry per chunk so
 * the two modes stay directly comparable. */
typedef struct {
    u32 region_id;
    u32 guest_start;
    u32 guest_end;
    u32 guest_instructions;
    u32 ir_instructions;
    u32 blocks;
    u32 loops;
    u32 code_bytes;
    u64 optimize_ns;
    u64 codegen_ns;
    int cache_hit;
} DolPerfRegion;

typedef struct {
    DolPerfCounters counters;
    DolPerfRegion* regions;
    u32 region_count;
    u32 region_capacity;

    /* Build identity, reproduced verbatim into the report so a number can
     * always be traced back to the configuration that produced it. */
    char backend[32];
    char region_mode[32];
    char target_triple[128];
    char target_cpu[64];
    char target_features[256];
    char lto_mode[16];
    char pgo_mode[16];
    char pgo_profile_hash[80];
    char mod_policy[16];
    char memory_mode[32];
    char llvm_version[32];
    u64 wall_ns;
    int enabled;
} DolPerfReport;

/* The process-wide report used by the compiler. */
DolPerfReport* dolperf_report(void);

void dolperf_reset(DolPerfReport* report);
void dolperf_free(DolPerfReport* report);

/* Monotonic nanoseconds, for the *_ns counters. */
u64 dolperf_now_ns(void);

/* Records one region's compilation detail and folds it into the aggregate
 * counters. Safe to call with report == NULL. */
void dolperf_add_region(DolPerfReport* report, const DolPerfRegion* region);

/* Writes the machine-readable report. Returns false and leaves a message on
 * `diagnostics` if the file cannot be written. */
bool dolperf_write_json(const DolPerfReport* report, const char* path,
                        FILE* diagnostics);

/* Prints the human-readable summary table. Counters that are zero across a
 * whole group are omitted so an un-instrumented run stays readable. */
void dolperf_print_summary(const DolPerfReport* report, FILE* out);

/* Merges a counter block collected by a generated module at runtime (read back
 * through the generated dolrecomp_perf.h ABI) into a report, so one JSON file
 * can carry both halves. */
void dolperf_merge_runtime(DolPerfReport* report, const DolPerfCounters* from);

/* Parses a runtime counter dump written by the generated
 * dolrecomp_perf_write_json() into `out`. Used by the benchmark harness to pull
 * a game run's counters back into a comparison report. */
bool dolperf_read_runtime_json(const char* path, DolPerfCounters* out,
                               FILE* diagnostics);

/* The generated-code instrumentation header. The backends write this next to
 * the generated module so both the emitted C and the hosting runtime agree on
 * the counter block layout and the increment macros. */
void dolperf_emit_runtime_header(FILE* out);

#ifdef __cplusplus
}
#endif

#endif /* DOLRECOMP_PERF_H */
