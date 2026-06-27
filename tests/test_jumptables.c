#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/common/types.h"
#include "../src/cpu/cpu.h"
#include "../src/frontend/decoder.h"
#include "../src/backend/emitter.h"

//looks if its properly dealing with jts
//current stats is pass in all, should be working
//not an true indicative of it tho, im not sure about edge cases, further testing is probably needed here
#define BASE 0x80000000u
#define TABLE_ADDR 0x80002000u

static int pass_count = 0;
static int fail_count = 0;

static void check(int condition, const char* name) {
    printf("JUMPTABLE,%s,%s\n", name, condition ? "PASS" : "FAIL");
    if (condition)
        pass_count++;
    else
        fail_count++;
}

static u32 dolrecomp_index_scale(u32 value);

static u32 dform(u32 op, u32 rt, u32 ra, u16 imm) {
    return (op << 26) | (rt << 21) | (ra << 16) | imm;
}

static u32 rlwinm(u32 rs, u32 ra, u32 sh, u32 mb, u32 me) {
    return (21u << 26) | (rs << 21) | (ra << 16) | (sh << 11) | (mb << 6) | (me << 1);
}

static u32 lwzx(u32 rt, u32 ra, u32 rb) {
    return (31u << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (23u << 1);
}

static u32 mtspr(u32 spr, u32 rs) {
    u32 spr_field = ((spr & 0x1Fu) << 16) | ((spr & 0x3E0u) << 6);
    return (31u << 26) | (rs << 21) | spr_field | (467u << 1);
}

static u32 bctr(void) {
    return (19u << 26) | (20u << 21) | (528u << 1);
}

static u32 blr(void) {
    return (19u << 26) | (20u << 21) | (16u << 1);
}

static u32 branch(u32 from, u32 to) {
    return (18u << 26) | ((to - from) & 0x03FFFFFCu);
}

enum { CASE_COUNT = 4, INST_COUNT = 14 };

static const u32 case_targets[CASE_COUNT] = {
    BASE + 0x18u, BASE + 0x20u, BASE + 0x28u, BASE + 0x30u
};

static void build_program(u32 raw[INST_COUNT]) {
    raw[0] = rlwinm(3, 0, 2, 0, 29); // r0 = r3 << 2
    raw[1] = dform(15, 11, 0, 0x8000); // lis  r11, 0x8000
    raw[2] = dform(14, 11, 11, 0x2000); // addi r11, r11, 0x2000 -> TABLE_ADDR
    raw[3] = lwzx(12, 11, 0); // r12 = table[index]
    raw[4] = mtspr(9, 12); // mtctr r12
    raw[5] = bctr(); // bctr
    raw[6] = dform(14, 3, 0, 0x00AA); // case0: li r3, 0xAA
    raw[7] = branch(BASE + 0x1Cu, BASE + 0x34u);
    raw[8] = dform(14, 3, 0, 0x00BB); // case1: li r3, 0xBB
    raw[9] = branch(BASE + 0x24u, BASE + 0x34u);
    raw[10] = dform(14, 3, 0, 0x00CC); // case2: li r3, 0xCC
    raw[11] = branch(BASE + 0x2Cu, BASE + 0x34u);
    raw[12] = dform(14, 3, 0, 0x00DD); // case3: li r3, 0xDD
    raw[13] = blr(); // end of swc
}

static char* emit_program_to_string(const u32 raw[INST_COUNT]) {
    PPCInst insts[INST_COUNT];
    for (u32 i = 0; i < INST_COUNT; i++)
        insts[i] = ppc_decode(raw[i], BASE + i * 4u);

    FILE* f = tmpfile();
    if (!f)
        return NULL;

    emit_function(f, insts, INST_COUNT, BASE);
    fflush(f);

    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char* buf = (char*)malloc((size_t)size + 1u);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

static void test_decode_building_blocks(const u32 raw[INST_COUNT]) {
    PPCInst rl = ppc_decode(raw[0], BASE);
    check(rl.op == PPC_OP_RLWINM && rl.rA == 0 && rl.rS == 3 && rl.sh == 2,
          "rlwinm index scaling decodes");

    PPCInst lx = ppc_decode(raw[3], BASE + 0x0Cu);
    check(lx.op == PPC_OP_LWZX && lx.rD == 12 && lx.rA == 11 && lx.rB == 0,
          "lwzx table load decodes");

    PPCInst mt = ppc_decode(raw[4], BASE + 0x10u);
    check(mt.op == PPC_OP_MTSPR && mt.spr == 9 && mt.rS == 12,
          "mtctr decodes (spr 9)");

    PPCInst bc = ppc_decode(raw[5], BASE + 0x14u);
    check(bc.op == PPC_OP_BCCTR && bc.bo == 20 && bc.lk == 0,
          "bctr decodes as computed branch");
}

static void test_codegen_supports_jumptable(const char* code) {
    check(strstr(code, "ppc_fallback_instruction") == NULL,
          "no opcode falls back to interpreter");
    check(strstr(code, "ctx->ctr & ~3u") != NULL,
          "bctr lowers to a computed target from CTR");
    check(strstr(code, "ctx->pc = target;") != NULL &&
          strstr(code, "return;") != NULL,
          "computed branch hands the target to the dispatcher");
    check(strstr(code, "mem_read32(ctx, ea)") != NULL,
          "lwzx table load lowers to a memory read");
    check(strstr(code, "ctx->ctr = ctx->gpr[12];") != NULL,
          "mtctr loads the table entry into CTR");

    char needle[64];
    int all_cases_reentrant = 1;
    for (u32 i = 0; i < CASE_COUNT; i++) {
        snprintf(needle, sizeof(needle), "case 0x%08Xu: goto label_%08X;",
                 case_targets[i], case_targets[i]);
        if (!strstr(code, needle))
            all_cases_reentrant = 0;
    }
    check(all_cases_reentrant,
          "every case target is a dispatch re-entry point");
}

static void test_table_indexing_semantics(void) {
    CPUState cpu;
    if (!cpu_init(&cpu)) {
        check(0, "cpu_init for table semantics");
        return;
    }

    for (u32 i = 0; i < CASE_COUNT; i++)
        mem_write32(&cpu, TABLE_ADDR + i * 4u, case_targets[i]);

    int indexing_ok = 1;
    for (u32 index = 0; index < CASE_COUNT; index++) {
        cpu.gpr[3] = index;
        u32 r0 = dolrecomp_index_scale(cpu.gpr[3]);
        u32 r11 = TABLE_ADDR;
        u32 ea = r11 + r0; // lwzx effective address
        u32 target = mem_read32(&cpu, ea); // r12 = table[index]
        cpu.ctr = target;
        u32 pc = cpu.ctr & ~3u; // bctr resolves the target
        if (pc != case_targets[index])
            indexing_ok = 0;
    }
    check(indexing_ok, "computed index selects the right case target");

    cpu_free(&cpu);
}

static u32 dolrecomp_index_scale(u32 value) {
    return (value << 2) & 0xFFFFFFFCu;
}

int main(void) {
    u32 raw[INST_COUNT];
    build_program(raw);

    test_decode_building_blocks(raw);

    char* code = emit_program_to_string(raw);
    if (!code) {
        check(0, "emit jump-table function");
        printf("JUMPTABLE,total,%d passed %d failed\n", pass_count, fail_count);
        return 1;
    }

    test_codegen_supports_jumptable(code);
    free(code);

    test_table_indexing_semantics();

    printf("JUMPTABLE,total,%d passed %d failed\n", pass_count, fail_count);
    return fail_count == 0 ? 0 : 1;
}
