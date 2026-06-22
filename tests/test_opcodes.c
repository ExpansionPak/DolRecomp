#include <stdio.h>
#include <string.h>

#include "../src/core/types.h"
#include "../src/frontend/decoder.h"

#define BASE 0x80003000u

static int tests_run = 0;
static int tests_passed = 0;

#define CHECK(cond, fmt, ...) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("  FAIL: " fmt "\n", ##__VA_ARGS__); \
        return 0; \
    } \
    tests_passed++; \
} while (0)

typedef struct {
    u32 raw;
    PPCOpcode op;
    const char* name;
} OpcodeDecode;

static const OpcodeDecode opcode_cases[] = {
    { 0x1C64FFF9, PPC_OP_MULLI, "mulli" },
    { 0x20850001, PPC_OP_SUBFIC, "subfic" },
    { 0x38610010, PPC_OP_ADDI, "addi" },
    { 0x3084FFFF, PPC_OP_ADDIC, "addic" },
    { 0x34A5FFFF, PPC_OP_ADDIC_DOT, "addic." },
    { 0x3CA01234, PPC_OP_ADDIS, "addis" },
    { 0x2C03FFFF, PPC_OP_CMPI, "cmpi" },
    { 0x28038000, PPC_OP_CMPLI, "cmpli" },
    { 0x6064FF00, PPC_OP_ORI, "ori" },
    { 0x64851234, PPC_OP_ORIS, "oris" },
    { 0x68A6FFFF, PPC_OP_XORI, "xori" },
    { 0x6CC78000, PPC_OP_XORIS, "xoris" },
    { 0x70E800FF, PPC_OP_ANDI, "andi." },
    { 0x74E900FF, PPC_OP_ANDIS, "andis." },
    { 0x80610000, PPC_OP_LWZ, "lwz" },
    { 0x84810004, PPC_OP_LWZU, "lwzu" },
    { 0x88A10008, PPC_OP_LBZ, "lbz" },
    { 0x8CC1000C, PPC_OP_LBZU, "lbzu" },
    { 0xA0E10010, PPC_OP_LHZ, "lhz" },
    { 0xA5010014, PPC_OP_LHZU, "lhzu" },
    { 0xA921FFFC, PPC_OP_LHA, "lha" },
    { 0xAD410018, PPC_OP_LHAU, "lhau" },
    { 0x9061001C, PPC_OP_STW, "stw" },
    { 0x94810020, PPC_OP_STWU, "stwu" },
    { 0x98A10024, PPC_OP_STB, "stb" },
    { 0x9CC10028, PPC_OP_STBU, "stbu" },
    { 0xB0E1002C, PPC_OP_STH, "sth" },
    { 0xB5010030, PPC_OP_STHU, "sthu" },
    { 0xBA810034, PPC_OP_LMW, "lmw" },
    { 0xBE810064, PPC_OP_STMW, "stmw" },
    { 0x480000C8, PPC_OP_B, "b" },
    { 0x418200C4, PPC_OP_BC, "bc" },
    { 0x4E800020, PPC_OP_BCLR, "bclr" },
    { 0x4E800420, PPC_OP_BCCTR, "bcctr" },
    { 0x4C432202, PPC_OP_CRAND, "crand" },
    { 0x4C432102, PPC_OP_CRANDC, "crandc" },
    { 0x4C432242, PPC_OP_CREQV, "creqv" },
    { 0x4C4321C2, PPC_OP_CRNAND, "crnand" },
    { 0x4C432042, PPC_OP_CRNOR, "crnor" },
    { 0x4C432382, PPC_OP_CROR, "cror" },
    { 0x4C432342, PPC_OP_CRORC, "crorc" },
    { 0x4C432182, PPC_OP_CRXOR, "crxor" },
    { 0x4D0C0000, PPC_OP_MCRF, "mcrf" },
    { 0x7D400026, PPC_OP_MFCR, "mfcr" },
    { 0x7D4FF120, PPC_OP_MTCRF, "mtcrf" },
    { 0x7D4802A6, PPC_OP_MFSPR, "mfspr" },
    { 0x7D4803A6, PPC_OP_MTSPR, "mtspr" },
    { 0x7C832000, PPC_OP_CMP, "cmp" },
    { 0x7D032040, PPC_OP_CMPL, "cmpl" },
    { 0x7D4B6214, PPC_OP_ADD, "add" },
    { 0x7D6C6814, PPC_OP_ADDC, "addc" },
    { 0x7D8D7114, PPC_OP_ADDE, "adde" },
    { 0x7DAE0194, PPC_OP_ADDZE, "addze" },
    { 0x7DCF8050, PPC_OP_SUBF, "subf" },
    { 0x7DF08810, PPC_OP_SUBFC, "subfc" },
    { 0x7E119110, PPC_OP_SUBFE, "subfe" },
    { 0x7E320190, PPC_OP_SUBFZE, "subfze" },
    { 0x7E5300D0, PPC_OP_NEG, "neg" },
    { 0x7E93A838, PPC_OP_AND, "and" },
    { 0x7EB4B078, PPC_OP_ANDC, "andc" },
    { 0x7ED5BB78, PPC_OP_OR, "or" },
    { 0x7EF6C338, PPC_OP_ORC, "orc" },
    { 0x7F17CA78, PPC_OP_XOR, "xor" },
    { 0x7F38D3B8, PPC_OP_NAND, "nand" },
    { 0x7F59D8F8, PPC_OP_NOR, "nor" },
    { 0x7F7AE238, PPC_OP_EQV, "eqv" },
    { 0x7F9B0034, PPC_OP_CNTLZW, "cntlzw" },
    { 0x7FBC0774, PPC_OP_EXTSB, "extsb" },
    { 0x7FDD0734, PPC_OP_EXTSH, "extsh" },
    { 0x7FFE1830, PPC_OP_SLW, "slw" },
    { 0x7C7F2430, PPC_OP_SRW, "srw" },
    { 0x7C832E30, PPC_OP_SRAW, "sraw" },
    { 0x7CA43E70, PPC_OP_SRAWI, "srawi" },
    { 0x54C52A2E, PPC_OP_RLWINM, "rlwinm" },
    { 0x5CE64136, PPC_OP_RLWNM, "rlwnm" },
    { 0x5107421E, PPC_OP_RLWIMI, "rlwimi" },
    { 0x7C64282E, PPC_OP_LWZX, "lwzx" },
    { 0x7CC4286E, PPC_OP_LWZUX, "lwzux" },
    { 0x7CE428AE, PPC_OP_LBZX, "lbzx" },
    { 0x7D0428EE, PPC_OP_LBZUX, "lbzux" },
    { 0x7D242A2E, PPC_OP_LHZX, "lhzx" },
    { 0x7D442A6E, PPC_OP_LHZUX, "lhzux" },
    { 0x7D642AAE, PPC_OP_LHAX, "lhax" },
    { 0x7D842AEE, PPC_OP_LHAUX, "lhaux" },
    { 0x7C642C2C, PPC_OP_LWBRX, "lwbrx" },
    { 0x7CC7462C, PPC_OP_LHBRX, "lhbrx" },
    { 0x7C64292E, PPC_OP_STWX, "stwx" },
    { 0x7CC4296E, PPC_OP_STWUX, "stwux" },
    { 0x7CE429AE, PPC_OP_STBX, "stbx" },
    { 0x7D0429EE, PPC_OP_STBUX, "stbux" },
    { 0x7D242B2E, PPC_OP_STHX, "sthx" },
    { 0x7D442B6E, PPC_OP_STHUX, "sthux" },
    { 0x7D2A5D2C, PPC_OP_STWBRX, "stwbrx" },
    { 0x7D8D772C, PPC_OP_STHBRX, "sthbrx" },
    { 0x7C0F87EC, PPC_OP_DCBZ, "dcbz" },
    { 0xC0240000, PPC_OP_LFS, "lfs" },
    { 0xC4440004, PPC_OP_LFSU, "lfsu" },
    { 0xC8640008, PPC_OP_LFD, "lfd" },
    { 0xCC840010, PPC_OP_LFDU, "lfdu" },
    { 0xD0A40014, PPC_OP_STFS, "stfs" },
    { 0xD4C40018, PPC_OP_STFSU, "stfsu" },
    { 0xD8E40020, PPC_OP_STFD, "stfd" },
    { 0xDD040028, PPC_OP_STFDU, "stfdu" },
    { 0x7D242C2E, PPC_OP_LFSX, "lfsx" },
    { 0x7D442C6E, PPC_OP_LFSUX, "lfsux" },
    { 0x7D642CAE, PPC_OP_LFDX, "lfdx" },
    { 0x7D842CEE, PPC_OP_LFDUX, "lfdux" },
    { 0x7DA42D2E, PPC_OP_STFSX, "stfsx" },
    { 0x7DC42D6E, PPC_OP_STFSUX, "stfsux" },
    { 0x7DE42DAE, PPC_OP_STFDX, "stfdx" },
    { 0x7E042DEE, PPC_OP_STFDUX, "stfdux" },
    { 0xEC22182A, PPC_OP_FADDS, "fadds" },
    { 0xEC853028, PPC_OP_FSUBS, "fsubs" },
    { 0xECE80272, PPC_OP_FMULS, "fmuls" },
    { 0xED4B6024, PPC_OP_FDIVS, "fdivs" },
    { 0xFDAE782A, PPC_OP_FADD, "fadd" },
    { 0xFE119028, PPC_OP_FSUB, "fsub" },
    { 0xFE740572, PPC_OP_FMUL, "fmul" },
    { 0xFED7C024, PPC_OP_FDIV, "fdiv" },
    { 0xFF20D090, PPC_OP_FMR, "fmr" },
    { 0xFF60E050, PPC_OP_FNEG, "fneg" },
    { 0xFFA0F210, PPC_OP_FABS, "fabs" },
    { 0xFFE00110, PPC_OP_FNABS, "fnabs" },
    { 0xFC201018, PPC_OP_FRSP, "frsp" },
    { 0xFD032000, PPC_OP_FCMPU, "fcmpu" },
    { 0xFD853040, PPC_OP_FCMPO, "fcmpo" },
    { 0xE0240000, PPC_OP_PSQ_L, "psq_l" },
    { 0xE4640008, PPC_OP_PSQ_LU, "psq_lu" },
    { 0xF0A40010, PPC_OP_PSQ_ST, "psq_st" },
    { 0xF4E40018, PPC_OP_PSQ_STU, "psq_stu" },
    { 0x1124280C, PPC_OP_PSQ_LX, "psq_lx" },
    { 0x1164284C, PPC_OP_PSQ_LUX, "psq_lux" },
    { 0x11A4280E, PPC_OP_PSQ_STX, "psq_stx" },
    { 0x11E4284E, PPC_OP_PSQ_STUX, "psq_stux" },
    { 0x7C6429D6, PPC_OP_MULLW, "mullw" },
    { 0x7CC74096, PPC_OP_MULHW, "mulhw" },
    { 0x7D2A5816, PPC_OP_MULHWU, "mulhwu" },
    { 0x7D8D73D6, PPC_OP_DIVW, "divw" },
    { 0x7DF08B96, PPC_OP_DIVWU, "divwu" },
};

static u32 make_dform(u32 opcd, u32 rt, u32 ra, u16 imm) {
    return (opcd << 26) | (rt << 21) | (ra << 16) | imm;
}

static u32 make_iform(u32 opcd, s32 offset, bool aa, bool lk) {
    return (opcd << 26) | ((u32)offset & 0x03FFFFFCu) |
           (aa ? 2u : 0u) | (lk ? 1u : 0u);
}

static int test_sign_extend(void) {
    printf("  sign_extend\n");

    CHECK(sign_extend(0x0010, 16) == 16, "sign_extend +16");
    CHECK(sign_extend(0xFFF0, 16) == -16, "sign_extend -16");
    CHECK(sign_extend(0x7FFF, 16) == 32767, "sign_extend max positive");
    CHECK(sign_extend(0x8000, 16) == -32768, "sign_extend min negative");
    CHECK(sign_extend(0x00000004, 26) == 4, "sign_extend branch +4");
    CHECK(sign_extend(0x03FFFFFC, 26) == -4, "sign_extend branch -4");
    return 1;
}

static int test_current_opcode_count(void) {
    printf("  current opcode count is 139\n");
    CHECK(PPC_OP_COUNT - 1 == 139, "should expose 139 opcodes, got %d", PPC_OP_COUNT - 1);
    return 1;
}

static int test_current_opcode_decode_table(void) {
    int count = (int)(sizeof(opcode_cases) / sizeof(opcode_cases[0]));
    printf("  decode every opcode in the current 139-opcode set\n");

    CHECK(count == 139, "opcode table should have 139 entries, got %d", count);

    for (int i = 0; i < count; i++) {
        PPCInst inst = ppc_decode(opcode_cases[i].raw, BASE + (u32)(i * 4));
        CHECK(inst.op == opcode_cases[i].op, "%s decoded as %s",
              opcode_cases[i].name, ppc_op_name(inst.op));
    }

    return 1;
}

static int test_pseudoops_and_display(void) {
    char buf[64];

    printf("  pseudo-ops and disasm forms\n");

    PPCInst inst = ppc_decode(make_dform(14, 3, 0, 42), BASE);
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strcmp(buf, "li      r3, 42") == 0, "li disasm, got '%s'", buf);

    inst = ppc_decode(make_dform(15, 5, 0, 0x1234), BASE);
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strcmp(buf, "lis     r5, 4660") == 0, "lis disasm, got '%s'", buf);

    inst = ppc_decode(make_dform(24, 0, 0, 0), BASE);
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strcmp(buf, "nop") == 0, "nop disasm, got '%s'", buf);

    inst = ppc_decode(0x7C832000, BASE);
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strcmp(buf, "cmpw    cr1, r3, r4") == 0, "cmpw CR disasm, got '%s'", buf);

    inst = ppc_decode(0x4D0C0000, BASE);
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strcmp(buf, "mcrf    cr2, cr3") == 0, "mcrf disasm, got '%s'", buf);

    inst = ppc_decode(0x7D4FF120, BASE);
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strcmp(buf, "mtcr    r10") == 0, "mtcr disasm, got '%s'", buf);

    inst = ppc_decode(0x7D490120, BASE);
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strcmp(buf, "mtcrf   0x90, r10") == 0, "mtcrf disasm, got '%s'", buf);

    inst = ppc_decode(0x7C0087EC, BASE);
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strcmp(buf, "dcbz    0, r16") == 0, "dcbz zero-base disasm, got '%s'", buf);

    inst = ppc_decode(0xC0240000, BASE);
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strcmp(buf, "lfs     f1, 0(r4)") == 0, "lfs disasm, got '%s'", buf);

    inst = ppc_decode(0x7DE42DAE, BASE);
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strcmp(buf, "stfdx    f15, r4, r5") == 0, "stfdx disasm, got '%s'", buf);

    inst = ppc_decode(0xECE80272, BASE);
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strcmp(buf, "fmuls   f7, f8, f9") == 0, "fmuls disasm, got '%s'", buf);

    inst = ppc_decode(0xFD032000, BASE);
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strcmp(buf, "fcmpu   cr2, f3, f4") == 0, "fcmpu disasm, got '%s'", buf);

    inst = ppc_decode(0xE0448004, BASE);
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strcmp(buf, "psq_l   f2, 4(r4), 1, 0") == 0, "psq_l disasm, got '%s'", buf);

    return 1;
}

static int test_field_edges(void) {
    printf("  field extraction edge cases\n");

    PPCInst inst = ppc_decode(make_dform(14, 31, 31, 0xFFFF), BASE);
    CHECK(inst.rD == 31, "rD max should be 31");
    CHECK(inst.rA == 31, "rA max should be 31");
    CHECK(inst.simm == -1, "simm should be -1");

    inst = ppc_decode(0x5107421E, BASE);
    CHECK(inst.rS == 8, "rlwimi rS should be 8");
    CHECK(inst.rA == 7, "rlwimi rA should be 7");
    CHECK(inst.sh == 8, "rlwimi sh should be 8");
    CHECK(inst.mb == 8, "rlwimi mb should be 8");
    CHECK(inst.me == 15, "rlwimi me should be 15");

    inst = ppc_decode(0x4C432382, BASE);
    CHECK(inst.rD == 2, "cror dest bit should be 2");
    CHECK(inst.rA == 3, "cror source A bit should be 3");
    CHECK(inst.rB == 4, "cror source B bit should be 4");

    inst = ppc_decode(0x4D0C0000, BASE);
    CHECK(inst.crfD == 2, "mcrf dest field should be cr2");
    CHECK(inst.crfS == 3, "mcrf source field should be cr3");

    inst = ppc_decode(0x7D490120, BASE);
    CHECK(inst.rS == 10, "mtcrf source register should be r10");
    CHECK(inst.crm == 0x90, "mtcrf mask should be 0x90");

    inst = ppc_decode(0x7C602C2C, BASE);
    CHECK(inst.op == PPC_OP_LWBRX, "lwbrx zero-base should decode");
    CHECK(inst.rD == 3, "lwbrx rD should be 3");
    CHECK(inst.rA == 0, "lwbrx rA should be 0");
    CHECK(inst.rB == 5, "lwbrx rB should be 5");

    inst = ppc_decode(0x7C0F87EC, BASE);
    CHECK(inst.op == PPC_OP_DCBZ, "dcbz should decode");
    CHECK(inst.rA == 15, "dcbz rA should be 15");
    CHECK(inst.rB == 16, "dcbz rB should be 16");

    inst = ppc_decode(0xE0243000, BASE);
    CHECK(inst.op == PPC_OP_PSQ_L, "psq_l should decode");
    CHECK(inst.rD == 1, "psq_l fD should be 1");
    CHECK(inst.rA == 4, "psq_l rA should be 4");
    CHECK(inst.w == 0, "psq_l W should be 0");
    CHECK(inst.i == 3, "psq_l I should be 3");
    CHECK(inst.simm == 0, "psq_l d should be 0");

    inst = ppc_decode(0x11442C0C, BASE);
    CHECK(inst.op == PPC_OP_PSQ_LX, "psq_lx W=1 should decode");
    CHECK(inst.rD == 10, "psq_lx fD should be 10");
    CHECK(inst.w == 1, "psq_lx W should be 1");
    CHECK(inst.i == 0, "psq_lx I should be 0");

    return 1;
}

static int test_branch_edges(void) {
    printf("  branch target edge cases\n");

    PPCInst inst = ppc_decode(make_iform(18, 0x01FFFFFC, false, false), 0x80000000u);
    CHECK(inst.branch_target == 0x81FFFFFCu, "max forward branch target got 0x%08X",
          inst.branch_target);

    inst = ppc_decode(make_iform(18, -0x02000000, false, false), 0x82000000u);
    CHECK(inst.branch_target == 0x80000000u, "max backward branch target got 0x%08X",
          inst.branch_target);

    inst = ppc_decode(make_iform(18, 0x300, true, true), BASE);
    CHECK(inst.aa == true, "absolute branch should set aa");
    CHECK(inst.lk == true, "linked branch should set lk");
    CHECK(inst.branch_target == 0x300, "absolute branch target got 0x%08X",
          inst.branch_target);

    return 1;
}

static int test_unknown_opcode(void) {
    printf("  unknown opcode 63\n");
    u32 raw = (63u << 26) | 0x12345u;
    PPCInst inst = ppc_decode(raw, BASE);

    CHECK(inst.op == PPC_OP_UNKNOWN, "unrecognized opcode should be UNKNOWN");
    CHECK(inst.raw == raw, "raw should be preserved");
    CHECK(inst.address == BASE, "address should be preserved");
    return 1;
}

typedef int (*test_fn)(void);

typedef struct {
    const char* name;
    test_fn fn;
} TestCase;

static TestCase all_tests[] = {
    { "sign_extend", test_sign_extend },
    { "current opcode count", test_current_opcode_count },
    { "current opcode decode", test_current_opcode_decode_table },
    { "pseudo-ops and display", test_pseudoops_and_display },
    { "field edges", test_field_edges },
    { "branch edges", test_branch_edges },
    { "unknown opcode", test_unknown_opcode },
};

int main(void) {
    int num_tests = (int)(sizeof(all_tests) / sizeof(all_tests[0]));
    int passed = 0;

    printf("running %d opcode tests\n\n", num_tests);

    for (int i = 0; i < num_tests; i++) {
        printf("[%2d] %s\n", i + 1, all_tests[i].name);
        if (all_tests[i].fn()) {
            passed++;
            printf("     PASS\n");
        } else {
            printf("     FAIL\n");
        }
    }

    printf("\n%d/%d tests passed, %d/%d checks passed\n",
           passed, num_tests, tests_passed, tests_run);

    return (passed == num_tests) ? 0 : 1;
}
