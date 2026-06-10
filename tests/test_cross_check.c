#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../src/core/types.h"
#include "../src/frontend/decoder.h"

// raw machine code from devkitPPC (powerpc-eabi-as + powerpc-eabi-objdump)
// every byte here was assembled by a real PPC toolchain
static const u32 test_code[] = {
    // addi
    0x38610010,  // 00: addi r3,r1,16
    0x38A2FF9C,  // 04: addi r5,r2,-100
    0x3860002A,  // 08: li r3,42
    0x3800FFFF,  // 0C: li r0,-1
    0x3BFF0001,  // 10: addi r31,r31,1
    0x38640000,  // 14: addi r3,r4,0

    // ori
    0x6064FF00,  // 18: ori r4,r3,65280
    0x60000000,  // 1C: nop
    0x6063FFFF,  // 20: ori r3,r3,65535
    0x628A0001,  // 24: ori r10,r20,1

    // lwz
    0x80610000,  // 28: lwz r3,0(r1)
    0x8081FFFC,  // 2C: lwz r4,-4(r1)
    0x80600100,  // 30: lwz r3,256(0)
    0x83FF7FFF,  // 34: lwz r31,32767(r31)
    0x80018000,  // 38: lwz r0,-32768(r1)

    // stw
    0x90610000,  // 3C: stw r3,0(r1)
    0x93E1FFF8,  // 40: stw r31,-8(r1)
    0x9000000C,  // 44: stw r0,12(0)
    0x90647FFF,  // 48: stw r3,32767(r4)

    // branch (nop at 0x4C, then branches)
    0x60000000,  // 4C: nop
    0x4BFFFFFC,  // 50: b 4C (backward, displacement=-4)
    0x4BFFFFF9,  // 54: bl 4C (backward call, displacement=-8)
    0x48000018,  // 58: b 70 (forward, displacement=+0x18)
    0x48000015,  // 5C: bl 70 (forward call, displacement=+0x14)
    0x60000000,  // 60: nop
    0x60000000,  // 64: nop
    0x60000000,  // 68: nop
    0x60000000,  // 6C: nop
    0x60000000,  // 70: nop (branch target)
};

#define BASE 0x80000000u
#define NUM_INSTS (sizeof(test_code) / sizeof(test_code[0]))

static int pass = 0;
static int fail = 0;

static void check(bool cond, int idx, const char* what, const char* detail) {
    if (cond) {
        pass++;
    } else {
        fail++;
        printf("    FAIL [%02d] %s: %s\n", idx, what, detail);
    }
}

#define CHK(cond, idx, what) do { \
    char _buf[128]; \
    snprintf(_buf, sizeof(_buf), "%s", #cond); \
    check((cond), (idx), (what), _buf); \
} while(0)

#define CHK_EQ_U(got, want, idx, what) do { \
    char _buf[128]; \
    snprintf(_buf, sizeof(_buf), "got 0x%X, want 0x%X", (unsigned)(got), (unsigned)(want)); \
    check((got) == (want), (idx), (what), _buf); \
} while(0)

#define CHK_EQ_D(got, want, idx, what) do { \
    char _buf[128]; \
    snprintf(_buf, sizeof(_buf), "got %d, want %d", (int)(got), (int)(want)); \
    check((got) == (want), (idx), (what), _buf); \
} while(0)

int main(void) {
    printf("cross-check: %d instructions against devkitPPC ground truth\n\n", (int)NUM_INSTS);

    for (int i = 0; i < (int)NUM_INSTS; i++) {
        u32 addr = BASE + (u32)(i * 4);
        PPCInst inst = ppc_decode(test_code[i], addr);

        char buf[64];
        ppc_disasm(buf, sizeof(buf), &inst);
        printf("[%02d] %08X  %08X  %s\n", i, addr, test_code[i], buf);
    }

    printf("\n--- field verification ---\n\n");

    // [00] addi r3, r1, 16
    { PPCInst i = ppc_decode(0x38610010, BASE+0x00);
      CHK_EQ_D(i.op, PPC_OP_ADDI, 0, "op");
      CHK_EQ_U(i.rD, 3, 0, "rD"); CHK_EQ_U(i.rA, 1, 0, "rA");
      CHK_EQ_D(i.simm, 16, 0, "simm"); }

    // [01] addi r5, r2, -100
    { PPCInst i = ppc_decode(0x38A2FF9C, BASE+0x04);
      CHK_EQ_D(i.op, PPC_OP_ADDI, 1, "op");
      CHK_EQ_U(i.rD, 5, 1, "rD"); CHK_EQ_U(i.rA, 2, 1, "rA");
      CHK_EQ_D(i.simm, -100, 1, "simm"); }

    // [02] li r3, 42
    { PPCInst i = ppc_decode(0x3860002A, BASE+0x08);
      CHK_EQ_D(i.op, PPC_OP_ADDI, 2, "op");
      CHK_EQ_U(i.rD, 3, 2, "rD"); CHK_EQ_U(i.rA, 0, 2, "rA");
      CHK_EQ_D(i.simm, 42, 2, "simm"); }

    // [03] li r0, -1
    { PPCInst i = ppc_decode(0x3800FFFF, BASE+0x0C);
      CHK_EQ_D(i.op, PPC_OP_ADDI, 3, "op");
      CHK_EQ_U(i.rD, 0, 3, "rD"); CHK_EQ_U(i.rA, 0, 3, "rA");
      CHK_EQ_D(i.simm, -1, 3, "simm"); }

    // [04] addi r31, r31, 1
    { PPCInst i = ppc_decode(0x3BFF0001, BASE+0x10);
      CHK_EQ_D(i.op, PPC_OP_ADDI, 4, "op");
      CHK_EQ_U(i.rD, 31, 4, "rD"); CHK_EQ_U(i.rA, 31, 4, "rA");
      CHK_EQ_D(i.simm, 1, 4, "simm"); }

    // [05] addi r3, r4, 0
    { PPCInst i = ppc_decode(0x38640000, BASE+0x14);
      CHK_EQ_D(i.op, PPC_OP_ADDI, 5, "op");
      CHK_EQ_U(i.rD, 3, 5, "rD"); CHK_EQ_U(i.rA, 4, 5, "rA");
      CHK_EQ_D(i.simm, 0, 5, "simm"); }

    // [06] ori r4, r3, 0xFF00
    { PPCInst i = ppc_decode(0x6064FF00, BASE+0x18);
      CHK_EQ_D(i.op, PPC_OP_ORI, 6, "op");
      CHK_EQ_U(i.rS, 3, 6, "rS"); CHK_EQ_U(i.rA, 4, 6, "rA");
      CHK_EQ_U(i.uimm, 0xFF00, 6, "uimm"); }

    // [07] nop
    { PPCInst i = ppc_decode(0x60000000, BASE+0x1C);
      CHK_EQ_D(i.op, PPC_OP_ORI, 7, "op");
      CHK_EQ_U(i.rS, 0, 7, "rS"); CHK_EQ_U(i.rA, 0, 7, "rA");
      CHK_EQ_U(i.uimm, 0, 7, "uimm"); }

    // [08] ori r3, r3, 0xFFFF
    { PPCInst i = ppc_decode(0x6063FFFF, BASE+0x20);
      CHK_EQ_D(i.op, PPC_OP_ORI, 8, "op");
      CHK_EQ_U(i.rS, 3, 8, "rS"); CHK_EQ_U(i.rA, 3, 8, "rA");
      CHK_EQ_U(i.uimm, 0xFFFF, 8, "uimm"); }

    // [09] ori r10, r20, 1
    { PPCInst i = ppc_decode(0x628A0001, BASE+0x24);
      CHK_EQ_D(i.op, PPC_OP_ORI, 9, "op");
      CHK_EQ_U(i.rS, 20, 9, "rS"); CHK_EQ_U(i.rA, 10, 9, "rA");
      CHK_EQ_U(i.uimm, 1, 9, "uimm"); }

    // [10] lwz r3, 0(r1)
    { PPCInst i = ppc_decode(0x80610000, BASE+0x28);
      CHK_EQ_D(i.op, PPC_OP_LWZ, 10, "op");
      CHK_EQ_U(i.rD, 3, 10, "rD"); CHK_EQ_U(i.rA, 1, 10, "rA");
      CHK_EQ_D(i.simm, 0, 10, "simm"); }

    // [11] lwz r4, -4(r1)
    { PPCInst i = ppc_decode(0x8081FFFC, BASE+0x2C);
      CHK_EQ_D(i.op, PPC_OP_LWZ, 11, "op");
      CHK_EQ_U(i.rD, 4, 11, "rD"); CHK_EQ_U(i.rA, 1, 11, "rA");
      CHK_EQ_D(i.simm, -4, 11, "simm"); }

    // [12] lwz r3, 256(r0) — rA=0 absolute
    { PPCInst i = ppc_decode(0x80600100, BASE+0x30);
      CHK_EQ_D(i.op, PPC_OP_LWZ, 12, "op");
      CHK_EQ_U(i.rD, 3, 12, "rD"); CHK_EQ_U(i.rA, 0, 12, "rA");
      CHK_EQ_D(i.simm, 256, 12, "simm"); }

    // [13] lwz r31, 32767(r31) — max positive offset
    { PPCInst i = ppc_decode(0x83FF7FFF, BASE+0x34);
      CHK_EQ_D(i.op, PPC_OP_LWZ, 13, "op");
      CHK_EQ_U(i.rD, 31, 13, "rD"); CHK_EQ_U(i.rA, 31, 13, "rA");
      CHK_EQ_D(i.simm, 32767, 13, "simm"); }

    // [14] lwz r0, -32768(r1) — min negative offset
    { PPCInst i = ppc_decode(0x80018000, BASE+0x38);
      CHK_EQ_D(i.op, PPC_OP_LWZ, 14, "op");
      CHK_EQ_U(i.rD, 0, 14, "rD"); CHK_EQ_U(i.rA, 1, 14, "rA");
      CHK_EQ_D(i.simm, -32768, 14, "simm"); }

    // [15] stw r3, 0(r1)
    { PPCInst i = ppc_decode(0x90610000, BASE+0x3C);
      CHK_EQ_D(i.op, PPC_OP_STW, 15, "op");
      CHK_EQ_U(i.rS, 3, 15, "rS"); CHK_EQ_U(i.rA, 1, 15, "rA");
      CHK_EQ_D(i.simm, 0, 15, "simm"); }

    // [16] stw r31, -8(r1)
    { PPCInst i = ppc_decode(0x93E1FFF8, BASE+0x40);
      CHK_EQ_D(i.op, PPC_OP_STW, 16, "op");
      CHK_EQ_U(i.rS, 31, 16, "rS"); CHK_EQ_U(i.rA, 1, 16, "rA");
      CHK_EQ_D(i.simm, -8, 16, "simm"); }

    // [17] stw r0, 12(r0) — rA=0 absolute
    { PPCInst i = ppc_decode(0x9000000C, BASE+0x44);
      CHK_EQ_D(i.op, PPC_OP_STW, 17, "op");
      CHK_EQ_U(i.rS, 0, 17, "rS"); CHK_EQ_U(i.rA, 0, 17, "rA");
      CHK_EQ_D(i.simm, 12, 17, "simm"); }

    // [18] stw r3, 32767(r4)
    { PPCInst i = ppc_decode(0x90647FFF, BASE+0x48);
      CHK_EQ_D(i.op, PPC_OP_STW, 18, "op");
      CHK_EQ_U(i.rS, 3, 18, "rS"); CHK_EQ_U(i.rA, 4, 18, "rA");
      CHK_EQ_D(i.simm, 32767, 18, "simm"); }

    // [19] nop at 0x4C (skip)

    // [20] b 0x4C (backward, raw=0x4BFFFFFC, displacement=-4)
    { PPCInst i = ppc_decode(0x4BFFFFFC, BASE+0x50);
      CHK_EQ_D(i.op, PPC_OP_B, 20, "op");
      CHK_EQ_U(i.branch_target, BASE+0x4C, 20, "target");
      CHK_EQ_D(i.aa, 0, 20, "aa"); CHK_EQ_D(i.lk, 0, 20, "lk"); }

    // [21] bl 0x4C (backward call, raw=0x4BFFFFF9, displacement=-8)
    { PPCInst i = ppc_decode(0x4BFFFFF9, BASE+0x54);
      CHK_EQ_D(i.op, PPC_OP_B, 21, "op");
      CHK_EQ_U(i.branch_target, BASE+0x4C, 21, "target");
      CHK_EQ_D(i.aa, 0, 21, "aa"); CHK_EQ_D(i.lk, 1, 21, "lk"); }

    // [22] b 0x70 (forward, raw=0x48000018, displacement=+0x18)
    { PPCInst i = ppc_decode(0x48000018, BASE+0x58);
      CHK_EQ_D(i.op, PPC_OP_B, 22, "op");
      CHK_EQ_U(i.branch_target, BASE+0x70, 22, "target");
      CHK_EQ_D(i.aa, 0, 22, "aa"); CHK_EQ_D(i.lk, 0, 22, "lk"); }

    // [23] bl 0x70 (forward call, raw=0x48000015, displacement=+0x14)
    { PPCInst i = ppc_decode(0x48000015, BASE+0x58+0x04);
      CHK_EQ_D(i.op, PPC_OP_B, 23, "op");
      CHK_EQ_U(i.branch_target, BASE+0x70, 23, "target");
      CHK_EQ_D(i.aa, 0, 23, "aa"); CHK_EQ_D(i.lk, 1, 23, "lk"); }

    printf("\n%d/%d checks passed", pass, pass + fail);
    if (fail > 0)
        printf(", %d FAILED", fail);
    printf("\n");

    return fail > 0 ? 1 : 0;
}
