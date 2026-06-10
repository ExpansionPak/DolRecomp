#ifndef DOLRECOMP_DECODER_H
#define DOLRECOMP_DECODER_H

#include "../core/types.h"

// PPC instruction decoder
// only these 30 CPU opcodes decode as known

typedef enum {
    PPC_OP_UNKNOWN = 0,
    PPC_OP_ADDI,
    PPC_OP_ADDIC,
    PPC_OP_ADDIS,
    PPC_OP_CMPI,
    PPC_OP_CMPLI,
    PPC_OP_ORI,
    PPC_OP_ORIS,
    PPC_OP_XORI,
    PPC_OP_XORIS,
    PPC_OP_ANDI,
    PPC_OP_ANDIS,
    PPC_OP_LWZ,
    PPC_OP_LWZU,
    PPC_OP_LBZ,
    PPC_OP_LBZU,
    PPC_OP_STW,
    PPC_OP_STWU,
    PPC_OP_STB,
    PPC_OP_STBU,
    PPC_OP_LHZ,
    PPC_OP_LHZU,
    PPC_OP_LHA,
    PPC_OP_STH,
    PPC_OP_STHU,
    PPC_OP_B,
    PPC_OP_BC,
    PPC_OP_BCLR,
    PPC_OP_BCCTR,
    PPC_OP_MFSPR,
    PPC_OP_MTSPR,
    PPC_OP_COUNT
} PPCOpcode;

typedef struct {
    PPCOpcode op;
    u32 raw;
    u32 address;

    u8  rD;
    u8  rA;
    u8  rS;
    u8  rB;
    u8  crfD;
    u8  l;
    u8  bo;
    u8  bi;
    s16 simm;
    u16 uimm;
    u16 spr;
    u32 branch_target;
    bool aa;
    bool lk;
    bool rc;
} PPCInst;

// decode a single instruction (raw = host endian)
PPCInst ppc_decode(u32 raw, u32 address);

// opcode name string
const char* ppc_op_name(PPCOpcode op);

// disassemble to buf, returns buf
char* ppc_disasm(char* buf, size_t buf_size, const PPCInst* inst);

#endif /* DOLRECOMP_DECODER_H */
