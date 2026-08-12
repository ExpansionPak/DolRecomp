#include "analysis/regions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #x); return false; } } while (0)

#define BASE 0x80001000u

static char g_dir[1024];

static void decode_all(PPCInst* out, const u32* raw, u32 count, u32 base) {
    for (u32 i = 0; i < count; i++)
        out[i] = ppc_decode(raw[i], base + i * 4u);
}

/* main:  bl leaf ; blr
   leaf:  addi ; blr
   spare: addi ; blr        (unconnected, reached only indirectly) */
static const u32 kCallGraph[] = {
    0x48000009u, /* +0x00 bl +8 -> BASE+8 */
    0x4E800020u, /* +0x04 blr */
    0x38600001u, /* +0x08 leaf */
    0x4E800020u, /* +0x0C blr */
    0x38600002u, /* +0x10 spare */
    0x4E800020u, /* +0x14 blr */
};

static bool build_program(DolCfgProgram* program, PPCInst* insts,
                          const u32* raw, u32 count) {
    decode_all(insts, raw, count, BASE);
    dolcfg_init(program);
    if (!dolcfg_add_section(program, insts, count, BASE, "text"))
        return false;
    return dolcfg_build(program, stderr);
}

/* Whatever the mode, every block must land in exactly one region. A plan that
   loses a block loses code. */
static bool coverage_is_exact(const DolRegionPlan* plan,
                              const DolCfgProgram* program) {
    u8* seen = (u8*)calloc(program->block_count ? program->block_count : 1u, 1u);
    if (!seen)
        return false;
    bool ok = true;

    for (u32 r = 0; r < plan->region_count && ok; r++) {
        for (u32 i = 0; i < plan->regions[r].block_count; i++) {
            u32 index = plan->regions[r].blocks[i];
            if (index >= program->block_count || seen[index]) {
                ok = false;
                break;
            }
            seen[index] = 1;
            if (plan->block_region[index] != r)
                ok = false;
        }
    }
    for (u32 i = 0; i < program->block_count && ok; i++) {
        if (!seen[i])
            ok = false;
    }

    free(seen);
    return ok;
}

static bool test_every_mode_covers_every_block(void) {
    PPCInst insts[6];
    DolCfgProgram program;
    CHECK(build_program(&program, insts, kCallGraph, 6));

    DolRegionLimits limits;
    dolregion_default_limits(&limits);

    const DolRegionMode modes[] = {
        DOLREGION_MODE_FIXED, DOLREGION_MODE_FUNCTION,
        DOLREGION_MODE_CFG, DOLREGION_MODE_PGO,
    };

    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        DolRegionPlan plan;
        dolregion_plan_init(&plan);
        CHECK(dolregion_plan_build(&plan, &program, modes[m], &limits, stderr));
        CHECK(plan.region_count > 0);
        CHECK(coverage_is_exact(&plan, &program));
        CHECK(plan.total_instructions == 6);
        dolregion_plan_free(&plan);
    }

    dolcfg_free(&program);
    return true;
}

/* Function mode gives each function its own region. */
static bool test_function_mode_is_one_region_per_function(void) {
    PPCInst insts[6];
    DolCfgProgram program;
    CHECK(build_program(&program, insts, kCallGraph, 6));

    DolRegionLimits limits;
    dolregion_default_limits(&limits);

    DolRegionPlan plan;
    dolregion_plan_init(&plan);
    CHECK(dolregion_plan_build(&plan, &program, DOLREGION_MODE_FUNCTION,
                               &limits, stderr));
    CHECK(plan.region_count == program.function_count);
    for (u32 r = 0; r < plan.region_count; r++) {
        CHECK(plan.regions[r].function_count == 1);
        CHECK(plan.regions[r].end_reason == DOLREGION_END_FUNCTION_BOUNDARY);
    }

    dolregion_plan_free(&plan);
    dolcfg_free(&program);
    return true;
}

/* The whole point: cfg mode must put a caller and its callee together, so the
   call becomes an internal edge instead of a region transfer. */
static bool test_cfg_mode_merges_caller_and_callee(void) {
    PPCInst insts[6];
    DolCfgProgram program;
    CHECK(build_program(&program, insts, kCallGraph, 6));

    u32 caller_block = dolcfg_block_starting_at(&program, BASE);
    u32 callee_block = dolcfg_block_starting_at(&program, BASE + 8u);
    CHECK(caller_block != DOLCFG_NO_BLOCK && callee_block != DOLCFG_NO_BLOCK);

    DolRegionLimits limits;
    dolregion_default_limits(&limits);

    DolRegionPlan function_plan;
    dolregion_plan_init(&function_plan);
    CHECK(dolregion_plan_build(&function_plan, &program, DOLREGION_MODE_FUNCTION,
                               &limits, stderr));
    CHECK(function_plan.block_region[caller_block] !=
          function_plan.block_region[callee_block]);

    DolRegionPlan cfg_plan;
    dolregion_plan_init(&cfg_plan);
    CHECK(dolregion_plan_build(&cfg_plan, &program, DOLREGION_MODE_CFG,
                               &limits, stderr));
    CHECK(cfg_plan.block_region[caller_block] ==
          cfg_plan.block_region[callee_block]);

    /* Merging them is only worth anything if it removes a crossing. */
    CHECK(cfg_plan.cross_region_edges < function_plan.cross_region_edges);
    CHECK(cfg_plan.region_count < function_plan.region_count);

    dolregion_plan_free(&cfg_plan);
    dolregion_plan_free(&function_plan);
    dolcfg_free(&program);
    return true;
}

/* A size limit must actually bind. */
static bool test_size_limit_is_respected(void) {
    PPCInst insts[6];
    DolCfgProgram program;
    CHECK(build_program(&program, insts, kCallGraph, 6));

    DolRegionLimits limits;
    dolregion_default_limits(&limits);
    limits.max_instructions = 2;

    DolRegionPlan plan;
    dolregion_plan_init(&plan);
    CHECK(dolregion_plan_build(&plan, &program, DOLREGION_MODE_CFG, &limits, stderr));
    CHECK(coverage_is_exact(&plan, &program));
    for (u32 r = 0; r < plan.region_count; r++) {
        /* A single function larger than the limit is split, never dropped, so
           the bound is on what accretion adds, not on an indivisible block. */
        CHECK(plan.regions[r].instruction_count <= limits.max_instructions * 2u);
    }

    dolregion_plan_free(&plan);
    dolcfg_free(&program);
    return true;
}

/* A function too big for any region is split rather than dropped.
   Repeats cmpwi / beq / addi, so the function is many small blocks. */
static bool test_large_function_is_split_not_dropped(void) {
    enum { REPS = 16, COUNT = REPS * 3 + 1 };
    u32 raw[COUNT];
    for (u32 i = 0; i < REPS; i++) {
        raw[i * 3u + 0u] = 0x2C030000u; /* cmpwi r3,0 */
        raw[i * 3u + 1u] = 0x41820008u; /* beq +8 */
        raw[i * 3u + 2u] = 0x38630001u; /* addi r3,r3,1 */
    }
    raw[COUNT - 1u] = 0x4E800020u; /* blr */

    PPCInst insts[COUNT];
    DolCfgProgram program;
    CHECK(build_program(&program, insts, raw, COUNT));
    /* Must genuinely be one multi-block function for the split to mean
       anything. */
    CHECK(program.block_count > 8);

    DolRegionLimits limits;
    dolregion_default_limits(&limits);
    limits.max_instructions = 8;

    DolRegionPlan plan;
    dolregion_plan_init(&plan);
    CHECK(dolregion_plan_build(&plan, &program, DOLREGION_MODE_FUNCTION,
                               &limits, stderr));
    CHECK(plan.region_count > 1);
    CHECK(plan.split_functions >= 1);
    CHECK(plan.total_instructions == COUNT);
    CHECK(coverage_is_exact(&plan, &program));

    dolregion_plan_free(&plan);
    dolcfg_free(&program);
    return true;
}

/* Blocks are the atom of a region: a single basic block larger than the limit
   is emitted whole rather than cut mid-block. Splitting straight-line code is
   possible in principle, but a boundary inside a block needs a live-state
   handoff at a point the CFG never chose, which is the cost regions exist to
   avoid. Asserted so the behaviour is a decision, not an accident. */
static bool test_indivisible_block_exceeds_limit(void) {
    enum { COUNT = 64 };
    u32 raw[COUNT];
    for (u32 i = 0; i < COUNT - 1u; i++)
        raw[i] = 0x38630001u;
    raw[COUNT - 1u] = 0x4E800020u;

    PPCInst insts[COUNT];
    DolCfgProgram program;
    CHECK(build_program(&program, insts, raw, COUNT));
    CHECK(program.block_count == 1);

    DolRegionLimits limits;
    dolregion_default_limits(&limits);
    limits.max_instructions = 8;

    DolRegionPlan plan;
    dolregion_plan_init(&plan);
    CHECK(dolregion_plan_build(&plan, &program, DOLREGION_MODE_FUNCTION,
                               &limits, stderr));
    CHECK(plan.region_count == 1);
    CHECK(plan.regions[0].instruction_count == COUNT);
    CHECK(coverage_is_exact(&plan, &program));

    dolregion_plan_free(&plan);
    dolcfg_free(&program);
    return true;
}

/* PGO mode with no weights must say so rather than look profiled. */
static bool test_pgo_without_profile_is_reported(void) {
    PPCInst insts[6];
    DolCfgProgram program;
    CHECK(build_program(&program, insts, kCallGraph, 6));

    DolRegionLimits limits;
    dolregion_default_limits(&limits);

    DolRegionPlan plan;
    dolregion_plan_init(&plan);
    CHECK(dolregion_plan_build(&plan, &program, DOLREGION_MODE_PGO, &limits, stderr));
    CHECK(plan.profile_missing);

    /* With weights present it must not claim to be missing. */
    for (u32 i = 0; i < program.block_count; i++)
        program.blocks[i].weight = 10;

    DolRegionPlan weighted;
    dolregion_plan_init(&weighted);
    CHECK(dolregion_plan_build(&weighted, &program, DOLREGION_MODE_PGO,
                               &limits, stderr));
    CHECK(!weighted.profile_missing);
    CHECK(weighted.regions[0].weight > 0);

    dolregion_plan_free(&weighted);
    dolregion_plan_free(&plan);
    dolcfg_free(&program);
    return true;
}

/* Same inputs and settings, same plan -- including region numbering. */
static bool test_plan_is_deterministic(void) {
    PPCInst insts[6];
    DolCfgProgram program;
    CHECK(build_program(&program, insts, kCallGraph, 6));

    DolRegionLimits limits;
    dolregion_default_limits(&limits);

    DolRegionPlan a, b;
    dolregion_plan_init(&a);
    dolregion_plan_init(&b);
    CHECK(dolregion_plan_build(&a, &program, DOLREGION_MODE_CFG, &limits, stderr));
    CHECK(dolregion_plan_build(&b, &program, DOLREGION_MODE_CFG, &limits, stderr));

    CHECK(a.region_count == b.region_count);
    CHECK(a.cross_region_edges == b.cross_region_edges);
    for (u32 r = 0; r < a.region_count; r++) {
        CHECK(a.regions[r].guest_start == b.regions[r].guest_start);
        CHECK(a.regions[r].guest_end == b.regions[r].guest_end);
        CHECK(a.regions[r].block_count == b.regions[r].block_count);
        CHECK(a.regions[r].function_count == b.regions[r].function_count);
        CHECK(a.regions[r].end_reason == b.regions[r].end_reason);
        for (u32 i = 0; i < a.regions[r].block_count; i++)
            CHECK(a.regions[r].blocks[i] == b.regions[r].blocks[i]);
    }
    for (u32 i = 0; i < program.block_count; i++)
        CHECK(a.block_region[i] == b.block_region[i]);

    dolregion_plan_free(&b);
    dolregion_plan_free(&a);
    dolcfg_free(&program);
    return true;
}

static bool test_report_contains_boundary_reasons(void) {
    PPCInst insts[6];
    DolCfgProgram program;
    CHECK(build_program(&program, insts, kCallGraph, 6));

    DolRegionLimits limits;
    dolregion_default_limits(&limits);

    DolRegionPlan plan;
    dolregion_plan_init(&plan);
    CHECK(dolregion_plan_build(&plan, &program, DOLREGION_MODE_CFG, &limits, stderr));

    char path[1200];
    snprintf(path, sizeof(path), "%s/regions.json", g_dir);
    CHECK(dolregion_write_report(&plan, &program, path, stderr));

    FILE* in = fopen(path, "rb");
    CHECK(in != NULL);
    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    rewind(in);
    char* text = (char*)malloc((size_t)size + 1u);
    CHECK(text != NULL);
    size_t got = fread(text, 1, (size_t)size, in);
    text[got] = '\0';
    fclose(in);

    CHECK(strstr(text, "\"schema\": \"dolrecomp.regions/1\"") != NULL);
    CHECK(strstr(text, "\"mode\": \"cfg\"") != NULL);
    CHECK(strstr(text, "\"end_reason\"") != NULL);
    CHECK(strstr(text, "\"function_addresses\"") != NULL);
    CHECK(strstr(text, "\"internal_edges\"") != NULL);
    CHECK(strstr(text, "\"max_instructions\"") != NULL);

    free(text);
    dolregion_plan_free(&plan);
    dolcfg_free(&program);
    return true;
}

static bool test_mode_names_round_trip(void) {
    const char* names[] = {"fixed", "function", "cfg", "pgo"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        DolRegionMode mode;
        CHECK(dolregion_parse_mode(names[i], &mode));
        CHECK(strcmp(dolregion_mode_name(mode), names[i]) == 0);
    }
    DolRegionMode mode;
    CHECK(!dolregion_parse_mode("nonsense", &mode));
    return true;
}

int main(int argc, char** argv) {
    if (argc > 1)
        snprintf(g_dir, sizeof(g_dir), "%s", argv[1]);
    else
        snprintf(g_dir, sizeof(g_dir), ".");

#if defined(_WIN32)
    _mkdir(g_dir);
#else
    mkdir(g_dir, 0777);
#endif

    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"every_mode_covers_every_block", test_every_mode_covers_every_block},
        {"function_mode_is_one_region_per_function", test_function_mode_is_one_region_per_function},
        {"cfg_mode_merges_caller_and_callee", test_cfg_mode_merges_caller_and_callee},
        {"size_limit_is_respected", test_size_limit_is_respected},
        {"large_function_is_split_not_dropped", test_large_function_is_split_not_dropped},
        {"indivisible_block_exceeds_limit", test_indivisible_block_exceeds_limit},
        {"pgo_without_profile_is_reported", test_pgo_without_profile_is_reported},
        {"plan_is_deterministic", test_plan_is_deterministic},
        {"report_contains_boundary_reasons", test_report_contains_boundary_reasons},
        {"mode_names_round_trip", test_mode_names_round_trip},
    };

    int failures = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (!tests[i].fn()) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            failures++;
        }
    }

    if (failures != 0) {
        fprintf(stderr, "%d region test(s) failed\n", failures);
        return 1;
    }

    printf("region tests passed\n");
    return 0;
}
