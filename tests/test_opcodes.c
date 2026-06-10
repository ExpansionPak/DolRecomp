#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../src/core/types.h"
#include "../src/frontend/decoder.h"

// hand-encoded instructions verified against PPC ISA spec
// each test: raw instruction word, expected opcode, expected fields

static int tests_run = 0;
static int tests_passed = 0;

#define CHECK(cond, fmt, ...) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("  FAIL: " fmt "\n", ##__VA_ARGS__); \
        return 0; \
    } \
    tests_passed++; \
} while(0)

// helper to build D-form: [opcd(6) | rD/rS(5) | rA(5) | imm(16)]
static u32 make_dform(u32 opcd, u32 rd, u32 ra, u16 imm) {
    return (opcd << 26) | (rd << 21) | (ra << 16) | imm;
}

// helper to build I-form: [opcd(6) | LI(24) | AA(1) | LK(1)]
static u32 make_iform(u32 opcd, s32 offset, bool aa, bool lk) {
    // offset must be aligned to 4, fits in 26 bits signed
    u32 li_field = (u32)offset & 0x03FFFFFC;
    return (opcd << 26) | li_field | (aa ? 2 : 0) | (lk ? 1 : 0);
}

// addi tests
static int test_addi_basic(void) {
    printf("  addi r3, r1, 16\n");
    // addi r3, r1, 16 => opcd=14, rD=3, rA=1, SIMM=16
    u32 raw = make_dform(14, 3, 1, 16);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_ADDI, "op should be ADDI, got %d", inst.op);
    CHECK(inst.rD == 3, "rD should be 3, got %u", inst.rD);
    CHECK(inst.rA == 1, "rA should be 1, got %u", inst.rA);
    CHECK(inst.simm == 16, "simm should be 16, got %d", inst.simm);

    char buf[64];
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strstr(buf, "addi") != NULL, "disasm should contain 'addi', got '%s'", buf);
    CHECK(strstr(buf, "r3") != NULL, "disasm should contain 'r3', got '%s'", buf);
    return 1;
}

static int test_addi_negative(void) {
    printf("  addi r5, r2, -100\n");
    // SIMM = -100 = 0xFF9C as u16
    u32 raw = make_dform(14, 5, 2, (u16)(s16)-100);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_ADDI, "op should be ADDI");
    CHECK(inst.rD == 5, "rD should be 5, got %u", inst.rD);
    CHECK(inst.rA == 2, "rA should be 2, got %u", inst.rA);
    CHECK(inst.simm == -100, "simm should be -100, got %d", inst.simm);
    return 1;
}

static int test_li_pseudoop(void) {
    printf("  li r3, 42 (addi r3, r0, 42)\n");
    // rA=0 makes it 'li'
    u32 raw = make_dform(14, 3, 0, 42);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_ADDI, "op should be ADDI");
    CHECK(inst.rA == 0, "rA should be 0 for li pseudo-op");
    CHECK(inst.simm == 42, "simm should be 42, got %d", inst.simm);

    char buf[64];
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strstr(buf, "li") != NULL, "disasm should show 'li', got '%s'", buf);
    return 1;
}

// ori tests
static int test_ori_basic(void) {
    printf("  ori r4, r3, 0xFF00\n");
    // ori: opcd=24, rS=3 (bits 6-10), rA=4 (bits 11-15), UIMM=0xFF00
    u32 raw = make_dform(24, 3, 4, 0xFF00);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_ORI, "op should be ORI");
    CHECK(inst.rS == 3, "rS should be 3, got %u", inst.rS);
    CHECK(inst.rA == 4, "rA should be 4, got %u", inst.rA);
    CHECK(inst.uimm == 0xFF00, "uimm should be 0xFF00, got 0x%04X", inst.uimm);
    return 1;
}

static int test_nop(void) {
    printf("  nop (ori r0, r0, 0)\n");
    u32 raw = make_dform(24, 0, 0, 0);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_ORI, "op should be ORI");
    CHECK(inst.rS == 0, "rS should be 0");
    CHECK(inst.rA == 0, "rA should be 0");
    CHECK(inst.uimm == 0, "uimm should be 0");

    char buf[64];
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strcmp(buf, "nop") == 0, "disasm should be 'nop', got '%s'", buf);
    return 1;
}


// lwz tests
static int test_lwz_basic(void) {
    printf("  lwz r3, 8(r1)\n");
    // lwz: opcd=32, rD=3, rA=1, d=8
    u32 raw = make_dform(32, 3, 1, 8);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_LWZ, "op should be LWZ");
    CHECK(inst.rD == 3, "rD should be 3, got %u", inst.rD);
    CHECK(inst.rA == 1, "rA should be 1, got %u", inst.rA);
    CHECK(inst.simm == 8, "simm should be 8, got %d", inst.simm);

    char buf[64];
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strstr(buf, "lwz") != NULL, "disasm should contain 'lwz', got '%s'", buf);
    CHECK(strstr(buf, "8(r1)") != NULL, "disasm should contain '8(r1)', got '%s'", buf);
    return 1;
}

static int test_lwz_negative_offset(void) {
    printf("  lwz r4, -4(r1)\n");
    // d = -4 = 0xFFFC
    u32 raw = make_dform(32, 4, 1, (u16)(s16)-4);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_LWZ, "op should be LWZ");
    CHECK(inst.simm == -4, "simm should be -4, got %d", inst.simm);
    return 1;
}

static int test_lwz_zero_base(void) {
    printf("  lwz r3, 0x100(r0) — absolute load\n");
    // rA=0 means base = 0
    u32 raw = make_dform(32, 3, 0, 0x100);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_LWZ, "op should be LWZ");
    CHECK(inst.rA == 0, "rA should be 0 for absolute load");
    CHECK(inst.simm == 0x100, "simm should be 0x100, got %d", inst.simm);
    return 1;
}

// stw tests
static int test_stw_basic(void) {
    printf("  stw r3, 12(r1)\n");
    // stw: opcd=36, rS=3 (bits 6-10), rA=1, d=12
    u32 raw = make_dform(36, 3, 1, 12);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_STW, "op should be STW");
    CHECK(inst.rS == 3, "rS should be 3, got %u", inst.rS);
    CHECK(inst.rA == 1, "rA should be 1, got %u", inst.rA);
    CHECK(inst.simm == 12, "simm should be 12, got %d", inst.simm);

    char buf[64];
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strstr(buf, "stw") != NULL, "disasm should contain 'stw', got '%s'", buf);
    CHECK(strstr(buf, "12(r1)") != NULL, "disasm should contain '12(r1)', got '%s'", buf);
    return 1;
}

static int test_stw_negative(void) {
    printf("  stw r31, -8(r1)\n");
    u32 raw = make_dform(36, 31, 1, (u16)(s16)-8);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_STW, "op should be STW");
    CHECK(inst.rS == 31, "rS should be 31, got %u", inst.rS);
    CHECK(inst.simm == -8, "simm should be -8, got %d", inst.simm);
    return 1;
}

// branch tests
static int test_b_forward(void) {
    printf("  b +0x100 (forward relative)\n");
    u32 raw = make_iform(18, 0x100, false, false);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_B, "op should be B");
    CHECK(inst.aa == false, "aa should be false");
    CHECK(inst.lk == false, "lk should be false");
    CHECK(inst.branch_target == 0x80003100, "target should be 0x80003100, got 0x%08X",
          inst.branch_target);

    char buf[64];
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strstr(buf, "b ") != NULL || buf[0] == 'b', "disasm should start with 'b'");
    return 1;
}

static int test_b_backward(void) {
    printf("  b -0x20 (backward relative)\n");
    u32 raw = make_iform(18, -0x20, false, false);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_B, "op should be B");
    CHECK(inst.branch_target == 0x80002FE0, "target should be 0x80002FE0, got 0x%08X",
          inst.branch_target);
    return 1;
}

static int test_bl(void) {
    printf("  bl +0x200 (call)\n");
    u32 raw = make_iform(18, 0x200, false, true);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_B, "op should be B");
    CHECK(inst.lk == true, "lk should be true for bl");
    CHECK(inst.aa == false, "aa should be false");
    CHECK(inst.branch_target == 0x80003200, "target should be 0x80003200, got 0x%08X",
          inst.branch_target);

    char buf[64];
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strstr(buf, "bl") != NULL, "disasm should contain 'bl', got '%s'", buf);
    return 1;
}

static int test_ba(void) {
    printf("  ba 0x100 (absolute)\n");
    u32 raw = make_iform(18, 0x100, true, false);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_B, "op should be B");
    CHECK(inst.aa == true, "aa should be true for ba");
    CHECK(inst.lk == false, "lk should be false");
    CHECK(inst.branch_target == 0x100, "target should be 0x100, got 0x%08X",
          inst.branch_target);

    char buf[64];
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strstr(buf, "ba") != NULL, "disasm should contain 'ba', got '%s'", buf);
    return 1;
}

static int test_bla(void) {
    printf("  bla 0x300 (absolute + link)\n");
    u32 raw = make_iform(18, 0x300, true, true);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_B, "op should be B");
    CHECK(inst.aa == true, "aa should be true");
    CHECK(inst.lk == true, "lk should be true");
    CHECK(inst.branch_target == 0x300, "target should be 0x300, got 0x%08X",
          inst.branch_target);

    char buf[64];
    ppc_disasm(buf, sizeof(buf), &inst);
    CHECK(strstr(buf, "bla") != NULL, "disasm should contain 'bla', got '%s'", buf);
    return 1;
}

static int test_current_opcode_count(void) {
    printf("  current opcode count is 30\n");
    CHECK(PPC_OP_COUNT - 1 == 30, "should expose 30 opcodes, got %d", PPC_OP_COUNT - 1);
    return 1;
}

static int test_current_opcode_decode_table(void) {
    printf("  decode every opcode in the current 30-opcode set\n");

    typedef struct {
        u32 raw;
        PPCOpcode op;
        const char* name;
    } OpcodeDecode;

    static const OpcodeDecode opcodes[] = {
        { 0x38610010, PPC_OP_ADDI,  "addi" },
        { 0x3084FFFF, PPC_OP_ADDIC, "addic" },
        { 0x3CA01234, PPC_OP_ADDIS, "addis" },
        { 0x2C03FFFF, PPC_OP_CMPI,  "cmpi" },
        { 0x28038000, PPC_OP_CMPLI, "cmpli" },
        { 0x6064FF00, PPC_OP_ORI,   "ori" },
        { 0x64851234, PPC_OP_ORIS,  "oris" },
        { 0x68A6FFFF, PPC_OP_XORI,  "xori" },
        { 0x6CC78000, PPC_OP_XORIS, "xoris" },
        { 0x70E800FF, PPC_OP_ANDI,  "andi." },
        { 0x74E900FF, PPC_OP_ANDIS, "andis." },
        { 0x80610000, PPC_OP_LWZ,   "lwz" },
        { 0x84810004, PPC_OP_LWZU,  "lwzu" },
        { 0x88A10008, PPC_OP_LBZ,   "lbz" },
        { 0x8CC1000C, PPC_OP_LBZU,  "lbzu" },
        { 0x90610018, PPC_OP_STW,   "stw" },
        { 0x9481001C, PPC_OP_STWU,  "stwu" },
        { 0x98A10020, PPC_OP_STB,   "stb" },
        { 0x9CC10024, PPC_OP_STBU,  "stbu" },
        { 0xA0E10010, PPC_OP_LHZ,   "lhz" },
        { 0xA5010014, PPC_OP_LHZU,  "lhzu" },
        { 0xA921FFFC, PPC_OP_LHA,   "lha" },
        { 0xB0E10028, PPC_OP_STH,   "sth" },
        { 0xB501002C, PPC_OP_STHU,  "sthu" },
        { 0x48000018, PPC_OP_B,     "b" },
        { 0x41820014, PPC_OP_BC,    "bc" },
        { 0x4E800020, PPC_OP_BCLR,  "bclr" },
        { 0x4E800420, PPC_OP_BCCTR, "bcctr" },
        { 0x7D4802A6, PPC_OP_MFSPR, "mfspr" },
        { 0x7D4803A6, PPC_OP_MTSPR, "mtspr" },
    };

    int count = (int)(sizeof(opcodes) / sizeof(opcodes[0]));
    CHECK(count == 30, "opcode table should have 30 entries, got %d", count);

    for (int i = 0; i < count; i++) {
        PPCInst inst = ppc_decode(opcodes[i].raw, 0x80000000u + (u32)(i * 4));
        CHECK(inst.op == opcodes[i].op, "%s decoded as %s",
              opcodes[i].name, ppc_op_name(inst.op));
    }

    return 1;
}
// edge cases
static int test_unknown_opcode(void) {
    printf("  unknown opcode 63\n");
    u32 raw = (63u << 26) | 0x12345;
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.op == PPC_OP_UNKNOWN, "unrecognized opcode should be UNKNOWN");
    CHECK(inst.raw == raw, "raw should be preserved");
    return 1;
}

static int test_field_extraction_max(void) {
    printf("  addi r31, r31, -1 (max register indices, simm=-1)\n");
    u32 raw = make_dform(14, 31, 31, 0xFFFF);
    PPCInst inst = ppc_decode(raw, 0x80003000);

    CHECK(inst.rD == 31, "rD should be 31, got %u", inst.rD);
    CHECK(inst.rA == 31, "rA should be 31, got %u", inst.rA);
    CHECK(inst.simm == -1, "simm should be -1, got %d", inst.simm);
    return 1;
}

static int test_branch_max_forward(void) {
    printf("  b +0x01FFFFFC (max forward)\n");
    // max positive 26-bit signed: 0x01FFFFFC
    u32 raw = make_iform(18, 0x01FFFFFC, false, false);
    PPCInst inst = ppc_decode(raw, 0x80000000);

    CHECK(inst.branch_target == 0x81FFFFFC, "target should be 0x81FFFFFC, got 0x%08X",
          inst.branch_target);
    return 1;
}

static int test_branch_max_backward(void) {
    printf("  b -0x02000000 (max backward)\n");
    // max negative 26-bit signed: -0x02000000
    u32 raw = make_iform(18, -0x02000000, false, false);
    PPCInst inst = ppc_decode(raw, 0x82000000);

    CHECK(inst.branch_target == 0x80000000, "target should be 0x80000000, got 0x%08X",
          inst.branch_target);
    return 1;
}

static int test_address_preserved(void) {
    printf("  instruction address preserved\n");
    u32 raw = make_dform(14, 0, 0, 0);
    PPCInst inst = ppc_decode(raw, 0xDEADBEEF);

    CHECK(inst.address == 0xDEADBEEF, "address should be 0xDEADBEEF, got 0x%08X",
          inst.address);
    CHECK(inst.raw == raw, "raw should be preserved");
    return 1;
}


// sign_extend verification
static int test_sign_extend(void) {
    printf("  sign_extend\n");

    CHECK(sign_extend(0x0010, 16) == 16,
          "sign_extend(0x0010, 16) should be 16, got %d", sign_extend(0x0010, 16));
    CHECK(sign_extend(0xFFF0, 16) == -16,
          "sign_extend(0xFFF0, 16) should be -16, got %d", sign_extend(0xFFF0, 16));
    CHECK(sign_extend(0x7FFF, 16) == 32767,
          "sign_extend(0x7FFF, 16) should be 32767");
    CHECK(sign_extend(0x8000, 16) == -32768,
          "sign_extend(0x8000, 16) should be -32768");
    CHECK(sign_extend(0x00000004, 26) == 4,
          "sign_extend(4, 26) should be 4");
    CHECK(sign_extend(0x03FFFFFC, 26) == -4,
          "sign_extend(0x03FFFFFC, 26) should be -4, got %d",
          sign_extend(0x03FFFFFC, 26));
    return 1;
}

// runner
typedef int (*test_fn)(void);

typedef struct {
    const char* name;
    test_fn fn;
} TestCase;

static TestCase all_tests[] = {
    { "sign_extend",           test_sign_extend },
    { "current opcode count",  test_current_opcode_count },
    { "current opcode decode", test_current_opcode_decode_table },
    { "addi basic",            test_addi_basic },
    { "addi negative SIMM",    test_addi_negative },
    { "li pseudo-op",          test_li_pseudoop },
    { "ori basic",             test_ori_basic },
    { "nop",                   test_nop },
    { "lwz basic",             test_lwz_basic },
    { "lwz negative offset",   test_lwz_negative_offset },
    { "lwz zero base",         test_lwz_zero_base },
    { "stw basic",             test_stw_basic },
    { "stw negative offset",   test_stw_negative },
    { "b forward",             test_b_forward },
    { "b backward",            test_b_backward },
    { "bl (call)",             test_bl },
    { "ba (absolute)",         test_ba },
    { "bla (absolute+link)",   test_bla },
    { "unknown opcode",        test_unknown_opcode },
    { "max register/simm",     test_field_extraction_max },
    { "branch max forward",    test_branch_max_forward },
    { "branch max backward",   test_branch_max_backward },
    { "address preserved",     test_address_preserved },
};

int main(void) {
    int num_tests = sizeof(all_tests) / sizeof(all_tests[0]);
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
