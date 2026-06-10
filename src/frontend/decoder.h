#ifndef DOLRECOMP_DECODER_H
#define DOLRECOMP_DECODER_H

#include "../core/types.h"

// PPC instruction decoder
// 6 opcodes so far: addi, addic, ori, lwz, stw, b

typedef enum {
    PPC_OP_UNKNOWN = 0,
    PPC_OP_ADDI,
    PPC_OP_ADDIC,
    PPC_OP_ORI,
    PPC_OP_LWZ,
    PPC_OP_STW,
    PPC_OP_B,
    PPC_OP_COUNT
} PPCOpcode;

typedef struct {
    PPCOpcode op;
    u32 raw;
    u32 address;

    u8  rD;
    u8  rA;
    u8  rS;
    s16 simm;
    u16 uimm;
    u32 branch_target;
    bool aa;
    bool lk;
} PPCInst;

// decode a single instruction (raw = host endian)
PPCInst ppc_decode(u32 raw, u32 address);

// opcode name string
const char* ppc_op_name(PPCOpcode op);

// disassemble to buf, returns buf
char* ppc_disasm(char* buf, size_t buf_size, const PPCInst* inst);

#endif /* DOLRECOMP_DECODER_H */
