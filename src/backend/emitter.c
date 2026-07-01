// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 ExpansionPak

#include "emitter.h"

static u32 cr_field_shift(u8 crf) {
    return 4u * (7u - (u32)crf);
}

static u32 ppc_mask32(u8 mb, u8 me) {
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

static void emit_set_cr0_from_gpr(FILE* out, u8 reg) {
    fprintf(out, "        u32 cr_bits = 0;\n");
    fprintf(out, "        s32 cr_value = (s32)ctx->gpr[%u];\n", reg);
    fprintf(out, "        if (cr_value < 0)  cr_bits |= 0x8u;\n");
    fprintf(out, "        if (cr_value > 0)  cr_bits |= 0x4u;\n");
    fprintf(out, "        if (cr_value == 0) cr_bits |= 0x2u;\n");
    fprintf(out, "        cr_bits |= (ctx->xer >> 31) & 1u;\n");
    fprintf(out, "        ctx->cr = (ctx->cr & 0x0FFFFFFFu) | (cr_bits << 28);\n");
}

static void emit_set_cr1_from_fpscr(FILE* out) {
    fprintf(out, "        ctx->cr = (ctx->cr & 0xF0FFFFFFu) | ((ctx->fpscr >> 4) & 0x0F000000u);\n");
}

static void emit_compare_s32(FILE* out, u8 crf, const char* lhs, const char* rhs) {
    u32 shift = cr_field_shift(crf);

    fprintf(out, "    {\n");
    fprintf(out, "        s32 val_a = (s32)(%s);\n", lhs);
    fprintf(out, "        s32 val_b = (s32)(%s);\n", rhs);
    fprintf(out, "        u32 cr_bits = 0;\n");
    fprintf(out, "        if (val_a < val_b)  cr_bits |= 0x8u;\n");
    fprintf(out, "        if (val_a > val_b)  cr_bits |= 0x4u;\n");
    fprintf(out, "        if (val_a == val_b) cr_bits |= 0x2u;\n");
    fprintf(out, "        cr_bits |= (ctx->xer >> 31) & 1u;\n");
    fprintf(out, "        ctx->cr = (ctx->cr & ~(0xFu << %u)) | (cr_bits << %u);\n",
            shift, shift);
    fprintf(out, "    }\n");
}

static void emit_compare_u32(FILE* out, u8 crf, const char* lhs, const char* rhs) {
    u32 shift = cr_field_shift(crf);

    fprintf(out, "    {\n");
    fprintf(out, "        u32 val_a = (u32)(%s);\n", lhs);
    fprintf(out, "        u32 val_b = (u32)(%s);\n", rhs);
    fprintf(out, "        u32 cr_bits = 0;\n");
    fprintf(out, "        if (val_a < val_b)  cr_bits |= 0x8u;\n");
    fprintf(out, "        if (val_a > val_b)  cr_bits |= 0x4u;\n");
    fprintf(out, "        if (val_a == val_b) cr_bits |= 0x2u;\n");
    fprintf(out, "        cr_bits |= (ctx->xer >> 31) & 1u;\n");
    fprintf(out, "        ctx->cr = (ctx->cr & ~(0xFu << %u)) | (cr_bits << %u);\n",
            shift, shift);
    fprintf(out, "    }\n");
}

static void emit_fcompare(FILE* out, const PPCInst* inst) {
    u32 shift = cr_field_shift(inst->crfD);

    fprintf(out, "    {\n");
    fprintf(out, "        f64 val_a = ctx->fpr[%u];\n", inst->rA);
    fprintf(out, "        f64 val_b = ctx->fpr[%u];\n", inst->rB);
    fprintf(out, "        u32 cr_bits = 0;\n");
    fprintf(out, "        if (val_a < val_b)       cr_bits = 0x8u;\n");
    fprintf(out, "        else if (val_a > val_b)  cr_bits = 0x4u;\n");
    fprintf(out, "        else if (val_a == val_b) cr_bits = 0x2u;\n");
    fprintf(out, "        else                     cr_bits = 0x1u;\n");
    fprintf(out, "        ctx->cr = (ctx->cr & ~(0xFu << %u)) | (cr_bits << %u);\n",
            shift, shift);
    fprintf(out, "    }\n");
}

static void emit_dform_ea(FILE* out, u8 ra, s16 simm, bool update) {
    if (ra == 0 && !update) {
        fprintf(out, "(u32)(s32)(%d)", (int)simm);
    } else {
        fprintf(out, "ctx->gpr[%u] + (u32)(s32)(%d)", ra, (int)simm);
    }
}

static void emit_xform_ea(FILE* out, u8 ra, u8 rb, bool update) {
    if (ra == 0 && !update) {
        fprintf(out, "ctx->gpr[%u]", rb);
    } else {
        fprintf(out, "ctx->gpr[%u] + ctx->gpr[%u]", ra, rb);
    }
}

static void emit_load(FILE* out, const PPCInst* inst, const char* read_expr,
                      bool update) {
    fprintf(out, "    {\n");
    fprintf(out, "        u32 ea = ");
    emit_dform_ea(out, inst->rA, inst->simm, update);
    fprintf(out, ";\n");
    fprintf(out, "        ctx->gpr[%u] = %s;\n", inst->rD, read_expr);
    if (update) {
        fprintf(out, "        ctx->gpr[%u] = ea;\n", inst->rA);
    }
    fprintf(out, "    }\n");
}

static void emit_loadx(FILE* out, const PPCInst* inst, const char* read_expr,
                       bool update) {
    fprintf(out, "    {\n");
    fprintf(out, "        u32 ea = ");
    emit_xform_ea(out, inst->rA, inst->rB, update);
    fprintf(out, ";\n");
    fprintf(out, "        ctx->gpr[%u] = %s;\n", inst->rD, read_expr);
    if (update) {
        fprintf(out, "        ctx->gpr[%u] = ea;\n", inst->rA);
    }
    fprintf(out, "    }\n");
}

static void emit_store(FILE* out, const PPCInst* inst, const char* write_func,
                       const char* cast_type, bool update) {
    fprintf(out, "    {\n");
    fprintf(out, "        u32 ea = ");
    emit_dform_ea(out, inst->rA, inst->simm, update);
    fprintf(out, ";\n");
    fprintf(out, "        %s(ctx, ea, (%s)ctx->gpr[%u]);\n",
            write_func, cast_type, inst->rS);
    if (update) {
        fprintf(out, "        ctx->gpr[%u] = ea;\n", inst->rA);
    }
    fprintf(out, "    }\n");
}

static void emit_storex(FILE* out, const PPCInst* inst, const char* write_func,
                        const char* cast_type, bool update) {
    fprintf(out, "    {\n");
    fprintf(out, "        u32 ea = ");
    emit_xform_ea(out, inst->rA, inst->rB, update);
    fprintf(out, ";\n");
    fprintf(out, "        %s(ctx, ea, (%s)ctx->gpr[%u]);\n",
            write_func, cast_type, inst->rS);
    if (update) {
        fprintf(out, "        ctx->gpr[%u] = ea;\n", inst->rA);
    }
    fprintf(out, "    }\n");
}

static void emit_fload(FILE* out, const PPCInst* inst, bool single,
                       bool update) {
    fprintf(out, "    {\n");
    fprintf(out, "        u32 ea = ");
    emit_dform_ea(out, inst->rA, inst->simm, update);
    fprintf(out, ";\n");
    if (single) {
        fprintf(out, "        f64 value = (f64)dolrecomp_f32_from_bits(mem_read32(ctx, ea));\n");
        fprintf(out, "        ctx->fpr[%u] = value;\n", inst->rD);
        fprintf(out, "        ctx->ps1[%u] = value;\n", inst->rD);
    } else {
        fprintf(out, "        ctx->fpr[%u] = dolrecomp_f64_from_bits(mem_read64(ctx, ea));\n",
                inst->rD);
    }
    if (update) {
        fprintf(out, "        ctx->gpr[%u] = ea;\n", inst->rA);
    }
    fprintf(out, "    }\n");
}

static void emit_floadx(FILE* out, const PPCInst* inst, bool single,
                        bool update) {
    fprintf(out, "    {\n");
    fprintf(out, "        u32 ea = ");
    emit_xform_ea(out, inst->rA, inst->rB, update);
    fprintf(out, ";\n");
    if (single) {
        fprintf(out, "        f64 value = (f64)dolrecomp_f32_from_bits(mem_read32(ctx, ea));\n");
        fprintf(out, "        ctx->fpr[%u] = value;\n", inst->rD);
        fprintf(out, "        ctx->ps1[%u] = value;\n", inst->rD);
    } else {
        fprintf(out, "        ctx->fpr[%u] = dolrecomp_f64_from_bits(mem_read64(ctx, ea));\n",
                inst->rD);
    }
    if (update) {
        fprintf(out, "        ctx->gpr[%u] = ea;\n", inst->rA);
    }
    fprintf(out, "    }\n");
}

static void emit_fstore(FILE* out, const PPCInst* inst, bool single,
                        bool update) {
    fprintf(out, "    {\n");
    fprintf(out, "        u32 ea = ");
    emit_dform_ea(out, inst->rA, inst->simm, update);
    fprintf(out, ";\n");
    if (single) {
        fprintf(out, "        mem_write32(ctx, ea, dolrecomp_f32_to_bits((f32)ctx->fpr[%u]));\n",
                inst->rS);
    } else {
        fprintf(out, "        mem_write64(ctx, ea, dolrecomp_f64_to_bits(ctx->fpr[%u]));\n",
                inst->rS);
    }
    if (update) {
        fprintf(out, "        ctx->gpr[%u] = ea;\n", inst->rA);
    }
    fprintf(out, "    }\n");
}

static void emit_fstorex(FILE* out, const PPCInst* inst, bool single,
                         bool update) {
    fprintf(out, "    {\n");
    fprintf(out, "        u32 ea = ");
    emit_xform_ea(out, inst->rA, inst->rB, update);
    fprintf(out, ";\n");
    if (single) {
        fprintf(out, "        mem_write32(ctx, ea, dolrecomp_f32_to_bits((f32)ctx->fpr[%u]));\n",
                inst->rS);
    } else {
        fprintf(out, "        mem_write64(ctx, ea, dolrecomp_f64_to_bits(ctx->fpr[%u]));\n",
                inst->rS);
    }
    if (update) {
        fprintf(out, "        ctx->gpr[%u] = ea;\n", inst->rA);
    }
    fprintf(out, "    }\n");
}

static void emit_psq_load(FILE* out, const PPCInst* inst, bool indexed,
                          bool update) {
    fprintf(out, "    {\n");
    fprintf(out, "        u32 ea = ");
    if (indexed) {
        emit_xform_ea(out, inst->rA, inst->rB, update);
    } else {
        emit_dform_ea(out, inst->rA, inst->simm, update);
    }
    fprintf(out, ";\n");
    fprintf(out, "        ppc_psq_load(ctx, %uu, ea, %s, %uu, %s, 0x%08Xu);\n",
            inst->rD, inst->w ? "true" : "false", inst->i,
            indexed ? "true" : "false", inst->address);
    fprintf(out, "        if (ctx->exception) return;\n");
    if (update) {
        fprintf(out, "        ctx->gpr[%u] = ea;\n", inst->rA);
    }
    fprintf(out, "    }\n");
}

static void emit_psq_store(FILE* out, const PPCInst* inst, bool indexed,
                           bool update) {
    fprintf(out, "    {\n");
    fprintf(out, "        u32 ea = ");
    if (indexed) {
        emit_xform_ea(out, inst->rA, inst->rB, update);
    } else {
        emit_dform_ea(out, inst->rA, inst->simm, update);
    }
    fprintf(out, ";\n");
    fprintf(out, "        ppc_psq_store(ctx, %uu, ea, %s, %uu, %s, 0x%08Xu);\n",
            inst->rS, inst->w ? "true" : "false", inst->i,
            indexed ? "true" : "false", inst->address);
    fprintf(out, "        if (ctx->exception) return;\n");
    if (update) {
        fprintf(out, "        ctx->gpr[%u] = ea;\n", inst->rA);
    }
    fprintf(out, "    }\n");
}

static void emit_dcbz(FILE* out, const PPCInst* inst) {
    fprintf(out, "    {\n");
    fprintf(out, "        u32 ea = ");
    emit_xform_ea(out, inst->rA, inst->rB, false);
    fprintf(out, ";\n");
    fprintf(out, "        ea &= ~31u;\n");
    fprintf(out, "        for (u32 i = 0; i < 32; i += 4) mem_write32(ctx, ea + i, 0);\n");
    fprintf(out, "    }\n");
}

static void emit_branch_condition(FILE* out, u8 bo, u8 bi) {
    bool ctr_ignored = (bo & 0x04) != 0;
    bool cond_ignored = (bo & 0x10) != 0;

    if (!ctr_ignored) {
        fprintf(out, "        ctx->ctr--;\n");
        fprintf(out, "        bool ctr_ok = (((ctx->ctr != 0) ? 1u : 0u) ^ %uu) != 0;\n",
                (bo >> 1) & 1u);
    } else {
        fprintf(out, "        bool ctr_ok = true;\n");
    }

    if (!cond_ignored) {
        u32 mask = 0x80000000u >> bi;
        fprintf(out, "        bool cr_ok = (((ctx->cr & 0x%08Xu) != 0) == %s);\n",
                mask, ((bo >> 3) & 1u) ? "true" : "false");
    } else {
        fprintf(out, "        bool cr_ok = true;\n");
    }
}

static bool branch_target_is_local(u32 func_start, u32 func_end, u32 target) {
    return target >= func_start && target < func_end && ((target - func_start) & 3u) == 0;
}

static void emit_direct_branch(FILE* out, const PPCInst* inst, bool local_target) {
    bool local_backward = local_target && inst->branch_target <= inst->address;

    if (inst->lk) {
        fprintf(out, "            ctx->lr = 0x%08Xu;\n", inst->address + 4);
        fprintf(out, "            ctx->pc = 0x%08Xu;\n", inst->branch_target);
        fprintf(out, "            return;\n");
        return;
    }
    if (local_backward) {
        fprintf(out, "            ctx->pc = 0x%08Xu;\n", inst->branch_target);
        fprintf(out, "            return;\n");
    } else if (local_target) {
        fprintf(out, "            goto label_%08X;\n", inst->branch_target);
    } else {
        fprintf(out, "            ctx->pc = 0x%08Xu;\n", inst->branch_target);
        fprintf(out, "            return;\n");
    }
}

static void emit_dynamic_branch(FILE* out, const PPCInst* inst,
                                const char* target_expr) {
    fprintf(out, "    {\n");
    fprintf(out, "        u32 target = %s;\n", target_expr);
    emit_branch_condition(out, inst->bo, inst->bi);
    fprintf(out, "        if (ctr_ok && cr_ok) {\n");
    if (inst->lk) {
        fprintf(out, "            ctx->lr = 0x%08Xu;\n", inst->address + 4);
    }
    fprintf(out, "            ctx->pc = target;\n");
    fprintf(out, "            return;\n");
    fprintf(out, "        }\n");
    fprintf(out, "    }\n");
}

static void emit_cr_logical(FILE* out, const PPCInst* inst, const char* expr) {
    fprintf(out, "    {\n");
    fprintf(out, "        u32 a = (ctx->cr >> (31u - %uu)) & 1u;\n", inst->rA);
    fprintf(out, "        u32 b = (ctx->cr >> (31u - %uu)) & 1u;\n", inst->rB);
    fprintf(out, "        u32 mask = 0x80000000u >> %u;\n", inst->rD);
    fprintf(out, "        u32 value = (%s) & 1u;\n", expr);
    fprintf(out, "        ctx->cr = (ctx->cr & ~mask) | (value ? mask : 0u);\n");
    fprintf(out, "    }\n");
}

static void emit_record_if_needed(FILE* out, const PPCInst* inst, u8 reg) {
    if (inst->rc) {
        emit_set_cr0_from_gpr(out, reg);
    }
}

static const char* emit_cpu_macro(DolRecompCPU cpu) {
    switch (cpu) {
    case DOLRECOMP_CPU_BROADWAY:
        return "BROADWAY";
    case DOLRECOMP_CPU_ESPRESSO:
        return "ESPRESSO";
    case DOLRECOMP_CPU_GEKKO:
    default:
        return "GEKKO";
    }
}

static const char* emit_cpu_label(DolRecompCPU cpu) {
    switch (cpu) {
    case DOLRECOMP_CPU_BROADWAY:
        return "broadway";
    case DOLRECOMP_CPU_ESPRESSO:
        return "espresso";
    case DOLRECOMP_CPU_GEKKO:
    default:
        return "gekko";
    }
}

void emit_header_for_cpu(FILE* out, DolRecompCPU cpu) {
    fprintf(out,
        "// DolRecomp output\n"
        "// cpu: %s\n"
        "\n"
        "#define DOLRECOMP_CPU_%s 1\n"
        "#define DOLRECOMP_CPU_NAME \"%s\"\n"
        "\n"
        "#include <string.h>\n"
        "#include <math.h>\n"
        "#include \"cpu/cpu.h\"\n"
        "\n"
        "static inline u32 dolrecomp_rotl32(u32 value, u32 sh) {\n"
        "    sh &= 31u;\n"
        "    return sh ? ((value << sh) | (value >> (32u - sh))) : value;\n"
        "}\n"
        "\n"
        "static inline f32 dolrecomp_f32_from_bits(u32 bits) {\n"
        "    f32 value;\n"
        "    memcpy(&value, &bits, sizeof(value));\n"
        "    return value;\n"
        "}\n"
        "\n"
        "static inline u32 dolrecomp_f32_to_bits(f32 value) {\n"
        "    u32 bits;\n"
        "    memcpy(&bits, &value, sizeof(bits));\n"
        "    return bits;\n"
        "}\n"
        "\n"
        "static inline f64 dolrecomp_f64_from_bits(u64 bits) {\n"
        "    f64 value;\n"
        "    memcpy(&value, &bits, sizeof(value));\n"
        "    return value;\n"
        "}\n"
        "\n"
        "static inline u64 dolrecomp_f64_to_bits(f64 value) {\n"
        "    u64 bits;\n"
        "    memcpy(&bits, &value, sizeof(bits));\n"
        "    return bits;\n"
        "}\n"
        "\n"
        "static inline f64 dolrecomp_ps_round(f64 value) {\n"
        "    return (f64)(f32)value;\n"
        "}\n"
        "\n"
        "static inline f64 dolrecomp_ps_from_bits(u32 bits) {\n"
        "    return (f64)dolrecomp_f32_from_bits(bits);\n"
        "}\n"
        "\n"
        "static inline u32 dolrecomp_ps_to_bits(f64 value) {\n"
        "    return dolrecomp_f32_to_bits((f32)value);\n"
        "}\n"
        "\n"
        ,
        emit_cpu_label(cpu),
        emit_cpu_macro(cpu),
        emit_cpu_label(cpu));
}

void emit_header(FILE* out) {
    emit_header_for_cpu(out, DOLRECOMP_CPU_GEKKO);
}

void emit_footer(FILE* out) {
    fprintf(out, "\n// end\n");
}

static void emit_instruction_with_range(FILE* out, const PPCInst* inst,
                                        u32 func_start, u32 func_end) {
    char disasm[64];
    ppc_disasm(disasm, sizeof(disasm), inst);
    fprintf(out, "    // %08X: %s\n", inst->address, disasm);

    if (inst->embedded_data) {
        fprintf(out, "    // embedded data\n\n");
        return;
    }

    switch (inst->op) {
    case PPC_OP_MULLI:
        fprintf(out, "    ctx->gpr[%u] = (u32)((s64)(s32)ctx->gpr[%u] * (s64)(s32)%d);\n",
                inst->rD, inst->rA, (int)inst->simm);
        break;

    case PPC_OP_SUBFIC:
        fprintf(out, "    {\n");
        fprintf(out, "        u64 res = (u64)(u32)(s32)(%d) + (u64)(~ctx->gpr[%u]) + 1u;\n",
                (int)inst->simm, inst->rA);
        fprintf(out, "        ctx->gpr[%u] = (u32)res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(res >> 32) & 1u) << 29);\n");
        fprintf(out, "    }\n");
        break;

    case PPC_OP_ADDI:
        if (inst->rA == 0) {
            fprintf(out, "    ctx->gpr[%u] = (u32)(s32)(%d);\n",
                    inst->rD, (int)inst->simm);
        } else {
            fprintf(out, "    ctx->gpr[%u] = ctx->gpr[%u] + (u32)(s32)(%d);\n",
                    inst->rD, inst->rA, (int)inst->simm);
        }
        break;

    case PPC_OP_ADDIC:
    case PPC_OP_ADDIC_DOT:
        fprintf(out, "    {\n");
        fprintf(out, "        u64 a = ctx->gpr[%u];\n", inst->rA);
        fprintf(out, "        u64 b = (u32)(s32)(%d);\n", (int)inst->simm);
        fprintf(out, "        u64 res = a + b;\n");
        fprintf(out, "        ctx->gpr[%u] = (u32)res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(res >> 32) & 1u) << 29);\n");
        if (inst->op == PPC_OP_ADDIC_DOT) {
            emit_set_cr0_from_gpr(out, inst->rD);
        }
        fprintf(out, "    }\n");
        break;

    case PPC_OP_ADDIS:
        if (inst->rA == 0) {
            fprintf(out, "    ctx->gpr[%u] = ((u32)(s32)(%d) << 16);\n",
                    inst->rD, (int)inst->simm);
        } else {
            fprintf(out, "    ctx->gpr[%u] = ctx->gpr[%u] + ((u32)(s32)(%d) << 16);\n",
                    inst->rD, inst->rA, (int)inst->simm);
        }
        break;

    case PPC_OP_CMPI:
        {
            char rhs[32];
            snprintf(rhs, sizeof(rhs), "%d", (int)inst->simm);
            char lhs[32];
            snprintf(lhs, sizeof(lhs), "ctx->gpr[%u]", inst->rA);
            emit_compare_s32(out, inst->crfD, lhs, rhs);
        }
        break;

    case PPC_OP_CMPLI:
        {
            char rhs[32];
            snprintf(rhs, sizeof(rhs), "0x%04Xu", inst->uimm);
            char lhs[32];
            snprintf(lhs, sizeof(lhs), "ctx->gpr[%u]", inst->rA);
            emit_compare_u32(out, inst->crfD, lhs, rhs);
        }
        break;

    case PPC_OP_CMP:
        {
            char lhs[32], rhs[32];
            snprintf(lhs, sizeof(lhs), "ctx->gpr[%u]", inst->rA);
            snprintf(rhs, sizeof(rhs), "ctx->gpr[%u]", inst->rB);
            emit_compare_s32(out, inst->crfD, lhs, rhs);
        }
        break;

    case PPC_OP_CMPL:
        {
            char lhs[32], rhs[32];
            snprintf(lhs, sizeof(lhs), "ctx->gpr[%u]", inst->rA);
            snprintf(rhs, sizeof(rhs), "ctx->gpr[%u]", inst->rB);
            emit_compare_u32(out, inst->crfD, lhs, rhs);
        }
        break;

    case PPC_OP_ORI:
        if (inst->rS == 0 && inst->rA == 0 && inst->uimm == 0) {
            fprintf(out, "    // nop\n");
        } else {
            fprintf(out, "    ctx->gpr[%u] = ctx->gpr[%u] | 0x%04Xu;\n",
                    inst->rA, inst->rS, inst->uimm);
        }
        break;

    case PPC_OP_ORIS:
        fprintf(out, "    ctx->gpr[%u] = ctx->gpr[%u] | (0x%04Xu << 16);\n",
                inst->rA, inst->rS, inst->uimm);
        break;

    case PPC_OP_XORI:
        fprintf(out, "    ctx->gpr[%u] = ctx->gpr[%u] ^ 0x%04Xu;\n",
                inst->rA, inst->rS, inst->uimm);
        break;

    case PPC_OP_XORIS:
        fprintf(out, "    ctx->gpr[%u] = ctx->gpr[%u] ^ (0x%04Xu << 16);\n",
                inst->rA, inst->rS, inst->uimm);
        break;

    case PPC_OP_ANDI:
        fprintf(out, "    {\n");
        fprintf(out, "        ctx->gpr[%u] = ctx->gpr[%u] & 0x%04Xu;\n",
                inst->rA, inst->rS, inst->uimm);
        emit_set_cr0_from_gpr(out, inst->rA);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_ANDIS:
        fprintf(out, "    {\n");
        fprintf(out, "        ctx->gpr[%u] = ctx->gpr[%u] & (0x%04Xu << 16);\n",
                inst->rA, inst->rS, inst->uimm);
        emit_set_cr0_from_gpr(out, inst->rA);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_ADD:
    case PPC_OP_ADDO:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 a = ctx->gpr[%u];\n", inst->rA);
        fprintf(out, "        u32 b = ctx->gpr[%u];\n", inst->rB);
        fprintf(out, "        u32 res = a + b;\n");
        fprintf(out, "        ctx->gpr[%u] = res;\n", inst->rD);
        if (inst->oe)
            fprintf(out, "        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, b, res));\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_ADDC:
    case PPC_OP_ADDCO:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 a = ctx->gpr[%u];\n", inst->rA);
        fprintf(out, "        u32 b = ctx->gpr[%u];\n", inst->rB);
        fprintf(out, "        u64 wide = (u64)a + (u64)b;\n");
        fprintf(out, "        u32 res = (u32)wide;\n");
        fprintf(out, "        ctx->gpr[%u] = res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);\n");
        if (inst->oe)
            fprintf(out, "        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, b, res));\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_ADDE:
    case PPC_OP_ADDEO:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 carry = (ctx->xer >> 29) & 1u;\n");
        fprintf(out, "        u32 a = ctx->gpr[%u];\n", inst->rA);
        fprintf(out, "        u32 b = ctx->gpr[%u];\n", inst->rB);
        fprintf(out, "        u64 wide = (u64)a + (u64)b + carry;\n");
        fprintf(out, "        u32 res = (u32)wide;\n");
        fprintf(out, "        ctx->gpr[%u] = res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);\n");
        if (inst->oe)
            fprintf(out, "        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, b, res));\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_ADDME:
    case PPC_OP_ADDMEO:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 input = ctx->gpr[%u];\n", inst->rA);
        fprintf(out, "        u32 carry = (ctx->xer >> 29) & 1u;\n");
        fprintf(out, "        u64 res = (u64)input + 0xFFFFFFFFull + carry;\n");
        fprintf(out, "        ctx->gpr[%u] = (u32)res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | ((res >> 32) ? 0x20000000u : 0u);\n");
        if (inst->oe)
            fprintf(out, "        ppc_set_xer_ov(ctx, ppc_add_overflowed(input, 0xFFFFFFFFu, (u32)res));\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_ADDZE:
    case PPC_OP_ADDZEO:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 a = ctx->gpr[%u];\n", inst->rA);
        fprintf(out, "        u64 wide = (u64)a + ((ctx->xer >> 29) & 1u);\n");
        fprintf(out, "        u32 res = (u32)wide;\n");
        fprintf(out, "        ctx->gpr[%u] = res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);\n");
        if (inst->oe)
            fprintf(out, "        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, 0u, res));\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_SUBF:
    case PPC_OP_SUBFO:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 a = ~ctx->gpr[%u];\n", inst->rA);
        fprintf(out, "        u32 b = ctx->gpr[%u];\n", inst->rB);
        fprintf(out, "        u32 res = a + b + 1u;\n");
        fprintf(out, "        ctx->gpr[%u] = res;\n", inst->rD);
        if (inst->oe)
            fprintf(out, "        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, b, res));\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_SUBFC:
    case PPC_OP_SUBFCO:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 a = ~ctx->gpr[%u];\n", inst->rA);
        fprintf(out, "        u32 b = ctx->gpr[%u];\n", inst->rB);
        fprintf(out, "        u64 wide = (u64)b + (u64)a + 1u;\n");
        fprintf(out, "        u32 res = (u32)wide;\n");
        fprintf(out, "        ctx->gpr[%u] = res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);\n");
        if (inst->oe)
            fprintf(out, "        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, b, res));\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_SUBFE:
    case PPC_OP_SUBFEO:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 a = ~ctx->gpr[%u];\n", inst->rA);
        fprintf(out, "        u32 b = ctx->gpr[%u];\n", inst->rB);
        fprintf(out, "        u32 carry = (ctx->xer >> 29) & 1u;\n");
        fprintf(out, "        u64 wide = (u64)a + (u64)b + carry;\n");
        fprintf(out, "        u32 res = (u32)wide;\n");
        fprintf(out, "        ctx->gpr[%u] = res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);\n");
        if (inst->oe)
            fprintf(out, "        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, b, res));\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_SUBFME:
    case PPC_OP_SUBFMEO:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 input = ~ctx->gpr[%u];\n", inst->rA);
        fprintf(out, "        u32 carry = (ctx->xer >> 29) & 1u;\n");
        fprintf(out, "        u64 res = (u64)input + 0xFFFFFFFFull + carry;\n");
        fprintf(out, "        ctx->gpr[%u] = (u32)res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | ((res >> 32) ? 0x20000000u : 0u);\n");
        if (inst->oe)
            fprintf(out, "        ppc_set_xer_ov(ctx, ppc_add_overflowed(input, 0xFFFFFFFFu, (u32)res));\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_SUBFZE:
    case PPC_OP_SUBFZEO:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 a = ~ctx->gpr[%u];\n", inst->rA);
        fprintf(out, "        u64 wide = (u64)a + ((ctx->xer >> 29) & 1u);\n");
        fprintf(out, "        u32 res = (u32)wide;\n");
        fprintf(out, "        ctx->gpr[%u] = res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);\n");
        if (inst->oe)
            fprintf(out, "        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, 0u, res));\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_NEG:
    case PPC_OP_NEGO:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 a = ctx->gpr[%u];\n", inst->rA);
        fprintf(out, "        ctx->gpr[%u] = (~a) + 1u;\n", inst->rD);
        if (inst->oe)
            fprintf(out, "        ppc_set_xer_ov(ctx, a == 0x80000000u);\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_MULLW:
    case PPC_OP_MULLWO:
        fprintf(out, "    {\n");
        fprintf(out, "        s64 product = (s64)(s32)ctx->gpr[%u] * (s64)(s32)ctx->gpr[%u];\n",
                inst->rA, inst->rB);
        fprintf(out, "        ctx->gpr[%u] = (u32)product;\n", inst->rD);
        if (inst->oe)
            fprintf(out, "        ppc_set_xer_ov(ctx, product < -0x80000000ll || product > 0x7fffffffll);\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_MULHW:
        fprintf(out, "    {\n");
        fprintf(out, "        s64 product = (s64)(s32)ctx->gpr[%u] * (s64)(s32)ctx->gpr[%u];\n",
                inst->rA, inst->rB);
        fprintf(out, "        ctx->gpr[%u] = (u32)(product >> 32);\n", inst->rD);
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_MULHWU:
        fprintf(out, "    {\n");
        fprintf(out, "        u64 product = (u64)ctx->gpr[%u] * (u64)ctx->gpr[%u];\n",
                inst->rA, inst->rB);
        fprintf(out, "        ctx->gpr[%u] = (u32)(product >> 32);\n", inst->rD);
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_DIVW:
    case PPC_OP_DIVWO:
        fprintf(out, "    {\n");
        fprintf(out, "        s32 dividend = (s32)ctx->gpr[%u];\n", inst->rA);
        fprintf(out, "        s32 divisor = (s32)ctx->gpr[%u];\n", inst->rB);
        fprintf(out, "        bool ov = divisor == 0 || ((u32)dividend == 0x80000000u && divisor == -1);\n");
        fprintf(out, "        ctx->gpr[%u] = ov ? ((dividend < 0) ? 0xFFFFFFFFu : 0u) : (u32)(dividend / divisor);\n",
                inst->rD);
        if (inst->oe)
            fprintf(out, "        ppc_set_xer_ov(ctx, ov);\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_DIVWU:
    case PPC_OP_DIVWUO:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 divisor = ctx->gpr[%u];\n", inst->rB);
        fprintf(out, "        ctx->gpr[%u] = divisor == 0 ? 0u : ctx->gpr[%u] / divisor;\n",
                inst->rD, inst->rA);
        if (inst->oe)
            fprintf(out, "        ppc_set_xer_ov(ctx, divisor == 0);\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_AND:
    case PPC_OP_ANDC:
    case PPC_OP_OR:
    case PPC_OP_ORC:
    case PPC_OP_XOR:
    case PPC_OP_NAND:
    case PPC_OP_NOR:
    case PPC_OP_EQV: {
        const char* expr = NULL;
        switch (inst->op) {
        case PPC_OP_AND:  expr = "ctx->gpr[%u] & ctx->gpr[%u]"; break;
        case PPC_OP_ANDC: expr = "ctx->gpr[%u] & ~ctx->gpr[%u]"; break;
        case PPC_OP_OR:   expr = "ctx->gpr[%u] | ctx->gpr[%u]"; break;
        case PPC_OP_ORC:  expr = "ctx->gpr[%u] | ~ctx->gpr[%u]"; break;
        case PPC_OP_XOR:  expr = "ctx->gpr[%u] ^ ctx->gpr[%u]"; break;
        case PPC_OP_NAND: expr = "~(ctx->gpr[%u] & ctx->gpr[%u])"; break;
        case PPC_OP_NOR:  expr = "~(ctx->gpr[%u] | ctx->gpr[%u])"; break;
        default:          expr = "~(ctx->gpr[%u] ^ ctx->gpr[%u])"; break;
        }
        fprintf(out, "    {\n");
        fprintf(out, "        ctx->gpr[%u] = ", inst->rA);
        fprintf(out, expr, inst->rS, inst->rB);
        fprintf(out, ";\n");
        emit_record_if_needed(out, inst, inst->rA);
        fprintf(out, "    }\n");
        break;
    }

    case PPC_OP_CNTLZW:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 v = ctx->gpr[%u];\n", inst->rS);
        fprintf(out, "        u32 n = 0;\n");
        fprintf(out, "        while (n < 32 && ((v & (0x80000000u >> n)) == 0)) n++;\n");
        fprintf(out, "        ctx->gpr[%u] = n;\n", inst->rA);
        emit_record_if_needed(out, inst, inst->rA);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_EXTSB:
        fprintf(out, "    {\n");
        fprintf(out, "        ctx->gpr[%u] = (u32)(s32)(s8)ctx->gpr[%u];\n",
                inst->rA, inst->rS);
        emit_record_if_needed(out, inst, inst->rA);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_EXTSH:
        fprintf(out, "    {\n");
        fprintf(out, "        ctx->gpr[%u] = (u32)(s32)(s16)ctx->gpr[%u];\n",
                inst->rA, inst->rS);
        emit_record_if_needed(out, inst, inst->rA);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_SLW:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 sh = ctx->gpr[%u] & 0x3Fu;\n", inst->rB);
        fprintf(out, "        ctx->gpr[%u] = sh > 31 ? 0u : (ctx->gpr[%u] << sh);\n",
                inst->rA, inst->rS);
        emit_record_if_needed(out, inst, inst->rA);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_SRW:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 sh = ctx->gpr[%u] & 0x3Fu;\n", inst->rB);
        fprintf(out, "        ctx->gpr[%u] = sh > 31 ? 0u : (ctx->gpr[%u] >> sh);\n",
                inst->rA, inst->rS);
        emit_record_if_needed(out, inst, inst->rA);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_SRAW:
    case PPC_OP_SRAWI:
        fprintf(out, "    {\n");
        if (inst->op == PPC_OP_SRAWI) {
            fprintf(out, "        u32 sh = %uu;\n", inst->sh);
        } else {
            fprintf(out, "        u32 sh = ctx->gpr[%u] & 0x3Fu;\n", inst->rB);
        }
        fprintf(out, "        u32 value = ctx->gpr[%u];\n", inst->rS);
        fprintf(out, "        bool ca = false;\n");
        fprintf(out, "        if (sh == 0) {\n");
        fprintf(out, "            ctx->gpr[%u] = value;\n", inst->rA);
        fprintf(out, "        } else if (sh > 31) {\n");
        fprintf(out, "            ctx->gpr[%u] = (value & 0x80000000u) ? 0xFFFFFFFFu : 0u;\n", inst->rA);
        fprintf(out, "            ca = (value & 0x80000000u) != 0;\n");
        fprintf(out, "        } else {\n");
        fprintf(out, "            ctx->gpr[%u] = (u32)((s32)value >> sh);\n", inst->rA);
        fprintf(out, "            ca = (value & 0x80000000u) && ((value << (32u - sh)) != 0);\n");
        fprintf(out, "        }\n");
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | (ca ? 0x20000000u : 0u);\n");
        emit_record_if_needed(out, inst, inst->rA);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_RLWINM:
        {
            u32 mask = ppc_mask32(inst->mb, inst->me);
            fprintf(out, "    {\n");
            fprintf(out, "        ctx->gpr[%u] = dolrecomp_rotl32(ctx->gpr[%u], %uu) & 0x%08Xu;\n",
                    inst->rA, inst->rS, inst->sh, mask);
            emit_record_if_needed(out, inst, inst->rA);
            fprintf(out, "    }\n");
        }
        break;

    case PPC_OP_RLWNM:
        {
            u32 mask = ppc_mask32(inst->mb, inst->me);
            fprintf(out, "    {\n");
            fprintf(out, "        ctx->gpr[%u] = dolrecomp_rotl32(ctx->gpr[%u], ctx->gpr[%u]) & 0x%08Xu;\n",
                    inst->rA, inst->rS, inst->rB, mask);
            emit_record_if_needed(out, inst, inst->rA);
            fprintf(out, "    }\n");
        }
        break;

    case PPC_OP_RLWIMI:
        {
            u32 mask = ppc_mask32(inst->mb, inst->me);
            fprintf(out, "    {\n");
            fprintf(out, "        u32 rot = dolrecomp_rotl32(ctx->gpr[%u], %uu);\n",
                    inst->rS, inst->sh);
            fprintf(out, "        ctx->gpr[%u] = (ctx->gpr[%u] & ~0x%08Xu) | (rot & 0x%08Xu);\n",
                    inst->rA, inst->rA, mask, mask);
            emit_record_if_needed(out, inst, inst->rA);
            fprintf(out, "    }\n");
        }
        break;

    case PPC_OP_FADDS:
        fprintf(out, "    ctx->fpr[%u] = (f64)(f32)(ctx->fpr[%u] + ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rB);
        break;

    case PPC_OP_FSUBS:
        fprintf(out, "    ctx->fpr[%u] = (f64)(f32)(ctx->fpr[%u] - ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rB);
        break;

    case PPC_OP_FMULS:
        fprintf(out, "    ctx->fpr[%u] = (f64)(f32)(ctx->fpr[%u] * ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rC);
        break;

    case PPC_OP_FDIVS:
        fprintf(out, "    ctx->fpr[%u] = (f64)(f32)(ctx->fpr[%u] / ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rB);
        break;

    case PPC_OP_FRES:
        fprintf(out, "    { f64 result; if (ppc_fres(ctx, ctx->fpr[%u], &result)) ctx->fpr[%u] = ctx->ps1[%u] = result; }\n",
                inst->rB, inst->rD, inst->rD);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_FMADDS:
    case PPC_OP_FMSUBS:
    case PPC_OP_FNMADDS:
    case PPC_OP_FNMSUBS: {
        const bool sub = inst->op == PPC_OP_FMSUBS || inst->op == PPC_OP_FNMSUBS;
        const bool neg = inst->op == PPC_OP_FNMADDS || inst->op == PPC_OP_FNMSUBS;
        fprintf(out, "    {\n");
        fprintf(out, "        f64 result;\n");
        fprintf(out, "        if (ppc_fma(ctx, ctx->fpr[%u], ctx->fpr[%u], ctx->fpr[%u], true, %s, %s, &result))\n",
                inst->rA, inst->rC, inst->rB, sub ? "true" : "false", neg ? "true" : "false");
        fprintf(out, "            ctx->fpr[%u] = ctx->ps1[%u] = result;\n", inst->rD, inst->rD);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        fprintf(out, "    }\n");
        break;
    }

    case PPC_OP_FADD:
        fprintf(out, "    ctx->fpr[%u] = ctx->fpr[%u] + ctx->fpr[%u];\n",
                inst->rD, inst->rA, inst->rB);
        break;

    case PPC_OP_FSUB:
        fprintf(out, "    ctx->fpr[%u] = ctx->fpr[%u] - ctx->fpr[%u];\n",
                inst->rD, inst->rA, inst->rB);
        break;

    case PPC_OP_FMUL:
        fprintf(out, "    ctx->fpr[%u] = ctx->fpr[%u] * ctx->fpr[%u];\n",
                inst->rD, inst->rA, inst->rC);
        break;

    case PPC_OP_FDIV:
        fprintf(out, "    ctx->fpr[%u] = ctx->fpr[%u] / ctx->fpr[%u];\n",
                inst->rD, inst->rA, inst->rB);
        break;

    case PPC_OP_FRSQRTE:
        fprintf(out, "    { f64 result; if (ppc_frsqrte(ctx, ctx->fpr[%u], &result)) ctx->fpr[%u] = result; }\n",
                inst->rB, inst->rD);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_FMADD:
    case PPC_OP_FMSUB:
    case PPC_OP_FNMADD:
    case PPC_OP_FNMSUB: {
        const bool sub = inst->op == PPC_OP_FMSUB || inst->op == PPC_OP_FNMSUB;
        const bool neg = inst->op == PPC_OP_FNMADD || inst->op == PPC_OP_FNMSUB;
        fprintf(out, "    {\n");
        fprintf(out, "        f64 result;\n");
        fprintf(out, "        if (ppc_fma(ctx, ctx->fpr[%u], ctx->fpr[%u], ctx->fpr[%u], false, %s, %s, &result))\n",
                inst->rA, inst->rC, inst->rB, sub ? "true" : "false", neg ? "true" : "false");
        fprintf(out, "            ctx->fpr[%u] = result;\n", inst->rD);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        fprintf(out, "    }\n");
        break;
    }

    case PPC_OP_FCTIW:
    case PPC_OP_FCTIWZ:
        fprintf(out, "    { u64 result; if (ppc_fctiw(ctx, ctx->fpr[%u], %s, &result)) ctx->fpr[%u] = dolrecomp_f64_from_bits(result); }\n",
                inst->rB, inst->op == PPC_OP_FCTIWZ ? "true" : "false", inst->rD);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_FMR:
        fprintf(out, "    ctx->fpr[%u] = ctx->fpr[%u];\n", inst->rD, inst->rB);
        break;

    case PPC_OP_FNEG:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_f64_from_bits(dolrecomp_f64_to_bits(ctx->fpr[%u]) ^ 0x8000000000000000ull);\n",
                inst->rD, inst->rB);
        break;

    case PPC_OP_FABS:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_f64_from_bits(dolrecomp_f64_to_bits(ctx->fpr[%u]) & 0x7FFFFFFFFFFFFFFFull);\n",
                inst->rD, inst->rB);
        break;

    case PPC_OP_FNABS:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_f64_from_bits(dolrecomp_f64_to_bits(ctx->fpr[%u]) | 0x8000000000000000ull);\n",
                inst->rD, inst->rB);
        break;

    case PPC_OP_FRSP:
        fprintf(out, "    ctx->fpr[%u] = (f64)(f32)ctx->fpr[%u];\n", inst->rD, inst->rB);
        break;

    case PPC_OP_FSEL:
        fprintf(out, "    {\n");
        fprintf(out, "        ctx->fpr[%u] = (ctx->fpr[%u] >= 0.0) ? ctx->fpr[%u] : ctx->fpr[%u];\n",
                inst->rD, inst->rA, inst->rC, inst->rB);
        if (inst->rc) {
            emit_set_cr1_from_fpscr(out);
        }
        fprintf(out, "    }\n");
        break;

    case PPC_OP_MTFSB0:
    case PPC_OP_MTFSB1:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 mask = 0x80000000u >> %u;\n", inst->rD);
        if (inst->op == PPC_OP_MTFSB0) {
            fprintf(out, "        if (%u != 1 && %u != 2) ctx->fpscr &= ~mask;\n",
                    inst->rD, inst->rD);
        } else {
            fprintf(out, "        if (%u != 1 && %u != 2) ctx->fpscr |= mask;\n",
                    inst->rD, inst->rD);
        }
        if (inst->rc) {
            emit_set_cr1_from_fpscr(out);
        }
        fprintf(out, "    }\n");
        break;

    case PPC_OP_MFFS:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_f64_from_bits(0xFFF8000000000000ull | ctx->fpscr);\n", inst->rD);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_MCRFS: {
        u32 shift = cr_field_shift(inst->crfS);
        u32 dst_shift = cr_field_shift(inst->crfD);
        fprintf(out, "    {\n");
        fprintf(out, "        u32 field = (ctx->fpscr >> %u) & 0xFu;\n", shift);
        fprintf(out, "        ctx->fpscr &= ~((0xFu << %u) & 0x83F80700u);\n", shift);
        fprintf(out, "        ppc_fpscr_updated(ctx);\n");
        fprintf(out, "        ctx->cr = (ctx->cr & ~(0xFu << %u)) | (field << %u);\n", dst_shift, dst_shift);
        fprintf(out, "    }\n");
        break;
    }

    case PPC_OP_MTFSFI: {
        u32 shift = cr_field_shift(inst->crfD);
        fprintf(out, "    ctx->fpscr = (ctx->fpscr & ~(0xFu << %u)) | (0x%Xu << %u);\n",
                shift, inst->imm, shift);
        fprintf(out, "    ppc_fpscr_updated(ctx);\n");
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;
    }

    case PPC_OP_MTFSF:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 mask = 0;\n");
        fprintf(out, "        for (u32 i = 0; i < 8; i++) if (0x%02Xu & (1u << i)) mask |= 0xFu << (i * 4);\n", inst->fm);
        fprintf(out, "        u32 source = (u32)dolrecomp_f64_to_bits(ctx->fpr[%u]);\n", inst->rB);
        fprintf(out, "        ctx->fpscr = (ctx->fpscr & ~mask) | (source & mask);\n");
        fprintf(out, "        ppc_fpscr_updated(ctx);\n");
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_PS_ADD:
        fprintf(out, "    {\n");
        fprintf(out, "        ctx->fpr[%u] = dolrecomp_ps_round((f32)ctx->fpr[%u] + (f32)ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rB);
        fprintf(out, "        ctx->ps1[%u] = dolrecomp_ps_round((f32)ctx->ps1[%u] + (f32)ctx->ps1[%u]);\n",
                inst->rD, inst->rA, inst->rB);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_PS_SUB:
        fprintf(out, "    {\n");
        fprintf(out, "        ctx->fpr[%u] = dolrecomp_ps_round((f32)ctx->fpr[%u] - (f32)ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rB);
        fprintf(out, "        ctx->ps1[%u] = dolrecomp_ps_round((f32)ctx->ps1[%u] - (f32)ctx->ps1[%u]);\n",
                inst->rD, inst->rA, inst->rB);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_PS_MUL:
        fprintf(out, "    {\n");
        fprintf(out, "        ctx->fpr[%u] = dolrecomp_ps_round((f32)ctx->fpr[%u] * (f32)ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rC);
        fprintf(out, "        ctx->ps1[%u] = dolrecomp_ps_round((f32)ctx->ps1[%u] * (f32)ctx->ps1[%u]);\n",
                inst->rD, inst->rA, inst->rC);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_PS_DIV:
        fprintf(out, "    {\n");
        fprintf(out, "        ctx->fpr[%u] = dolrecomp_ps_round((f32)ctx->fpr[%u] / (f32)ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rB);
        fprintf(out, "        ctx->ps1[%u] = dolrecomp_ps_round((f32)ctx->ps1[%u] / (f32)ctx->ps1[%u]);\n",
                inst->rD, inst->rA, inst->rB);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_PS_RES:
        fprintf(out, "    { f64 a, b; ppc_ps_res(ctx, ctx->fpr[%u], ctx->ps1[%u], &a, &b); ctx->fpr[%u] = dolrecomp_ps_round(a); ctx->ps1[%u] = dolrecomp_ps_round(b); }\n",
                inst->rB, inst->rB, inst->rD, inst->rD);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_RSQRTE:
        fprintf(out, "    { f64 a, b; ppc_ps_rsqrte(ctx, ctx->fpr[%u], ctx->ps1[%u], &a, &b); ctx->fpr[%u] = dolrecomp_ps_round(a); ctx->ps1[%u] = dolrecomp_ps_round(b); }\n",
                inst->rB, inst->rB, inst->rD, inst->rD);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_MADD:
    case PPC_OP_PS_MSUB:
    case PPC_OP_PS_NMADD:
    case PPC_OP_PS_NMSUB:
        fprintf(out, "    {\n");
        fprintf(out, "        f32 ps0 = (f32)ctx->fpr[%u] * (f32)ctx->fpr[%u];\n",
                inst->rA, inst->rC);
        fprintf(out, "        f32 ps1 = (f32)ctx->ps1[%u] * (f32)ctx->ps1[%u];\n",
                inst->rA, inst->rC);
        if (inst->op == PPC_OP_PS_MADD || inst->op == PPC_OP_PS_NMADD) {
            fprintf(out, "        ps0 += (f32)ctx->fpr[%u];\n", inst->rB);
            fprintf(out, "        ps1 += (f32)ctx->ps1[%u];\n", inst->rB);
        } else {
            fprintf(out, "        ps0 -= (f32)ctx->fpr[%u];\n", inst->rB);
            fprintf(out, "        ps1 -= (f32)ctx->ps1[%u];\n", inst->rB);
        }
        if (inst->op == PPC_OP_PS_NMADD || inst->op == PPC_OP_PS_NMSUB) {
            fprintf(out, "        ps0 = -ps0;\n");
            fprintf(out, "        ps1 = -ps1;\n");
        }
        fprintf(out, "        ctx->fpr[%u] = dolrecomp_ps_round(ps0);\n", inst->rD);
        fprintf(out, "        ctx->ps1[%u] = dolrecomp_ps_round(ps1);\n", inst->rD);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_PS_NEG:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_ps_from_bits(dolrecomp_ps_to_bits(ctx->fpr[%u]) ^ 0x80000000u);\n",
                inst->rD, inst->rB);
        fprintf(out, "    ctx->ps1[%u] = dolrecomp_ps_from_bits(dolrecomp_ps_to_bits(ctx->ps1[%u]) ^ 0x80000000u);\n",
                inst->rD, inst->rB);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_ABS:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_ps_from_bits(dolrecomp_ps_to_bits(ctx->fpr[%u]) & 0x7FFFFFFFu);\n",
                inst->rD, inst->rB);
        fprintf(out, "    ctx->ps1[%u] = dolrecomp_ps_from_bits(dolrecomp_ps_to_bits(ctx->ps1[%u]) & 0x7FFFFFFFu);\n",
                inst->rD, inst->rB);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_NABS:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_ps_from_bits(dolrecomp_ps_to_bits(ctx->fpr[%u]) | 0x80000000u);\n",
                inst->rD, inst->rB);
        fprintf(out, "    ctx->ps1[%u] = dolrecomp_ps_from_bits(dolrecomp_ps_to_bits(ctx->ps1[%u]) | 0x80000000u);\n",
                inst->rD, inst->rB);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_MR:
        fprintf(out, "    ctx->fpr[%u] = ctx->fpr[%u];\n", inst->rD, inst->rB);
        fprintf(out, "    ctx->ps1[%u] = ctx->ps1[%u];\n", inst->rD, inst->rB);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_SUM0:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_ps_round((f32)ctx->fpr[%u] + (f32)ctx->ps1[%u]);\n",
                inst->rD, inst->rA, inst->rB);
        fprintf(out, "    ctx->ps1[%u] = dolrecomp_ps_round(ctx->ps1[%u]);\n",
                inst->rD, inst->rC);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_SUM1:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_ps_round(ctx->fpr[%u]);\n",
                inst->rD, inst->rC);
        fprintf(out, "    ctx->ps1[%u] = dolrecomp_ps_round((f32)ctx->fpr[%u] + (f32)ctx->ps1[%u]);\n",
                inst->rD, inst->rA, inst->rB);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_MULS0:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_ps_round((f32)ctx->fpr[%u] * (f32)ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rC);
        fprintf(out, "    ctx->ps1[%u] = dolrecomp_ps_round((f32)ctx->ps1[%u] * (f32)ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rC);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_MULS1:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_ps_round((f32)ctx->fpr[%u] * (f32)ctx->ps1[%u]);\n",
                inst->rD, inst->rA, inst->rC);
        fprintf(out, "    ctx->ps1[%u] = dolrecomp_ps_round((f32)ctx->ps1[%u] * (f32)ctx->ps1[%u]);\n",
                inst->rD, inst->rA, inst->rC);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_MADDS0:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_ps_round((f32)ctx->fpr[%u] * (f32)ctx->fpr[%u] + (f32)ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rC, inst->rB);
        fprintf(out, "    ctx->ps1[%u] = dolrecomp_ps_round((f32)ctx->ps1[%u] * (f32)ctx->fpr[%u] + (f32)ctx->ps1[%u]);\n",
                inst->rD, inst->rA, inst->rC, inst->rB);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_MADDS1:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_ps_round((f32)ctx->fpr[%u] * (f32)ctx->ps1[%u] + (f32)ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rC, inst->rB);
        fprintf(out, "    ctx->ps1[%u] = dolrecomp_ps_round((f32)ctx->ps1[%u] * (f32)ctx->ps1[%u] + (f32)ctx->ps1[%u]);\n",
                inst->rD, inst->rA, inst->rC, inst->rB);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_MERGE00:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_ps_round(ctx->fpr[%u]);\n", inst->rD, inst->rA);
        fprintf(out, "    ctx->ps1[%u] = dolrecomp_ps_round(ctx->fpr[%u]);\n", inst->rD, inst->rB);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_MERGE01:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_ps_round(ctx->fpr[%u]);\n", inst->rD, inst->rA);
        fprintf(out, "    ctx->ps1[%u] = dolrecomp_ps_round(ctx->ps1[%u]);\n", inst->rD, inst->rB);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_MERGE10:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_ps_round(ctx->ps1[%u]);\n", inst->rD, inst->rA);
        fprintf(out, "    ctx->ps1[%u] = dolrecomp_ps_round(ctx->fpr[%u]);\n", inst->rD, inst->rB);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_MERGE11:
        fprintf(out, "    ctx->fpr[%u] = dolrecomp_ps_round(ctx->ps1[%u]);\n", inst->rD, inst->rA);
        fprintf(out, "    ctx->ps1[%u] = dolrecomp_ps_round(ctx->ps1[%u]);\n", inst->rD, inst->rB);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_PS_CMPU0:
    case PPC_OP_PS_CMPO0:
    case PPC_OP_PS_CMPU1:
    case PPC_OP_PS_CMPO1:
        fprintf(out, "    {\n");
        if (inst->op == PPC_OP_PS_CMPU0 || inst->op == PPC_OP_PS_CMPO0) {
            fprintf(out, "        f32 val_a = (f32)ctx->fpr[%u];\n", inst->rA);
            fprintf(out, "        f32 val_b = (f32)ctx->fpr[%u];\n", inst->rB);
        } else {
            fprintf(out, "        f32 val_a = (f32)ctx->ps1[%u];\n", inst->rA);
            fprintf(out, "        f32 val_b = (f32)ctx->ps1[%u];\n", inst->rB);
        }
        fprintf(out, "        u32 cr_bits = 0;\n");
        fprintf(out, "        if (val_a < val_b)       cr_bits = 0x8u;\n");
        fprintf(out, "        else if (val_a > val_b)  cr_bits = 0x4u;\n");
        fprintf(out, "        else if (val_a == val_b) cr_bits = 0x2u;\n");
        fprintf(out, "        else                     cr_bits = 0x1u;\n");
        fprintf(out, "        ctx->cr = (ctx->cr & ~(0xFu << %u)) | (cr_bits << %u);\n",
                cr_field_shift(inst->crfD), cr_field_shift(inst->crfD));
        fprintf(out, "    }\n");
        break;

    case PPC_OP_PS_SEL:
        fprintf(out, "    ctx->fpr[%u] = ((f32)ctx->fpr[%u] >= 0.0f) ? ctx->fpr[%u] : ctx->fpr[%u];\n",
                inst->rD, inst->rA, inst->rC, inst->rB);
        fprintf(out, "    ctx->ps1[%u] = ((f32)ctx->ps1[%u] >= 0.0f) ? ctx->ps1[%u] : ctx->ps1[%u];\n",
                inst->rD, inst->rA, inst->rC, inst->rB);
        if (inst->rc) emit_set_cr1_from_fpscr(out);
        break;

    case PPC_OP_FCMPU:
    case PPC_OP_FCMPO:
        emit_fcompare(out, inst);
        break;

    case PPC_OP_LWZ:  emit_load(out, inst, "mem_read32(ctx, ea)", false); break;
    case PPC_OP_LWZU: emit_load(out, inst, "mem_read32(ctx, ea)", true); break;
    case PPC_OP_LBZ:  emit_load(out, inst, "mem_read8(ctx, ea)", false); break;
    case PPC_OP_LBZU: emit_load(out, inst, "mem_read8(ctx, ea)", true); break;
    case PPC_OP_LHZ:  emit_load(out, inst, "mem_read16(ctx, ea)", false); break;
    case PPC_OP_LHZU: emit_load(out, inst, "mem_read16(ctx, ea)", true); break;
    case PPC_OP_LHA:  emit_load(out, inst, "(u32)(s32)(s16)mem_read16(ctx, ea)", false); break;
    case PPC_OP_LHAU: emit_load(out, inst, "(u32)(s32)(s16)mem_read16(ctx, ea)", true); break;

    case PPC_OP_LWZX:  emit_loadx(out, inst, "mem_read32(ctx, ea)", false); break;
    case PPC_OP_LWZUX: emit_loadx(out, inst, "mem_read32(ctx, ea)", true); break;
    case PPC_OP_LBZX:  emit_loadx(out, inst, "mem_read8(ctx, ea)", false); break;
    case PPC_OP_LBZUX: emit_loadx(out, inst, "mem_read8(ctx, ea)", true); break;
    case PPC_OP_LHZX:  emit_loadx(out, inst, "mem_read16(ctx, ea)", false); break;
    case PPC_OP_LHZUX: emit_loadx(out, inst, "mem_read16(ctx, ea)", true); break;
    case PPC_OP_LHAX:  emit_loadx(out, inst, "(u32)(s32)(s16)mem_read16(ctx, ea)", false); break;
    case PPC_OP_LHAUX: emit_loadx(out, inst, "(u32)(s32)(s16)mem_read16(ctx, ea)", true); break;
    case PPC_OP_LWBRX: emit_loadx(out, inst, "bswap32(mem_read32(ctx, ea))", false); break;
    case PPC_OP_LHBRX: emit_loadx(out, inst, "bswap16(mem_read16(ctx, ea))", false); break;

    case PPC_OP_LFS:   emit_fload(out, inst, true,  false); break;
    case PPC_OP_LFSU:  emit_fload(out, inst, true,  true); break;
    case PPC_OP_LFD:   emit_fload(out, inst, false, false); break;
    case PPC_OP_LFDU:  emit_fload(out, inst, false, true); break;

    case PPC_OP_LFSX:  emit_floadx(out, inst, true,  false); break;
    case PPC_OP_LFSUX: emit_floadx(out, inst, true,  true); break;
    case PPC_OP_LFDX:  emit_floadx(out, inst, false, false); break;
    case PPC_OP_LFDUX: emit_floadx(out, inst, false, true); break;

    case PPC_OP_PSQ_L:   emit_psq_load(out, inst, false, false); break;
    case PPC_OP_PSQ_LU:  emit_psq_load(out, inst, false, true); break;
    case PPC_OP_PSQ_LX:  emit_psq_load(out, inst, true,  false); break;
    case PPC_OP_PSQ_LUX: emit_psq_load(out, inst, true,  true); break;

    case PPC_OP_STW:  emit_store(out, inst, "mem_write32", "u32", false); break;
    case PPC_OP_STWU: emit_store(out, inst, "mem_write32", "u32", true); break;
    case PPC_OP_STB:  emit_store(out, inst, "mem_write8", "u8", false); break;
    case PPC_OP_STBU: emit_store(out, inst, "mem_write8", "u8", true); break;
    case PPC_OP_STH:  emit_store(out, inst, "mem_write16", "u16", false); break;
    case PPC_OP_STHU: emit_store(out, inst, "mem_write16", "u16", true); break;

    case PPC_OP_STWX:  emit_storex(out, inst, "mem_write32", "u32", false); break;
    case PPC_OP_STWUX: emit_storex(out, inst, "mem_write32", "u32", true); break;
    case PPC_OP_STBX:  emit_storex(out, inst, "mem_write8", "u8", false); break;
    case PPC_OP_STBUX: emit_storex(out, inst, "mem_write8", "u8", true); break;
    case PPC_OP_STHX:  emit_storex(out, inst, "mem_write16", "u16", false); break;
    case PPC_OP_STHUX: emit_storex(out, inst, "mem_write16", "u16", true); break;

    case PPC_OP_STFS:   emit_fstore(out, inst, true,  false); break;
    case PPC_OP_STFSU:  emit_fstore(out, inst, true,  true); break;
    case PPC_OP_STFD:   emit_fstore(out, inst, false, false); break;
    case PPC_OP_STFDU:  emit_fstore(out, inst, false, true); break;

    case PPC_OP_STFSX:  emit_fstorex(out, inst, true,  false); break;
    case PPC_OP_STFSUX: emit_fstorex(out, inst, true,  true); break;
    case PPC_OP_STFDX:  emit_fstorex(out, inst, false, false); break;
    case PPC_OP_STFDUX: emit_fstorex(out, inst, false, true); break;

    case PPC_OP_PSQ_ST:   emit_psq_store(out, inst, false, false); break;
    case PPC_OP_PSQ_STU:  emit_psq_store(out, inst, false, true); break;
    case PPC_OP_PSQ_STX:  emit_psq_store(out, inst, true,  false); break;
    case PPC_OP_PSQ_STUX: emit_psq_store(out, inst, true,  true); break;

    case PPC_OP_STWBRX:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 ea = ");
        emit_xform_ea(out, inst->rA, inst->rB, false);
        fprintf(out, ";\n");
        fprintf(out, "        mem_write32(ctx, ea, bswap32(ctx->gpr[%u]));\n", inst->rS);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_STHBRX:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 ea = ");
        emit_xform_ea(out, inst->rA, inst->rB, false);
        fprintf(out, ";\n");
        fprintf(out, "        mem_write16(ctx, ea, bswap16((u16)ctx->gpr[%u]));\n", inst->rS);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_LSWI:
    case PPC_OP_LSWX: {
        u32 count = inst->op == PPC_OP_LSWI ? (inst->nb ? inst->nb : 32u) : 0u;
        fprintf(out, "    {\n");
        if (inst->op == PPC_OP_LSWX) {
            fprintf(out, "        u32 ea = ctx->gpr[%u];\n", inst->rB);
            if (inst->rA)
                fprintf(out, "        ea += ctx->gpr[%u];\n", inst->rA);
            fprintf(out, "        u32 count = ctx->xer & 0x7Fu;\n");
            fprintf(out, "        u32 reg_count = (count + 3u) / 4u;\n");
            fprintf(out, "        for (u32 r = 0; r < reg_count; r++) {\n");
            fprintf(out, "            u32 reg = (%uu + r) & 31u;\n", inst->rD);
            fprintf(out, "            if (reg == %uu || reg == %uu) {\n", inst->rA, inst->rB);
            fprintf(out, "                ppc_program_exception(ctx, PPC_PROGRAM_ILLEGAL, 0x%08Xu);\n",
                    inst->address);
            fprintf(out, "                return;\n");
            fprintf(out, "            }\n");
            fprintf(out, "        }\n");
        } else {
            if (inst->rA) fprintf(out, "        u32 ea = ctx->gpr[%u];\n", inst->rA);
            else fprintf(out, "        u32 ea = 0u;\n");
            fprintf(out, "        u32 count = %uu;\n", count);
        }
        fprintf(out, "        for (u32 n = 0; n < count; n++) {\n");
        fprintf(out, "            u32 reg = (%uu + n / 4u) & 31u;\n", inst->rD);
        fprintf(out, "            if ((n & 3u) == 0) ctx->gpr[reg] = 0;\n");
        fprintf(out, "            ctx->gpr[reg] |= (u32)mem_read8(ctx, ea + n) << (24u - 8u * (n & 3u));\n");
        fprintf(out, "        }\n");
        fprintf(out, "    }\n");
        break;
    }

    case PPC_OP_STSWI:
    case PPC_OP_STSWX: {
        u32 count = inst->op == PPC_OP_STSWI ? (inst->nb ? inst->nb : 32u) : 0u;
        fprintf(out, "    {\n");
        if (inst->op == PPC_OP_STSWX) {
            fprintf(out, "        u32 ea = ctx->gpr[%u]", inst->rB);
            if (inst->rA) fprintf(out, " + ctx->gpr[%u]", inst->rA);
            fprintf(out, ";\n        u32 count = ctx->xer & 0x7Fu;\n");
        } else {
            if (inst->rA) fprintf(out, "        u32 ea = ctx->gpr[%u];\n", inst->rA);
            else fprintf(out, "        u32 ea = 0u;\n");
            fprintf(out, "        u32 count = %uu;\n", count);
        }
        fprintf(out, "        for (u32 n = 0; n < count; n++) {\n");
        fprintf(out, "            u32 reg = (%uu + n / 4u) & 31u;\n", inst->rS);
        fprintf(out, "            u8 value = (u8)(ctx->gpr[reg] >> (24u - 8u * (n & 3u)));\n");
        fprintf(out, "            mem_write8(ctx, ea + n, value);\n");
        fprintf(out, "        }\n");
        fprintf(out, "    }\n");
        break;
    }

    case PPC_OP_LWARX:
        fprintf(out, "    {\n        u32 ea = ");
        emit_xform_ea(out, inst->rA, inst->rB, false);
        fprintf(out, ";\n        ctx->gpr[%u] = mem_read32(ctx, ea);\n", inst->rD);
        fprintf(out, "        ctx->reserve_addr = ea;\n        ctx->reserve_valid = true;\n    }\n");
        break;

    case PPC_OP_STWCX:
        fprintf(out, "    {\n        u32 ea = ");
        emit_xform_ea(out, inst->rA, inst->rB, false);
        fprintf(out, ";\n        bool success = ctx->reserve_valid;\n");
        fprintf(out, "        ctx->reserve_valid = false;\n");
        fprintf(out, "        if (success) mem_write32(ctx, ea, ctx->gpr[%u]);\n", inst->rS);
        fprintf(out, "        ctx->cr = (ctx->cr & 0x0FFFFFFFu) | ((success ? 2u : 0u) << 28) | ((ctx->xer >> 3) & 0x10000000u);\n");
        fprintf(out, "    }\n");
        break;

    case PPC_OP_STFIWX:
        fprintf(out, "    {\n        u32 ea = ");
        emit_xform_ea(out, inst->rA, inst->rB, false);
        fprintf(out, ";\n        mem_write32(ctx, ea, (u32)dolrecomp_f64_to_bits(ctx->fpr[%u]));\n    }\n", inst->rS);
        break;

    case PPC_OP_DCBZ:
        emit_dcbz(out, inst);
        break;

    case PPC_OP_DCBZ_L:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 ea = ");
        emit_xform_ea(out, inst->rA, inst->rB, false);
        fprintf(out, ";\n");
        fprintf(out, "        ppc_dcbz_l(ctx, ea, 0x%08Xu);\n", inst->address);
        fprintf(out, "        if (ctx->exception) return;\n");
        fprintf(out, "    }\n");
        break;

    case PPC_OP_DCBST:
    case PPC_OP_DCBF:
    case PPC_OP_DCBTST:
    case PPC_OP_DCBT:
    case PPC_OP_DCBI:
    case PPC_OP_ICBI:
        fprintf(out, "    (void)ctx;\n");
        break;

    case PPC_OP_LMW:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 ea = ");
        emit_dform_ea(out, inst->rA, inst->simm, false);
        fprintf(out, ";\n");
        fprintf(out, "        for (u32 r = %u; r < 32; r++, ea += 4) ctx->gpr[r] = mem_read32(ctx, ea);\n",
                inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_STMW:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 ea = ");
        emit_dform_ea(out, inst->rA, inst->simm, false);
        fprintf(out, ";\n");
        fprintf(out, "        for (u32 r = %u; r < 32; r++, ea += 4) mem_write32(ctx, ea, ctx->gpr[r]);\n",
                inst->rS);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_B:
        fprintf(out, "    {\n");
        emit_direct_branch(out, inst,
                           branch_target_is_local(func_start, func_end, inst->branch_target));
        fprintf(out, "    }\n");
        break;

    case PPC_OP_BC:
        fprintf(out, "    {\n");
        emit_branch_condition(out, inst->bo, inst->bi);
        fprintf(out, "        if (ctr_ok && cr_ok) {\n");
        emit_direct_branch(out, inst,
                           branch_target_is_local(func_start, func_end, inst->branch_target));
        fprintf(out, "        }\n");
        fprintf(out, "    }\n");
        break;

    case PPC_OP_BCLR:
        emit_dynamic_branch(out, inst, "ctx->lr & ~3u");
        break;

    case PPC_OP_BCCTR:
        emit_dynamic_branch(out, inst, "ctx->ctr & ~3u");
        break;

    case PPC_OP_TWI:
        fprintf(out, "    if (ppc_trap_condition(%uu, ctx->gpr[%u], (u32)(s32)%d)) {\n",
                inst->to, inst->rA, (int)inst->simm);
        fprintf(out, "        ppc_program_exception(ctx, PPC_PROGRAM_TRAP, 0x%08Xu);\n", inst->address);
        fprintf(out, "        return;\n");
        fprintf(out, "    }\n");
        break;

    case PPC_OP_TW:
        fprintf(out, "    if (ppc_trap_condition(%uu, ctx->gpr[%u], ctx->gpr[%u])) {\n",
                inst->to, inst->rA, inst->rB);
        fprintf(out, "        ppc_program_exception(ctx, PPC_PROGRAM_TRAP, 0x%08Xu);\n", inst->address);
        fprintf(out, "        return;\n");
        fprintf(out, "    }\n");
        break;

    case PPC_OP_SC:
        fprintf(out, "    ppc_system_call_exception(ctx, 0x%08Xu);\n", inst->address);
        fprintf(out, "    return;\n");
        break;

    case PPC_OP_RFI:
        fprintf(out, "    ppc_rfi(ctx, 0x%08Xu);\n", inst->address);
        fprintf(out, "    return;\n");
        break;

    case PPC_OP_CRAND:  emit_cr_logical(out, inst, "a & b"); break;
    case PPC_OP_CRANDC: emit_cr_logical(out, inst, "a & ~b"); break;
    case PPC_OP_CREQV:  emit_cr_logical(out, inst, "~(a ^ b)"); break;
    case PPC_OP_CRNAND: emit_cr_logical(out, inst, "~(a & b)"); break;
    case PPC_OP_CRNOR:  emit_cr_logical(out, inst, "~(a | b)"); break;
    case PPC_OP_CROR:   emit_cr_logical(out, inst, "a | b"); break;
    case PPC_OP_CRORC:  emit_cr_logical(out, inst, "a | ~b"); break;
    case PPC_OP_CRXOR:  emit_cr_logical(out, inst, "a ^ b"); break;

    case PPC_OP_MCRF: {
        u32 dst_shift = cr_field_shift(inst->crfD);
        u32 src_shift = cr_field_shift(inst->crfS);
        fprintf(out, "    {\n");
        fprintf(out, "        u32 bits = (ctx->cr >> %u) & 0xFu;\n", src_shift);
        fprintf(out, "        ctx->cr = (ctx->cr & ~(0xFu << %u)) | (bits << %u);\n",
                dst_shift, dst_shift);
        fprintf(out, "    }\n");
        break;
    }

    case PPC_OP_MCRXR: {
        u32 dst_shift = cr_field_shift(inst->crfD);
        fprintf(out, "    {\n");
        fprintf(out, "        u32 bits = (ctx->xer >> 28) & 0xFu;\n");
        fprintf(out, "        ctx->cr = (ctx->cr & ~(0xFu << %u)) | (bits << %u);\n",
                dst_shift, dst_shift);
        fprintf(out, "        ctx->xer &= ~0xE0000000u;\n");
        fprintf(out, "    }\n");
        break;
    }

    case PPC_OP_MFCR:
        fprintf(out, "    ctx->gpr[%u] = ctx->cr;\n", inst->rD);
        break;

    case PPC_OP_MTCRF: {
        u32 mask = 0;
        for (u32 crf = 0; crf < 8; crf++) {
            if (inst->crm & (0x80u >> crf))
                mask |= 0xFu << cr_field_shift((u8)crf);
        }
        if (mask) {
            fprintf(out, "    ctx->cr = (ctx->cr & ~0x%08Xu) | (ctx->gpr[%u] & 0x%08Xu);\n",
                    mask, inst->rS, mask);
        } else {
            fprintf(out, "    // mtcrf mask selects no CR fields\n");
        }
        break;
    }

    case PPC_OP_MFMSR:
        fprintf(out, "    ctx->gpr[%u] = ctx->msr;\n", inst->rD);
        break;

    case PPC_OP_MTMSR:
        fprintf(out, "    ctx->msr = ctx->gpr[%u];\n", inst->rS);
        break;

    case PPC_OP_MFSR:
        fprintf(out, "    ctx->gpr[%u] = ctx->sr[%u];\n", inst->rD, inst->sr);
        break;

    case PPC_OP_MFSRIN:
        fprintf(out, "    ctx->gpr[%u] = ctx->sr[(ctx->gpr[%u] >> 28) & 0xFu];\n",
                inst->rD, inst->rB);
        break;

    case PPC_OP_MTSR:
        fprintf(out, "    ctx->sr[%u] = ctx->gpr[%u];\n", inst->sr, inst->rS);
        break;

    case PPC_OP_MTSRIN:
        fprintf(out, "    ctx->sr[(ctx->gpr[%u] >> 28) & 0xFu] = ctx->gpr[%u];\n",
                inst->rB, inst->rS);
        break;

    case PPC_OP_MFTB:
        fprintf(out, "    ctx->gpr[%u] = ppc_mftb(ctx, %uu, 0x%08Xu);\n",
                inst->rD, inst->spr, inst->address);
        fprintf(out, "    if (ctx->exception) return;\n");
        break;

    case PPC_OP_MFSPR:
        fprintf(out, "    ctx->gpr[%u] = ppc_mfspr(ctx, %uu, 0x%08Xu);\n",
                inst->rD, inst->spr, inst->address);
        fprintf(out, "    if (ctx->exception) return;\n");
        break;

    case PPC_OP_MTSPR:
        fprintf(out, "    ppc_mtspr(ctx, %uu, ctx->gpr[%u], 0x%08Xu);\n",
                inst->spr, inst->rS, inst->address);
        fprintf(out, "    if (ctx->exception) return;\n");
        break;

    case PPC_OP_TLBIE:
        fprintf(out, "    ppc_tlbie(ctx, ctx->gpr[%u], 0x%08Xu);\n", inst->rB, inst->address);
        fprintf(out, "    if (ctx->exception) return;\n");
        break;

    case PPC_OP_SYNC:
    case PPC_OP_EIEIO:
    case PPC_OP_ISYNC:
    case PPC_OP_TLBSYNC:
        fprintf(out, "    ppc_memory_fence();\n");
        break;

    case PPC_OP_ECIWX:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 ea = ");
        emit_xform_ea(out, inst->rA, inst->rB, false);
        fprintf(out, ";\n");
        fprintf(out, "        u32 value = ppc_eciwx(ctx, ea, 0x%08Xu);\n", inst->address);
        fprintf(out, "        if (ctx->exception) return;\n");
        fprintf(out, "        ctx->gpr[%u] = value;\n", inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_ECOWX:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 ea = ");
        emit_xform_ea(out, inst->rA, inst->rB, false);
        fprintf(out, ";\n");
        fprintf(out, "        ppc_ecowx(ctx, ea, ctx->gpr[%u], 0x%08Xu);\n",
                inst->rS, inst->address);
        fprintf(out, "        if (ctx->exception) return;\n");
        fprintf(out, "    }\n");
        break;

    default:
        fprintf(out, "    ppc_fallback_instruction(ctx, 0x%08Xu, 0x%08Xu);\n",
                inst->raw, inst->address);
        fprintf(out, "    return;\n");
        break;
    }

    fprintf(out, "\n");
}

void emit_instruction(FILE* out, const PPCInst* inst) {
    emit_instruction_with_range(out, inst, 0, (u32)-1);
}

void emit_function(FILE* out, const PPCInst* insts, u32 count, u32 func_addr) {
    u32 i;
    u32 func_end = func_addr + count * 4u;

    fprintf(out, "void func_%08X(CPUState* ctx) {\n", func_addr);
    fprintf(out, "    switch (ctx->pc) {\n");
    for (i = 0; i < count; i++) {
        fprintf(out, "    case 0x%08Xu: goto label_%08X;\n",
                insts[i].address, insts[i].address);
    }
    fprintf(out, "    default: return;\n");
    fprintf(out, "    }\n");

    for (i = 0; i < count; i++) {
        fprintf(out, "label_%08X:\n", insts[i].address);
        fprintf(out, "    ctx->pc = 0x%08Xu;\n", insts[i].address);
        emit_instruction_with_range(out, &insts[i], func_addr, func_end);
    }

    fprintf(out, "    ctx->pc = 0x%08Xu;\n", func_end);
    fprintf(out, "}\n\n");
}
