#include <stdio.h>

#include "../src/core/types.h"
#include "../src/frontend/decoder.h"

// Raw machine code from devkitPPC (powerpc-eabi-as + powerpc-eabi-objdump).

#define BASE 0x80000000u

enum {
    F_RD     = 1u << 0,
    F_RA     = 1u << 1,
    F_RS     = 1u << 2,
    F_CRF    = 1u << 3,
    F_L      = 1u << 4,
    F_BO     = 1u << 5,
    F_BI     = 1u << 6,
    F_SIMM   = 1u << 7,
    F_UIMM   = 1u << 8,
    F_TARGET = 1u << 9,
    F_AA     = 1u << 10,
    F_LK     = 1u << 11,
    F_RC     = 1u << 12,
    F_SPR    = 1u << 13,
};

typedef struct {
    const char* name;
    u32 raw;
    PPCOpcode op;
    u32 fields;
    u8 rD;
    u8 rA;
    u8 rS;
    u8 crfD;
    u8 l;
    u8 bo;
    u8 bi;
    s16 simm;
    u16 uimm;
    u32 target;
    bool aa;
    bool lk;
    bool rc;
    u16 spr;
} DecodeCase;

static const DecodeCase cases[] = {
    { "addi",  0x38610010, PPC_OP_ADDI,  F_RD|F_RA|F_SIMM, .rD=3, .rA=1, .simm=16 },
    { "addic", 0x3084FFFF, PPC_OP_ADDIC, F_RD|F_RA|F_SIMM, .rD=4, .rA=4, .simm=-1 },
    { "addis", 0x3CA01234, PPC_OP_ADDIS, F_RD|F_RA|F_SIMM, .rD=5, .rA=0, .simm=0x1234 },
    { "cmpi",  0x2C03FFFF, PPC_OP_CMPI,  F_CRF|F_L|F_RA|F_SIMM, .crfD=0, .l=0, .rA=3, .simm=-1 },
    { "cmpli", 0x28038000, PPC_OP_CMPLI, F_CRF|F_L|F_RA|F_UIMM, .crfD=0, .l=0, .rA=3, .uimm=0x8000 },

    { "ori",    0x6064FF00, PPC_OP_ORI,   F_RS|F_RA|F_UIMM, .rS=3, .rA=4, .uimm=0xFF00 },
    { "oris",   0x64851234, PPC_OP_ORIS,  F_RS|F_RA|F_UIMM, .rS=4, .rA=5, .uimm=0x1234 },
    { "xori",   0x68A6FFFF, PPC_OP_XORI,  F_RS|F_RA|F_UIMM, .rS=5, .rA=6, .uimm=0xFFFF },
    { "xoris",  0x6CC78000, PPC_OP_XORIS, F_RS|F_RA|F_UIMM, .rS=6, .rA=7, .uimm=0x8000 },
    { "andi.",  0x70E800FF, PPC_OP_ANDI,  F_RS|F_RA|F_UIMM|F_RC, .rS=7, .rA=8, .uimm=0x00FF, .rc=true },
    { "andis.", 0x74E900FF, PPC_OP_ANDIS, F_RS|F_RA|F_UIMM|F_RC, .rS=7, .rA=9, .uimm=0x00FF, .rc=true },

    { "lwz",  0x80610000, PPC_OP_LWZ,  F_RD|F_RA|F_SIMM, .rD=3, .rA=1, .simm=0 },
    { "lwzu", 0x84810004, PPC_OP_LWZU, F_RD|F_RA|F_SIMM, .rD=4, .rA=1, .simm=4 },
    { "lbz",  0x88A10008, PPC_OP_LBZ,  F_RD|F_RA|F_SIMM, .rD=5, .rA=1, .simm=8 },
    { "lbzu", 0x8CC1000C, PPC_OP_LBZU, F_RD|F_RA|F_SIMM, .rD=6, .rA=1, .simm=12 },
    { "lhz",  0xA0E10010, PPC_OP_LHZ,  F_RD|F_RA|F_SIMM, .rD=7, .rA=1, .simm=16 },
    { "lhzu", 0xA5010014, PPC_OP_LHZU, F_RD|F_RA|F_SIMM, .rD=8, .rA=1, .simm=20 },
    { "lha",  0xA921FFFC, PPC_OP_LHA,  F_RD|F_RA|F_SIMM, .rD=9, .rA=1, .simm=-4 },

    { "stw",  0x90610018, PPC_OP_STW,  F_RS|F_RA|F_SIMM, .rS=3, .rA=1, .simm=24 },
    { "stwu", 0x9481001C, PPC_OP_STWU, F_RS|F_RA|F_SIMM, .rS=4, .rA=1, .simm=28 },
    { "stb",  0x98A10020, PPC_OP_STB,  F_RS|F_RA|F_SIMM, .rS=5, .rA=1, .simm=32 },
    { "stbu", 0x9CC10024, PPC_OP_STBU, F_RS|F_RA|F_SIMM, .rS=6, .rA=1, .simm=36 },
    { "sth",  0xB0E10028, PPC_OP_STH,  F_RS|F_RA|F_SIMM, .rS=7, .rA=1, .simm=40 },
    { "sthu", 0xB501002C, PPC_OP_STHU, F_RS|F_RA|F_SIMM, .rS=8, .rA=1, .simm=44 },

    { "b",     0x48000018, PPC_OP_B,     F_TARGET|F_AA|F_LK, .target=BASE+0x78, .aa=false, .lk=false },
    { "bc",    0x41820014, PPC_OP_BC,    F_BO|F_BI|F_TARGET|F_AA|F_LK, .bo=12, .bi=2, .target=BASE+0x78, .aa=false, .lk=false },
    { "bclr",  0x4E800020, PPC_OP_BCLR,  F_BO|F_BI|F_LK, .bo=20, .bi=0, .lk=false },
    { "bcctr", 0x4E800420, PPC_OP_BCCTR, F_BO|F_BI|F_LK, .bo=20, .bi=0, .lk=false },
    { "mfspr", 0x7D4802A6, PPC_OP_MFSPR, F_RD|F_SPR, .rD=10, .spr=8 },
    { "mtspr", 0x7D4803A6, PPC_OP_MTSPR, F_RS|F_SPR, .rS=10, .spr=8 },
};

static int pass = 0;
static int fail = 0;

static void check(bool cond, int idx, const char* field, u32 got, u32 want) {
    if (cond) {
        pass++;
    } else {
        fail++;
        printf("    FAIL [%02d] %s: got 0x%X, want 0x%X\n",
               idx, field, got, want);
    }
}

static void check_s(bool cond, int idx, const char* field, s32 got, s32 want) {
    if (cond) {
        pass++;
    } else {
        fail++;
        printf("    FAIL [%02d] %s: got %d, want %d\n",
               idx, field, got, want);
    }
}

int main(void) {
    int num_cases = (int)(sizeof(cases) / sizeof(cases[0]));
    printf("cross-check: %d opcodes against devkitPPC ground truth\n\n", num_cases);

    check((PPC_OP_COUNT - 1) == 30, -1, "opcode count", PPC_OP_COUNT - 1, 30);

    for (int n = 0; n < num_cases; n++) {
        const DecodeCase* c = &cases[n];
        u32 addr = BASE + (u32)(n * 4);
        PPCInst inst = ppc_decode(c->raw, addr);

        char buf[64];
        ppc_disasm(buf, sizeof(buf), &inst);
        printf("[%02d] %08X  %08X  %-7s  %s\n", n, addr, c->raw, c->name, buf);

        check(inst.op == c->op, n, "op", inst.op, c->op);
        if (c->fields & F_RD)     check(inst.rD == c->rD, n, "rD", inst.rD, c->rD);
        if (c->fields & F_RA)     check(inst.rA == c->rA, n, "rA", inst.rA, c->rA);
        if (c->fields & F_RS)     check(inst.rS == c->rS, n, "rS", inst.rS, c->rS);
        if (c->fields & F_CRF)    check(inst.crfD == c->crfD, n, "crfD", inst.crfD, c->crfD);
        if (c->fields & F_L)      check(inst.l == c->l, n, "l", inst.l, c->l);
        if (c->fields & F_BO)     check(inst.bo == c->bo, n, "bo", inst.bo, c->bo);
        if (c->fields & F_BI)     check(inst.bi == c->bi, n, "bi", inst.bi, c->bi);
        if (c->fields & F_SIMM)   check_s(inst.simm == c->simm, n, "simm", inst.simm, c->simm);
        if (c->fields & F_UIMM)   check(inst.uimm == c->uimm, n, "uimm", inst.uimm, c->uimm);
        if (c->fields & F_TARGET) check(inst.branch_target == c->target, n, "target", inst.branch_target, c->target);
        if (c->fields & F_AA)     check(inst.aa == c->aa, n, "aa", inst.aa, c->aa);
        if (c->fields & F_LK)     check(inst.lk == c->lk, n, "lk", inst.lk, c->lk);
        if (c->fields & F_RC)     check(inst.rc == c->rc, n, "rc", inst.rc, c->rc);
        if (c->fields & F_SPR)    check(inst.spr == c->spr, n, "spr", inst.spr, c->spr);
    }

    printf("\n%d/%d checks passed", pass, pass + fail);
    if (fail > 0)
        printf(", %d FAILED", fail);
    printf("\n");

    return fail > 0 ? 1 : 0;
}
