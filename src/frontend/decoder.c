#include "decoder.h"
#include <stdio.h>
#include <string.h>

// Field extraction from a host-endian 32-bit instruction word.
#define PPC_PRIMARY(raw)   ((raw) >> 26)
#define PPC_RD(raw)        (((raw) >> 21) & 0x1F)
#define PPC_RS(raw)        (((raw) >> 21) & 0x1F)
#define PPC_RA(raw)        (((raw) >> 16) & 0x1F)
#define PPC_RB(raw)        (((raw) >> 11) & 0x1F)
#define PPC_RC_REG(raw)    (((raw) >> 6) & 0x1F)
#define PPC_BO(raw)        (((raw) >> 21) & 0x1F)
#define PPC_BI(raw)        (((raw) >> 16) & 0x1F)
#define PPC_XO(raw)        (((raw) >> 1) & 0x3FF)
#define PPC_A_XO(raw)      (((raw) >> 1) & 0x1F)
#define PPC_SPR(raw)       ((((raw) >> 16) & 0x1F) | (((raw) >> 6) & 0x3E0))
#define PPC_CRM(raw)       (((raw) >> 12) & 0xFF)
#define PPC_MB(raw)        (((raw) >> 6) & 0x1F)
#define PPC_ME(raw)        (((raw) >> 1) & 0x1F)
#define PPC_SH(raw)        (((raw) >> 11) & 0x1F)
#define PPC_RC(raw)        (((raw) & 1) != 0)
#define PPC_SIMM(raw)      ((s16)((raw) & 0xFFFF))
#define PPC_UIMM(raw)      ((u16)((raw) & 0xFFFF))

static void decode_d_rt_ra(PPCInst* inst, PPCOpcode op, u32 raw) {
    inst->op = op;
    inst->rD = PPC_RD(raw);
    inst->rA = PPC_RA(raw);
    inst->simm = PPC_SIMM(raw);
}

static void decode_d_rs_ra(PPCInst* inst, PPCOpcode op, u32 raw) {
    inst->op = op;
    inst->rS = PPC_RS(raw);
    inst->rA = PPC_RA(raw);
    inst->simm = PPC_SIMM(raw);
}

static void decode_x_rt_ra_rb(PPCInst* inst, PPCOpcode op, u32 raw) {
    inst->op = op;
    inst->rD = PPC_RD(raw);
    inst->rA = PPC_RA(raw);
    inst->rB = PPC_RB(raw);
    inst->rc = PPC_RC(raw);
}

static void decode_x_rs_ra_rb(PPCInst* inst, PPCOpcode op, u32 raw) {
    inst->op = op;
    inst->rS = PPC_RS(raw);
    inst->rA = PPC_RA(raw);
    inst->rB = PPC_RB(raw);
    inst->rc = PPC_RC(raw);
}

static void decode_a_frt_fra_frb_frc(PPCInst* inst, PPCOpcode op, u32 raw) {
    inst->op = op;
    inst->rD = PPC_RD(raw);
    inst->rA = PPC_RA(raw);
    inst->rB = PPC_RB(raw);
    inst->rC = PPC_RC_REG(raw);
    inst->rc = PPC_RC(raw);
}

static void decode_x_frt_frb(PPCInst* inst, PPCOpcode op, u32 raw) {
    inst->op = op;
    inst->rD = PPC_RD(raw);
    inst->rB = PPC_RB(raw);
    inst->rc = PPC_RC(raw);
}

static void decode_fcmp(PPCInst* inst, PPCOpcode op, u32 raw) {
    inst->op = op;
    inst->crfD = (raw >> 23) & 0x7;
    inst->rA = PPC_RA(raw);
    inst->rB = PPC_RB(raw);
}

static void decode_psq_d_rt_ra(PPCInst* inst, PPCOpcode op, u32 raw) {
    inst->op = op;
    inst->rD = PPC_RD(raw);
    inst->rA = PPC_RA(raw);
    inst->w = (raw >> 15) & 1u;
    inst->i = (raw >> 12) & 7u;
    inst->simm = (s16)sign_extend(raw & 0x0FFFu, 12);
}

static void decode_psq_d_rs_ra(PPCInst* inst, PPCOpcode op, u32 raw) {
    inst->op = op;
    inst->rS = PPC_RS(raw);
    inst->rA = PPC_RA(raw);
    inst->w = (raw >> 15) & 1u;
    inst->i = (raw >> 12) & 7u;
    inst->simm = (s16)sign_extend(raw & 0x0FFFu, 12);
}

static void decode_psq_x_rt_ra_rb(PPCInst* inst, PPCOpcode op, u32 raw) {
    inst->op = op;
    inst->rD = PPC_RD(raw);
    inst->rA = PPC_RA(raw);
    inst->rB = PPC_RB(raw);
    inst->w = (raw >> 10) & 1u;
    inst->i = (raw >> 7) & 7u;
}

static void decode_psq_x_rs_ra_rb(PPCInst* inst, PPCOpcode op, u32 raw) {
    inst->op = op;
    inst->rS = PPC_RS(raw);
    inst->rA = PPC_RA(raw);
    inst->rB = PPC_RB(raw);
    inst->w = (raw >> 10) & 1u;
    inst->i = (raw >> 7) & 7u;
}

PPCInst ppc_decode(u32 raw, u32 address) {
    PPCInst inst;
    memset(&inst, 0, sizeof(inst));
    inst.raw     = raw;
    inst.address = address;

    switch (PPC_PRIMARY(raw)) {
    case 4: {
        u32 psxo = PPC_XO(raw) & 0x3Fu;
        switch (psxo) {
        case 6:  decode_psq_x_rt_ra_rb(&inst, PPC_OP_PSQ_LX, raw); break;
        case 7:  decode_psq_x_rs_ra_rb(&inst, PPC_OP_PSQ_STX, raw); break;
        case 38: decode_psq_x_rt_ra_rb(&inst, PPC_OP_PSQ_LUX, raw); break;
        case 39: decode_psq_x_rs_ra_rb(&inst, PPC_OP_PSQ_STUX, raw); break;
        default: inst.op = PPC_OP_UNKNOWN; break;
        }
        break;
    }

    case 7: // mulli
        decode_d_rt_ra(&inst, PPC_OP_MULLI, raw);
        break;

    case 8: // subfic
        decode_d_rt_ra(&inst, PPC_OP_SUBFIC, raw);
        break;

    case 10: // cmpli/cmplwi
        inst.op   = PPC_OP_CMPLI;
        inst.crfD = (raw >> 23) & 0x7;
        inst.l    = (raw >> 21) & 0x1;
        inst.rA   = PPC_RA(raw);
        inst.uimm = PPC_UIMM(raw);
        break;

    case 11: // cmpi/cmpwi
        inst.op   = PPC_OP_CMPI;
        inst.crfD = (raw >> 23) & 0x7;
        inst.l    = (raw >> 21) & 0x1;
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 12: // addic
        decode_d_rt_ra(&inst, PPC_OP_ADDIC, raw);
        break;

    case 13: // addic.
        decode_d_rt_ra(&inst, PPC_OP_ADDIC_DOT, raw);
        inst.rc = true;
        break;

    case 14: // addi, with rA=0 using literal zero
        decode_d_rt_ra(&inst, PPC_OP_ADDI, raw);
        break;

    case 15: // addis, with rA=0 using literal zero
        decode_d_rt_ra(&inst, PPC_OP_ADDIS, raw);
        break;

    case 16: { // bc/bcl/bca/bcla
        inst.op = PPC_OP_BC;
        inst.bo = PPC_BO(raw);
        inst.bi = PPC_BI(raw);
        inst.aa = (raw >> 1) & 1;
        inst.lk = raw & 1;

        s32 displacement = sign_extend(raw & 0x0000FFFC, 16);
        inst.branch_target = inst.aa
            ? (u32)displacement
            : address + (u32)displacement;
        break;
    }

    case 18: { // b/bl/ba/bla
        inst.op = PPC_OP_B;
        inst.aa = (raw >> 1) & 1;
        inst.lk = raw & 1;

        s32 displacement = sign_extend(raw & 0x03FFFFFC, 26);
        inst.branch_target = inst.aa
            ? (u32)displacement
            : address + (u32)displacement;
        break;
    }

    case 19: {
        u32 xo = PPC_XO(raw);
        if (xo == 0) {
            inst.op   = PPC_OP_MCRF;
            inst.crfD = (raw >> 23) & 0x7;
            inst.crfS = (raw >> 18) & 0x7;
        } else if (xo == 16) {
            inst.op = PPC_OP_BCLR;
            inst.bo = PPC_BO(raw);
            inst.bi = PPC_BI(raw);
            inst.lk = raw & 1;
        } else if (xo == 33) {
            inst.op = PPC_OP_CRNOR;
            inst.rD = PPC_RD(raw);
            inst.rA = PPC_RA(raw);
            inst.rB = PPC_RB(raw);
        } else if (xo == 129) {
            inst.op = PPC_OP_CRANDC;
            inst.rD = PPC_RD(raw);
            inst.rA = PPC_RA(raw);
            inst.rB = PPC_RB(raw);
        } else if (xo == 193) {
            inst.op = PPC_OP_CRXOR;
            inst.rD = PPC_RD(raw);
            inst.rA = PPC_RA(raw);
            inst.rB = PPC_RB(raw);
        } else if (xo == 225) {
            inst.op = PPC_OP_CRNAND;
            inst.rD = PPC_RD(raw);
            inst.rA = PPC_RA(raw);
            inst.rB = PPC_RB(raw);
        } else if (xo == 257) {
            inst.op = PPC_OP_CRAND;
            inst.rD = PPC_RD(raw);
            inst.rA = PPC_RA(raw);
            inst.rB = PPC_RB(raw);
        } else if (xo == 289) {
            inst.op = PPC_OP_CREQV;
            inst.rD = PPC_RD(raw);
            inst.rA = PPC_RA(raw);
            inst.rB = PPC_RB(raw);
        } else if (xo == 417) {
            inst.op = PPC_OP_CRORC;
            inst.rD = PPC_RD(raw);
            inst.rA = PPC_RA(raw);
            inst.rB = PPC_RB(raw);
        } else if (xo == 449) {
            inst.op = PPC_OP_CROR;
            inst.rD = PPC_RD(raw);
            inst.rA = PPC_RA(raw);
            inst.rB = PPC_RB(raw);
        } else if (xo == 528) {
            inst.op = PPC_OP_BCCTR;
            inst.bo = PPC_BO(raw);
            inst.bi = PPC_BI(raw);
            inst.lk = raw & 1;
        } else {
            inst.op = PPC_OP_UNKNOWN;
        }
        break;
    }

    case 20: // rlwimi
        inst.op = PPC_OP_RLWIMI;
        inst.rS = PPC_RS(raw);
        inst.rA = PPC_RA(raw);
        inst.sh = PPC_SH(raw);
        inst.mb = PPC_MB(raw);
        inst.me = PPC_ME(raw);
        inst.rc = PPC_RC(raw);
        break;

    case 21: // rlwinm
        inst.op = PPC_OP_RLWINM;
        inst.rS = PPC_RS(raw);
        inst.rA = PPC_RA(raw);
        inst.sh = PPC_SH(raw);
        inst.mb = PPC_MB(raw);
        inst.me = PPC_ME(raw);
        inst.rc = PPC_RC(raw);
        break;

    case 23: // rlwnm
        inst.op = PPC_OP_RLWNM;
        inst.rS = PPC_RS(raw);
        inst.rA = PPC_RA(raw);
        inst.rB = PPC_RB(raw);
        inst.mb = PPC_MB(raw);
        inst.me = PPC_ME(raw);
        inst.rc = PPC_RC(raw);
        break;

    case 24: // ori, with ori r0,r0,0 serving as nop
        inst.op   = PPC_OP_ORI;
        inst.rS   = PPC_RS(raw);
        inst.rA   = PPC_RA(raw);
        inst.uimm = PPC_UIMM(raw);
        break;

    case 25: // oris
        inst.op   = PPC_OP_ORIS;
        inst.rS   = PPC_RS(raw);
        inst.rA   = PPC_RA(raw);
        inst.uimm = PPC_UIMM(raw);
        break;

    case 26: // xori
        inst.op   = PPC_OP_XORI;
        inst.rS   = PPC_RS(raw);
        inst.rA   = PPC_RA(raw);
        inst.uimm = PPC_UIMM(raw);
        break;

    case 27: // xoris
        inst.op   = PPC_OP_XORIS;
        inst.rS   = PPC_RS(raw);
        inst.rA   = PPC_RA(raw);
        inst.uimm = PPC_UIMM(raw);
        break;

    case 28: // andi.
        inst.op   = PPC_OP_ANDI;
        inst.rS   = PPC_RS(raw);
        inst.rA   = PPC_RA(raw);
        inst.uimm = PPC_UIMM(raw);
        inst.rc   = true;
        break;

    case 29: // andis.
        inst.op   = PPC_OP_ANDIS;
        inst.rS   = PPC_RS(raw);
        inst.rA   = PPC_RA(raw);
        inst.uimm = PPC_UIMM(raw);
        inst.rc   = true;
        break;

    case 31: {
        u32 xo = PPC_XO(raw);
        switch (xo) {
        case 0: // cmp/cmpw
            inst.op   = PPC_OP_CMP;
            inst.crfD = (raw >> 23) & 0x7;
            inst.l    = (raw >> 21) & 0x1;
            inst.rA   = PPC_RA(raw);
            inst.rB   = PPC_RB(raw);
            break;
        case 11:  decode_x_rt_ra_rb(&inst, PPC_OP_MULHWU, raw); break;
        case 8:   decode_x_rt_ra_rb(&inst, PPC_OP_SUBFC, raw); break;
        case 10:  decode_x_rt_ra_rb(&inst, PPC_OP_ADDC, raw); break;
        case 19:
            inst.op = PPC_OP_MFCR;
            inst.rD = PPC_RD(raw);
            break;
        case 23:  decode_x_rt_ra_rb(&inst, PPC_OP_LWZX, raw); break;
        case 24:  decode_x_rs_ra_rb(&inst, PPC_OP_SLW, raw); break;
        case 26:  decode_x_rs_ra_rb(&inst, PPC_OP_CNTLZW, raw); break;
        case 28:  decode_x_rs_ra_rb(&inst, PPC_OP_AND, raw); break;
        case 32: // cmpl/cmplw
            inst.op   = PPC_OP_CMPL;
            inst.crfD = (raw >> 23) & 0x7;
            inst.l    = (raw >> 21) & 0x1;
            inst.rA   = PPC_RA(raw);
            inst.rB   = PPC_RB(raw);
            break;
        case 40:  decode_x_rt_ra_rb(&inst, PPC_OP_SUBF, raw); break;
        case 55:  decode_x_rt_ra_rb(&inst, PPC_OP_LWZUX, raw); break;
        case 60:  decode_x_rs_ra_rb(&inst, PPC_OP_ANDC, raw); break;
        case 75:  decode_x_rt_ra_rb(&inst, PPC_OP_MULHW, raw); break;
        case 87:  decode_x_rt_ra_rb(&inst, PPC_OP_LBZX, raw); break;
        case 104: decode_x_rt_ra_rb(&inst, PPC_OP_NEG, raw); break;
        case 119: decode_x_rt_ra_rb(&inst, PPC_OP_LBZUX, raw); break;
        case 124: decode_x_rs_ra_rb(&inst, PPC_OP_NOR, raw); break;
        case 136: decode_x_rt_ra_rb(&inst, PPC_OP_SUBFE, raw); break;
        case 138: decode_x_rt_ra_rb(&inst, PPC_OP_ADDE, raw); break;
        case 144:
            inst.op  = PPC_OP_MTCRF;
            inst.rS  = PPC_RS(raw);
            inst.crm = PPC_CRM(raw);
            break;
        case 151: decode_x_rs_ra_rb(&inst, PPC_OP_STWX, raw); break;
        case 183: decode_x_rs_ra_rb(&inst, PPC_OP_STWUX, raw); break;
        case 200: decode_x_rt_ra_rb(&inst, PPC_OP_SUBFZE, raw); break;
        case 202: decode_x_rt_ra_rb(&inst, PPC_OP_ADDZE, raw); break;
        case 215: decode_x_rs_ra_rb(&inst, PPC_OP_STBX, raw); break;
        case 235: decode_x_rt_ra_rb(&inst, PPC_OP_MULLW, raw); break;
        case 247: decode_x_rs_ra_rb(&inst, PPC_OP_STBUX, raw); break;
        case 266: decode_x_rt_ra_rb(&inst, PPC_OP_ADD, raw); break;
        case 279: decode_x_rt_ra_rb(&inst, PPC_OP_LHZX, raw); break;
        case 284: decode_x_rs_ra_rb(&inst, PPC_OP_EQV, raw); break;
        case 311: decode_x_rt_ra_rb(&inst, PPC_OP_LHZUX, raw); break;
        case 316: decode_x_rs_ra_rb(&inst, PPC_OP_XOR, raw); break;
        case 339:
            inst.op  = PPC_OP_MFSPR;
            inst.rD  = PPC_RD(raw);
            inst.spr = PPC_SPR(raw);
            break;
        case 343: decode_x_rt_ra_rb(&inst, PPC_OP_LHAX, raw); break;
        case 375: decode_x_rt_ra_rb(&inst, PPC_OP_LHAUX, raw); break;
        case 407: decode_x_rs_ra_rb(&inst, PPC_OP_STHX, raw); break;
        case 412: decode_x_rs_ra_rb(&inst, PPC_OP_ORC, raw); break;
        case 439: decode_x_rs_ra_rb(&inst, PPC_OP_STHUX, raw); break;
        case 444: decode_x_rs_ra_rb(&inst, PPC_OP_OR, raw); break;
        case 459: decode_x_rt_ra_rb(&inst, PPC_OP_DIVWU, raw); break;
        case 467:
            inst.op  = PPC_OP_MTSPR;
            inst.rS  = PPC_RS(raw);
            inst.spr = PPC_SPR(raw);
            break;
        case 476: decode_x_rs_ra_rb(&inst, PPC_OP_NAND, raw); break;
        case 491: decode_x_rt_ra_rb(&inst, PPC_OP_DIVW, raw); break;
        case 534: decode_x_rt_ra_rb(&inst, PPC_OP_LWBRX, raw); break;
        case 535: decode_x_rt_ra_rb(&inst, PPC_OP_LFSX, raw); break;
        case 536: decode_x_rs_ra_rb(&inst, PPC_OP_SRW, raw); break;
        case 567: decode_x_rt_ra_rb(&inst, PPC_OP_LFSUX, raw); break;
        case 599: decode_x_rt_ra_rb(&inst, PPC_OP_LFDX, raw); break;
        case 631: decode_x_rt_ra_rb(&inst, PPC_OP_LFDUX, raw); break;
        case 662: decode_x_rs_ra_rb(&inst, PPC_OP_STWBRX, raw); break;
        case 663: decode_x_rs_ra_rb(&inst, PPC_OP_STFSX, raw); break;
        case 695: decode_x_rs_ra_rb(&inst, PPC_OP_STFSUX, raw); break;
        case 727: decode_x_rs_ra_rb(&inst, PPC_OP_STFDX, raw); break;
        case 759: decode_x_rs_ra_rb(&inst, PPC_OP_STFDUX, raw); break;
        case 790: decode_x_rt_ra_rb(&inst, PPC_OP_LHBRX, raw); break;
        case 792: decode_x_rs_ra_rb(&inst, PPC_OP_SRAW, raw); break;
        case 824:
            inst.op = PPC_OP_SRAWI;
            inst.rS = PPC_RS(raw);
            inst.rA = PPC_RA(raw);
            inst.sh = PPC_SH(raw);
            inst.rc = PPC_RC(raw);
            break;
        case 918: decode_x_rs_ra_rb(&inst, PPC_OP_STHBRX, raw); break;
        case 922: decode_x_rs_ra_rb(&inst, PPC_OP_EXTSH, raw); break;
        case 954: decode_x_rs_ra_rb(&inst, PPC_OP_EXTSB, raw); break;
        case 1014:
            inst.op = PPC_OP_DCBZ;
            inst.rA = PPC_RA(raw);
            inst.rB = PPC_RB(raw);
            break;
        default:
            inst.op = PPC_OP_UNKNOWN;
            break;
        }
        break;
    }

    case 32: decode_d_rt_ra(&inst, PPC_OP_LWZ, raw); break;
    case 33: decode_d_rt_ra(&inst, PPC_OP_LWZU, raw); break;
    case 34: decode_d_rt_ra(&inst, PPC_OP_LBZ, raw); break;
    case 35: decode_d_rt_ra(&inst, PPC_OP_LBZU, raw); break;
    case 36: decode_d_rs_ra(&inst, PPC_OP_STW, raw); break;
    case 37: decode_d_rs_ra(&inst, PPC_OP_STWU, raw); break;
    case 38: decode_d_rs_ra(&inst, PPC_OP_STB, raw); break;
    case 39: decode_d_rs_ra(&inst, PPC_OP_STBU, raw); break;
    case 40: decode_d_rt_ra(&inst, PPC_OP_LHZ, raw); break;
    case 41: decode_d_rt_ra(&inst, PPC_OP_LHZU, raw); break;
    case 42: decode_d_rt_ra(&inst, PPC_OP_LHA, raw); break;
    case 43: decode_d_rt_ra(&inst, PPC_OP_LHAU, raw); break;
    case 44: decode_d_rs_ra(&inst, PPC_OP_STH, raw); break;
    case 45: decode_d_rs_ra(&inst, PPC_OP_STHU, raw); break;
    case 46: decode_d_rt_ra(&inst, PPC_OP_LMW, raw); break;
    case 47: decode_d_rs_ra(&inst, PPC_OP_STMW, raw); break;
    case 48: decode_d_rt_ra(&inst, PPC_OP_LFS, raw); break;
    case 49: decode_d_rt_ra(&inst, PPC_OP_LFSU, raw); break;
    case 50: decode_d_rt_ra(&inst, PPC_OP_LFD, raw); break;
    case 51: decode_d_rt_ra(&inst, PPC_OP_LFDU, raw); break;
    case 52: decode_d_rs_ra(&inst, PPC_OP_STFS, raw); break;
    case 53: decode_d_rs_ra(&inst, PPC_OP_STFSU, raw); break;
    case 54: decode_d_rs_ra(&inst, PPC_OP_STFD, raw); break;
    case 55: decode_d_rs_ra(&inst, PPC_OP_STFDU, raw); break;
    case 56: decode_psq_d_rt_ra(&inst, PPC_OP_PSQ_L, raw); break;
    case 57: decode_psq_d_rt_ra(&inst, PPC_OP_PSQ_LU, raw); break;
    case 60: decode_psq_d_rs_ra(&inst, PPC_OP_PSQ_ST, raw); break;
    case 61: decode_psq_d_rs_ra(&inst, PPC_OP_PSQ_STU, raw); break;

    case 59:
        switch (PPC_A_XO(raw)) {
        case 18: decode_a_frt_fra_frb_frc(&inst, PPC_OP_FDIVS, raw); break;
        case 20: decode_a_frt_fra_frb_frc(&inst, PPC_OP_FSUBS, raw); break;
        case 21: decode_a_frt_fra_frb_frc(&inst, PPC_OP_FADDS, raw); break;
        case 25: decode_a_frt_fra_frb_frc(&inst, PPC_OP_FMULS, raw); break;
        default: inst.op = PPC_OP_UNKNOWN; break;
        }
        break;

    case 63:
        switch (PPC_XO(raw)) {
        case 0:   decode_fcmp(&inst, PPC_OP_FCMPU, raw); break;
        case 12:  decode_x_frt_frb(&inst, PPC_OP_FRSP, raw); break;
        case 32:  decode_fcmp(&inst, PPC_OP_FCMPO, raw); break;
        case 40:  decode_x_frt_frb(&inst, PPC_OP_FNEG, raw); break;
        case 72:  decode_x_frt_frb(&inst, PPC_OP_FMR, raw); break;
        case 136: decode_x_frt_frb(&inst, PPC_OP_FNABS, raw); break;
        case 264: decode_x_frt_frb(&inst, PPC_OP_FABS, raw); break;
        default:
            switch (PPC_A_XO(raw)) {
            case 18: decode_a_frt_fra_frb_frc(&inst, PPC_OP_FDIV, raw); break;
            case 20: decode_a_frt_fra_frb_frc(&inst, PPC_OP_FSUB, raw); break;
            case 21: decode_a_frt_fra_frb_frc(&inst, PPC_OP_FADD, raw); break;
            case 25: decode_a_frt_fra_frb_frc(&inst, PPC_OP_FMUL, raw); break;
            default: inst.op = PPC_OP_UNKNOWN; break;
            }
            break;
        }
        break;

    default:
        inst.op = PPC_OP_UNKNOWN;
        break;
    }

    return inst;
}

static const char* opcode_names[PPC_OP_COUNT] = {
    [PPC_OP_UNKNOWN] = "???",
    [PPC_OP_MULLI]   = "mulli",
    [PPC_OP_SUBFIC]  = "subfic",
    [PPC_OP_ADDI]    = "addi",
    [PPC_OP_ADDIC]   = "addic",
    [PPC_OP_ADDIC_DOT] = "addic.",
    [PPC_OP_ADDIS]   = "addis",
    [PPC_OP_CMPI]    = "cmpi",
    [PPC_OP_CMPLI]   = "cmpli",
    [PPC_OP_ORI]     = "ori",
    [PPC_OP_ORIS]    = "oris",
    [PPC_OP_XORI]    = "xori",
    [PPC_OP_XORIS]   = "xoris",
    [PPC_OP_ANDI]    = "andi.",
    [PPC_OP_ANDIS]   = "andis.",
    [PPC_OP_LWZ]     = "lwz",
    [PPC_OP_LWZU]    = "lwzu",
    [PPC_OP_LBZ]     = "lbz",
    [PPC_OP_LBZU]    = "lbzu",
    [PPC_OP_STW]     = "stw",
    [PPC_OP_STWU]    = "stwu",
    [PPC_OP_STB]     = "stb",
    [PPC_OP_STBU]    = "stbu",
    [PPC_OP_LHZ]     = "lhz",
    [PPC_OP_LHZU]    = "lhzu",
    [PPC_OP_LHA]     = "lha",
    [PPC_OP_LHAU]    = "lhau",
    [PPC_OP_STH]     = "sth",
    [PPC_OP_STHU]    = "sthu",
    [PPC_OP_LMW]     = "lmw",
    [PPC_OP_STMW]    = "stmw",
    [PPC_OP_B]       = "b",
    [PPC_OP_BC]      = "bc",
    [PPC_OP_BCLR]    = "bclr",
    [PPC_OP_BCCTR]   = "bcctr",
    [PPC_OP_CRAND]   = "crand",
    [PPC_OP_CRANDC]  = "crandc",
    [PPC_OP_CREQV]   = "creqv",
    [PPC_OP_CRNAND]  = "crnand",
    [PPC_OP_CRNOR]   = "crnor",
    [PPC_OP_CROR]    = "cror",
    [PPC_OP_CRORC]   = "crorc",
    [PPC_OP_CRXOR]   = "crxor",
    [PPC_OP_MCRF]    = "mcrf",
    [PPC_OP_MFCR]    = "mfcr",
    [PPC_OP_MTCRF]   = "mtcrf",
    [PPC_OP_MFSPR]   = "mfspr",
    [PPC_OP_MTSPR]   = "mtspr",
    [PPC_OP_CMP]     = "cmp",
    [PPC_OP_CMPL]    = "cmpl",
    [PPC_OP_ADD]     = "add",
    [PPC_OP_ADDC]    = "addc",
    [PPC_OP_ADDE]    = "adde",
    [PPC_OP_ADDZE]   = "addze",
    [PPC_OP_SUBF]    = "subf",
    [PPC_OP_SUBFC]   = "subfc",
    [PPC_OP_SUBFE]   = "subfe",
    [PPC_OP_SUBFZE]  = "subfze",
    [PPC_OP_NEG]     = "neg",
    [PPC_OP_AND]     = "and",
    [PPC_OP_ANDC]    = "andc",
    [PPC_OP_OR]      = "or",
    [PPC_OP_ORC]     = "orc",
    [PPC_OP_XOR]     = "xor",
    [PPC_OP_NAND]    = "nand",
    [PPC_OP_NOR]     = "nor",
    [PPC_OP_EQV]     = "eqv",
    [PPC_OP_CNTLZW]  = "cntlzw",
    [PPC_OP_EXTSB]   = "extsb",
    [PPC_OP_EXTSH]   = "extsh",
    [PPC_OP_SLW]     = "slw",
    [PPC_OP_SRW]     = "srw",
    [PPC_OP_SRAW]    = "sraw",
    [PPC_OP_SRAWI]   = "srawi",
    [PPC_OP_RLWINM]  = "rlwinm",
    [PPC_OP_RLWNM]   = "rlwnm",
    [PPC_OP_RLWIMI]  = "rlwimi",
    [PPC_OP_LWZX]    = "lwzx",
    [PPC_OP_LWZUX]   = "lwzux",
    [PPC_OP_LBZX]    = "lbzx",
    [PPC_OP_LBZUX]   = "lbzux",
    [PPC_OP_LHZX]    = "lhzx",
    [PPC_OP_LHZUX]   = "lhzux",
    [PPC_OP_LHAX]    = "lhax",
    [PPC_OP_LHAUX]   = "lhaux",
    [PPC_OP_LWBRX]   = "lwbrx",
    [PPC_OP_LHBRX]   = "lhbrx",
    [PPC_OP_STWX]    = "stwx",
    [PPC_OP_STWUX]   = "stwux",
    [PPC_OP_STBX]    = "stbx",
    [PPC_OP_STBUX]   = "stbux",
    [PPC_OP_STHX]    = "sthx",
    [PPC_OP_STHUX]   = "sthux",
    [PPC_OP_STWBRX]  = "stwbrx",
    [PPC_OP_STHBRX]  = "sthbrx",
    [PPC_OP_LFS]     = "lfs",
    [PPC_OP_LFSU]    = "lfsu",
    [PPC_OP_LFD]     = "lfd",
    [PPC_OP_LFDU]    = "lfdu",
    [PPC_OP_STFS]    = "stfs",
    [PPC_OP_STFSU]   = "stfsu",
    [PPC_OP_STFD]    = "stfd",
    [PPC_OP_STFDU]   = "stfdu",
    [PPC_OP_LFSX]    = "lfsx",
    [PPC_OP_LFSUX]   = "lfsux",
    [PPC_OP_LFDX]    = "lfdx",
    [PPC_OP_LFDUX]   = "lfdux",
    [PPC_OP_STFSX]   = "stfsx",
    [PPC_OP_STFSUX]  = "stfsux",
    [PPC_OP_STFDX]   = "stfdx",
    [PPC_OP_STFDUX]  = "stfdux",
    [PPC_OP_FADDS]   = "fadds",
    [PPC_OP_FSUBS]   = "fsubs",
    [PPC_OP_FMULS]   = "fmuls",
    [PPC_OP_FDIVS]   = "fdivs",
    [PPC_OP_FADD]    = "fadd",
    [PPC_OP_FSUB]    = "fsub",
    [PPC_OP_FMUL]    = "fmul",
    [PPC_OP_FDIV]    = "fdiv",
    [PPC_OP_FMR]     = "fmr",
    [PPC_OP_FNEG]    = "fneg",
    [PPC_OP_FABS]    = "fabs",
    [PPC_OP_FNABS]   = "fnabs",
    [PPC_OP_FRSP]    = "frsp",
    [PPC_OP_FCMPU]   = "fcmpu",
    [PPC_OP_FCMPO]   = "fcmpo",
    [PPC_OP_PSQ_L]   = "psq_l",
    [PPC_OP_PSQ_LU]  = "psq_lu",
    [PPC_OP_PSQ_ST]  = "psq_st",
    [PPC_OP_PSQ_STU] = "psq_stu",
    [PPC_OP_PSQ_LX]  = "psq_lx",
    [PPC_OP_PSQ_LUX] = "psq_lux",
    [PPC_OP_PSQ_STX] = "psq_stx",
    [PPC_OP_PSQ_STUX] = "psq_stux",
    [PPC_OP_MULLW]   = "mullw",
    [PPC_OP_MULHW]   = "mulhw",
    [PPC_OP_MULHWU]  = "mulhwu",
    [PPC_OP_DIVW]    = "divw",
    [PPC_OP_DIVWU]   = "divwu",
    [PPC_OP_DCBZ]    = "dcbz",
};

const char* ppc_op_name(PPCOpcode op) {
    if (op >= PPC_OP_COUNT) return "???";
    return opcode_names[op];
}

static const char* spr_name(u16 spr) {
    switch (spr) {
    case 1: return "xer";
    case 8: return "lr";
    case 9: return "ctr";
    default: return NULL;
    }
}

static const char* dot(const PPCInst* inst) {
    return inst->rc ? "." : "";
}

char* ppc_disasm(char* buf, size_t buf_size, const PPCInst* inst) {
    switch (inst->op) {
    case PPC_OP_MULLI:
        snprintf(buf, buf_size, "mulli   r%u, r%u, %d",
                 inst->rD, inst->rA, (int)inst->simm);
        break;

    case PPC_OP_SUBFIC:
        snprintf(buf, buf_size, "subfic  r%u, r%u, %d",
                 inst->rD, inst->rA, (int)inst->simm);
        break;

    case PPC_OP_ADDI:
        if (inst->rA == 0) {
            snprintf(buf, buf_size, "li      r%u, %d",
                     inst->rD, (int)inst->simm);
        } else {
            snprintf(buf, buf_size, "addi    r%u, r%u, %d",
                     inst->rD, inst->rA, (int)inst->simm);
        }
        break;

    case PPC_OP_ADDIC:
        snprintf(buf, buf_size, "addic   r%u, r%u, %d",
                 inst->rD, inst->rA, (int)inst->simm);
        break;

    case PPC_OP_ADDIC_DOT:
        snprintf(buf, buf_size, "addic.  r%u, r%u, %d",
                 inst->rD, inst->rA, (int)inst->simm);
        break;

    case PPC_OP_ADDIS:
        if (inst->rA == 0) {
            snprintf(buf, buf_size, "lis     r%u, %d",
                     inst->rD, (int)inst->simm);
        } else {
            snprintf(buf, buf_size, "addis   r%u, r%u, %d",
                     inst->rD, inst->rA, (int)inst->simm);
        }
        break;

    case PPC_OP_CMPI:
        if (inst->crfD == 0) {
            snprintf(buf, buf_size, "cmpwi   r%u, %d",
                     inst->rA, (int)inst->simm);
        } else {
            snprintf(buf, buf_size, "cmpwi   cr%u, r%u, %d",
                     inst->crfD, inst->rA, (int)inst->simm);
        }
        break;

    case PPC_OP_CMPLI:
        if (inst->crfD == 0) {
            snprintf(buf, buf_size, "cmplwi  r%u, 0x%04X",
                     inst->rA, inst->uimm);
        } else {
            snprintf(buf, buf_size, "cmplwi  cr%u, r%u, 0x%04X",
                     inst->crfD, inst->rA, inst->uimm);
        }
        break;

    case PPC_OP_CMP:
        if (inst->crfD == 0) {
            snprintf(buf, buf_size, "cmpw    r%u, r%u",
                     inst->rA, inst->rB);
        } else {
            snprintf(buf, buf_size, "cmpw    cr%u, r%u, r%u",
                     inst->crfD, inst->rA, inst->rB);
        }
        break;

    case PPC_OP_CMPL:
        if (inst->crfD == 0) {
            snprintf(buf, buf_size, "cmplw   r%u, r%u",
                     inst->rA, inst->rB);
        } else {
            snprintf(buf, buf_size, "cmplw   cr%u, r%u, r%u",
                     inst->crfD, inst->rA, inst->rB);
        }
        break;

    case PPC_OP_ORI:
        if (inst->rS == 0 && inst->rA == 0 && inst->uimm == 0) {
            snprintf(buf, buf_size, "nop");
        } else {
            snprintf(buf, buf_size, "ori     r%u, r%u, 0x%04X",
                     inst->rA, inst->rS, inst->uimm);
        }
        break;

    case PPC_OP_ORIS:
        snprintf(buf, buf_size, "oris    r%u, r%u, 0x%04X",
                 inst->rA, inst->rS, inst->uimm);
        break;

    case PPC_OP_XORI:
        snprintf(buf, buf_size, "xori    r%u, r%u, 0x%04X",
                 inst->rA, inst->rS, inst->uimm);
        break;

    case PPC_OP_XORIS:
        snprintf(buf, buf_size, "xoris   r%u, r%u, 0x%04X",
                 inst->rA, inst->rS, inst->uimm);
        break;

    case PPC_OP_ANDI:
        snprintf(buf, buf_size, "andi.   r%u, r%u, 0x%04X",
                 inst->rA, inst->rS, inst->uimm);
        break;

    case PPC_OP_ANDIS:
        snprintf(buf, buf_size, "andis.  r%u, r%u, 0x%04X",
                 inst->rA, inst->rS, inst->uimm);
        break;

    case PPC_OP_AND:
    case PPC_OP_ANDC:
    case PPC_OP_OR:
    case PPC_OP_ORC:
    case PPC_OP_XOR:
    case PPC_OP_NAND:
    case PPC_OP_NOR:
    case PPC_OP_EQV:
    case PPC_OP_SLW:
    case PPC_OP_SRW:
    case PPC_OP_SRAW:
        snprintf(buf, buf_size, "%s%s   r%u, r%u, r%u",
                 ppc_op_name(inst->op), dot(inst), inst->rA, inst->rS, inst->rB);
        break;

    case PPC_OP_CNTLZW:
    case PPC_OP_EXTSB:
    case PPC_OP_EXTSH:
        snprintf(buf, buf_size, "%s%s r%u, r%u",
                 ppc_op_name(inst->op), dot(inst), inst->rA, inst->rS);
        break;

    case PPC_OP_ADD:
    case PPC_OP_ADDC:
    case PPC_OP_ADDE:
    case PPC_OP_SUBF:
    case PPC_OP_SUBFC:
    case PPC_OP_SUBFE:
    case PPC_OP_MULLW:
    case PPC_OP_MULHW:
    case PPC_OP_MULHWU:
    case PPC_OP_DIVW:
    case PPC_OP_DIVWU:
        snprintf(buf, buf_size, "%s%s   r%u, r%u, r%u",
                 ppc_op_name(inst->op), dot(inst), inst->rD, inst->rA, inst->rB);
        break;

    case PPC_OP_ADDZE:
    case PPC_OP_SUBFZE:
    case PPC_OP_NEG:
        snprintf(buf, buf_size, "%s%s  r%u, r%u",
                 ppc_op_name(inst->op), dot(inst), inst->rD, inst->rA);
        break;

    case PPC_OP_SRAWI:
        snprintf(buf, buf_size, "srawi%s r%u, r%u, %u",
                 dot(inst), inst->rA, inst->rS, inst->sh);
        break;

    case PPC_OP_RLWINM:
        snprintf(buf, buf_size, "rlwinm%s r%u, r%u, %u, %u, %u",
                 dot(inst), inst->rA, inst->rS, inst->sh, inst->mb, inst->me);
        break;

    case PPC_OP_RLWNM:
        snprintf(buf, buf_size, "rlwnm%s r%u, r%u, r%u, %u, %u",
                 dot(inst), inst->rA, inst->rS, inst->rB, inst->mb, inst->me);
        break;

    case PPC_OP_RLWIMI:
        snprintf(buf, buf_size, "rlwimi%s r%u, r%u, %u, %u, %u",
                 dot(inst), inst->rA, inst->rS, inst->sh, inst->mb, inst->me);
        break;

    case PPC_OP_LWZ:
    case PPC_OP_LWZU:
    case PPC_OP_LBZ:
    case PPC_OP_LBZU:
    case PPC_OP_LHZ:
    case PPC_OP_LHZU:
    case PPC_OP_LHA:
    case PPC_OP_LHAU:
    case PPC_OP_LMW:
        snprintf(buf, buf_size, "%s     r%u, %d(r%u)",
                 ppc_op_name(inst->op), inst->rD, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_LFS:
    case PPC_OP_LFSU:
    case PPC_OP_LFD:
    case PPC_OP_LFDU:
        snprintf(buf, buf_size, "%s     f%u, %d(r%u)",
                 ppc_op_name(inst->op), inst->rD, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_STW:
    case PPC_OP_STWU:
    case PPC_OP_STB:
    case PPC_OP_STBU:
    case PPC_OP_STH:
    case PPC_OP_STHU:
    case PPC_OP_STMW:
        snprintf(buf, buf_size, "%s     r%u, %d(r%u)",
                 ppc_op_name(inst->op), inst->rS, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_STFS:
    case PPC_OP_STFSU:
    case PPC_OP_STFD:
    case PPC_OP_STFDU:
        snprintf(buf, buf_size, "%s     f%u, %d(r%u)",
                 ppc_op_name(inst->op), inst->rS, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_LWZX:
    case PPC_OP_LWZUX:
    case PPC_OP_LBZX:
    case PPC_OP_LBZUX:
    case PPC_OP_LHZX:
    case PPC_OP_LHZUX:
    case PPC_OP_LHAX:
    case PPC_OP_LHAUX:
    case PPC_OP_LWBRX:
    case PPC_OP_LHBRX:
        snprintf(buf, buf_size, "%s    r%u, r%u, r%u",
                 ppc_op_name(inst->op), inst->rD, inst->rA, inst->rB);
        break;

    case PPC_OP_LFSX:
    case PPC_OP_LFSUX:
    case PPC_OP_LFDX:
    case PPC_OP_LFDUX:
        snprintf(buf, buf_size, "%s    f%u, r%u, r%u",
                 ppc_op_name(inst->op), inst->rD, inst->rA, inst->rB);
        break;

    case PPC_OP_STWX:
    case PPC_OP_STWUX:
    case PPC_OP_STBX:
    case PPC_OP_STBUX:
    case PPC_OP_STHX:
    case PPC_OP_STHUX:
    case PPC_OP_STWBRX:
    case PPC_OP_STHBRX:
        snprintf(buf, buf_size, "%s    r%u, r%u, r%u",
                 ppc_op_name(inst->op), inst->rS, inst->rA, inst->rB);
        break;

    case PPC_OP_STFSX:
    case PPC_OP_STFSUX:
    case PPC_OP_STFDX:
    case PPC_OP_STFDUX:
        snprintf(buf, buf_size, "%s    f%u, r%u, r%u",
                 ppc_op_name(inst->op), inst->rS, inst->rA, inst->rB);
        break;

    case PPC_OP_FADDS:
    case PPC_OP_FSUBS:
    case PPC_OP_FDIVS:
    case PPC_OP_FADD:
    case PPC_OP_FSUB:
    case PPC_OP_FDIV:
        snprintf(buf, buf_size, "%s%s   f%u, f%u, f%u",
                 ppc_op_name(inst->op), dot(inst), inst->rD, inst->rA, inst->rB);
        break;

    case PPC_OP_FMULS:
    case PPC_OP_FMUL:
        snprintf(buf, buf_size, "%s%s   f%u, f%u, f%u",
                 ppc_op_name(inst->op), dot(inst), inst->rD, inst->rA, inst->rC);
        break;

    case PPC_OP_FMR:
    case PPC_OP_FNEG:
    case PPC_OP_FABS:
    case PPC_OP_FNABS:
    case PPC_OP_FRSP:
        snprintf(buf, buf_size, "%s%s    f%u, f%u",
                 ppc_op_name(inst->op), dot(inst), inst->rD, inst->rB);
        break;

    case PPC_OP_FCMPU:
    case PPC_OP_FCMPO:
        snprintf(buf, buf_size, "%s   cr%u, f%u, f%u",
                 ppc_op_name(inst->op), inst->crfD, inst->rA, inst->rB);
        break;

    case PPC_OP_PSQ_L:
    case PPC_OP_PSQ_LU:
        snprintf(buf, buf_size, "%s   f%u, %d(r%u), %u, %u",
                 ppc_op_name(inst->op), inst->rD, (int)inst->simm,
                 inst->rA, inst->w, inst->i);
        break;

    case PPC_OP_PSQ_ST:
    case PPC_OP_PSQ_STU:
        snprintf(buf, buf_size, "%s   f%u, %d(r%u), %u, %u",
                 ppc_op_name(inst->op), inst->rS, (int)inst->simm,
                 inst->rA, inst->w, inst->i);
        break;

    case PPC_OP_PSQ_LX:
    case PPC_OP_PSQ_LUX:
        snprintf(buf, buf_size, "%s   f%u, r%u, r%u, %u, %u",
                 ppc_op_name(inst->op), inst->rD, inst->rA, inst->rB,
                 inst->w, inst->i);
        break;

    case PPC_OP_PSQ_STX:
    case PPC_OP_PSQ_STUX:
        snprintf(buf, buf_size, "%s   f%u, r%u, r%u, %u, %u",
                 ppc_op_name(inst->op), inst->rS, inst->rA, inst->rB,
                 inst->w, inst->i);
        break;

    case PPC_OP_DCBZ:
        if (inst->rA == 0) {
            snprintf(buf, buf_size, "dcbz    0, r%u", inst->rB);
        } else {
            snprintf(buf, buf_size, "dcbz    r%u, r%u", inst->rA, inst->rB);
        }
        break;

    case PPC_OP_B: {
        const char* mnemonic = "b";
        if (inst->lk && inst->aa)       mnemonic = "bla";
        else if (inst->lk)              mnemonic = "bl";
        else if (inst->aa)              mnemonic = "ba";

        snprintf(buf, buf_size, "%-7s 0x%08X", mnemonic, inst->branch_target);
        break;
    }

    case PPC_OP_BC: {
        const char* suffix = "";
        if (inst->lk && inst->aa)       suffix = "la";
        else if (inst->lk)              suffix = "l";
        else if (inst->aa)              suffix = "a";

        snprintf(buf, buf_size, "bc%s    %u, %u, 0x%08X",
                 suffix, inst->bo, inst->bi, inst->branch_target);
        break;
    }

    case PPC_OP_BCLR:
        if (inst->bo == 20 && inst->bi == 0) {
            snprintf(buf, buf_size, "%s", inst->lk ? "blrl" : "blr");
        } else {
            snprintf(buf, buf_size, "bclr%s  %u, %u",
                     inst->lk ? "l" : "", inst->bo, inst->bi);
        }
        break;

    case PPC_OP_BCCTR:
        if (inst->bo == 20 && inst->bi == 0) {
            snprintf(buf, buf_size, "%s", inst->lk ? "bctrl" : "bctr");
        } else {
            snprintf(buf, buf_size, "bcctr%s %u, %u",
                     inst->lk ? "l" : "", inst->bo, inst->bi);
        }
        break;

    case PPC_OP_CRAND:
    case PPC_OP_CRANDC:
    case PPC_OP_CREQV:
    case PPC_OP_CRNAND:
    case PPC_OP_CRNOR:
    case PPC_OP_CROR:
    case PPC_OP_CRORC:
    case PPC_OP_CRXOR:
        snprintf(buf, buf_size, "%-7s %u, %u, %u",
                 ppc_op_name(inst->op), inst->rD, inst->rA, inst->rB);
        break;

    case PPC_OP_MCRF:
        snprintf(buf, buf_size, "mcrf    cr%u, cr%u", inst->crfD, inst->crfS);
        break;

    case PPC_OP_MFCR:
        snprintf(buf, buf_size, "mfcr    r%u", inst->rD);
        break;

    case PPC_OP_MTCRF:
        if (inst->crm == 0xFF)
            snprintf(buf, buf_size, "mtcr    r%u", inst->rS);
        else
            snprintf(buf, buf_size, "mtcrf   0x%02X, r%u", inst->crm, inst->rS);
        break;

    case PPC_OP_MFSPR: {
        const char* name = spr_name(inst->spr);
        if (name)
            snprintf(buf, buf_size, "mf%s    r%u", name, inst->rD);
        else
            snprintf(buf, buf_size, "mfspr   r%u, %u", inst->rD, inst->spr);
        break;
    }

    case PPC_OP_MTSPR: {
        const char* name = spr_name(inst->spr);
        if (name)
            snprintf(buf, buf_size, "mt%s    r%u", name, inst->rS);
        else
            snprintf(buf, buf_size, "mtspr   %u, r%u", inst->spr, inst->rS);
        break;
    }

    default:
        snprintf(buf, buf_size, ".long   0x%08X", inst->raw);
        break;
    }

    return buf;
}
