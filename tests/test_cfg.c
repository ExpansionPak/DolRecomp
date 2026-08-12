#include "analysis/cfg.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #x); return false; } } while (0)

#define BASE 0x80001000u

static void decode_all(PPCInst* out, const u32* raw, u32 count, u32 base) {
    for (u32 i = 0; i < count; i++)
        out[i] = ppc_decode(raw[i], base + i * 4u);
}

/* addi r3,r0,0 / addi r3,r3,1 / cmpwi r3,10 / blt -8 / blr */
static const u32 kLoop[] = {
    0x38600000u,
    0x38630001u,
    0x2C03000Au,
    0x4180FFF8u,
    0x4E800020u,
};

static bool test_loop_blocks_and_header(void) {
    PPCInst insts[5];
    decode_all(insts, kLoop, 5, BASE);

    DolCfgProgram program;
    dolcfg_init(&program);
    CHECK(dolcfg_add_section(&program, insts, 5, BASE, "text"));
    CHECK(dolcfg_build(&program, stderr));

    /* Leaders: BASE (section start), BASE+4 (branch target), BASE+16 (after
       the conditional branch). */
    CHECK(program.block_count == 3);

    u32 head = dolcfg_block_starting_at(&program, BASE);
    u32 body = dolcfg_block_starting_at(&program, BASE + 4u);
    u32 tail = dolcfg_block_starting_at(&program, BASE + 16u);
    CHECK(head != DOLCFG_NO_BLOCK && body != DOLCFG_NO_BLOCK &&
          tail != DOLCFG_NO_BLOCK);

    CHECK(program.blocks[head].terminator == DOLCFG_TERM_FALLTHROUGH);
    CHECK(program.blocks[head].successor_count == 1);
    CHECK(program.blocks[head].successors[0] == body);

    /* The conditional branch keeps both edges: taken back to the body, and
       fallthrough to the return. */
    CHECK(program.blocks[body].terminator == DOLCFG_TERM_COND_BRANCH);
    CHECK(program.blocks[body].successor_count == 2);
    CHECK(program.blocks[body].successors[0] == body);
    CHECK(program.blocks[body].successors[1] == tail);
    CHECK(program.blocks[body].flags & DOLCFG_BLOCK_LOOP_HEADER);
    CHECK(program.blocks[body].loop_depth == 1);

    CHECK(program.blocks[tail].terminator == DOLCFG_TERM_RETURN);
    CHECK(program.blocks[tail].successor_count == 0);
    CHECK(program.blocks[tail].loop_depth == 0);
    CHECK(program.loop_count == 1);

    dolcfg_free(&program);
    return true;
}

/* A conditional branch that BO marks as always-taken has no fallthrough. */
static bool test_unconditional_bc_drops_fallthrough(void) {
    /* bc 20,0,+8  -> BO=20 (1z1zz), always taken */
    const u32 raw[] = {
        0x42800008u | 0x02000000u, /* placeholder, replaced below */
        0x60000000u,
        0x4E800020u,
    };
    u32 fixed[3];
    memcpy(fixed, raw, sizeof(fixed));
    /* bc with BO=20, BI=0, BD=+8, AA=0, LK=0 */
    fixed[0] = 0x40000000u | (20u << 21) | (0u << 16) | (8u & 0xFFFCu);

    PPCInst insts[3];
    decode_all(insts, fixed, 3, BASE);
    CHECK(insts[0].op == PPC_OP_BC);
    CHECK(insts[0].bo == 20);

    DolCfgProgram program;
    dolcfg_init(&program);
    CHECK(dolcfg_add_section(&program, insts, 3, BASE, "text"));
    CHECK(dolcfg_build(&program, stderr));

    u32 head = dolcfg_block_starting_at(&program, BASE);
    CHECK(head != DOLCFG_NO_BLOCK);
    CHECK(program.blocks[head].terminator == DOLCFG_TERM_BRANCH);
    CHECK(program.blocks[head].successor_count == 1);
    CHECK(program.blocks[head].successor_addresses[0] == BASE + 8u);

    dolcfg_free(&program);
    return true;
}

/* bl records the callee as a call target and continues at the return point;
   the callee becomes an inferred function entry. */
static bool test_call_creates_function(void) {
    const u32 raw[] = {
        0x48000011u, /* bl +0x10 -> BASE+0x10 */
        0x4E800020u, /* blr */
        0x60000000u,
        0x60000000u,
        0x38600001u, /* BASE+0x10: addi r3,r0,1 */
        0x4E800020u, /* blr */
    };
    PPCInst insts[6];
    decode_all(insts, raw, 6, BASE);
    CHECK(insts[0].op == PPC_OP_B && insts[0].lk);
    CHECK(insts[0].branch_target == BASE + 0x10u);

    DolCfgProgram program;
    dolcfg_init(&program);
    CHECK(dolcfg_add_section(&program, insts, 6, BASE, "text"));
    CHECK(dolcfg_build(&program, stderr));

    u32 caller = dolcfg_block_starting_at(&program, BASE);
    CHECK(caller != DOLCFG_NO_BLOCK);
    CHECK(program.blocks[caller].terminator == DOLCFG_TERM_CALL);
    CHECK(program.blocks[caller].call_target == BASE + 0x10u);
    /* The successor is the return point, never the callee. */
    CHECK(program.blocks[caller].successor_count == 1);
    CHECK(program.blocks[caller].successor_addresses[0] == BASE + 4u);

    u32 callee = dolcfg_block_starting_at(&program, BASE + 0x10u);
    CHECK(callee != DOLCFG_NO_BLOCK);
    CHECK(program.blocks[callee].flags & DOLCFG_BLOCK_FUNCTION_ENTRY);

    bool found = false;
    for (u32 i = 0; i < program.function_count; i++) {
        if (program.functions[i].entry_address == BASE + 0x10u) {
            found = true;
            CHECK(program.functions[i].flags & DOLCFG_FUNC_FROM_CALL);
        }
    }
    CHECK(found);

    dolcfg_free(&program);
    return true;
}

/* A plain b into another function's entry is a tail call, not a branch. */
static bool test_tail_call_reclassification(void) {
    const u32 raw[] = {
        0x48000008u, /* BASE+0: b +8 -> BASE+8 */
        0x60000000u,
        0x38600001u, /* BASE+8 */
        0x4E800020u,
    };
    PPCInst insts[4];
    decode_all(insts, raw, 4, BASE);

    DolCfgProgram program;
    dolcfg_init(&program);
    CHECK(dolcfg_add_section(&program, insts, 4, BASE, "text"));
    /* Declared, not inferred: no bl points at it. */
    CHECK(dolcfg_add_known_function(&program, BASE + 8u, "target_fn", 0));
    CHECK(dolcfg_add_known_function(&program, BASE, "caller_fn", 0));
    CHECK(dolcfg_build(&program, stderr));

    u32 head = dolcfg_block_starting_at(&program, BASE);
    CHECK(head != DOLCFG_NO_BLOCK);
    CHECK(program.blocks[head].terminator == DOLCFG_TERM_TAIL_CALL);
    CHECK(program.blocks[head].call_target == BASE + 8u);

    bool named = false;
    for (u32 i = 0; i < program.function_count; i++) {
        if (program.functions[i].entry_address == BASE + 8u) {
            named = strcmp(program.functions[i].name, "target_fn") == 0;
            CHECK(program.functions[i].flags & DOLCFG_FUNC_FROM_SYMBOL);
        }
    }
    CHECK(named);

    dolcfg_free(&program);
    return true;
}

/* bcctr is an unresolved indirect transfer. It must not acquire a guessed
   successor -- Phase 4 attaches target sets, and until then a region ends. */
static bool test_indirect_has_no_guessed_target(void) {
    const u32 raw[] = {
        0x38600001u,
        0x4E800420u, /* bctr */
        0x60000000u,
    };
    PPCInst insts[3];
    decode_all(insts, raw, 3, BASE);
    CHECK(insts[1].op == PPC_OP_BCCTR);

    DolCfgProgram program;
    dolcfg_init(&program);
    CHECK(dolcfg_add_section(&program, insts, 3, BASE, "text"));
    CHECK(dolcfg_build(&program, stderr));

    u32 head = dolcfg_block_starting_at(&program, BASE);
    CHECK(head != DOLCFG_NO_BLOCK);
    CHECK(program.blocks[head].terminator == DOLCFG_TERM_INDIRECT);
    /* Unconditional, no link: nothing follows it in the model. */
    CHECK(program.blocks[head].successor_count == 0);
    CHECK(program.indirect_site_count == 1);

    dolcfg_free(&program);
    return true;
}

/* Embedded data must never become a block. */
static bool test_embedded_data_is_not_code(void) {
    const u32 raw[] = {
        0x38600001u,
        0x4E800020u, /* blr */
        0xDEADBEEFu, /* data */
        0xCAFEBABEu, /* data */
        0x38600002u,
        0x4E800020u,
    };
    PPCInst insts[6];
    decode_all(insts, raw, 6, BASE);
    insts[2].embedded_data = true;
    insts[3].embedded_data = true;

    DolCfgProgram program;
    dolcfg_init(&program);
    CHECK(dolcfg_add_section(&program, insts, 6, BASE, "text"));
    CHECK(dolcfg_build(&program, stderr));

    CHECK(dolcfg_block_at(&program, BASE + 8u) == DOLCFG_NO_BLOCK);
    CHECK(dolcfg_block_at(&program, BASE + 12u) == DOLCFG_NO_BLOCK);
    /* Code resumes after the data run. */
    CHECK(dolcfg_block_starting_at(&program, BASE + 16u) != DOLCFG_NO_BLOCK);

    u32 covered = 0;
    for (u32 i = 0; i < program.block_count; i++)
        covered += program.blocks[i].instruction_count;
    CHECK(covered == 4);

    dolcfg_free(&program);
    return true;
}

static bool test_smc_ranges_flag_blocks(void) {
    PPCInst insts[5];
    decode_all(insts, kLoop, 5, BASE);

    DolCfgProgram program;
    dolcfg_init(&program);
    CHECK(dolcfg_add_section(&program, insts, 5, BASE, "text"));
    CHECK(dolcfg_add_smc_range(&program, BASE + 16u, BASE + 16u));
    CHECK(dolcfg_build(&program, stderr));

    u32 tail = dolcfg_block_starting_at(&program, BASE + 16u);
    u32 head = dolcfg_block_starting_at(&program, BASE);
    CHECK(tail != DOLCFG_NO_BLOCK && head != DOLCFG_NO_BLOCK);
    CHECK(program.blocks[tail].flags & DOLCFG_BLOCK_SMC_SUSPECT);
    CHECK(!(program.blocks[head].flags & DOLCFG_BLOCK_SMC_SUSPECT));

    dolcfg_free(&program);
    return true;
}

/* Region planning has to be reproducible from the same inputs, which starts
   with the CFG numbering being reproducible. */
static bool test_build_is_deterministic(void) {
    const u32 raw[] = {
        0x48000011u, 0x4E800020u, 0x60000000u, 0x60000000u,
        0x38600001u, 0x2C03000Au, 0x4180FFFCu, 0x4E800020u,
    };
    PPCInst insts[8];
    decode_all(insts, raw, 8, BASE);

    DolCfgProgram a, b;
    dolcfg_init(&a);
    dolcfg_init(&b);
    CHECK(dolcfg_add_section(&a, insts, 8, BASE, "text"));
    CHECK(dolcfg_add_section(&b, insts, 8, BASE, "text"));
    CHECK(dolcfg_build(&a, stderr));
    CHECK(dolcfg_build(&b, stderr));

    CHECK(a.block_count == b.block_count);
    CHECK(a.function_count == b.function_count);
    CHECK(a.scc_count == b.scc_count);
    CHECK(a.loop_count == b.loop_count);
    for (u32 i = 0; i < a.block_count; i++) {
        CHECK(a.blocks[i].start == b.blocks[i].start);
        CHECK(a.blocks[i].end == b.blocks[i].end);
        CHECK(a.blocks[i].terminator == b.blocks[i].terminator);
        CHECK(a.blocks[i].successor_count == b.blocks[i].successor_count);
        CHECK(a.blocks[i].successors[0] == b.blocks[i].successors[0]);
        CHECK(a.blocks[i].successors[1] == b.blocks[i].successors[1]);
        CHECK(a.blocks[i].function == b.blocks[i].function);
        CHECK(a.blocks[i].scc == b.blocks[i].scc);
        CHECK(a.blocks[i].flags == b.blocks[i].flags);
    }

    dolcfg_free(&a);
    dolcfg_free(&b);
    return true;
}

/* Every decoded, non-data instruction must land in exactly one block. */
static bool test_blocks_cover_all_code_exactly_once(void) {
    const u32 raw[] = {
        0x38600000u, 0x2C03000Au, 0x4180FFF8u, 0x48000011u,
        0x4E800020u, 0x60000000u, 0x38600001u, 0x4E800020u,
    };
    PPCInst insts[8];
    decode_all(insts, raw, 8, BASE);

    DolCfgProgram program;
    dolcfg_init(&program);
    CHECK(dolcfg_add_section(&program, insts, 8, BASE, "text"));
    CHECK(dolcfg_build(&program, stderr));

    u8 seen[8];
    memset(seen, 0, sizeof(seen));
    for (u32 i = 0; i < program.block_count; i++) {
        const DolCfgBlock* block = &program.blocks[i];
        CHECK(block->end > block->start);
        for (u32 a = block->start; a < block->end; a += 4u) {
            u32 slot = (a - BASE) / 4u;
            CHECK(slot < 8u);
            CHECK(seen[slot] == 0);
            seen[slot] = 1;
        }
    }
    for (u32 i = 0; i < 8; i++)
        CHECK(seen[i] == 1);

    dolcfg_free(&program);
    return true;
}

int main(void) {
    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"loop_blocks_and_header", test_loop_blocks_and_header},
        {"unconditional_bc_drops_fallthrough", test_unconditional_bc_drops_fallthrough},
        {"call_creates_function", test_call_creates_function},
        {"tail_call_reclassification", test_tail_call_reclassification},
        {"indirect_has_no_guessed_target", test_indirect_has_no_guessed_target},
        {"embedded_data_is_not_code", test_embedded_data_is_not_code},
        {"smc_ranges_flag_blocks", test_smc_ranges_flag_blocks},
        {"build_is_deterministic", test_build_is_deterministic},
        {"blocks_cover_all_code_exactly_once", test_blocks_cover_all_code_exactly_once},
    };

    int failures = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (!tests[i].fn()) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            failures++;
        }
    }

    if (failures != 0) {
        fprintf(stderr, "%d cfg test(s) failed\n", failures);
        return 1;
    }

    printf("cfg tests passed\n");
    return 0;
}
