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
        fprintf(out, "        ctx->fpr[%u] = (f64)dolrecomp_f32_from_bits(mem_read32(ctx, ea));\n",
                inst->rD);
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
        fprintf(out, "        ctx->fpr[%u] = (f64)dolrecomp_f32_from_bits(mem_read32(ctx, ea));\n",
                inst->rD);
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

static void emit_psq_quant_note(FILE* out, const PPCInst* inst) {
    if (inst->i != 0) {
        fprintf(out, "        // TODO: psq quantization via GQR%u; treating memory as f32 for now\n",
                inst->i);
    }
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
    emit_psq_quant_note(out, inst);
    fprintf(out, "        ctx->fpr[%u] = (f64)dolrecomp_f32_from_bits(mem_read32(ctx, ea));\n",
            inst->rD);
    if (inst->w) {
        fprintf(out, "        ctx->ps1[%u] = 1.0;\n", inst->rD);
    } else {
        fprintf(out, "        ctx->ps1[%u] = (f64)dolrecomp_f32_from_bits(mem_read32(ctx, ea + 4));\n",
                inst->rD);
    }
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
    emit_psq_quant_note(out, inst);
    fprintf(out, "        mem_write32(ctx, ea, dolrecomp_f32_to_bits((f32)ctx->fpr[%u]));\n",
            inst->rS);
    if (!inst->w) {
        fprintf(out, "        mem_write32(ctx, ea + 4, dolrecomp_f32_to_bits((f32)ctx->ps1[%u]));\n",
                inst->rS);
    }
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
    if (inst->lk) {
        fprintf(out, "            ctx->lr = 0x%08Xu;\n", inst->address + 4);
    }
    if (local_target) {
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

void emit_header(FILE* out) {
    fprintf(out,
        "// DolRecomp output\n"
        "\n"
        "#include <string.h>\n"
        "#include \"core/cpu.h\"\n"
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
    );
}

void emit_footer(FILE* out) {
    fprintf(out, "\n// end\n");
}

static void emit_instruction_with_range(FILE* out, const PPCInst* inst,
                                        u32 func_start, u32 func_end) {
    char disasm[64];
    ppc_disasm(disasm, sizeof(disasm), inst);
    fprintf(out, "    // %08X: %s\n", inst->address, disasm);

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
        fprintf(out, "    {\n");
        fprintf(out, "        ctx->gpr[%u] = ctx->gpr[%u] + ctx->gpr[%u];\n",
                inst->rD, inst->rA, inst->rB);
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_ADDC:
        fprintf(out, "    {\n");
        fprintf(out, "        u64 res = (u64)ctx->gpr[%u] + (u64)ctx->gpr[%u];\n",
                inst->rA, inst->rB);
        fprintf(out, "        ctx->gpr[%u] = (u32)res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(res >> 32) & 1u) << 29);\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_ADDE:
        fprintf(out, "    {\n");
        fprintf(out, "        u64 res = (u64)ctx->gpr[%u] + (u64)ctx->gpr[%u] + ((ctx->xer >> 29) & 1u);\n",
                inst->rA, inst->rB);
        fprintf(out, "        ctx->gpr[%u] = (u32)res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(res >> 32) & 1u) << 29);\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_ADDZE:
        fprintf(out, "    {\n");
        fprintf(out, "        u64 res = (u64)ctx->gpr[%u] + ((ctx->xer >> 29) & 1u);\n",
                inst->rA);
        fprintf(out, "        ctx->gpr[%u] = (u32)res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(res >> 32) & 1u) << 29);\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_SUBF:
        fprintf(out, "    {\n");
        fprintf(out, "        ctx->gpr[%u] = ctx->gpr[%u] - ctx->gpr[%u];\n",
                inst->rD, inst->rB, inst->rA);
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_SUBFC:
        fprintf(out, "    {\n");
        fprintf(out, "        u64 res = (u64)ctx->gpr[%u] + (u64)(~ctx->gpr[%u]) + 1u;\n",
                inst->rB, inst->rA);
        fprintf(out, "        ctx->gpr[%u] = (u32)res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(res >> 32) & 1u) << 29);\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_SUBFE:
        fprintf(out, "    {\n");
        fprintf(out, "        u64 res = (u64)ctx->gpr[%u] + (u64)(~ctx->gpr[%u]) + ((ctx->xer >> 29) & 1u);\n",
                inst->rB, inst->rA);
        fprintf(out, "        ctx->gpr[%u] = (u32)res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(res >> 32) & 1u) << 29);\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_SUBFZE:
        fprintf(out, "    {\n");
        fprintf(out, "        u64 res = (u64)(~ctx->gpr[%u]) + ((ctx->xer >> 29) & 1u);\n",
                inst->rA);
        fprintf(out, "        ctx->gpr[%u] = (u32)res;\n", inst->rD);
        fprintf(out, "        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(res >> 32) & 1u) << 29);\n");
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_NEG:
        fprintf(out, "    {\n");
        fprintf(out, "        ctx->gpr[%u] = 0u - ctx->gpr[%u];\n",
                inst->rD, inst->rA);
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_MULLW:
        fprintf(out, "    {\n");
        fprintf(out, "        ctx->gpr[%u] = (u32)((s64)(s32)ctx->gpr[%u] * (s64)(s32)ctx->gpr[%u]);\n",
                inst->rD, inst->rA, inst->rB);
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
        fprintf(out, "    {\n");
        fprintf(out, "        s32 dividend = (s32)ctx->gpr[%u];\n", inst->rA);
        fprintf(out, "        s32 divisor = (s32)ctx->gpr[%u];\n", inst->rB);
        fprintf(out, "        ctx->gpr[%u] = (divisor == 0 || (dividend == (s32)0x80000000 && divisor == -1)) ? 0u : (u32)(dividend / divisor);\n",
                inst->rD);
        emit_record_if_needed(out, inst, inst->rD);
        fprintf(out, "    }\n");
        break;

    case PPC_OP_DIVWU:
        fprintf(out, "    {\n");
        fprintf(out, "        u32 divisor = ctx->gpr[%u];\n", inst->rB);
        fprintf(out, "        ctx->gpr[%u] = divisor == 0 ? 0u : ctx->gpr[%u] / divisor;\n",
                inst->rD, inst->rA);
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
        fprintf(out, "    ctx->fpr[%u] = (f64)((f32)ctx->fpr[%u] + (f32)ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rB);
        break;

    case PPC_OP_FSUBS:
        fprintf(out, "    ctx->fpr[%u] = (f64)((f32)ctx->fpr[%u] - (f32)ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rB);
        break;

    case PPC_OP_FMULS:
        fprintf(out, "    ctx->fpr[%u] = (f64)((f32)ctx->fpr[%u] * (f32)ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rC);
        break;

    case PPC_OP_FDIVS:
        fprintf(out, "    ctx->fpr[%u] = (f64)((f32)ctx->fpr[%u] / (f32)ctx->fpr[%u]);\n",
                inst->rD, inst->rA, inst->rB);
        break;

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

    case PPC_OP_DCBZ:
        emit_dcbz(out, inst);
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

    case PPC_OP_MFSPR:
        switch (inst->spr) {
        case 1: fprintf(out, "    ctx->gpr[%u] = ctx->xer;\n", inst->rD); break;
        case 8: fprintf(out, "    ctx->gpr[%u] = ctx->lr;\n", inst->rD); break;
        case 9: fprintf(out, "    ctx->gpr[%u] = ctx->ctr;\n", inst->rD); break;
        default:
            fprintf(out, "    // TODO: mfspr %u\n", inst->spr);
            fprintf(out, "    ctx->gpr[%u] = 0;\n", inst->rD);
            break;
        }
        break;

    case PPC_OP_MTSPR:
        switch (inst->spr) {
        case 1: fprintf(out, "    ctx->xer = ctx->gpr[%u];\n", inst->rS); break;
        case 8: fprintf(out, "    ctx->lr = ctx->gpr[%u];\n", inst->rS); break;
        case 9: fprintf(out, "    ctx->ctr = ctx->gpr[%u];\n", inst->rS); break;
        default:
            fprintf(out, "    // TODO: mtspr %u\n", inst->spr);
            break;
        }
        break;

    default:
        fprintf(out, "    // TODO: unimplemented (raw: 0x%08X)\n", inst->raw);
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

    for (i = 0; i < count; i++) {
        fprintf(out, "label_%08X:\n", insts[i].address);
        emit_instruction_with_range(out, &insts[i], func_addr, func_end);
    }

    fprintf(out, "}\n\n");
}
