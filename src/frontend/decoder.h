#ifndef DOLRECOMP_DECODER_H
#define DOLRECOMP_DECODER_H

#include "../core/types.h"

// PPC instruction decoder
// current CPU opcode set

typedef enum {
    PPC_OP_UNKNOWN = 0,
    PPC_OP_MULLI,
    PPC_OP_SUBFIC,
    PPC_OP_ADDI,
    PPC_OP_ADDIC,
    PPC_OP_ADDIC_DOT,
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
    PPC_OP_LHAU,
    PPC_OP_STH,
    PPC_OP_STHU,
    PPC_OP_LMW,
    PPC_OP_STMW,
    PPC_OP_B,
    PPC_OP_BC,
    PPC_OP_BCLR,
    PPC_OP_BCCTR,
    PPC_OP_CRAND,
    PPC_OP_CRANDC,
    PPC_OP_CREQV,
    PPC_OP_CRNAND,
    PPC_OP_CRNOR,
    PPC_OP_CROR,
    PPC_OP_CRORC,
    PPC_OP_CRXOR,
    PPC_OP_MCRF,
    PPC_OP_MFCR,
    PPC_OP_MTCRF,
    PPC_OP_MFSPR,
    PPC_OP_MTSPR,
    PPC_OP_CMP,
    PPC_OP_CMPL,
    PPC_OP_ADD,
    PPC_OP_ADDC,
    PPC_OP_ADDE,
    PPC_OP_ADDZE,
    PPC_OP_SUBF,
    PPC_OP_SUBFC,
    PPC_OP_SUBFE,
    PPC_OP_SUBFZE,
    PPC_OP_NEG,
    PPC_OP_AND,
    PPC_OP_ANDC,
    PPC_OP_OR,
    PPC_OP_ORC,
    PPC_OP_XOR,
    PPC_OP_NAND,
    PPC_OP_NOR,
    PPC_OP_EQV,
    PPC_OP_CNTLZW,
    PPC_OP_EXTSB,
    PPC_OP_EXTSH,
    PPC_OP_SLW,
    PPC_OP_SRW,
    PPC_OP_SRAW,
    PPC_OP_SRAWI,
    PPC_OP_RLWINM,
    PPC_OP_RLWNM,
    PPC_OP_RLWIMI,
    PPC_OP_LWZX,
    PPC_OP_LWZUX,
    PPC_OP_LBZX,
    PPC_OP_LBZUX,
    PPC_OP_LHZX,
    PPC_OP_LHZUX,
    PPC_OP_LHAX,
    PPC_OP_LHAUX,
    PPC_OP_LWBRX,
    PPC_OP_LHBRX,
    PPC_OP_STWX,
    PPC_OP_STWUX,
    PPC_OP_STBX,
    PPC_OP_STBUX,
    PPC_OP_STHX,
    PPC_OP_STHUX,
    PPC_OP_STWBRX,
    PPC_OP_STHBRX,
    PPC_OP_LFS,
    PPC_OP_LFSU,
    PPC_OP_LFD,
    PPC_OP_LFDU,
    PPC_OP_STFS,
    PPC_OP_STFSU,
    PPC_OP_STFD,
    PPC_OP_STFDU,
    PPC_OP_LFSX,
    PPC_OP_LFSUX,
    PPC_OP_LFDX,
    PPC_OP_LFDUX,
    PPC_OP_STFSX,
    PPC_OP_STFSUX,
    PPC_OP_STFDX,
    PPC_OP_STFDUX,
    PPC_OP_FADDS,
    PPC_OP_FSUBS,
    PPC_OP_FMULS,
    PPC_OP_FDIVS,
    PPC_OP_FADD,
    PPC_OP_FSUB,
    PPC_OP_FMUL,
    PPC_OP_FDIV,
    PPC_OP_FMR,
    PPC_OP_FNEG,
    PPC_OP_FABS,
    PPC_OP_FNABS,
    PPC_OP_FRSP,
    PPC_OP_FCMPU,
    PPC_OP_FCMPO,
    PPC_OP_PSQ_L,
    PPC_OP_PSQ_LU,
    PPC_OP_PSQ_ST,
    PPC_OP_PSQ_STU,
    PPC_OP_PSQ_LX,
    PPC_OP_PSQ_LUX,
    PPC_OP_PSQ_STX,
    PPC_OP_PSQ_STUX,
    PPC_OP_MULLW,
    PPC_OP_MULHW,
    PPC_OP_MULHWU,
    PPC_OP_DIVW,
    PPC_OP_DIVWU,
    PPC_OP_DCBZ,
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
    u8  rC;
    u8  crfD;
    u8  crfS;
    u8  crm;
    u8  l;
    u8  w;
    u8  i;
    u8  bo;
    u8  bi;
    u8  mb;
    u8  me;
    u8  sh;
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
