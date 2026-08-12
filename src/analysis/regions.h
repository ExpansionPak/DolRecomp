#ifndef DOLRECOMP_ANALYSIS_REGIONS_H
#define DOLRECOMP_ANALYSIS_REGIONS_H

/* Deterministic region planning.
 *
 * A region is the unit the LLVM backend compiles as one module: control flow
 * inside it can be a native branch, and only leaving it costs a state
 * materialization. The fixed-chunk model cuts every N instructions, so a
 * boundary lands wherever it lands. The point of planning is to put boundaries
 * where control flow is *already* leaving.
 *
 * Membership is by whole function, not by block. Splitting a function across
 * regions reintroduces the exact cost being removed -- a live-state handoff in
 * the middle of straight-line code -- so a function is only ever split when it
 * alone exceeds the size limit, and then at block boundaries that keep SCCs
 * intact.
 *
 * Every mode is deterministic: functions are visited in address order and ties
 * break on address, so the same inputs and settings always produce the same
 * plan with the same region numbering.
 */

#include "analysis/cfg.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* Reproduces the current backend: fixed-size cuts, no CFG awareness.
       Kept for comparison and as a fallback, never removed. */
    DOLREGION_MODE_FIXED,
    /* One region per function, subject to the size limit. */
    DOLREGION_MODE_FUNCTION,
    /* Accretes connected functions while they fit, following the call graph. */
    DOLREGION_MODE_CFG,
    /* CFG accretion ordered by profile weight, keeping cold code out of hot
       regions. Falls back to CFG ordering when no weights are loaded, and says
       so in the report rather than silently pretending to be profiled. */
    DOLREGION_MODE_PGO,
} DolRegionMode;

/* Why a region stopped growing. Reported per region so a plan can be argued
   with rather than taken on faith. */
typedef enum {
    DOLREGION_END_SIZE_LIMIT,
    DOLREGION_END_IR_LIMIT,
    DOLREGION_END_NO_CANDIDATE,
    DOLREGION_END_INDIRECT,
    DOLREGION_END_SMC,
    DOLREGION_END_PATCHABLE,
    DOLREGION_END_COLD,
    DOLREGION_END_FUNCTION_BOUNDARY,
    DOLREGION_END_FIXED_CHUNK,
    DOLREGION_END_SECTION_END,
} DolRegionEndReason;

typedef struct {
    /* Guest instructions per region. The dominant control. */
    u32 max_instructions;
    /* Estimated DolIR instructions per region. Estimated, not measured: the IR
       does not exist at plan time. See DOLREGION_IR_PER_GUEST_INSN. */
    u32 max_ir_instructions;
    /* Functions per region, a guard against pathological accretion. */
    u32 max_functions;
    /* Below this weight a function is cold and is not merged into a hot
       region. Only consulted in PGO mode. */
    u64 cold_weight_threshold;

    /* When the call graph runs dry before the size budget does, keep growing
       along addresses instead of closing the region.
     *
     * Without this, accretion is connectivity-bound rather than size-bound: a
     * function reached only indirectly that itself calls nothing has no
     * call-graph neighbours at all, so it becomes a region of one. Measured on
     * Luigi's Mansion, regions averaged 70 instructions against a limit of
     * 1024, and the plan emitted 7,520 compilation units where the fixed arm
     * needed 909.
     *
     * Merging address-adjacent code is cheap and safe: it never adds a
     * crossing, it keeps runs contiguous so a region stays one run instead of
     * several, and functions laid out next to each other generally came from
     * the same translation unit. */
    int merge_address_adjacent;
    /* How far past a region's end the next function may start and still be
       treated as adjacent. Covers alignment padding and small data holes;
       beyond it the code is unrelated and merging only costs size. */
    u32 max_adjacency_gap;
} DolRegionLimits;

/* Rough DolIR instructions emitted per guest instruction. Used only to apply
   max_ir_instructions at plan time; Phase 2 replaces it with the real count
   once regions are actually lowered. */
#define DOLREGION_IR_PER_GUEST_INSN 6u

typedef struct {
    u32 id;
    u32 guest_start;
    u32 guest_end;

    /* Owned blocks, ascending. Indices into DolCfgProgram::blocks. */
    u32* blocks;
    u32 block_count;

    /* Owned function indices, ascending. */
    u32* functions;
    u32 function_count;

    u32 instruction_count;
    u32 estimated_ir_instructions;
    u32 loop_count;
    u32 scc_count;

    /* Edges crossing the region boundary. in_edges is what must be able to
       enter; out_edges is what costs a transfer. */
    u32 in_edges;
    u32 out_edges;
    /* Edges that stayed inside, which is the number being maximised. */
    u32 internal_edges;

    u32 indirect_sites;
    u64 weight;
    bool patchable;
    bool contains_smc;

    DolRegionEndReason end_reason;
} DolRegion;

typedef struct {
    DolRegion* regions;
    u32 region_count;
    u32 region_capacity;

    /* block index -> region id, or DOLCFG_NO_BLOCK. */
    u32* block_region;
    /* function index -> region id, or DOLCFG_NO_BLOCK. */
    u32* function_region;

    DolRegionMode mode;
    DolRegionLimits limits;

    /* True when PGO mode ran without any weights and degraded to CFG order. */
    bool profile_missing;

    /* Totals, for the report and the perf counters. */
    u32 total_instructions;
    u32 split_functions;
    u32 cross_region_edges;
} DolRegionPlan;

void dolregion_default_limits(DolRegionLimits* limits);
bool dolregion_parse_mode(const char* text, DolRegionMode* mode);
const char* dolregion_mode_name(DolRegionMode mode);
const char* dolregion_end_reason_name(DolRegionEndReason reason);

void dolregion_plan_init(DolRegionPlan* plan);
void dolregion_plan_free(DolRegionPlan* plan);

/* Builds the plan. `program` must already be built. */
bool dolregion_plan_build(DolRegionPlan* plan, const DolCfgProgram* program,
                          DolRegionMode mode, const DolRegionLimits* limits,
                          FILE* diagnostics);

/* Writes the machine-readable region report. */
bool dolregion_write_report(const DolRegionPlan* plan,
                            const DolCfgProgram* program, const char* path,
                            FILE* diagnostics);

#ifdef __cplusplus
}
#endif

#endif /* DOLRECOMP_ANALYSIS_REGIONS_H */
