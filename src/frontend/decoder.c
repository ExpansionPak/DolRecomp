#include "decoder.h"
#include <stdio.h>
#include <string.h>

// field extraction (host endian after bswap)
#define PPC_PRIMARY(raw)   ((raw) >> 26)
#define PPC_RD(raw)        (((raw) >> 21) & 0x1F)
#define PPC_RS(raw)        (((raw) >> 21) & 0x1F)
#define PPC_RA(raw)        (((raw) >> 16) & 0x1F)
#define PPC_SIMM(raw)      ((s16)((raw) & 0xFFFF))
#define PPC_UIMM(raw)      ((u16)((raw) & 0xFFFF))

PPCInst ppc_decode(u32 raw, u32 address) {
    PPCInst inst;
    memset(&inst, 0, sizeof(inst));
    inst.raw     = raw;
    inst.address = address;

    u32 primary = PPC_PRIMARY(raw);

    switch (primary) {
    case 14: // addi — rA=0 means use literal 0 (li pseudo-op)
        inst.op   = PPC_OP_ADDI;
        inst.rD   = PPC_RD(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 12: // addic — rD = rA + SIMM. Updates Carry (CA). rA=0 reads r0 register.
        inst.op   = PPC_OP_ADDIC;
        inst.rD   = PPC_RD(raw);
        inst.rA   = PPC_RA(raw);
        inst.simm = PPC_SIMM(raw);
        break;

    case 24: // ori — ori r0,r0,0 is nop
        inst.op   = PPC_OP_ORI;
        inst.rS   = PPC_RS(raw);
        inst.rA   = PPC_RA(raw);
        inst.uimm = PPC_UIMM(raw);
        break;

    case 32: // lwz
        inst.op   = PPC_OP_LWZ;
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

    case 18: { // b/bl/ba/bla
        inst.op = PPC_OP_B;
        s32 displacement = sign_extend(raw & 0x03FFFFFC, 26);
        inst.aa = (raw >> 1) & 1;
        inst.lk = raw & 1;

        if (inst.aa)
            inst.branch_target = (u32)displacement;
        else
            inst.branch_target = address + (u32)displacement;
        break;
    }

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
    [PPC_OP_ORI]     = "ori",
    [PPC_OP_LWZ]     = "lwz",
    [PPC_OP_STW]     = "stw",
    [PPC_OP_B]       = "b",
};

const char* ppc_op_name(PPCOpcode op) {
    if (op >= PPC_OP_COUNT) return "???";
    return opcode_names[op];
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
        // Always prints rA, even if it is r0. No pseudo-ops allowed here.
        snprintf(buf, buf_size, "addic   r%u, r%u, %d",
                 inst->rD, inst->rA, (int)inst->simm);
        break;

    case PPC_OP_ORI:
        if (inst->rS == 0 && inst->rA == 0 && inst->uimm == 0) {
            snprintf(buf, buf_size, "nop");
        } else {
            snprintf(buf, buf_size, "ori     r%u, r%u, 0x%04X",
                     inst->rA, inst->rS, inst->uimm);
        }
        break;

    case PPC_OP_LWZ:
        snprintf(buf, buf_size, "lwz     r%u, %d(r%u)",
                 inst->rD, (int)inst->simm, inst->rA);
        break;

    case PPC_OP_STW:
        snprintf(buf, buf_size, "stw     r%u, %d(r%u)",
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

    default:
        snprintf(buf, buf_size, ".long   0x%08X", inst->raw);
        break;
    }

    return buf;
}
