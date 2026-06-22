#include <stdio.h>
#include <string.h>

#include "../src/core/types.h"
#include "../src/core/cpu.h"
#include "../src/frontend/decoder.h"

#define BASE 0x80000000u
#define XER_SO 0x80000000u
#define XER_CA 0x20000000u

static int pass_count = 0;
static int fail_count = 0;

static void check_eq(u32 got, u32 want, const char* name) {
    printf("PCREF,%s,0x%08X,0x%08X,%s\n",
           name, got, want, got == want ? "PASS" : "FAIL");

    if (got == want) {
        pass_count++;
    } else {
        fail_count++;
        printf("  FAIL: %s - got 0x%08X, want 0x%08X\n", name, got, want);
    }
}

static void check_eq64(u64 got, u64 want, const char* name) {
    char label[96];

    snprintf(label, sizeof(label), "%s hi", name);
    check_eq((u32)(got >> 32), (u32)(want >> 32), label);

    snprintf(label, sizeof(label), "%s lo", name);
    check_eq((u32)got, (u32)want, label);
}

static u32 make_dform(u32 opcd, u32 rt, u32 ra, u16 imm) {
    return (opcd << 26) | (rt << 21) | (ra << 16) | imm;
}

static u32 make_iform(u32 opcd, s32 offset, bool aa, bool lk) {
    return (opcd << 26) | ((u32)offset & 0x03FFFFFCu) |
           (aa ? 2u : 0u) | (lk ? 1u : 0u);
}

static u32 make_xform(u32 xo, u32 rt, u32 ra, u32 rb, bool rc) {
    return (31u << 26) | (rt << 21) | (ra << 16) | (rb << 11) |
           (xo << 1) | (rc ? 1u : 0u);
}

static u32 make_srawi(u32 rs, u32 ra, u32 sh, bool rc) {
    return (31u << 26) | (rs << 21) | (ra << 16) | (sh << 11) |
           (824u << 1) | (rc ? 1u : 0u);
}

static u32 make_mform(u32 opcd, u32 rs, u32 ra, u32 sh_or_rb,
                      u32 mb, u32 me, bool rc) {
    return (opcd << 26) | (rs << 21) | (ra << 16) | (sh_or_rb << 11) |
           (mb << 6) | (me << 1) | (rc ? 1u : 0u);
}

static u32 make_xfx(u32 xo, u32 rt, u16 spr) {
    u32 spr_field = ((u32)(spr & 0x1F) << 16) | ((u32)(spr & 0x3E0) << 6);
    return (31u << 26) | (rt << 21) | spr_field | (xo << 1);
}

static u32 make_crform(u32 xo, u32 bt, u32 ba, u32 bb) {
    return (19u << 26) | (bt << 21) | (ba << 16) | (bb << 11) | (xo << 1);
}

static u32 make_mcrf(u32 crfd, u32 crfs) {
    return (19u << 26) | (crfd << 23) | (crfs << 18);
}

static u32 make_mtcrf(u32 rs, u32 crm) {
    return (31u << 26) | (rs << 21) | (crm << 12) | (144u << 1);
}

static u32 cr_shift(u8 crf) {
    return 4u * (7u - (u32)crf);
}

static u32 get_cr_field(const CPUState* cpu, u8 crf) {
    return (cpu->cr >> cr_shift(crf)) & 0xFu;
}

static void set_cr_field(CPUState* cpu, u8 crf, u32 bits) {
    u32 shift = cr_shift(crf);
    cpu->cr = (cpu->cr & ~(0xFu << shift)) | ((bits & 0xFu) << shift);
}

static u32 mtcrf_mask(u8 crm) {
    u32 mask = 0;
    for (u32 crf = 0; crf < 8; crf++) {
        if (crm & (0x80u >> crf))
            mask |= 0xFu << cr_shift((u8)crf);
    }
    return mask;
}

static void set_cr0_from_gpr(CPUState* cpu, u8 reg) {
    s32 value = (s32)cpu->gpr[reg];
    u32 bits = 0;

    if (value < 0) bits |= 0x8u;
    if (value > 0) bits |= 0x4u;
    if (value == 0) bits |= 0x2u;
    bits |= (cpu->xer & XER_SO) ? 1u : 0u;

    set_cr_field(cpu, 0, bits);
}

static void compare_s32_values(CPUState* cpu, u8 crf, u32 lhs, u32 rhs) {
    s32 a = (s32)lhs;
    s32 b = (s32)rhs;
    u32 bits = 0;

    if (a < b) bits |= 0x8u;
    if (a > b) bits |= 0x4u;
    if (a == b) bits |= 0x2u;
    bits |= (cpu->xer & XER_SO) ? 1u : 0u;

    set_cr_field(cpu, crf, bits);
}

static void compare_u32_values(CPUState* cpu, u8 crf, u32 a, u32 b) {
    u32 bits = 0;

    if (a < b) bits |= 0x8u;
    if (a > b) bits |= 0x4u;
    if (a == b) bits |= 0x2u;
    bits |= (cpu->xer & XER_SO) ? 1u : 0u;

    set_cr_field(cpu, crf, bits);
}

static void set_ca(CPUState* cpu, bool ca) {
    cpu->xer = (cpu->xer & ~XER_CA) | (ca ? XER_CA : 0u);
}

static void set_ca_from_u64(CPUState* cpu, u64 value) {
    set_ca(cpu, ((value >> 32) & 1u) != 0);
}

static u32 dform_ea(CPUState* cpu, const PPCInst* inst, bool update) {
    if (inst->rA == 0 && !update)
        return (u32)(s32)inst->simm;
    return cpu->gpr[inst->rA] + (u32)(s32)inst->simm;
}

static u32 xform_ea(CPUState* cpu, const PPCInst* inst, bool update) {
    if (inst->rA == 0 && !update)
        return cpu->gpr[inst->rB];
    return cpu->gpr[inst->rA] + cpu->gpr[inst->rB];
}

static bool branch_condition(CPUState* cpu, u8 bo, u8 bi) {
    bool ctr_ok = true;
    bool cr_ok = true;

    if ((bo & 0x04u) == 0) {
        cpu->ctr--;
        ctr_ok = ((((cpu->ctr != 0) ? 1u : 0u) ^ ((bo >> 1) & 1u)) != 0);
    }

    if ((bo & 0x10u) == 0) {
        bool bit_set = (cpu->cr & (0x80000000u >> bi)) != 0;
        cr_ok = bit_set == (((bo >> 3) & 1u) != 0);
    }

    return ctr_ok && cr_ok;
}

static u32 mask32(u8 mb, u8 me) {
    u32 mask = 0;
    u8 bit = mb;

    for (;;) {
        mask |= 0x80000000u >> bit;
        if (bit == me)
            break;
        bit = (u8)((bit + 1) & 31);
    }

    return mask;
}

static u32 rotl32(u32 value, u32 sh) {
    sh &= 31u;
    return sh ? ((value << sh) | (value >> (32u - sh))) : value;
}

static f32 f32_from_bits(u32 bits) {
    f32 value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static u32 f32_to_bits(f32 value) {
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static f64 f64_from_bits(u64 bits) {
    f64 value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static u64 f64_to_bits(f64 value) {
    u64 bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static u32 arith_shift_right(u32 value, u32 sh) {
    if (sh == 0)
        return value;
    if (sh > 31)
        return (value & 0x80000000u) ? 0xFFFFFFFFu : 0u;

    if ((value & 0x80000000u) == 0)
        return value >> sh;

    return (value >> sh) | ~(0xFFFFFFFFu >> sh);
}

static void update_sraw_ca(CPUState* cpu, u32 value, u32 sh) {
    bool ca = false;

    if (sh > 0) {
        if (sh > 31)
            ca = (value & 0x80000000u) != 0;
        else
            ca = (value & 0x80000000u) && ((value << (32u - sh)) != 0);
    }

    set_ca(cpu, ca);
}

static u32 cr_bit(const CPUState* cpu, u8 bit) {
    return (cpu->cr >> (31u - bit)) & 1u;
}

static void set_cr_bit(CPUState* cpu, u8 bit, u32 value) {
    u32 mask = 0x80000000u >> bit;
    cpu->cr = (cpu->cr & ~mask) | (value ? mask : 0u);
}

static void exec_inst(CPUState* cpu, const PPCInst* inst) {
    cpu->pc = inst->address + 4;

    switch (inst->op) {
    case PPC_OP_MULLI:
        cpu->gpr[inst->rD] = (u32)((s64)(s32)cpu->gpr[inst->rA] *
                                    (s64)(s32)inst->simm);
        break;

    case PPC_OP_SUBFIC: {
        u64 res = (u64)(u32)(s32)inst->simm + (u64)(~cpu->gpr[inst->rA]) + 1u;
        cpu->gpr[inst->rD] = (u32)res;
        set_ca_from_u64(cpu, res);
        break;
    }

    case PPC_OP_ADDI:
        cpu->gpr[inst->rD] = (inst->rA == 0)
            ? (u32)(s32)inst->simm
            : cpu->gpr[inst->rA] + (u32)(s32)inst->simm;
        break;

    case PPC_OP_ADDIC:
    case PPC_OP_ADDIC_DOT: {
        u64 res = (u64)cpu->gpr[inst->rA] + (u64)(u32)(s32)inst->simm;
        cpu->gpr[inst->rD] = (u32)res;
        set_ca_from_u64(cpu, res);
        if (inst->op == PPC_OP_ADDIC_DOT)
            set_cr0_from_gpr(cpu, inst->rD);
        break;
    }

    case PPC_OP_ADDIS:
        cpu->gpr[inst->rD] = (inst->rA == 0)
            ? ((u32)(s32)inst->simm << 16)
            : cpu->gpr[inst->rA] + ((u32)(s32)inst->simm << 16);
        break;

    case PPC_OP_CMPI:
        compare_s32_values(cpu, inst->crfD, cpu->gpr[inst->rA], (u32)(s32)inst->simm);
        break;

    case PPC_OP_CMPLI:
        compare_u32_values(cpu, inst->crfD, cpu->gpr[inst->rA], inst->uimm);
        break;

    case PPC_OP_CMP:
        compare_s32_values(cpu, inst->crfD, cpu->gpr[inst->rA], cpu->gpr[inst->rB]);
        break;

    case PPC_OP_CMPL:
        compare_u32_values(cpu, inst->crfD, cpu->gpr[inst->rA], cpu->gpr[inst->rB]);
        break;

    case PPC_OP_ORI:
        cpu->gpr[inst->rA] = cpu->gpr[inst->rS] | inst->uimm;
        break;

    case PPC_OP_ORIS:
        cpu->gpr[inst->rA] = cpu->gpr[inst->rS] | ((u32)inst->uimm << 16);
        break;

    case PPC_OP_XORI:
        cpu->gpr[inst->rA] = cpu->gpr[inst->rS] ^ inst->uimm;
        break;

    case PPC_OP_XORIS:
        cpu->gpr[inst->rA] = cpu->gpr[inst->rS] ^ ((u32)inst->uimm << 16);
        break;

    case PPC_OP_ANDI:
        cpu->gpr[inst->rA] = cpu->gpr[inst->rS] & inst->uimm;
        set_cr0_from_gpr(cpu, inst->rA);
        break;

    case PPC_OP_ANDIS:
        cpu->gpr[inst->rA] = cpu->gpr[inst->rS] & ((u32)inst->uimm << 16);
        set_cr0_from_gpr(cpu, inst->rA);
        break;

    case PPC_OP_ADD:
        cpu->gpr[inst->rD] = cpu->gpr[inst->rA] + cpu->gpr[inst->rB];
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rD);
        break;

    case PPC_OP_ADDC: {
        u64 res = (u64)cpu->gpr[inst->rA] + (u64)cpu->gpr[inst->rB];
        cpu->gpr[inst->rD] = (u32)res;
        set_ca_from_u64(cpu, res);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rD);
        break;
    }

    case PPC_OP_ADDE: {
        u64 res = (u64)cpu->gpr[inst->rA] + (u64)cpu->gpr[inst->rB] +
                  ((cpu->xer & XER_CA) ? 1u : 0u);
        cpu->gpr[inst->rD] = (u32)res;
        set_ca_from_u64(cpu, res);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rD);
        break;
    }

    case PPC_OP_ADDZE: {
        u64 res = (u64)cpu->gpr[inst->rA] + ((cpu->xer & XER_CA) ? 1u : 0u);
        cpu->gpr[inst->rD] = (u32)res;
        set_ca_from_u64(cpu, res);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rD);
        break;
    }

    case PPC_OP_SUBF:
        cpu->gpr[inst->rD] = cpu->gpr[inst->rB] - cpu->gpr[inst->rA];
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rD);
        break;

    case PPC_OP_SUBFC: {
        u64 res = (u64)cpu->gpr[inst->rB] + (u64)(~cpu->gpr[inst->rA]) + 1u;
        cpu->gpr[inst->rD] = (u32)res;
        set_ca_from_u64(cpu, res);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rD);
        break;
    }

    case PPC_OP_SUBFE: {
        u64 res = (u64)cpu->gpr[inst->rB] + (u64)(~cpu->gpr[inst->rA]) +
                  ((cpu->xer & XER_CA) ? 1u : 0u);
        cpu->gpr[inst->rD] = (u32)res;
        set_ca_from_u64(cpu, res);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rD);
        break;
    }

    case PPC_OP_SUBFZE: {
        u64 res = (u64)(~cpu->gpr[inst->rA]) + ((cpu->xer & XER_CA) ? 1u : 0u);
        cpu->gpr[inst->rD] = (u32)res;
        set_ca_from_u64(cpu, res);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rD);
        break;
    }

    case PPC_OP_NEG:
        cpu->gpr[inst->rD] = 0u - cpu->gpr[inst->rA];
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rD);
        break;

    case PPC_OP_MULLW:
        cpu->gpr[inst->rD] = (u32)((s64)(s32)cpu->gpr[inst->rA] *
                                    (s64)(s32)cpu->gpr[inst->rB]);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rD);
        break;

    case PPC_OP_MULHW: {
        s64 product = (s64)(s32)cpu->gpr[inst->rA] *
                      (s64)(s32)cpu->gpr[inst->rB];
        cpu->gpr[inst->rD] = (u32)(product >> 32);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rD);
        break;
    }

    case PPC_OP_MULHWU: {
        u64 product = (u64)cpu->gpr[inst->rA] * (u64)cpu->gpr[inst->rB];
        cpu->gpr[inst->rD] = (u32)(product >> 32);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rD);
        break;
    }

    case PPC_OP_DIVW: {
        s32 dividend = (s32)cpu->gpr[inst->rA];
        s32 divisor = (s32)cpu->gpr[inst->rB];
        cpu->gpr[inst->rD] = (divisor == 0 || (dividend == (s32)0x80000000 && divisor == -1))
            ? 0u
            : (u32)(dividend / divisor);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rD);
        break;
    }

    case PPC_OP_DIVWU:
        cpu->gpr[inst->rD] = cpu->gpr[inst->rB] == 0
            ? 0u
            : cpu->gpr[inst->rA] / cpu->gpr[inst->rB];
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rD);
        break;

    case PPC_OP_AND:
        cpu->gpr[inst->rA] = cpu->gpr[inst->rS] & cpu->gpr[inst->rB];
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;

    case PPC_OP_ANDC:
        cpu->gpr[inst->rA] = cpu->gpr[inst->rS] & ~cpu->gpr[inst->rB];
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;

    case PPC_OP_OR:
        cpu->gpr[inst->rA] = cpu->gpr[inst->rS] | cpu->gpr[inst->rB];
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;

    case PPC_OP_ORC:
        cpu->gpr[inst->rA] = cpu->gpr[inst->rS] | ~cpu->gpr[inst->rB];
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;

    case PPC_OP_XOR:
        cpu->gpr[inst->rA] = cpu->gpr[inst->rS] ^ cpu->gpr[inst->rB];
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;

    case PPC_OP_NAND:
        cpu->gpr[inst->rA] = ~(cpu->gpr[inst->rS] & cpu->gpr[inst->rB]);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;

    case PPC_OP_NOR:
        cpu->gpr[inst->rA] = ~(cpu->gpr[inst->rS] | cpu->gpr[inst->rB]);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;

    case PPC_OP_EQV:
        cpu->gpr[inst->rA] = ~(cpu->gpr[inst->rS] ^ cpu->gpr[inst->rB]);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;

    case PPC_OP_CNTLZW: {
        u32 n = 0;
        while (n < 32 && ((cpu->gpr[inst->rS] & (0x80000000u >> n)) == 0))
            n++;
        cpu->gpr[inst->rA] = n;
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;
    }

    case PPC_OP_EXTSB:
        cpu->gpr[inst->rA] = (u32)(s32)(s8)cpu->gpr[inst->rS];
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;

    case PPC_OP_EXTSH:
        cpu->gpr[inst->rA] = (u32)(s32)(s16)cpu->gpr[inst->rS];
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;

    case PPC_OP_SLW: {
        u32 sh = cpu->gpr[inst->rB] & 0x3Fu;
        cpu->gpr[inst->rA] = sh > 31 ? 0u : (cpu->gpr[inst->rS] << sh);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;
    }

    case PPC_OP_SRW: {
        u32 sh = cpu->gpr[inst->rB] & 0x3Fu;
        cpu->gpr[inst->rA] = sh > 31 ? 0u : (cpu->gpr[inst->rS] >> sh);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;
    }

    case PPC_OP_SRAW:
    case PPC_OP_SRAWI: {
        u32 sh = (inst->op == PPC_OP_SRAWI) ? inst->sh : (cpu->gpr[inst->rB] & 0x3Fu);
        u32 value = cpu->gpr[inst->rS];
        cpu->gpr[inst->rA] = arith_shift_right(value, sh);
        update_sraw_ca(cpu, value, sh);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;
    }

    case PPC_OP_RLWINM:
        cpu->gpr[inst->rA] = rotl32(cpu->gpr[inst->rS], inst->sh) &
                             mask32(inst->mb, inst->me);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;

    case PPC_OP_RLWNM:
        cpu->gpr[inst->rA] = rotl32(cpu->gpr[inst->rS], cpu->gpr[inst->rB]) &
                             mask32(inst->mb, inst->me);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;

    case PPC_OP_RLWIMI: {
        u32 mask = mask32(inst->mb, inst->me);
        u32 rot = rotl32(cpu->gpr[inst->rS], inst->sh);
        cpu->gpr[inst->rA] = (cpu->gpr[inst->rA] & ~mask) | (rot & mask);
        if (inst->rc) set_cr0_from_gpr(cpu, inst->rA);
        break;
    }

    case PPC_OP_FADDS:
        cpu->fpr[inst->rD] = (f64)((f32)cpu->fpr[inst->rA] + (f32)cpu->fpr[inst->rB]);
        break;

    case PPC_OP_FSUBS:
        cpu->fpr[inst->rD] = (f64)((f32)cpu->fpr[inst->rA] - (f32)cpu->fpr[inst->rB]);
        break;

    case PPC_OP_FMULS:
        cpu->fpr[inst->rD] = (f64)((f32)cpu->fpr[inst->rA] * (f32)cpu->fpr[inst->rC]);
        break;

    case PPC_OP_FDIVS:
        cpu->fpr[inst->rD] = (f64)((f32)cpu->fpr[inst->rA] / (f32)cpu->fpr[inst->rB]);
        break;

    case PPC_OP_FADD:
        cpu->fpr[inst->rD] = cpu->fpr[inst->rA] + cpu->fpr[inst->rB];
        break;

    case PPC_OP_FSUB:
        cpu->fpr[inst->rD] = cpu->fpr[inst->rA] - cpu->fpr[inst->rB];
        break;

    case PPC_OP_FMUL:
        cpu->fpr[inst->rD] = cpu->fpr[inst->rA] * cpu->fpr[inst->rC];
        break;

    case PPC_OP_FDIV:
        cpu->fpr[inst->rD] = cpu->fpr[inst->rA] / cpu->fpr[inst->rB];
        break;

    case PPC_OP_FMR:
        cpu->fpr[inst->rD] = cpu->fpr[inst->rB];
        break;

    case PPC_OP_FNEG:
        cpu->fpr[inst->rD] = f64_from_bits(f64_to_bits(cpu->fpr[inst->rB]) ^ 0x8000000000000000ull);
        break;

    case PPC_OP_FABS:
        cpu->fpr[inst->rD] = f64_from_bits(f64_to_bits(cpu->fpr[inst->rB]) & 0x7FFFFFFFFFFFFFFFull);
        break;

    case PPC_OP_FNABS:
        cpu->fpr[inst->rD] = f64_from_bits(f64_to_bits(cpu->fpr[inst->rB]) | 0x8000000000000000ull);
        break;

    case PPC_OP_FRSP:
        cpu->fpr[inst->rD] = (f64)(f32)cpu->fpr[inst->rB];
        break;

    case PPC_OP_FCMPU:
    case PPC_OP_FCMPO: {
        f64 a = cpu->fpr[inst->rA];
        f64 b = cpu->fpr[inst->rB];
        u32 bits;
        if (a < b) bits = 0x8u;
        else if (a > b) bits = 0x4u;
        else if (a == b) bits = 0x2u;
        else bits = 0x1u;
        set_cr_field(cpu, inst->crfD, bits);
        break;
    }

    case PPC_OP_LWZ:
    case PPC_OP_LWZU: {
        bool update = inst->op == PPC_OP_LWZU;
        u32 ea = dform_ea(cpu, inst, update);
        cpu->gpr[inst->rD] = mem_read32(cpu, ea);
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_LBZ:
    case PPC_OP_LBZU: {
        bool update = inst->op == PPC_OP_LBZU;
        u32 ea = dform_ea(cpu, inst, update);
        cpu->gpr[inst->rD] = mem_read8(cpu, ea);
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_LHZ:
    case PPC_OP_LHZU: {
        bool update = inst->op == PPC_OP_LHZU;
        u32 ea = dform_ea(cpu, inst, update);
        cpu->gpr[inst->rD] = mem_read16(cpu, ea);
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_LHA:
    case PPC_OP_LHAU: {
        bool update = inst->op == PPC_OP_LHAU;
        u32 ea = dform_ea(cpu, inst, update);
        cpu->gpr[inst->rD] = (u32)(s32)(s16)mem_read16(cpu, ea);
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_LWZX:
    case PPC_OP_LWZUX: {
        bool update = inst->op == PPC_OP_LWZUX;
        u32 ea = xform_ea(cpu, inst, update);
        cpu->gpr[inst->rD] = mem_read32(cpu, ea);
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_LBZX:
    case PPC_OP_LBZUX: {
        bool update = inst->op == PPC_OP_LBZUX;
        u32 ea = xform_ea(cpu, inst, update);
        cpu->gpr[inst->rD] = mem_read8(cpu, ea);
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_LHZX:
    case PPC_OP_LHZUX: {
        bool update = inst->op == PPC_OP_LHZUX;
        u32 ea = xform_ea(cpu, inst, update);
        cpu->gpr[inst->rD] = mem_read16(cpu, ea);
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_LHAX:
    case PPC_OP_LHAUX: {
        bool update = inst->op == PPC_OP_LHAUX;
        u32 ea = xform_ea(cpu, inst, update);
        cpu->gpr[inst->rD] = (u32)(s32)(s16)mem_read16(cpu, ea);
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_LWBRX: {
        u32 ea = xform_ea(cpu, inst, false);
        cpu->gpr[inst->rD] = bswap32(mem_read32(cpu, ea));
        break;
    }

    case PPC_OP_LHBRX: {
        u32 ea = xform_ea(cpu, inst, false);
        cpu->gpr[inst->rD] = bswap16(mem_read16(cpu, ea));
        break;
    }

    case PPC_OP_LFS:
    case PPC_OP_LFSU: {
        bool update = inst->op == PPC_OP_LFSU;
        u32 ea = dform_ea(cpu, inst, update);
        cpu->fpr[inst->rD] = (f64)f32_from_bits(mem_read32(cpu, ea));
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_LFD:
    case PPC_OP_LFDU: {
        bool update = inst->op == PPC_OP_LFDU;
        u32 ea = dform_ea(cpu, inst, update);
        cpu->fpr[inst->rD] = f64_from_bits(mem_read64(cpu, ea));
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_LFSX:
    case PPC_OP_LFSUX: {
        bool update = inst->op == PPC_OP_LFSUX;
        u32 ea = xform_ea(cpu, inst, update);
        cpu->fpr[inst->rD] = (f64)f32_from_bits(mem_read32(cpu, ea));
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_LFDX:
    case PPC_OP_LFDUX: {
        bool update = inst->op == PPC_OP_LFDUX;
        u32 ea = xform_ea(cpu, inst, update);
        cpu->fpr[inst->rD] = f64_from_bits(mem_read64(cpu, ea));
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_PSQ_L:
    case PPC_OP_PSQ_LU: {
        bool update = inst->op == PPC_OP_PSQ_LU;
        u32 ea = dform_ea(cpu, inst, update);
        cpu->fpr[inst->rD] = (f64)f32_from_bits(mem_read32(cpu, ea));
        cpu->ps1[inst->rD] = inst->w ? 1.0 : (f64)f32_from_bits(mem_read32(cpu, ea + 4));
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_PSQ_LX:
    case PPC_OP_PSQ_LUX: {
        bool update = inst->op == PPC_OP_PSQ_LUX;
        u32 ea = xform_ea(cpu, inst, update);
        cpu->fpr[inst->rD] = (f64)f32_from_bits(mem_read32(cpu, ea));
        cpu->ps1[inst->rD] = inst->w ? 1.0 : (f64)f32_from_bits(mem_read32(cpu, ea + 4));
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_STW:
    case PPC_OP_STWU: {
        bool update = inst->op == PPC_OP_STWU;
        u32 ea = dform_ea(cpu, inst, update);
        mem_write32(cpu, ea, cpu->gpr[inst->rS]);
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_STB:
    case PPC_OP_STBU: {
        bool update = inst->op == PPC_OP_STBU;
        u32 ea = dform_ea(cpu, inst, update);
        mem_write8(cpu, ea, (u8)cpu->gpr[inst->rS]);
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_STH:
    case PPC_OP_STHU: {
        bool update = inst->op == PPC_OP_STHU;
        u32 ea = dform_ea(cpu, inst, update);
        mem_write16(cpu, ea, (u16)cpu->gpr[inst->rS]);
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_STWX:
    case PPC_OP_STWUX: {
        bool update = inst->op == PPC_OP_STWUX;
        u32 ea = xform_ea(cpu, inst, update);
        mem_write32(cpu, ea, cpu->gpr[inst->rS]);
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_STBX:
    case PPC_OP_STBUX: {
        bool update = inst->op == PPC_OP_STBUX;
        u32 ea = xform_ea(cpu, inst, update);
        mem_write8(cpu, ea, (u8)cpu->gpr[inst->rS]);
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_STHX:
    case PPC_OP_STHUX: {
        bool update = inst->op == PPC_OP_STHUX;
        u32 ea = xform_ea(cpu, inst, update);
        mem_write16(cpu, ea, (u16)cpu->gpr[inst->rS]);
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_STWBRX: {
        u32 ea = xform_ea(cpu, inst, false);
        mem_write32(cpu, ea, bswap32(cpu->gpr[inst->rS]));
        break;
    }

    case PPC_OP_STHBRX: {
        u32 ea = xform_ea(cpu, inst, false);
        mem_write16(cpu, ea, bswap16((u16)cpu->gpr[inst->rS]));
        break;
    }

    case PPC_OP_STFS:
    case PPC_OP_STFSU: {
        bool update = inst->op == PPC_OP_STFSU;
        u32 ea = dform_ea(cpu, inst, update);
        mem_write32(cpu, ea, f32_to_bits((f32)cpu->fpr[inst->rS]));
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_STFD:
    case PPC_OP_STFDU: {
        bool update = inst->op == PPC_OP_STFDU;
        u32 ea = dform_ea(cpu, inst, update);
        mem_write64(cpu, ea, f64_to_bits(cpu->fpr[inst->rS]));
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_STFSX:
    case PPC_OP_STFSUX: {
        bool update = inst->op == PPC_OP_STFSUX;
        u32 ea = xform_ea(cpu, inst, update);
        mem_write32(cpu, ea, f32_to_bits((f32)cpu->fpr[inst->rS]));
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_STFDX:
    case PPC_OP_STFDUX: {
        bool update = inst->op == PPC_OP_STFDUX;
        u32 ea = xform_ea(cpu, inst, update);
        mem_write64(cpu, ea, f64_to_bits(cpu->fpr[inst->rS]));
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_PSQ_ST:
    case PPC_OP_PSQ_STU: {
        bool update = inst->op == PPC_OP_PSQ_STU;
        u32 ea = dform_ea(cpu, inst, update);
        mem_write32(cpu, ea, f32_to_bits((f32)cpu->fpr[inst->rS]));
        if (!inst->w)
            mem_write32(cpu, ea + 4, f32_to_bits((f32)cpu->ps1[inst->rS]));
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_PSQ_STX:
    case PPC_OP_PSQ_STUX: {
        bool update = inst->op == PPC_OP_PSQ_STUX;
        u32 ea = xform_ea(cpu, inst, update);
        mem_write32(cpu, ea, f32_to_bits((f32)cpu->fpr[inst->rS]));
        if (!inst->w)
            mem_write32(cpu, ea + 4, f32_to_bits((f32)cpu->ps1[inst->rS]));
        if (update) cpu->gpr[inst->rA] = ea;
        break;
    }

    case PPC_OP_LMW: {
        u32 ea = dform_ea(cpu, inst, false);
        for (u32 r = inst->rD; r < 32; r++, ea += 4)
            cpu->gpr[r] = mem_read32(cpu, ea);
        break;
    }

    case PPC_OP_STMW: {
        u32 ea = dform_ea(cpu, inst, false);
        for (u32 r = inst->rS; r < 32; r++, ea += 4)
            mem_write32(cpu, ea, cpu->gpr[r]);
        break;
    }

    case PPC_OP_DCBZ: {
        u32 ea = xform_ea(cpu, inst, false) & ~31u;
        for (u32 i = 0; i < 32; i += 4)
            mem_write32(cpu, ea + i, 0);
        break;
    }

    case PPC_OP_B:
        if (inst->lk)
            cpu->lr = inst->address + 4;
        cpu->pc = inst->branch_target;
        break;

    case PPC_OP_BC:
        if (branch_condition(cpu, inst->bo, inst->bi)) {
            if (inst->lk)
                cpu->lr = inst->address + 4;
            cpu->pc = inst->branch_target;
        }
        break;

    case PPC_OP_BCLR:
        if (branch_condition(cpu, inst->bo, inst->bi)) {
            u32 target = cpu->lr & ~3u;
            if (inst->lk)
                cpu->lr = inst->address + 4;
            cpu->pc = target;
        }
        break;

    case PPC_OP_BCCTR:
        if (branch_condition(cpu, inst->bo, inst->bi)) {
            u32 target = cpu->ctr & ~3u;
            if (inst->lk)
                cpu->lr = inst->address + 4;
            cpu->pc = target;
        }
        break;

    case PPC_OP_CRAND:
        set_cr_bit(cpu, inst->rD, cr_bit(cpu, inst->rA) & cr_bit(cpu, inst->rB));
        break;

    case PPC_OP_CRANDC:
        set_cr_bit(cpu, inst->rD, cr_bit(cpu, inst->rA) & !cr_bit(cpu, inst->rB));
        break;

    case PPC_OP_CREQV:
        set_cr_bit(cpu, inst->rD, !(cr_bit(cpu, inst->rA) ^ cr_bit(cpu, inst->rB)));
        break;

    case PPC_OP_CRNAND:
        set_cr_bit(cpu, inst->rD, !(cr_bit(cpu, inst->rA) & cr_bit(cpu, inst->rB)));
        break;

    case PPC_OP_CRNOR:
        set_cr_bit(cpu, inst->rD, !(cr_bit(cpu, inst->rA) | cr_bit(cpu, inst->rB)));
        break;

    case PPC_OP_CROR:
        set_cr_bit(cpu, inst->rD, cr_bit(cpu, inst->rA) | cr_bit(cpu, inst->rB));
        break;

    case PPC_OP_CRORC:
        set_cr_bit(cpu, inst->rD, cr_bit(cpu, inst->rA) | !cr_bit(cpu, inst->rB));
        break;

    case PPC_OP_CRXOR:
        set_cr_bit(cpu, inst->rD, cr_bit(cpu, inst->rA) ^ cr_bit(cpu, inst->rB));
        break;

    case PPC_OP_MCRF:
        set_cr_field(cpu, inst->crfD, get_cr_field(cpu, inst->crfS));
        break;

    case PPC_OP_MFCR:
        cpu->gpr[inst->rD] = cpu->cr;
        break;

    case PPC_OP_MTCRF: {
        u32 mask = mtcrf_mask(inst->crm);
        cpu->cr = (cpu->cr & ~mask) | (cpu->gpr[inst->rS] & mask);
        break;
    }

    case PPC_OP_MFSPR:
        if (inst->spr == 1) cpu->gpr[inst->rD] = cpu->xer;
        else if (inst->spr == 8) cpu->gpr[inst->rD] = cpu->lr;
        else if (inst->spr == 9) cpu->gpr[inst->rD] = cpu->ctr;
        break;

    case PPC_OP_MTSPR:
        if (inst->spr == 1) cpu->xer = cpu->gpr[inst->rS];
        else if (inst->spr == 8) cpu->lr = cpu->gpr[inst->rS];
        else if (inst->spr == 9) cpu->ctr = cpu->gpr[inst->rS];
        break;

    default:
        break;
    }
}

static void exec_raw(CPUState* cpu, u32 raw, u32 address) {
    PPCInst inst = ppc_decode(raw, address);
    exec_inst(cpu, &inst);
}

static void test_immediate_arithmetic(CPUState* cpu) {
    printf("--- immediate arithmetic ---\n");

    cpu_reset(cpu);
    exec_raw(cpu, make_dform(14, 3, 0, 42), BASE);
    check_eq(cpu->gpr[3], 42, "addi/li literal");

    cpu->gpr[4] = 100;
    exec_raw(cpu, make_dform(14, 3, 4, (u16)(s16)-10), BASE);
    check_eq(cpu->gpr[3], 90, "addi negative SIMM");

    exec_raw(cpu, 0x3CA01234, BASE);
    check_eq(cpu->gpr[5], 0x12340000, "addis/lis shifted immediate");

    cpu->gpr[4] = 0x00020000;
    exec_raw(cpu, make_dform(15, 5, 4, 0xFFFF), BASE);
    check_eq(cpu->gpr[5], 0x00010000, "addis negative shifted immediate");

    cpu->gpr[4] = 0xFFFFFFFEu;
    exec_raw(cpu, make_dform(7, 3, 4, (u16)(s16)-7), BASE);
    check_eq(cpu->gpr[3], 14, "mulli negative times negative");

    cpu->gpr[4] = 0x80000000u;
    exec_raw(cpu, make_dform(7, 3, 4, 2), BASE);
    check_eq(cpu->gpr[3], 0, "mulli keeps low 32 bits");

    cpu->gpr[5] = 5;
    exec_raw(cpu, make_dform(8, 4, 5, 1), BASE);
    check_eq(cpu->gpr[4], 0xFFFFFFFCu, "subfic immediate minus register");
    check_eq(cpu->xer & XER_CA, 0, "subfic borrow clears CA");

    cpu->gpr[5] = 1;
    exec_raw(cpu, make_dform(8, 4, 5, 1), BASE);
    check_eq(cpu->gpr[4], 0, "subfic equal result");
    check_eq(cpu->xer & XER_CA, XER_CA, "subfic no borrow sets CA");

    cpu->gpr[4] = 0;
    exec_raw(cpu, 0x3084FFFF, BASE);
    check_eq(cpu->gpr[4], 0xFFFFFFFFu, "addic 0 + -1 result");
    check_eq(cpu->xer & XER_CA, 0, "addic 0 + -1 clears CA");

    cpu->gpr[4] = 1;
    exec_raw(cpu, 0x3084FFFF, BASE);
    check_eq(cpu->gpr[4], 0, "addic 1 + -1 result");
    check_eq(cpu->xer & XER_CA, XER_CA, "addic 1 + -1 sets CA");

    cpu->gpr[5] = 1;
    cpu->xer = 0;
    exec_raw(cpu, 0x34A5FFFF, BASE);
    check_eq(cpu->gpr[5], 0, "addic. result");
    check_eq(get_cr_field(cpu, 0), 0x2, "addic. records EQ with SO clear");
}

static void test_compare_and_bc(CPUState* cpu) {
    printf("--- compare / bc ---\n");

    cpu_reset(cpu);
    cpu->gpr[3] = 7;
    exec_raw(cpu, make_dform(11, 0, 3, 7), BASE);
    exec_raw(cpu, 0x41820014, BASE + 0x64);
    check_eq(cpu->pc, BASE + 0x78, "cmpwi equal then beq taken");

    cpu->gpr[3] = 0xFFFFFFFFu;
    exec_raw(cpu, 0x28038000, BASE);
    check_eq(get_cr_field(cpu, 0), 0x4, "cmplwi unsigned greater CR0");

    cpu->gpr[3] = 0xFFFFFFFFu;
    cpu->gpr[4] = 0;
    exec_raw(cpu, 0x7C832000, BASE);
    check_eq(get_cr_field(cpu, 1), 0x8, "cmpw signed less in CR1");

    exec_raw(cpu, 0x7D032040, BASE);
    check_eq(get_cr_field(cpu, 2), 0x4, "cmplw unsigned greater in CR2");

    cpu->ctr = 1;
    cpu->cr = 0;
    exec_raw(cpu, make_dform(16, 0, 0, 0x0010), BASE + 0x100);
    check_eq(cpu->ctr, 0, "bc decrements CTR when BO says so");
    check_eq(cpu->pc == BASE + 0x104, 1, "bc not taken when CTR condition false");
}

static void test_register_arithmetic(CPUState* cpu) {
    printf("--- register arithmetic ---\n");

    cpu_reset(cpu);
    cpu->gpr[11] = 10;
    cpu->gpr[12] = 20;
    exec_raw(cpu, 0x7D4B6214, BASE);
    check_eq(cpu->gpr[10], 30, "add");

    cpu->gpr[12] = 0xFFFFFFFFu;
    cpu->gpr[13] = 1;
    exec_raw(cpu, 0x7D6C6814, BASE);
    check_eq(cpu->gpr[11], 0, "addc wraps");
    check_eq(cpu->xer & XER_CA, XER_CA, "addc sets CA on carry");

    cpu->gpr[13] = 0xFFFFFFFFu;
    cpu->gpr[14] = 0;
    cpu->xer = XER_CA;
    exec_raw(cpu, 0x7D8D7114, BASE);
    check_eq(cpu->gpr[12], 0, "adde includes CA");
    check_eq(cpu->xer & XER_CA, XER_CA, "adde keeps carry");

    cpu->gpr[14] = 0xFFFFFFFFu;
    cpu->xer = XER_CA;
    exec_raw(cpu, 0x7DAE0194, BASE);
    check_eq(cpu->gpr[13], 0, "addze includes CA");
    check_eq(cpu->xer & XER_CA, XER_CA, "addze sets carry");

    cpu->gpr[15] = 5;
    cpu->gpr[16] = 9;
    exec_raw(cpu, 0x7DCF8050, BASE);
    check_eq(cpu->gpr[14], 4, "subf rB-rA");

    cpu->gpr[16] = 5;
    cpu->gpr[17] = 5;
    exec_raw(cpu, 0x7DF08810, BASE);
    check_eq(cpu->gpr[15], 0, "subfc equal");
    check_eq(cpu->xer & XER_CA, XER_CA, "subfc equal sets CA");

    cpu->gpr[17] = 7;
    cpu->gpr[18] = 5;
    cpu->xer = XER_CA;
    exec_raw(cpu, 0x7E119110, BASE);
    check_eq(cpu->gpr[16], 0xFFFFFFFEu, "subfe with borrow result");
    check_eq(cpu->xer & XER_CA, 0, "subfe borrow clears CA");

    cpu->gpr[18] = 0;
    cpu->xer = XER_CA;
    exec_raw(cpu, 0x7E320190, BASE);
    check_eq(cpu->gpr[17], 0, "subfze zero with CA");
    check_eq(cpu->xer & XER_CA, XER_CA, "subfze zero sets CA");

    cpu->gpr[19] = 5;
    exec_raw(cpu, 0x7E5300D0, BASE);
    check_eq(cpu->gpr[18], 0xFFFFFFFBu, "neg");

    cpu->gpr[4] = 0x80000000u;
    cpu->gpr[5] = 2;
    exec_raw(cpu, 0x7C6429D6, BASE);
    check_eq(cpu->gpr[3], 0, "mullw keeps low word");

    cpu->gpr[7] = 0xFFFFFFFFu;
    cpu->gpr[8] = 2;
    exec_raw(cpu, 0x7CC74096, BASE);
    check_eq(cpu->gpr[6], 0xFFFFFFFFu, "mulhw signed high word");

    cpu->gpr[10] = 0xFFFFFFFFu;
    cpu->gpr[11] = 2;
    exec_raw(cpu, 0x7D2A5816, BASE);
    check_eq(cpu->gpr[9], 1, "mulhwu unsigned high word");

    cpu->gpr[13] = (u32)(s32)-7;
    cpu->gpr[14] = 2;
    exec_raw(cpu, 0x7D8D73D6, BASE);
    check_eq(cpu->gpr[12], 0xFFFFFFFDu, "divw truncates toward zero");

    cpu->gpr[16] = 7;
    cpu->gpr[17] = 2;
    exec_raw(cpu, 0x7DF08B96, BASE);
    check_eq(cpu->gpr[15], 3, "divwu unsigned quotient");

    cpu->gpr[3] = 0xFFFFFFFFu;
    cpu->gpr[4] = 1;
    exec_raw(cpu, make_xform(266, 5, 3, 4, true), BASE);
    check_eq(cpu->gpr[5], 0, "add. result");
    check_eq(get_cr_field(cpu, 0), 0x2, "add. records EQ");
}

static void test_logical_shift_rotate(CPUState* cpu) {
    printf("--- logical / shift / rotate ---\n");

    cpu_reset(cpu);
    cpu->gpr[20] = 0xF0F0F0F0u;
    cpu->gpr[21] = 0x0FF00FF0u;
    exec_raw(cpu, 0x7E93A838, BASE);
    check_eq(cpu->gpr[19], 0x00F000F0u, "and");

    cpu->gpr[22] = 0x00FF00FFu;
    exec_raw(cpu, 0x7EB4B078, BASE);
    check_eq(cpu->gpr[20], 0x0F000F00u, "andc");

    cpu->gpr[22] = 0x12340000u;
    cpu->gpr[23] = 0x00005678u;
    exec_raw(cpu, 0x7ED5BB78, BASE);
    check_eq(cpu->gpr[21], 0x12345678u, "or");

    cpu->gpr[23] = 0x00000000u;
    cpu->gpr[24] = 0xFFFF0000u;
    exec_raw(cpu, 0x7EF6C338, BASE);
    check_eq(cpu->gpr[22], 0x0000FFFFu, "orc");

    cpu->gpr[24] = 0xAAAA5555u;
    cpu->gpr[25] = 0xFFFF0000u;
    exec_raw(cpu, 0x7F17CA78, BASE);
    check_eq(cpu->gpr[23], 0x55555555u, "xor");

    cpu->gpr[25] = 0xFFFFFFFFu;
    cpu->gpr[26] = 0x0F0F0F0Fu;
    exec_raw(cpu, 0x7F38D3B8, BASE);
    check_eq(cpu->gpr[24], 0xF0F0F0F0u, "nand");

    cpu->gpr[26] = 0xF0000000u;
    cpu->gpr[27] = 0x0F000000u;
    exec_raw(cpu, 0x7F59D8F8, BASE);
    check_eq(cpu->gpr[25], 0x00FFFFFFu, "nor");

    cpu->gpr[27] = 0xFFFF0000u;
    cpu->gpr[28] = 0xFF00FF00u;
    exec_raw(cpu, 0x7F7AE238, BASE);
    check_eq(cpu->gpr[26], 0xFF0000FFu, "eqv");

    cpu->gpr[28] = 0;
    exec_raw(cpu, 0x7F9B0034, BASE);
    check_eq(cpu->gpr[27], 32, "cntlzw zero");
    cpu->gpr[28] = 0x00F00000u;
    exec_raw(cpu, 0x7F9B0034, BASE);
    check_eq(cpu->gpr[27], 8, "cntlzw leading zeros");

    cpu->gpr[29] = 0x00000080u;
    exec_raw(cpu, 0x7FBC0774, BASE);
    check_eq(cpu->gpr[28], 0xFFFFFF80u, "extsb sign");

    cpu->gpr[30] = 0x00008001u;
    exec_raw(cpu, 0x7FDD0734, BASE);
    check_eq(cpu->gpr[29], 0xFFFF8001u, "extsh sign");

    cpu->gpr[31] = 1;
    cpu->gpr[3] = 31;
    exec_raw(cpu, 0x7FFE1830, BASE);
    check_eq(cpu->gpr[30], 0x80000000u, "slw shift 31");
    cpu->gpr[3] = 32;
    exec_raw(cpu, 0x7FFE1830, BASE);
    check_eq(cpu->gpr[30], 0, "slw shift 32 clears");

    cpu->gpr[3] = 0x80000000u;
    cpu->gpr[4] = 31;
    exec_raw(cpu, 0x7C7F2430, BASE);
    check_eq(cpu->gpr[31], 1, "srw shift 31");
    cpu->gpr[4] = 32;
    exec_raw(cpu, 0x7C7F2430, BASE);
    check_eq(cpu->gpr[31], 0, "srw shift 32 clears");

    cpu->gpr[4] = 0x80000001u;
    cpu->gpr[5] = 1;
    exec_raw(cpu, 0x7C832E30, BASE);
    check_eq(cpu->gpr[3], 0xC0000000u, "sraw arithmetic shift");
    check_eq(cpu->xer & XER_CA, XER_CA, "sraw sets CA when shifting out ones");
    cpu->gpr[5] = 32;
    exec_raw(cpu, 0x7C832E30, BASE);
    check_eq(cpu->gpr[3], 0xFFFFFFFFu, "sraw shift 32 sign fills");
    check_eq(cpu->xer & XER_CA, XER_CA, "sraw shift 32 sets CA for negative");

    cpu->gpr[5] = 0x00000080u;
    exec_raw(cpu, make_srawi(5, 4, 7, false), BASE);
    check_eq(cpu->gpr[4], 1, "srawi positive");
    check_eq(cpu->xer & XER_CA, 0, "srawi positive clears CA");

    cpu->gpr[6] = 0x12345678u;
    exec_raw(cpu, make_mform(21, 6, 5, 5, 8, 23, false), BASE);
    check_eq(cpu->gpr[5], 0x008ACF00u, "rlwinm mask");

    cpu->gpr[7] = 0x89ABCDEFu;
    cpu->gpr[8] = 36;
    exec_raw(cpu, make_mform(23, 7, 6, 8, 4, 27, false), BASE);
    check_eq(cpu->gpr[6], 0x0ABCDEF0u, "rlwnm masks variable rotate");

    cpu->gpr[7] = 0xAA00AA00u;
    cpu->gpr[8] = 0x12345678u;
    exec_raw(cpu, make_mform(20, 8, 7, 8, 8, 15, false), BASE);
    check_eq(cpu->gpr[7], 0xAA56AA00u, "rlwimi inserts masked rotate");
}

static void test_immediate_logical(CPUState* cpu) {
    printf("--- immediate logical ---\n");

    cpu_reset(cpu);
    cpu->gpr[3] = 0x12340000;
    exec_raw(cpu, make_dform(24, 3, 4, 0x5678), BASE);
    check_eq(cpu->gpr[4], 0x12345678, "ori low half");

    cpu->gpr[4] = 0x00005678;
    exec_raw(cpu, 0x64851234, BASE);
    check_eq(cpu->gpr[5], 0x12345678, "oris high half");

    cpu->gpr[5] = 0xAAAA0000;
    exec_raw(cpu, 0x68A6FFFF, BASE);
    check_eq(cpu->gpr[6], 0xAAAAFFFF, "xori low half");

    cpu->gpr[6] = 0x2AAA5555;
    exec_raw(cpu, 0x6CC78000, BASE);
    check_eq(cpu->gpr[7], 0xAAAA5555, "xoris high half");

    cpu->gpr[7] = 0x123400F0;
    exec_raw(cpu, 0x70E800FF, BASE);
    check_eq(cpu->gpr[8], 0xF0, "andi. result");
    check_eq(get_cr_field(cpu, 0), 0x4, "andi. updates CR0 nonzero");

    cpu->gpr[7] = 0x12FF0000;
    exec_raw(cpu, 0x74E900FF, BASE);
    check_eq(cpu->gpr[9], 0x00FF0000, "andis. result");
    check_eq(get_cr_field(cpu, 0), 0x4, "andis. updates CR0 nonzero");
}

static void test_loads(CPUState* cpu) {
    u32 base = GC_RAM_BASE + 0x1000;
    printf("--- loads ---\n");

    cpu_reset(cpu);
    cpu->gpr[1] = base;
    mem_write32(cpu, base, 0xDEADBEEF);
    mem_write32(cpu, base + 4, 0xCAFEBABE);
    exec_raw(cpu, 0x80610000, BASE);
    check_eq(cpu->gpr[3], 0xDEADBEEF, "lwz offset 0");
    exec_raw(cpu, 0x84810004, BASE);
    check_eq(cpu->gpr[4], 0xCAFEBABE, "lwzu loads updated address");
    check_eq(cpu->gpr[1], base + 4, "lwzu updates rA");

    cpu->gpr[1] = base;
    mem_write8(cpu, base + 8, 0xA5);
    mem_write8(cpu, base + 12, 0x7F);
    exec_raw(cpu, 0x88A10008, BASE);
    check_eq(cpu->gpr[5], 0xA5, "lbz zero-extends byte");
    exec_raw(cpu, 0x8CC1000C, BASE);
    check_eq(cpu->gpr[6], 0x7F, "lbzu loads updated address");
    check_eq(cpu->gpr[1], base + 12, "lbzu updates rA");

    cpu->gpr[1] = base;
    mem_write16(cpu, base + 16, 0x1234);
    mem_write16(cpu, base + 20, 0x7FFF);
    mem_write16(cpu, base - 4, 0x8001);
    mem_write16(cpu, base + 24, 0x8001);
    exec_raw(cpu, 0xA0E10010, BASE);
    check_eq(cpu->gpr[7], 0x1234, "lhz zero-extends halfword");
    exec_raw(cpu, 0xA5010014, BASE);
    check_eq(cpu->gpr[8], 0x7FFF, "lhzu loads updated address");
    check_eq(cpu->gpr[1], base + 20, "lhzu updates rA");

    cpu->gpr[1] = base;
    exec_raw(cpu, 0xA921FFFC, BASE);
    check_eq(cpu->gpr[9], 0xFFFF8001, "lha sign-extends halfword");
    exec_raw(cpu, 0xAD410018, BASE);
    check_eq(cpu->gpr[10], 0xFFFF8001, "lhau sign-extends halfword");
    check_eq(cpu->gpr[1], base + 24, "lhau updates rA");

    cpu->gpr[1] = base;
    for (u32 r = 20; r < 32; r++)
        mem_write32(cpu, base + 52 + (r - 20) * 4, 0xA0000000u | r);
    exec_raw(cpu, 0xBA810034, BASE);
    check_eq(cpu->gpr[20], 0xA0000014u, "lmw first register");
    check_eq(cpu->gpr[31], 0xA000001Fu, "lmw last register");
}

static void test_stores(CPUState* cpu) {
    u32 base = GC_RAM_BASE + 0x2000;
    printf("--- stores ---\n");

    cpu_reset(cpu);
    cpu->gpr[1] = base;
    cpu->gpr[3] = 0xAABBCCDD;
    exec_raw(cpu, 0x9061001C, BASE);
    check_eq(mem_read32(cpu, base + 28), 0xAABBCCDD, "stw offset 28");

    cpu->gpr[4] = 0x11223344;
    exec_raw(cpu, 0x94810020, BASE);
    check_eq(mem_read32(cpu, base + 32), 0x11223344, "stwu stores updated address");
    check_eq(cpu->gpr[1], base + 32, "stwu updates rA");

    cpu->gpr[1] = base;
    cpu->gpr[5] = 0x123456A5;
    exec_raw(cpu, 0x98A10024, BASE);
    check_eq(mem_read8(cpu, base + 36), 0xA5, "stb stores low byte");

    cpu->gpr[6] = 0x7E;
    exec_raw(cpu, 0x9CC10028, BASE);
    check_eq(mem_read8(cpu, base + 40), 0x7E, "stbu stores updated address");
    check_eq(cpu->gpr[1], base + 40, "stbu updates rA");

    cpu->gpr[1] = base;
    cpu->gpr[7] = 0xFFFFABCD;
    exec_raw(cpu, 0xB0E1002C, BASE);
    check_eq(mem_read16(cpu, base + 44), 0xABCD, "sth stores low halfword");

    cpu->gpr[8] = 0x1234;
    exec_raw(cpu, 0xB5010030, BASE);
    check_eq(mem_read16(cpu, base + 48), 0x1234, "sthu stores updated address");
    check_eq(cpu->gpr[1], base + 48, "sthu updates rA");

    cpu->gpr[1] = base;
    for (u32 r = 20; r < 32; r++)
        cpu->gpr[r] = 0xB0000000u | r;
    exec_raw(cpu, 0xBE810064, BASE);
    check_eq(mem_read32(cpu, base + 100), 0xB0000014u, "stmw first register");
    check_eq(mem_read32(cpu, base + 100 + 11 * 4), 0xB000001Fu, "stmw last register");
}

static void test_indexed_memory(CPUState* cpu) {
    u32 base = GC_RAM_BASE + 0x3000;
    printf("--- indexed memory ---\n");

    cpu_reset(cpu);
    cpu->gpr[4] = base;
    cpu->gpr[5] = 0x20;
    mem_write32(cpu, base + 0x20, 0x01020304);
    mem_write8(cpu, base + 0x24, 0xA5);
    mem_write16(cpu, base + 0x28, 0x1234);
    mem_write16(cpu, base + 0x2C, 0x8003);

    exec_raw(cpu, 0x7C64282E, BASE);
    check_eq(cpu->gpr[3], 0x01020304, "lwzx");

    cpu->gpr[4] = base;
    exec_raw(cpu, 0x7CC4286E, BASE);
    check_eq(cpu->gpr[6], 0x01020304, "lwzux");
    check_eq(cpu->gpr[4], base + 0x20, "lwzux updates rA");

    cpu->gpr[4] = base + 4;
    exec_raw(cpu, 0x7CE428AE, BASE);
    check_eq(cpu->gpr[7], 0xA5, "lbzx");

    cpu->gpr[4] = base + 4;
    exec_raw(cpu, 0x7D0428EE, BASE);
    check_eq(cpu->gpr[8], 0xA5, "lbzux");
    check_eq(cpu->gpr[4], base + 0x24, "lbzux updates rA");

    cpu->gpr[4] = base + 8;
    exec_raw(cpu, 0x7D242A2E, BASE);
    check_eq(cpu->gpr[9], 0x1234, "lhzx");

    cpu->gpr[4] = base + 8;
    exec_raw(cpu, 0x7D442A6E, BASE);
    check_eq(cpu->gpr[10], 0x1234, "lhzux");
    check_eq(cpu->gpr[4], base + 0x28, "lhzux updates rA");

    cpu->gpr[4] = base + 12;
    exec_raw(cpu, 0x7D642AAE, BASE);
    check_eq(cpu->gpr[11], 0xFFFF8003, "lhax");

    cpu->gpr[4] = base + 12;
    exec_raw(cpu, 0x7D842AEE, BASE);
    check_eq(cpu->gpr[12], 0xFFFF8003, "lhaux");
    check_eq(cpu->gpr[4], base + 0x2C, "lhaux updates rA");

    cpu->gpr[4] = base;
    cpu->gpr[3] = 0xAABBCCDD;
    exec_raw(cpu, 0x7C64292E, BASE);
    check_eq(mem_read32(cpu, base + 0x20), 0xAABBCCDD, "stwx");

    cpu->gpr[4] = base + 4;
    cpu->gpr[6] = 0x11223344;
    exec_raw(cpu, 0x7CC4296E, BASE);
    check_eq(mem_read32(cpu, base + 0x24), 0x11223344, "stwux");
    check_eq(cpu->gpr[4], base + 0x24, "stwux updates rA");

    cpu->gpr[4] = base + 8;
    cpu->gpr[7] = 0x0000005A;
    exec_raw(cpu, 0x7CE429AE, BASE);
    check_eq(mem_read8(cpu, base + 0x28), 0x5A, "stbx");

    cpu->gpr[4] = base + 12;
    cpu->gpr[8] = 0x0000006B;
    exec_raw(cpu, 0x7D0429EE, BASE);
    check_eq(mem_read8(cpu, base + 0x2C), 0x6B, "stbux");
    check_eq(cpu->gpr[4], base + 0x2C, "stbux updates rA");

    cpu->gpr[4] = base + 16;
    cpu->gpr[9] = 0x0000CAFE;
    exec_raw(cpu, 0x7D242B2E, BASE);
    check_eq(mem_read16(cpu, base + 0x30), 0xCAFE, "sthx");

    cpu->gpr[4] = base + 20;
    cpu->gpr[10] = 0x0000FACE;
    exec_raw(cpu, 0x7D442B6E, BASE);
    check_eq(mem_read16(cpu, base + 0x34), 0xFACE, "sthux");
    check_eq(cpu->gpr[4], base + 0x34, "sthux updates rA");

    mem_write32(cpu, base + 0x38, 0x01020304);
    cpu->gpr[4] = base;
    cpu->gpr[5] = 0x38;
    exec_raw(cpu, 0x7C642C2C, BASE);
    check_eq(cpu->gpr[3], 0x04030201, "lwbrx byte-reverses word");

    mem_write32(cpu, base + 0x60, 0x11223344);
    cpu->gpr[5] = base + 0x60;
    exec_raw(cpu, 0x7C602C2C, BASE);
    check_eq(cpu->gpr[3], 0x44332211, "lwbrx uses zero base when rA is zero");

    mem_write16(cpu, base + 0x3C, 0x1234);
    cpu->gpr[7] = base;
    cpu->gpr[8] = 0x3C;
    exec_raw(cpu, 0x7CC7462C, BASE);
    check_eq(cpu->gpr[6], 0x3412, "lhbrx byte-reverses halfword");

    cpu->gpr[10] = base;
    cpu->gpr[11] = 0x40;
    cpu->gpr[9] = 0xA1B2C3D4;
    exec_raw(cpu, 0x7D2A5D2C, BASE);
    check_eq(mem_read32(cpu, base + 0x40), 0xD4C3B2A1, "stwbrx byte-reverses word");

    cpu->gpr[13] = base;
    cpu->gpr[14] = 0x44;
    cpu->gpr[12] = 0x00001234;
    exec_raw(cpu, 0x7D8D772C, BASE);
    check_eq(mem_read16(cpu, base + 0x44), 0x3412, "sthbrx byte-reverses halfword");

    mem_write32(cpu, base + 0x7C, 0x11111111);
    for (u32 i = 0; i < 32; i += 4)
        mem_write32(cpu, base + 0x80 + i, 0xFFFFFFFF);
    mem_write32(cpu, base + 0xA0, 0x22222222);
    cpu->gpr[15] = base + 0x80;
    cpu->gpr[16] = 0x13;
    exec_raw(cpu, 0x7C0F87EC, BASE);
    check_eq(mem_read32(cpu, base + 0x80), 0, "dcbz clears first word of cache block");
    check_eq(mem_read32(cpu, base + 0x9C), 0, "dcbz clears last word of cache block");
    check_eq(mem_read32(cpu, base + 0x7C), 0x11111111, "dcbz leaves previous block");
    check_eq(mem_read32(cpu, base + 0xA0), 0x22222222, "dcbz leaves next block");

    for (u32 i = 0; i < 32; i += 4)
        mem_write32(cpu, base + 0xC0 + i, 0x77777777);
    cpu->gpr[16] = base + 0xD7;
    exec_raw(cpu, 0x7C0087EC, BASE);
    check_eq(mem_read32(cpu, base + 0xC0), 0, "dcbz uses zero base when rA is zero");
}

static void test_fpu_memory(CPUState* cpu) {
    u32 base = GC_RAM_BASE + 0x4000;
    printf("--- FPU memory ---\n");

    cpu_reset(cpu);
    cpu->gpr[4] = base;

    mem_write32(cpu, base, 0x3F800000u);
    exec_raw(cpu, 0xC0240000, BASE);
    check_eq(f32_to_bits((f32)cpu->fpr[1]), 0x3F800000u, "lfs loads single");

    mem_write32(cpu, base + 4, 0x80000000u);
    exec_raw(cpu, 0xC4440004, BASE);
    check_eq(f32_to_bits((f32)cpu->fpr[2]), 0x80000000u, "lfsu preserves negative zero");
    check_eq(cpu->gpr[4], base + 4, "lfsu updates rA");

    cpu->gpr[4] = base;
    mem_write64(cpu, base + 8, 0x400921FB54442D18ull);
    exec_raw(cpu, 0xC8640008, BASE);
    check_eq64(f64_to_bits(cpu->fpr[3]), 0x400921FB54442D18ull, "lfd loads double");

    mem_write64(cpu, base + 16, 0x8000000000000000ull);
    exec_raw(cpu, 0xCC840010, BASE);
    check_eq64(f64_to_bits(cpu->fpr[4]), 0x8000000000000000ull, "lfdu preserves negative zero");
    check_eq(cpu->gpr[4], base + 16, "lfdu updates rA");

    cpu->gpr[4] = base;
    cpu->fpr[5] = (f64)f32_from_bits(0x3FC00000u);
    exec_raw(cpu, 0xD0A40014, BASE);
    check_eq(mem_read32(cpu, base + 20), 0x3FC00000u, "stfs stores single");

    cpu->fpr[6] = (f64)f32_from_bits(0xBF800000u);
    exec_raw(cpu, 0xD4C40018, BASE);
    check_eq(mem_read32(cpu, base + 24), 0xBF800000u, "stfsu stores single");
    check_eq(cpu->gpr[4], base + 24, "stfsu updates rA");

    cpu->gpr[4] = base;
    cpu->fpr[7] = f64_from_bits(0x4004000000000000ull);
    exec_raw(cpu, 0xD8E40020, BASE);
    check_eq64(mem_read64(cpu, base + 32), 0x4004000000000000ull, "stfd stores double");

    cpu->fpr[8] = f64_from_bits(0xBFF0000000000000ull);
    exec_raw(cpu, 0xDD040028, BASE);
    check_eq64(mem_read64(cpu, base + 40), 0xBFF0000000000000ull, "stfdu stores double");
    check_eq(cpu->gpr[4], base + 40, "stfdu updates rA");

    cpu->gpr[4] = base;
    cpu->gpr[5] = 0x80;
    mem_write32(cpu, base + 0x80, 0x40490FDBu);
    exec_raw(cpu, 0x7D242C2E, BASE);
    check_eq(f32_to_bits((f32)cpu->fpr[9]), 0x40490FDBu, "lfsx loads single");

    cpu->gpr[4] = base;
    cpu->gpr[5] = 0x84;
    mem_write32(cpu, base + 0x84, 0xC0200000u);
    exec_raw(cpu, 0x7D442C6E, BASE);
    check_eq(f32_to_bits((f32)cpu->fpr[10]), 0xC0200000u, "lfsux loads single");
    check_eq(cpu->gpr[4], base + 0x84, "lfsux updates rA");

    cpu->gpr[4] = base;
    cpu->gpr[5] = 0x88;
    mem_write64(cpu, base + 0x88, 0x3FF8000000000000ull);
    exec_raw(cpu, 0x7D642CAE, BASE);
    check_eq64(f64_to_bits(cpu->fpr[11]), 0x3FF8000000000000ull, "lfdx loads double");

    cpu->gpr[4] = base;
    cpu->gpr[5] = 0x90;
    mem_write64(cpu, base + 0x90, 0xC008000000000000ull);
    exec_raw(cpu, 0x7D842CEE, BASE);
    check_eq64(f64_to_bits(cpu->fpr[12]), 0xC008000000000000ull, "lfdux loads double");
    check_eq(cpu->gpr[4], base + 0x90, "lfdux updates rA");

    cpu->gpr[4] = base;
    cpu->gpr[5] = 0x98;
    cpu->fpr[13] = (f64)f32_from_bits(0x41100000u);
    exec_raw(cpu, 0x7DA42D2E, BASE);
    check_eq(mem_read32(cpu, base + 0x98), 0x41100000u, "stfsx stores single");

    cpu->gpr[4] = base;
    cpu->gpr[5] = 0x9C;
    cpu->fpr[14] = (f64)f32_from_bits(0xC1200000u);
    exec_raw(cpu, 0x7DC42D6E, BASE);
    check_eq(mem_read32(cpu, base + 0x9C), 0xC1200000u, "stfsux stores single");
    check_eq(cpu->gpr[4], base + 0x9C, "stfsux updates rA");

    cpu->gpr[4] = base;
    cpu->gpr[5] = 0xA0;
    cpu->fpr[15] = f64_from_bits(0x4014000000000000ull);
    exec_raw(cpu, 0x7DE42DAE, BASE);
    check_eq64(mem_read64(cpu, base + 0xA0), 0x4014000000000000ull, "stfdx stores double");

    cpu->gpr[4] = base;
    cpu->gpr[5] = 0xA8;
    cpu->fpr[16] = f64_from_bits(0xC014000000000000ull);
    exec_raw(cpu, 0x7E042DEE, BASE);
    check_eq64(mem_read64(cpu, base + 0xA8), 0xC014000000000000ull, "stfdux stores double");
    check_eq(cpu->gpr[4], base + 0xA8, "stfdux updates rA");
}

static void test_psq_memory(CPUState* cpu) {
    u32 base = GC_RAM_BASE + 0x5000;
    printf("--- paired-single memory ---\n");

    cpu_reset(cpu);
    cpu->gpr[4] = base;
    mem_write32(cpu, base, 0x3F800000u);
    mem_write32(cpu, base + 4, 0x40000000u);
    exec_raw(cpu, 0xE0240000, BASE);
    check_eq(f32_to_bits((f32)cpu->fpr[1]), 0x3F800000u, "psq_l w0 ps0");
    check_eq(f32_to_bits((f32)cpu->ps1[1]), 0x40000000u, "psq_l w0 ps1");

    mem_write32(cpu, base + 4, 0x40400000u);
    exec_raw(cpu, 0xE0448004, BASE);
    check_eq(f32_to_bits((f32)cpu->fpr[2]), 0x40400000u, "psq_l w1 ps0");
    check_eq(f32_to_bits((f32)cpu->ps1[2]), 0x3F800000u, "psq_l w1 ps1 one");

    mem_write32(cpu, base + 8, 0x40800000u);
    mem_write32(cpu, base + 12, 0x40A00000u);
    cpu->gpr[4] = base;
    exec_raw(cpu, 0xE4640008, BASE);
    check_eq(f32_to_bits((f32)cpu->fpr[3]), 0x40800000u, "psq_lu w0 ps0");
    check_eq(f32_to_bits((f32)cpu->ps1[3]), 0x40A00000u, "psq_lu w0 ps1");
    check_eq(cpu->gpr[4], base + 8, "psq_lu updates rA");

    cpu->gpr[4] = base;
    cpu->fpr[5] = (f64)f32_from_bits(0x40C00000u);
    cpu->ps1[5] = (f64)f32_from_bits(0x40E00000u);
    exec_raw(cpu, 0xF0A40010, BASE);
    check_eq(mem_read32(cpu, base + 16), 0x40C00000u, "psq_st w0 ps0");
    check_eq(mem_read32(cpu, base + 20), 0x40E00000u, "psq_st w0 ps1");

    mem_write32(cpu, base + 24, 0xDEADBEEFu);
    cpu->fpr[6] = (f64)f32_from_bits(0x41000000u);
    cpu->ps1[6] = (f64)f32_from_bits(0x41100000u);
    exec_raw(cpu, 0xF0C48014, BASE);
    check_eq(mem_read32(cpu, base + 20), 0x41000000u, "psq_st w1 ps0");
    check_eq(mem_read32(cpu, base + 24), 0xDEADBEEFu, "psq_st w1 leaves ps1 word");

    cpu->gpr[4] = base;
    cpu->fpr[7] = (f64)f32_from_bits(0x41200000u);
    cpu->ps1[7] = (f64)f32_from_bits(0x41300000u);
    exec_raw(cpu, 0xF4E40018, BASE);
    check_eq(mem_read32(cpu, base + 24), 0x41200000u, "psq_stu w0 ps0");
    check_eq(mem_read32(cpu, base + 28), 0x41300000u, "psq_stu w0 ps1");
    check_eq(cpu->gpr[4], base + 24, "psq_stu updates rA");

    cpu->gpr[4] = base;
    cpu->gpr[5] = 0x80;
    mem_write32(cpu, base + 0x80, 0x41400000u);
    mem_write32(cpu, base + 0x84, 0x41500000u);
    exec_raw(cpu, 0x1124280C, BASE);
    check_eq(f32_to_bits((f32)cpu->fpr[9]), 0x41400000u, "psq_lx w0 ps0");
    check_eq(f32_to_bits((f32)cpu->ps1[9]), 0x41500000u, "psq_lx w0 ps1");

    cpu->gpr[4] = base;
    cpu->gpr[5] = 0x88;
    mem_write32(cpu, base + 0x88, 0x41600000u);
    exec_raw(cpu, 0x11442C0C, BASE);
    check_eq(f32_to_bits((f32)cpu->fpr[10]), 0x41600000u, "psq_lx w1 ps0");
    check_eq(f32_to_bits((f32)cpu->ps1[10]), 0x3F800000u, "psq_lx w1 ps1 one");

    cpu->gpr[4] = base;
    cpu->gpr[5] = 0x90;
    mem_write32(cpu, base + 0x90, 0x41700000u);
    mem_write32(cpu, base + 0x94, 0x41800000u);
    exec_raw(cpu, 0x1164284C, BASE);
    check_eq(f32_to_bits((f32)cpu->fpr[11]), 0x41700000u, "psq_lux w0 ps0");
    check_eq(f32_to_bits((f32)cpu->ps1[11]), 0x41800000u, "psq_lux w0 ps1");
    check_eq(cpu->gpr[4], base + 0x90, "psq_lux updates rA");

    cpu->gpr[4] = base;
    cpu->gpr[5] = 0x98;
    cpu->fpr[13] = (f64)f32_from_bits(0x41880000u);
    cpu->ps1[13] = (f64)f32_from_bits(0x41900000u);
    exec_raw(cpu, 0x11A4280E, BASE);
    check_eq(mem_read32(cpu, base + 0x98), 0x41880000u, "psq_stx w0 ps0");
    check_eq(mem_read32(cpu, base + 0x9C), 0x41900000u, "psq_stx w0 ps1");

    cpu->gpr[4] = base;
    cpu->gpr[5] = 0xA0;
    cpu->fpr[15] = (f64)f32_from_bits(0x41980000u);
    cpu->ps1[15] = (f64)f32_from_bits(0x41A00000u);
    exec_raw(cpu, 0x11E4284E, BASE);
    check_eq(mem_read32(cpu, base + 0xA0), 0x41980000u, "psq_stux w0 ps0");
    check_eq(mem_read32(cpu, base + 0xA4), 0x41A00000u, "psq_stux w0 ps1");
    check_eq(cpu->gpr[4], base + 0xA0, "psq_stux updates rA");
}

static void test_fpu_arithmetic(CPUState* cpu) {
    printf("--- FPU arithmetic ---\n");

    cpu_reset(cpu);
    cpu->fpr[2] = 1.25;
    cpu->fpr[3] = 2.5;
    exec_raw(cpu, 0xEC22182A, BASE);
    check_eq(f32_to_bits((f32)cpu->fpr[1]), 0x40700000u, "fadds result");

    cpu->fpr[5] = 7.0;
    cpu->fpr[6] = 1.5;
    exec_raw(cpu, 0xEC853028, BASE);
    check_eq(f32_to_bits((f32)cpu->fpr[4]), 0x40B00000u, "fsubs result");

    cpu->fpr[8] = 3.0;
    cpu->fpr[9] = 2.0;
    exec_raw(cpu, 0xECE80272, BASE);
    check_eq(f32_to_bits((f32)cpu->fpr[7]), 0x40C00000u, "fmuls result");

    cpu->fpr[11] = 7.0;
    cpu->fpr[12] = 2.0;
    exec_raw(cpu, 0xED4B6024, BASE);
    check_eq(f32_to_bits((f32)cpu->fpr[10]), 0x40600000u, "fdivs result");

    cpu->fpr[14] = 1.25;
    cpu->fpr[15] = 2.5;
    exec_raw(cpu, 0xFDAE782A, BASE);
    check_eq64(f64_to_bits(cpu->fpr[13]), 0x400E000000000000ull, "fadd result");

    cpu->fpr[17] = 7.0;
    cpu->fpr[18] = 1.5;
    exec_raw(cpu, 0xFE119028, BASE);
    check_eq64(f64_to_bits(cpu->fpr[16]), 0x4016000000000000ull, "fsub result");

    cpu->fpr[20] = 3.0;
    cpu->fpr[21] = 2.0;
    exec_raw(cpu, 0xFE740572, BASE);
    check_eq64(f64_to_bits(cpu->fpr[19]), 0x4018000000000000ull, "fmul result");

    cpu->fpr[23] = 7.0;
    cpu->fpr[24] = 2.0;
    exec_raw(cpu, 0xFED7C024, BASE);
    check_eq64(f64_to_bits(cpu->fpr[22]), 0x400C000000000000ull, "fdiv result");

    cpu->fpr[26] = f64_from_bits(0x8000000000000000ull);
    exec_raw(cpu, 0xFF20D090, BASE);
    check_eq64(f64_to_bits(cpu->fpr[25]), 0x8000000000000000ull, "fmr preserves negative zero");

    cpu->fpr[28] = 2.5;
    exec_raw(cpu, 0xFF60E050, BASE);
    check_eq64(f64_to_bits(cpu->fpr[27]), 0xC004000000000000ull, "fneg flips sign");

    cpu->fpr[30] = -2.5;
    exec_raw(cpu, 0xFFA0F210, BASE);
    check_eq64(f64_to_bits(cpu->fpr[29]), 0x4004000000000000ull, "fabs clears sign");

    cpu->fpr[0] = 2.5;
    exec_raw(cpu, 0xFFE00110, BASE);
    check_eq64(f64_to_bits(cpu->fpr[31]), 0xC004000000000000ull, "fnabs sets sign");

    cpu->fpr[2] = 1.1;
    exec_raw(cpu, 0xFC201018, BASE);
    check_eq(f32_to_bits((f32)cpu->fpr[1]), 0x3F8CCCCDu, "frsp rounds to single");

    cpu->fpr[3] = 1.0;
    cpu->fpr[4] = 2.0;
    exec_raw(cpu, 0xFD032000, BASE);
    check_eq(get_cr_field(cpu, 2), 0x8, "fcmpu less");

    cpu->fpr[3] = 2.0;
    cpu->fpr[4] = 1.0;
    exec_raw(cpu, 0xFD032000, BASE);
    check_eq(get_cr_field(cpu, 2), 0x4, "fcmpu greater");

    cpu->fpr[3] = 2.0;
    cpu->fpr[4] = 2.0;
    exec_raw(cpu, 0xFD032000, BASE);
    check_eq(get_cr_field(cpu, 2), 0x2, "fcmpu equal");

    cpu->fpr[3] = f64_from_bits(0x7FF8000000000000ull);
    cpu->fpr[4] = 2.0;
    exec_raw(cpu, 0xFD032000, BASE);
    check_eq(get_cr_field(cpu, 2), 0x1, "fcmpu unordered");

    cpu->fpr[5] = 4.0;
    cpu->fpr[6] = 3.0;
    exec_raw(cpu, 0xFD853040, BASE);
    check_eq(get_cr_field(cpu, 3), 0x4, "fcmpo greater");
}

static void check_cr_logic(CPUState* cpu, const char* name, u32 xo,
                           const u8 expected[4]) {
    static const u32 bit3 = 0x10000000u;
    static const u32 bit4 = 0x08000000u;

    for (u32 i = 0; i < 4; i++) {
        u32 a = (i >> 1) & 1u;
        u32 b = i & 1u;
        char label[32];

        cpu->cr = (a ? bit3 : 0u) | (b ? bit4 : 0u);
        exec_raw(cpu, make_crform(xo, 2, 3, 4), BASE);
        snprintf(label, sizeof(label), "%s %u%u", name, a, b);
        check_eq(cr_bit(cpu, 2), expected[i], label);
    }
}

static void test_branches_cr_spr(CPUState* cpu) {
    printf("--- branches / CR / SPR ---\n");

    cpu_reset(cpu);
    exec_raw(cpu, make_iform(18, 0x18, false, false), BASE + 0x60);
    check_eq(cpu->pc, BASE + 0x78, "b changes PC");

    exec_raw(cpu, make_iform(18, 0x18, false, true), BASE + 0x60);
    check_eq(cpu->pc, BASE + 0x78, "bl changes PC");
    check_eq(cpu->lr, BASE + 0x64, "bl sets LR");

    cpu->lr = 0x80001235;
    exec_raw(cpu, 0x4E800020, BASE);
    check_eq(cpu->pc, 0x80001234, "bclr/blr uses LR");

    cpu->ctr = 0x80005679;
    exec_raw(cpu, 0x4E800420, BASE);
    check_eq(cpu->pc, 0x80005678, "bcctr/bctr uses CTR");

    static const u8 crand_expected[4] = {0, 0, 0, 1};
    static const u8 crandc_expected[4] = {0, 0, 1, 0};
    static const u8 creqv_expected[4] = {1, 0, 0, 1};
    static const u8 crnand_expected[4] = {1, 1, 1, 0};
    static const u8 crnor_expected[4] = {1, 0, 0, 0};
    static const u8 crorc_expected[4] = {1, 0, 1, 1};
    static const u8 crxor_expected[4] = {0, 1, 1, 0};
    check_cr_logic(cpu, "crand", 257, crand_expected);
    check_cr_logic(cpu, "crandc", 129, crandc_expected);
    check_cr_logic(cpu, "creqv", 289, creqv_expected);
    check_cr_logic(cpu, "crnand", 225, crnand_expected);
    check_cr_logic(cpu, "crnor", 33, crnor_expected);
    check_cr_logic(cpu, "crorc", 417, crorc_expected);
    check_cr_logic(cpu, "crxor", 193, crxor_expected);

    cpu->cr = 0;
    set_cr_bit(cpu, 3, 1);
    exec_raw(cpu, make_crform(449, 2, 3, 4), BASE);
    check_eq(cr_bit(cpu, 2), 1, "cror copies true source");

    cpu->cr = 0x12345678;
    exec_raw(cpu, make_mcrf(2, 3), BASE);
    check_eq(get_cr_field(cpu, 2), 0x4, "mcrf copies source field");
    check_eq(get_cr_field(cpu, 3), 0x4, "mcrf leaves source field");

    cpu->cr = 0xA5A50000;
    exec_raw(cpu, 0x7D400026, BASE);
    check_eq(cpu->gpr[10], 0xA5A50000, "mfcr reads CR");

    cpu->gpr[10] = 0x12345678;
    cpu->cr = 0;
    exec_raw(cpu, make_mtcrf(10, 0xFF), BASE);
    check_eq(cpu->cr, 0x12345678, "mtcrf full mask writes CR");

    cpu->gpr[10] = 0x89ABCDEF;
    cpu->cr = 0x11111111;
    exec_raw(cpu, make_mtcrf(10, 0x90), BASE);
    check_eq(cpu->cr, 0x811B1111, "mtcrf partial mask writes selected fields");

    cpu->gpr[10] = 0xFFFFFFFF;
    cpu->cr = 0x2468ACE0;
    exec_raw(cpu, make_mtcrf(10, 0x00), BASE);
    check_eq(cpu->cr, 0x2468ACE0, "mtcrf zero mask leaves CR");

    cpu->gpr[10] = 0x0000000F;
    cpu->cr = 0x12345670;
    exec_raw(cpu, make_mtcrf(10, 0x01), BASE);
    check_eq(cpu->cr, 0x1234567F, "mtcrf low mask writes CR7");

    cpu->gpr[10] = 0x12345678;
    exec_raw(cpu, 0x7D4803A6, BASE);
    check_eq(cpu->lr, 0x12345678, "mtspr/mtlr writes LR");
    cpu->gpr[10] = 0;
    exec_raw(cpu, 0x7D4802A6, BASE);
    check_eq(cpu->gpr[10], 0x12345678, "mfspr/mflr reads LR");

    cpu->gpr[10] = 5;
    exec_raw(cpu, make_xfx(467, 10, 9), BASE);
    check_eq(cpu->ctr, 5, "mtspr/mtctr writes CTR");
    cpu->gpr[10] = 0;
    exec_raw(cpu, make_xfx(339, 10, 9), BASE);
    check_eq(cpu->gpr[10], 5, "mfspr/mfctr reads CTR");

    cpu->gpr[10] = XER_CA;
    exec_raw(cpu, make_xfx(467, 10, 1), BASE);
    check_eq(cpu->xer, XER_CA, "mtspr/mtxer writes XER");
    cpu->gpr[10] = 0;
    exec_raw(cpu, make_xfx(339, 10, 1), BASE);
    check_eq(cpu->gpr[10], XER_CA, "mfspr/mfxer reads XER");
}

int main(void) {
    CPUState cpu;
    if (!cpu_init(&cpu))
        return 1;

    printf("PC reference check against Dolphin opcode expectations\n\n");

    test_immediate_arithmetic(&cpu);
    test_compare_and_bc(&cpu);
    test_register_arithmetic(&cpu);
    test_immediate_logical(&cpu);
    test_logical_shift_rotate(&cpu);
    test_loads(&cpu);
    test_stores(&cpu);
    test_indexed_memory(&cpu);
    test_fpu_memory(&cpu);
    test_psq_memory(&cpu);
    test_fpu_arithmetic(&cpu);
    test_branches_cr_spr(&cpu);

    cpu_free(&cpu);

    printf("\n%d passed, %d failed\n", pass_count, fail_count);
    if (fail_count == 0)
        printf("ALL TESTS PASSED\n");

    return fail_count == 0 ? 0 : 1;
}
