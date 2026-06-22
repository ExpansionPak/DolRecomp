#include <stdio.h>

#include "../src/core/types.h"
#include "../src/frontend/decoder.h"

// Raw machine code from devkitPPC (powerpc-eabi-as + powerpc-eabi-objdump).

#define BASE 0x80000000u

enum {
    F_RD     = 1u << 0,
    F_RA     = 1u << 1,
    F_RS     = 1u << 2,
    F_RB     = 1u << 3,
    F_CRF    = 1u << 4,
    F_L      = 1u << 5,
    F_BO     = 1u << 6,
    F_BI     = 1u << 7,
    F_SIMM   = 1u << 8,
    F_UIMM   = 1u << 9,
    F_TARGET = 1u << 10,
    F_AA     = 1u << 11,
    F_LK     = 1u << 12,
    F_RC     = 1u << 13,
    F_SPR    = 1u << 14,
    F_MB     = 1u << 15,
    F_ME     = 1u << 16,
    F_SH     = 1u << 17,
    F_CRFS   = 1u << 18,
    F_CRM    = 1u << 19,
    F_RC_REG = 1u << 20,
    F_W      = 1u << 21,
    F_I      = 1u << 22,
};

typedef struct {
    const char* name;
    u32 raw;
    PPCOpcode op;
    u32 fields;
    u8 rD;
    u8 rA;
    u8 rS;
    u8 rB;
    u8 rC;
    u8 crfD;
    u8 crfS;
    u8 crm;
    u8 l;
    u8 w;
    u8 i;
    u8 bo;
    u8 bi;
    u8 mb;
    u8 me;
    u8 sh;
    s16 simm;
    u16 uimm;
    u32 target;
    bool aa;
    bool lk;
    bool rc;
    u16 spr;
} DecodeCase;

static const DecodeCase cases[] = {
    { "mulli",  0x1C64FFF9, PPC_OP_MULLI,    F_RD|F_RA|F_SIMM, .rD=3,  .rA=4,  .simm=-7 },
    { "subfic", 0x20850001, PPC_OP_SUBFIC,   F_RD|F_RA|F_SIMM, .rD=4,  .rA=5,  .simm=1 },
    { "addi",   0x38610010, PPC_OP_ADDI,     F_RD|F_RA|F_SIMM, .rD=3,  .rA=1,  .simm=16 },
    { "addic",  0x3084FFFF, PPC_OP_ADDIC,    F_RD|F_RA|F_SIMM, .rD=4,  .rA=4,  .simm=-1 },
    { "addic.", 0x34A5FFFF, PPC_OP_ADDIC_DOT,F_RD|F_RA|F_SIMM|F_RC, .rD=5, .rA=5, .simm=-1, .rc=true },
    { "addis",  0x3CA01234, PPC_OP_ADDIS,    F_RD|F_RA|F_SIMM, .rD=5,  .rA=0,  .simm=0x1234 },
    { "cmpi",   0x2C03FFFF, PPC_OP_CMPI,     F_CRF|F_L|F_RA|F_SIMM, .crfD=0, .l=0, .rA=3, .simm=-1 },
    { "cmpli",  0x28038000, PPC_OP_CMPLI,    F_CRF|F_L|F_RA|F_UIMM, .crfD=0, .l=0, .rA=3, .uimm=0x8000 },
    { "ori",    0x6064FF00, PPC_OP_ORI,      F_RS|F_RA|F_UIMM, .rS=3,  .rA=4,  .uimm=0xFF00 },
    { "oris",   0x64851234, PPC_OP_ORIS,     F_RS|F_RA|F_UIMM, .rS=4,  .rA=5,  .uimm=0x1234 },
    { "xori",   0x68A6FFFF, PPC_OP_XORI,     F_RS|F_RA|F_UIMM, .rS=5,  .rA=6,  .uimm=0xFFFF },
    { "xoris",  0x6CC78000, PPC_OP_XORIS,    F_RS|F_RA|F_UIMM, .rS=6,  .rA=7,  .uimm=0x8000 },
    { "andi.",  0x70E800FF, PPC_OP_ANDI,     F_RS|F_RA|F_UIMM|F_RC, .rS=7, .rA=8, .uimm=0x00FF, .rc=true },
    { "andis.", 0x74E900FF, PPC_OP_ANDIS,    F_RS|F_RA|F_UIMM|F_RC, .rS=7, .rA=9, .uimm=0x00FF, .rc=true },

    { "lwz",  0x80610000, PPC_OP_LWZ,  F_RD|F_RA|F_SIMM, .rD=3,  .rA=1, .simm=0 },
    { "lwzu", 0x84810004, PPC_OP_LWZU, F_RD|F_RA|F_SIMM, .rD=4,  .rA=1, .simm=4 },
    { "lbz",  0x88A10008, PPC_OP_LBZ,  F_RD|F_RA|F_SIMM, .rD=5,  .rA=1, .simm=8 },
    { "lbzu", 0x8CC1000C, PPC_OP_LBZU, F_RD|F_RA|F_SIMM, .rD=6,  .rA=1, .simm=12 },
    { "lhz",  0xA0E10010, PPC_OP_LHZ,  F_RD|F_RA|F_SIMM, .rD=7,  .rA=1, .simm=16 },
    { "lhzu", 0xA5010014, PPC_OP_LHZU, F_RD|F_RA|F_SIMM, .rD=8,  .rA=1, .simm=20 },
    { "lha",  0xA921FFFC, PPC_OP_LHA,  F_RD|F_RA|F_SIMM, .rD=9,  .rA=1, .simm=-4 },
    { "lhau", 0xAD410018, PPC_OP_LHAU, F_RD|F_RA|F_SIMM, .rD=10, .rA=1, .simm=24 },
    { "stw",  0x9061001C, PPC_OP_STW,  F_RS|F_RA|F_SIMM, .rS=3,  .rA=1, .simm=28 },
    { "stwu", 0x94810020, PPC_OP_STWU, F_RS|F_RA|F_SIMM, .rS=4,  .rA=1, .simm=32 },
    { "stb",  0x98A10024, PPC_OP_STB,  F_RS|F_RA|F_SIMM, .rS=5,  .rA=1, .simm=36 },
    { "stbu", 0x9CC10028, PPC_OP_STBU, F_RS|F_RA|F_SIMM, .rS=6,  .rA=1, .simm=40 },
    { "sth",  0xB0E1002C, PPC_OP_STH,  F_RS|F_RA|F_SIMM, .rS=7,  .rA=1, .simm=44 },
    { "sthu", 0xB5010030, PPC_OP_STHU, F_RS|F_RA|F_SIMM, .rS=8,  .rA=1, .simm=48 },
    { "lmw",  0xBA810034, PPC_OP_LMW,  F_RD|F_RA|F_SIMM, .rD=20, .rA=1, .simm=52 },
    { "stmw", 0xBE810064, PPC_OP_STMW, F_RS|F_RA|F_SIMM, .rS=20, .rA=1, .simm=100 },

    { "b",     0x480000C8, PPC_OP_B,     F_TARGET|F_AA|F_LK, .target=BASE+0x140, .aa=false, .lk=false },
    { "bc",    0x418200C4, PPC_OP_BC,    F_BO|F_BI|F_TARGET|F_AA|F_LK, .bo=12, .bi=2, .target=BASE+0x140, .aa=false, .lk=false },
    { "bclr",  0x4E800020, PPC_OP_BCLR,  F_BO|F_BI|F_LK, .bo=20, .bi=0, .lk=false },
    { "bcctr", 0x4E800420, PPC_OP_BCCTR, F_BO|F_BI|F_LK, .bo=20, .bi=0, .lk=false },
    { "crand",  0x4C432202, PPC_OP_CRAND,  F_RD|F_RA|F_RB, .rD=2, .rA=3, .rB=4 },
    { "crandc", 0x4C432102, PPC_OP_CRANDC, F_RD|F_RA|F_RB, .rD=2, .rA=3, .rB=4 },
    { "creqv",  0x4C432242, PPC_OP_CREQV,  F_RD|F_RA|F_RB, .rD=2, .rA=3, .rB=4 },
    { "crnand", 0x4C4321C2, PPC_OP_CRNAND, F_RD|F_RA|F_RB, .rD=2, .rA=3, .rB=4 },
    { "crnor",  0x4C432042, PPC_OP_CRNOR,  F_RD|F_RA|F_RB, .rD=2, .rA=3, .rB=4 },
    { "cror",  0x4C432382, PPC_OP_CROR,  F_RD|F_RA|F_RB, .rD=2, .rA=3, .rB=4 },
    { "crorc", 0x4C432342, PPC_OP_CRORC, F_RD|F_RA|F_RB, .rD=2, .rA=3, .rB=4 },
    { "crxor", 0x4C432182, PPC_OP_CRXOR, F_RD|F_RA|F_RB, .rD=2, .rA=3, .rB=4 },
    { "mcrf",  0x4D0C0000, PPC_OP_MCRF,  F_CRF|F_CRFS, .crfD=2, .crfS=3 },
    { "mfcr",  0x7D400026, PPC_OP_MFCR,  F_RD, .rD=10 },
    { "mtcrf", 0x7D4FF120, PPC_OP_MTCRF, F_RS|F_CRM, .rS=10, .crm=0xFF },
    { "mfspr", 0x7D4802A6, PPC_OP_MFSPR, F_RD|F_SPR, .rD=10, .spr=8 },
    { "mtspr", 0x7D4803A6, PPC_OP_MTSPR, F_RS|F_SPR, .rS=10, .spr=8 },

    { "cmp",    0x7C832000, PPC_OP_CMP,     F_CRF|F_L|F_RA|F_RB, .crfD=1, .l=0, .rA=3,  .rB=4 },
    { "cmpl",   0x7D032040, PPC_OP_CMPL,    F_CRF|F_L|F_RA|F_RB, .crfD=2, .l=0, .rA=3,  .rB=4 },
    { "add",    0x7D4B6214, PPC_OP_ADD,     F_RD|F_RA|F_RB, .rD=10, .rA=11, .rB=12 },
    { "addc",   0x7D6C6814, PPC_OP_ADDC,    F_RD|F_RA|F_RB, .rD=11, .rA=12, .rB=13 },
    { "adde",   0x7D8D7114, PPC_OP_ADDE,    F_RD|F_RA|F_RB, .rD=12, .rA=13, .rB=14 },
    { "addze",  0x7DAE0194, PPC_OP_ADDZE,   F_RD|F_RA,      .rD=13, .rA=14 },
    { "subf",   0x7DCF8050, PPC_OP_SUBF,    F_RD|F_RA|F_RB, .rD=14, .rA=15, .rB=16 },
    { "subfc",  0x7DF08810, PPC_OP_SUBFC,   F_RD|F_RA|F_RB, .rD=15, .rA=16, .rB=17 },
    { "subfe",  0x7E119110, PPC_OP_SUBFE,   F_RD|F_RA|F_RB, .rD=16, .rA=17, .rB=18 },
    { "subfze", 0x7E320190, PPC_OP_SUBFZE,  F_RD|F_RA,      .rD=17, .rA=18 },
    { "neg",    0x7E5300D0, PPC_OP_NEG,     F_RD|F_RA,      .rD=18, .rA=19 },

    { "and",    0x7E93A838, PPC_OP_AND,     F_RS|F_RA|F_RB, .rS=20, .rA=19, .rB=21 },
    { "andc",   0x7EB4B078, PPC_OP_ANDC,    F_RS|F_RA|F_RB, .rS=21, .rA=20, .rB=22 },
    { "or",     0x7ED5BB78, PPC_OP_OR,      F_RS|F_RA|F_RB, .rS=22, .rA=21, .rB=23 },
    { "orc",    0x7EF6C338, PPC_OP_ORC,     F_RS|F_RA|F_RB, .rS=23, .rA=22, .rB=24 },
    { "xor",    0x7F17CA78, PPC_OP_XOR,     F_RS|F_RA|F_RB, .rS=24, .rA=23, .rB=25 },
    { "nand",   0x7F38D3B8, PPC_OP_NAND,    F_RS|F_RA|F_RB, .rS=25, .rA=24, .rB=26 },
    { "nor",    0x7F59D8F8, PPC_OP_NOR,     F_RS|F_RA|F_RB, .rS=26, .rA=25, .rB=27 },
    { "eqv",    0x7F7AE238, PPC_OP_EQV,     F_RS|F_RA|F_RB, .rS=27, .rA=26, .rB=28 },
    { "cntlzw", 0x7F9B0034, PPC_OP_CNTLZW,  F_RS|F_RA,      .rS=28, .rA=27 },
    { "extsb",  0x7FBC0774, PPC_OP_EXTSB,   F_RS|F_RA,      .rS=29, .rA=28 },
    { "extsh",  0x7FDD0734, PPC_OP_EXTSH,   F_RS|F_RA,      .rS=30, .rA=29 },
    { "slw",    0x7FFE1830, PPC_OP_SLW,     F_RS|F_RA|F_RB, .rS=31, .rA=30, .rB=3 },
    { "srw",    0x7C7F2430, PPC_OP_SRW,     F_RS|F_RA|F_RB, .rS=3,  .rA=31, .rB=4 },
    { "sraw",   0x7C832E30, PPC_OP_SRAW,    F_RS|F_RA|F_RB, .rS=4,  .rA=3,  .rB=5 },
    { "srawi",  0x7CA43E70, PPC_OP_SRAWI,   F_RS|F_RA|F_SH, .rS=5,  .rA=4,  .sh=7 },

    { "rlwinm", 0x54C52A2E, PPC_OP_RLWINM,  F_RS|F_RA|F_SH|F_MB|F_ME, .rS=6, .rA=5, .sh=5, .mb=8, .me=23 },
    { "rlwnm",  0x5CE64136, PPC_OP_RLWNM,   F_RS|F_RA|F_RB|F_MB|F_ME, .rS=7, .rA=6, .rB=8, .mb=4, .me=27 },
    { "rlwimi", 0x5107421E, PPC_OP_RLWIMI,  F_RS|F_RA|F_SH|F_MB|F_ME, .rS=8, .rA=7, .sh=8, .mb=8, .me=15 },

    { "lwzx",  0x7C64282E, PPC_OP_LWZX,  F_RD|F_RA|F_RB, .rD=3,  .rA=4, .rB=5 },
    { "lwzux", 0x7CC4286E, PPC_OP_LWZUX, F_RD|F_RA|F_RB, .rD=6,  .rA=4, .rB=5 },
    { "lbzx",  0x7CE428AE, PPC_OP_LBZX,  F_RD|F_RA|F_RB, .rD=7,  .rA=4, .rB=5 },
    { "lbzux", 0x7D0428EE, PPC_OP_LBZUX, F_RD|F_RA|F_RB, .rD=8,  .rA=4, .rB=5 },
    { "lhzx",  0x7D242A2E, PPC_OP_LHZX,  F_RD|F_RA|F_RB, .rD=9,  .rA=4, .rB=5 },
    { "lhzux", 0x7D442A6E, PPC_OP_LHZUX, F_RD|F_RA|F_RB, .rD=10, .rA=4, .rB=5 },
    { "lhax",  0x7D642AAE, PPC_OP_LHAX,  F_RD|F_RA|F_RB, .rD=11, .rA=4, .rB=5 },
    { "lhaux", 0x7D842AEE, PPC_OP_LHAUX, F_RD|F_RA|F_RB, .rD=12, .rA=4, .rB=5 },
    { "lwbrx", 0x7C642C2C, PPC_OP_LWBRX, F_RD|F_RA|F_RB, .rD=3,  .rA=4,  .rB=5 },
    { "lhbrx", 0x7CC7462C, PPC_OP_LHBRX, F_RD|F_RA|F_RB, .rD=6,  .rA=7,  .rB=8 },
    { "stwx",  0x7C64292E, PPC_OP_STWX,  F_RS|F_RA|F_RB, .rS=3,  .rA=4, .rB=5 },
    { "stwux", 0x7CC4296E, PPC_OP_STWUX, F_RS|F_RA|F_RB, .rS=6,  .rA=4, .rB=5 },
    { "stbx",  0x7CE429AE, PPC_OP_STBX,  F_RS|F_RA|F_RB, .rS=7,  .rA=4, .rB=5 },
    { "stbux", 0x7D0429EE, PPC_OP_STBUX, F_RS|F_RA|F_RB, .rS=8,  .rA=4, .rB=5 },
    { "sthx",  0x7D242B2E, PPC_OP_STHX,  F_RS|F_RA|F_RB, .rS=9,  .rA=4, .rB=5 },
    { "sthux", 0x7D442B6E, PPC_OP_STHUX, F_RS|F_RA|F_RB, .rS=10, .rA=4, .rB=5 },
    { "stwbrx", 0x7D2A5D2C, PPC_OP_STWBRX, F_RS|F_RA|F_RB, .rS=9,  .rA=10, .rB=11 },
    { "sthbrx", 0x7D8D772C, PPC_OP_STHBRX, F_RS|F_RA|F_RB, .rS=12, .rA=13, .rB=14 },
    { "dcbz",   0x7C0F87EC, PPC_OP_DCBZ,   F_RA|F_RB,      .rA=15, .rB=16 },

    { "lfs",    0xC0240000, PPC_OP_LFS,    F_RD|F_RA|F_SIMM, .rD=1,  .rA=4, .simm=0 },
    { "lfsu",   0xC4440004, PPC_OP_LFSU,   F_RD|F_RA|F_SIMM, .rD=2,  .rA=4, .simm=4 },
    { "lfd",    0xC8640008, PPC_OP_LFD,    F_RD|F_RA|F_SIMM, .rD=3,  .rA=4, .simm=8 },
    { "lfdu",   0xCC840010, PPC_OP_LFDU,   F_RD|F_RA|F_SIMM, .rD=4,  .rA=4, .simm=16 },
    { "stfs",   0xD0A40014, PPC_OP_STFS,   F_RS|F_RA|F_SIMM, .rS=5,  .rA=4, .simm=20 },
    { "stfsu",  0xD4C40018, PPC_OP_STFSU,  F_RS|F_RA|F_SIMM, .rS=6,  .rA=4, .simm=24 },
    { "stfd",   0xD8E40020, PPC_OP_STFD,   F_RS|F_RA|F_SIMM, .rS=7,  .rA=4, .simm=32 },
    { "stfdu",  0xDD040028, PPC_OP_STFDU,  F_RS|F_RA|F_SIMM, .rS=8,  .rA=4, .simm=40 },
    { "lfsx",   0x7D242C2E, PPC_OP_LFSX,   F_RD|F_RA|F_RB, .rD=9,  .rA=4, .rB=5 },
    { "lfsux",  0x7D442C6E, PPC_OP_LFSUX,  F_RD|F_RA|F_RB, .rD=10, .rA=4, .rB=5 },
    { "lfdx",   0x7D642CAE, PPC_OP_LFDX,   F_RD|F_RA|F_RB, .rD=11, .rA=4, .rB=5 },
    { "lfdux",  0x7D842CEE, PPC_OP_LFDUX,  F_RD|F_RA|F_RB, .rD=12, .rA=4, .rB=5 },
    { "stfsx",  0x7DA42D2E, PPC_OP_STFSX,  F_RS|F_RA|F_RB, .rS=13, .rA=4, .rB=5 },
    { "stfsux", 0x7DC42D6E, PPC_OP_STFSUX, F_RS|F_RA|F_RB, .rS=14, .rA=4, .rB=5 },
    { "stfdx",  0x7DE42DAE, PPC_OP_STFDX,  F_RS|F_RA|F_RB, .rS=15, .rA=4, .rB=5 },
    { "stfdux", 0x7E042DEE, PPC_OP_STFDUX, F_RS|F_RA|F_RB, .rS=16, .rA=4, .rB=5 },

    { "fadds",  0xEC22182A, PPC_OP_FADDS, F_RD|F_RA|F_RB, .rD=1,  .rA=2,  .rB=3 },
    { "fsubs",  0xEC853028, PPC_OP_FSUBS, F_RD|F_RA|F_RB, .rD=4,  .rA=5,  .rB=6 },
    { "fmuls",  0xECE80272, PPC_OP_FMULS, F_RD|F_RA|F_RC_REG, .rD=7,  .rA=8,  .rC=9 },
    { "fdivs",  0xED4B6024, PPC_OP_FDIVS, F_RD|F_RA|F_RB, .rD=10, .rA=11, .rB=12 },
    { "fadd",   0xFDAE782A, PPC_OP_FADD,  F_RD|F_RA|F_RB, .rD=13, .rA=14, .rB=15 },
    { "fsub",   0xFE119028, PPC_OP_FSUB,  F_RD|F_RA|F_RB, .rD=16, .rA=17, .rB=18 },
    { "fmul",   0xFE740572, PPC_OP_FMUL,  F_RD|F_RA|F_RC_REG, .rD=19, .rA=20, .rC=21 },
    { "fdiv",   0xFED7C024, PPC_OP_FDIV,  F_RD|F_RA|F_RB, .rD=22, .rA=23, .rB=24 },
    { "fmr",    0xFF20D090, PPC_OP_FMR,   F_RD|F_RB, .rD=25, .rB=26 },
    { "fneg",   0xFF60E050, PPC_OP_FNEG,  F_RD|F_RB, .rD=27, .rB=28 },
    { "fabs",   0xFFA0F210, PPC_OP_FABS,  F_RD|F_RB, .rD=29, .rB=30 },
    { "fnabs",  0xFFE00110, PPC_OP_FNABS, F_RD|F_RB, .rD=31, .rB=0 },
    { "frsp",   0xFC201018, PPC_OP_FRSP,  F_RD|F_RB, .rD=1,  .rB=2 },
    { "fcmpu",  0xFD032000, PPC_OP_FCMPU, F_CRF|F_RA|F_RB, .crfD=2, .rA=3, .rB=4 },
    { "fcmpo",  0xFD853040, PPC_OP_FCMPO, F_CRF|F_RA|F_RB, .crfD=3, .rA=5, .rB=6 },

    { "psq_l",   0xE0240000, PPC_OP_PSQ_L,   F_RD|F_RA|F_SIMM|F_W|F_I, .rD=1,  .rA=4, .simm=0,  .w=0, .i=0 },
    { "psq_lu",  0xE4640008, PPC_OP_PSQ_LU,  F_RD|F_RA|F_SIMM|F_W|F_I, .rD=3,  .rA=4, .simm=8,  .w=0, .i=0 },
    { "psq_st",  0xF0A40010, PPC_OP_PSQ_ST,  F_RS|F_RA|F_SIMM|F_W|F_I, .rS=5,  .rA=4, .simm=16, .w=0, .i=0 },
    { "psq_stu", 0xF4E40018, PPC_OP_PSQ_STU, F_RS|F_RA|F_SIMM|F_W|F_I, .rS=7,  .rA=4, .simm=24, .w=0, .i=0 },
    { "psq_lx",  0x1124280C, PPC_OP_PSQ_LX,  F_RD|F_RA|F_RB|F_W|F_I, .rD=9,  .rA=4, .rB=5, .w=0, .i=0 },
    { "psq_lux", 0x1164284C, PPC_OP_PSQ_LUX, F_RD|F_RA|F_RB|F_W|F_I, .rD=11, .rA=4, .rB=5, .w=0, .i=0 },
    { "psq_stx", 0x11A4280E, PPC_OP_PSQ_STX, F_RS|F_RA|F_RB|F_W|F_I, .rS=13, .rA=4, .rB=5, .w=0, .i=0 },
    { "psq_stux",0x11E4284E, PPC_OP_PSQ_STUX,F_RS|F_RA|F_RB|F_W|F_I, .rS=15, .rA=4, .rB=5, .w=0, .i=0 },

    { "mullw",  0x7C6429D6, PPC_OP_MULLW,  F_RD|F_RA|F_RB, .rD=3,  .rA=4,  .rB=5 },
    { "mulhw",  0x7CC74096, PPC_OP_MULHW,  F_RD|F_RA|F_RB, .rD=6,  .rA=7,  .rB=8 },
    { "mulhwu", 0x7D2A5816, PPC_OP_MULHWU, F_RD|F_RA|F_RB, .rD=9,  .rA=10, .rB=11 },
    { "divw",   0x7D8D73D6, PPC_OP_DIVW,   F_RD|F_RA|F_RB, .rD=12, .rA=13, .rB=14 },
    { "divwu",  0x7DF08B96, PPC_OP_DIVWU,  F_RD|F_RA|F_RB, .rD=15, .rA=16, .rB=17 },
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

    check((PPC_OP_COUNT - 1) == 139, -1, "opcode count", PPC_OP_COUNT - 1, 139);

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
        if (c->fields & F_RB)     check(inst.rB == c->rB, n, "rB", inst.rB, c->rB);
        if (c->fields & F_RC_REG) check(inst.rC == c->rC, n, "rC", inst.rC, c->rC);
        if (c->fields & F_CRF)    check(inst.crfD == c->crfD, n, "crfD", inst.crfD, c->crfD);
        if (c->fields & F_CRFS)   check(inst.crfS == c->crfS, n, "crfS", inst.crfS, c->crfS);
        if (c->fields & F_CRM)    check(inst.crm == c->crm, n, "crm", inst.crm, c->crm);
        if (c->fields & F_L)      check(inst.l == c->l, n, "l", inst.l, c->l);
        if (c->fields & F_W)      check(inst.w == c->w, n, "w", inst.w, c->w);
        if (c->fields & F_I)      check(inst.i == c->i, n, "i", inst.i, c->i);
        if (c->fields & F_BO)     check(inst.bo == c->bo, n, "bo", inst.bo, c->bo);
        if (c->fields & F_BI)     check(inst.bi == c->bi, n, "bi", inst.bi, c->bi);
        if (c->fields & F_MB)     check(inst.mb == c->mb, n, "mb", inst.mb, c->mb);
        if (c->fields & F_ME)     check(inst.me == c->me, n, "me", inst.me, c->me);
        if (c->fields & F_SH)     check(inst.sh == c->sh, n, "sh", inst.sh, c->sh);
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
