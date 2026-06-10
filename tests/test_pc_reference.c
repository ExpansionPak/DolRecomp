#include <stdio.h>
#include <string.h>

#include "../src/core/types.h"
#include "../src/frontend/decoder.h"
#include "../src/runtime/runtime.h"

#define BASE 0x80000000u
#define XER_SO 0x80000000u
#define XER_CA 0x20000000u

static int pass_count = 0;
static int fail_count = 0;

static void check_eq(u32 got, u32 want, const char* name) {
    if (got == want) {
        pass_count++;
    } else {
        fail_count++;
        printf("  FAIL: %s - got 0x%08X, want 0x%08X\n", name, got, want);
    }
}

static u32 make_dform(u32 opcd, u32 rt, u32 ra, u16 imm) {
    return (opcd << 26) | (rt << 21) | (ra << 16) | imm;
}

static u32 make_iform(u32 opcd, s32 offset, bool aa, bool lk) {
    return (opcd << 26) | ((u32)offset & 0x03FFFFFCu) |
           (aa ? 2u : 0u) | (lk ? 1u : 0u);
}

static u32 make_xfx(u32 xo, u32 rt, u16 spr) {
    u32 spr_field = ((u32)(spr & 0x1F) << 16) | ((u32)(spr & 0x3E0) << 6);
    return (31u << 26) | (rt << 21) | spr_field | (xo << 1);
}

static u32 cr_shift(u8 crf) {
    return 4u * (7u - (u32)crf);
}

static void set_cr_field(CPUState* cpu, u8 crf, u32 bits) {
    u32 shift = cr_shift(crf);
    cpu->cr = (cpu->cr & ~(0xFu << shift)) | ((bits & 0xFu) << shift);
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

static void compare_s32(CPUState* cpu, u8 crf, u8 ra, s16 simm) {
    s32 a = (s32)cpu->gpr[ra];
    s32 b = (s32)simm;
    u32 bits = 0;

    if (a < b) bits |= 0x8u;
    if (a > b) bits |= 0x4u;
    if (a == b) bits |= 0x2u;
    bits |= (cpu->xer & XER_SO) ? 1u : 0u;

    set_cr_field(cpu, crf, bits);
}

static void compare_u32(CPUState* cpu, u8 crf, u8 ra, u16 uimm) {
    u32 a = cpu->gpr[ra];
    u32 b = (u32)uimm;
    u32 bits = 0;

    if (a < b) bits |= 0x8u;
    if (a > b) bits |= 0x4u;
    if (a == b) bits |= 0x2u;
    bits |= (cpu->xer & XER_SO) ? 1u : 0u;

    set_cr_field(cpu, crf, bits);
}

static u32 dform_ea(CPUState* cpu, const PPCInst* inst, bool update) {
    if (inst->rA == 0 && !update)
        return (u32)(s32)inst->simm;
    return cpu->gpr[inst->rA] + (u32)(s32)inst->simm;
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

static void exec_inst(CPUState* cpu, const PPCInst* inst) {
    cpu->pc = inst->address + 4;

    switch (inst->op) {
    case PPC_OP_ADDI:
        cpu->gpr[inst->rD] = (inst->rA == 0)
            ? (u32)(s32)inst->simm
            : cpu->gpr[inst->rA] + (u32)(s32)inst->simm;
        break;

    case PPC_OP_ADDIC: {
        u64 a = cpu->gpr[inst->rA];
        u64 b = (u32)(s32)inst->simm;
        u64 res = a + b;
        cpu->gpr[inst->rD] = (u32)res;
        cpu->xer = (cpu->xer & ~XER_CA) | ((((u32)(res >> 32)) & 1u) << 29);
        break;
    }

    case PPC_OP_ADDIS:
        cpu->gpr[inst->rD] = (inst->rA == 0)
            ? ((u32)(s32)inst->simm << 16)
            : cpu->gpr[inst->rA] + ((u32)(s32)inst->simm << 16);
        break;

    case PPC_OP_CMPI:
        compare_s32(cpu, inst->crfD, inst->rA, inst->simm);
        break;

    case PPC_OP_CMPLI:
        compare_u32(cpu, inst->crfD, inst->rA, inst->uimm);
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

    case PPC_OP_LHA: {
        u32 ea = dform_ea(cpu, inst, false);
        cpu->gpr[inst->rD] = (u32)(s32)(s16)mem_read16(cpu, ea);
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

    cpu->gpr[4] = 0;
    exec_raw(cpu, 0x3084FFFF, BASE);
    check_eq(cpu->gpr[4], 0xFFFFFFFF, "addic 0 + -1 result");
    check_eq(cpu->xer & XER_CA, 0, "addic 0 + -1 clears CA");

    cpu->gpr[4] = 1;
    exec_raw(cpu, 0x3084FFFF, BASE);
    check_eq(cpu->gpr[4], 0, "addic 1 + -1 result");
    check_eq(cpu->xer & XER_CA, XER_CA, "addic 1 + -1 sets CA");
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
    check_eq(cpu->cr & 0xF0000000u, 0x40000000u, "cmplwi unsigned greater CR0");
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
    check_eq(cpu->cr & 0xF0000000u, 0x40000000u, "andi. updates CR0 nonzero");

    cpu->gpr[7] = 0x12FF0000;
    exec_raw(cpu, 0x74E900FF, BASE);
    check_eq(cpu->gpr[9], 0x00FF0000, "andis. result");
    check_eq(cpu->cr & 0xF0000000u, 0x40000000u, "andis. updates CR0 nonzero");
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
    exec_raw(cpu, 0xA0E10010, BASE);
    check_eq(cpu->gpr[7], 0x1234, "lhz zero-extends halfword");
    exec_raw(cpu, 0xA5010014, BASE);
    check_eq(cpu->gpr[8], 0x7FFF, "lhzu loads updated address");
    check_eq(cpu->gpr[1], base + 20, "lhzu updates rA");

    cpu->gpr[1] = base;
    exec_raw(cpu, 0xA921FFFC, BASE);
    check_eq(cpu->gpr[9], 0xFFFF8001, "lha sign-extends halfword");
}

static void test_stores(CPUState* cpu) {
    u32 base = GC_RAM_BASE + 0x2000;
    printf("--- stores ---\n");

    cpu_reset(cpu);
    cpu->gpr[1] = base;
    cpu->gpr[3] = 0xAABBCCDD;
    exec_raw(cpu, 0x90610018, BASE);
    check_eq(mem_read32(cpu, base + 24), 0xAABBCCDD, "stw offset 24");

    cpu->gpr[4] = 0x11223344;
    exec_raw(cpu, 0x9481001C, BASE);
    check_eq(mem_read32(cpu, base + 28), 0x11223344, "stwu stores updated address");
    check_eq(cpu->gpr[1], base + 28, "stwu updates rA");

    cpu->gpr[1] = base;
    cpu->gpr[5] = 0x123456A5;
    exec_raw(cpu, 0x98A10020, BASE);
    check_eq(mem_read8(cpu, base + 32), 0xA5, "stb stores low byte");

    cpu->gpr[6] = 0x7E;
    exec_raw(cpu, 0x9CC10024, BASE);
    check_eq(mem_read8(cpu, base + 36), 0x7E, "stbu stores updated address");
    check_eq(cpu->gpr[1], base + 36, "stbu updates rA");

    cpu->gpr[1] = base;
    cpu->gpr[7] = 0xFFFFABCD;
    exec_raw(cpu, 0xB0E10028, BASE);
    check_eq(mem_read16(cpu, base + 40), 0xABCD, "sth stores low halfword");

    cpu->gpr[8] = 0x1234;
    exec_raw(cpu, 0xB501002C, BASE);
    check_eq(mem_read16(cpu, base + 44), 0x1234, "sthu stores updated address");
    check_eq(cpu->gpr[1], base + 44, "sthu updates rA");
}

static void test_branches_and_spr(CPUState* cpu) {
    printf("--- branches / SPR ---\n");

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
    test_immediate_logical(&cpu);
    test_loads(&cpu);
    test_stores(&cpu);
    test_branches_and_spr(&cpu);

    cpu_free(&cpu);

    printf("\n%d passed, %d failed\n", pass_count, fail_count);
    if (fail_count == 0)
        printf("ALL TESTS PASSED\n");

    return fail_count == 0 ? 0 : 1;
}
