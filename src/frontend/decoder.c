#include "decoder.h"
#include <stdio.h>
#include <string.h>

// Field extraction from a host-endian 32-bit instruction word.
#define PPC_PRIMARY(raw)   ((raw) >> 26)
#define PPC_RD(raw)        (((raw) >> 21) & 0x1F)
#define PPC_RS(raw)        (((raw) >> 21) & 0x1F)
#define PPC_RA(raw)        (((raw) >> 16) & 0x1F)
#define PPC_BO(raw)        (((raw) >> 21) & 0x1F)
#define PPC_BI(raw)        (((raw) >> 16) & 0x1F)
#define PPC_XO(raw)        (((raw) >> 1) & 0x3FF)
#define PPC_SPR(raw)       ((((raw) >> 16) & 0x1F) | (((raw) >> 6) & 0x3E0))
#define PPC_SIMM(raw)      ((s16)((raw) & 0xFFFF))
#define PPC_UIMM(raw)      ((u16)((raw) & 0xFFFF))

PPCInst ppc_decode(u32 raw, u32 address) {
    PPCInst inst;
    memset(&inst, 0, sizeof(inst));
    inst.raw     = raw;
    inst.address = address;

    switch (PPC_PRIMARY(raw)) {
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
        inst.op   = PPC_OP_ADDIC;
        inst.rD   = PPC_RD(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 14: // addi, with rA=0 using literal zero
        inst.op   = PPC_OP_ADDI;
        inst.rD   = PPC_RD(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 15: // addis, with rA=0 using literal zero
        inst.op   = PPC_OP_ADDIS;
        inst.rD   = PPC_RD(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
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
        if (xo == 16) {
            inst.op = PPC_OP_BCLR;
            inst.bo = PPC_BO(raw);
            inst.bi = PPC_BI(raw);
            inst.lk = raw & 1;
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
        if (xo == 339) {
            inst.op  = PPC_OP_MFSPR;
            inst.rD  = PPC_RD(raw);
            inst.spr = PPC_SPR(raw);
        } else if (xo == 467) {
            inst.op  = PPC_OP_MTSPR;
            inst.rS  = PPC_RS(raw);
            inst.spr = PPC_SPR(raw);
        } else {
            inst.op = PPC_OP_UNKNOWN;
        }
        break;
    }

    case 32: // lwz
        inst.op   = PPC_OP_LWZ;
        inst.rD   = PPC_RD(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 33: // lwzu
        inst.op   = PPC_OP_LWZU;
        inst.rD   = PPC_RD(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 34: // lbz
        inst.op   = PPC_OP_LBZ;
        inst.rD   = PPC_RD(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 35: // lbzu
        inst.op   = PPC_OP_LBZU;
        inst.rD   = PPC_RD(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 36: // stw
        inst.op   = PPC_OP_STW;
        inst.rS   = PPC_RS(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 37: // stwu
        inst.op   = PPC_OP_STWU;
        inst.rS   = PPC_RS(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 38: // stb
        inst.op   = PPC_OP_STB;
        inst.rS   = PPC_RS(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 39: // stbu
        inst.op   = PPC_OP_STBU;
        inst.rS   = PPC_RS(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 40: // lhz
        inst.op   = PPC_OP_LHZ;
        inst.rD   = PPC_RD(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 41: // lhzu
        inst.op   = PPC_OP_LHZU;
        inst.rD   = PPC_RD(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 42: // lha
        inst.op   = PPC_OP_LHA;
        inst.rD   = PPC_RD(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 44: // sth
        inst.op   = PPC_OP_STH;
        inst.rS   = PPC_RS(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 45: // sthu
        inst.op   = PPC_OP_STHU;
        inst.rS   = PPC_RS(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    default:
        inst.op = PPC_OP_UNKNOWN;
        break;
    }

    return inst;
}

static const char* opcode_names[PPC_OP_COUNT] = {
    [PPC_OP_UNKNOWN] = "???",
    [PPC_OP_ADDI]    = "addi",
    [PPC_OP_ADDIC]   = "addic",
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
    [PPC_OP_STH]     = "sth",
    [PPC_OP_STHU]    = "sthu",
    [PPC_OP_B]       = "b",
    [PPC_OP_BC]      = "bc",
    [PPC_OP_BCLR]    = "bclr",
    [PPC_OP_BCCTR]   = "bcctr",
    [PPC_OP_MFSPR]   = "mfspr",
    [PPC_OP_MTSPR]   = "mtspr",
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

char* ppc_disasm(char* buf, size_t buf_size, const PPCInst* inst) {
    switch (inst->op) {
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

    case PPC_OP_LWZ:
        snprintf(buf, buf_size, "lwz     r%u, %d(r%u)",
                 inst->rD, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_LWZU:
        snprintf(buf, buf_size, "lwzu    r%u, %d(r%u)",
                 inst->rD, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_LBZ:
        snprintf(buf, buf_size, "lbz     r%u, %d(r%u)",
                 inst->rD, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_LBZU:
        snprintf(buf, buf_size, "lbzu    r%u, %d(r%u)",
                 inst->rD, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_STW:
        snprintf(buf, buf_size, "stw     r%u, %d(r%u)",
                 inst->rS, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_STWU:
        snprintf(buf, buf_size, "stwu    r%u, %d(r%u)",
                 inst->rS, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_STB:
        snprintf(buf, buf_size, "stb     r%u, %d(r%u)",
                 inst->rS, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_STBU:
        snprintf(buf, buf_size, "stbu    r%u, %d(r%u)",
                 inst->rS, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_LHZ:
        snprintf(buf, buf_size, "lhz     r%u, %d(r%u)",
                 inst->rD, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_LHZU:
        snprintf(buf, buf_size, "lhzu    r%u, %d(r%u)",
                 inst->rD, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_LHA:
        snprintf(buf, buf_size, "lha     r%u, %d(r%u)",
                 inst->rD, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_STH:
        snprintf(buf, buf_size, "sth     r%u, %d(r%u)",
                 inst->rS, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_STHU:
        snprintf(buf, buf_size, "sthu    r%u, %d(r%u)",
                 inst->rS, (int)inst->simm, inst->rA);
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
