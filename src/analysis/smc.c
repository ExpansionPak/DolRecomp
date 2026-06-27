#include "analysis/smc.h"
#include "analysis/embedded_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void smc_analysis_free(SMCAnalysis* smc) {
    free(smc->ranges);
    smc->ranges = NULL;
    smc->range_count = 0;
    smc->range_capacity = 0;
}

void smc_note(SMCAnalysis* smc, u32 addr) {
    smc->possible = 1;

    if (smc->range_count > 0) {
        SMCRange* last = &smc->ranges[smc->range_count - 1u];
        if (addr >= last->start && addr <= last->end + 4u) {
            if (addr > last->end)
                last->end = addr;
            return;
        }
    }

    if (smc->range_count >= smc->range_capacity) {
        u32 new_capacity = smc->range_capacity ? smc->range_capacity * 2u : 16u;
        SMCRange* new_ranges =
            (SMCRange*)realloc(smc->ranges, new_capacity * sizeof(SMCRange));
        if (!new_ranges) {
            smc->allocation_failed = 1;
            return;
        }
        smc->ranges = new_ranges;
        smc->range_capacity = new_capacity;
    }

    smc->ranges[smc->range_count].start = addr;
    smc->ranges[smc->range_count].end = addr;
    smc->range_count++;
}

int write_smc_report(const SMCAnalysis* smc, const char* path) {
    FILE* report = fopen(path, "w");
    if (!report) {
        fprintf(stderr, "error: can't open output '%s'\n", path);
        return 0;
    }

    fprintf(report, "possible patching instructions:\n");
    for (u32 i = 0; i < smc->range_count; i++) {
        fprintf(report, "0x%08X-0x%08X\n",
                smc->ranges[i].start, smc->ranges[i].end);
    }

    if (fclose(report) != 0) {
        fprintf(stderr, "error: failed writing '%s'\n", path);
        return 0;
    }

    return 1;
}

int code_range_overlaps(const LoadedCodeSection* sections,
                               u32 section_count, u32 address, u32 size) {
    u64 begin = address;
    u64 end = begin + (size ? size : 1u);

    for (u32 i = 0; i < section_count; i++) {
        const LoadedCodeSection* section = &sections[i];
        if (section->embedded_data_mode != EMBEDDED_DATA_DOL)
            continue;
        if (section->size == 0)
            continue;

        u64 section_begin = section->address;
        u64 section_end = section_begin + section->size;
        if (begin < section_end && end > section_begin)
            return 1;
    }

    return 0;
}

int known_dform_ea(const KnownReg regs[32], const PPCInst* inst, u32* ea) {
    if (inst->rA == 0) {
        *ea = (u32)(s32)inst->simm;
        return 1;
    }
    if (!regs[inst->rA].known)
        return 0;
    *ea = regs[inst->rA].value + (u32)(s32)inst->simm;
    return 1;
}

int known_indexed_ea(const KnownReg regs[32], const PPCInst* inst, u32* ea) {
    u32 base = 0;
    if (inst->rA != 0) {
        if (!regs[inst->rA].known)
            return 0;
        base = regs[inst->rA].value;
    }
    if (!regs[inst->rB].known)
        return 0;

    *ea = base + regs[inst->rB].value;
    return 1;
}

int store_size_for_inst(const PPCInst* inst, u32* size) {
    switch (inst->op) {
    case PPC_OP_STB:
    case PPC_OP_STBU:
    case PPC_OP_STBX:
    case PPC_OP_STBUX:
        *size = 1;
        return 1;

    case PPC_OP_STH:
    case PPC_OP_STHU:
    case PPC_OP_STHX:
    case PPC_OP_STHUX:
    case PPC_OP_STHBRX:
        *size = 2;
        return 1;

    case PPC_OP_STW:
    case PPC_OP_STWU:
    case PPC_OP_STWX:
    case PPC_OP_STWUX:
    case PPC_OP_STWBRX:
    case PPC_OP_STWCX:
    case PPC_OP_STFIWX:
    case PPC_OP_STFS:
    case PPC_OP_STFSU:
    case PPC_OP_STFSX:
    case PPC_OP_STFSUX:
    case PPC_OP_PSQ_ST:
    case PPC_OP_PSQ_STU:
    case PPC_OP_PSQ_STX:
    case PPC_OP_PSQ_STUX:
        *size = 4;
        return 1;

    case PPC_OP_STFD:
    case PPC_OP_STFDU:
    case PPC_OP_STFDX:
    case PPC_OP_STFDUX:
        *size = 8;
        return 1;

    case PPC_OP_STMW:
        *size = (32u - inst->rS) * 4u;
        return 1;

    case PPC_OP_STSWI:
        *size = inst->nb ? inst->nb : 32u;
        return 1;

    case PPC_OP_STSWX:
        *size = 1;
        return 1;

    case PPC_OP_DCBZ:
    case PPC_OP_DCBZ_L:
        *size = 32;
        return 1;

    default:
        return 0;
    }
}

int inst_has_dform_ea(PPCOpcode op) {
    switch (op) {
    case PPC_OP_STW:
    case PPC_OP_STWU:
    case PPC_OP_STB:
    case PPC_OP_STBU:
    case PPC_OP_STH:
    case PPC_OP_STHU:
    case PPC_OP_STMW:
    case PPC_OP_STSWI:
    case PPC_OP_STFS:
    case PPC_OP_STFSU:
    case PPC_OP_STFD:
    case PPC_OP_STFDU:
    case PPC_OP_PSQ_ST:
    case PPC_OP_PSQ_STU:
        return 1;
    default:
        return 0;
    }
}

int inst_has_indexed_ea(PPCOpcode op) {
    switch (op) {
    case PPC_OP_STWX:
    case PPC_OP_STWUX:
    case PPC_OP_STBX:
    case PPC_OP_STBUX:
    case PPC_OP_STHX:
    case PPC_OP_STHUX:
    case PPC_OP_STWBRX:
    case PPC_OP_STHBRX:
    case PPC_OP_STSWX:
    case PPC_OP_STWCX:
    case PPC_OP_STFIWX:
    case PPC_OP_STFSX:
    case PPC_OP_STFSUX:
    case PPC_OP_STFDX:
    case PPC_OP_STFDUX:
    case PPC_OP_PSQ_STX:
    case PPC_OP_PSQ_STUX:
    case PPC_OP_DCBZ:
    case PPC_OP_DCBZ_L:
        return 1;
    default:
        return 0;
    }
}

int smc_inst_targets_code(const LoadedCodeSection* sections,
                                 u32 section_count, const PPCInst* inst,
                                 const KnownReg regs[32]) {
    u32 ea = 0;
    u32 size = 0;

    if (!store_size_for_inst(inst, &size))
        return 0;

    if (inst_has_dform_ea(inst->op)) {
        if (!known_dform_ea(regs, inst, &ea))
            return 0;
    } else if (inst_has_indexed_ea(inst->op)) {
        if (!known_indexed_ea(regs, inst, &ea))
            return 0;
    } else {
        return 0;
    }

    return code_range_overlaps(sections, section_count, ea, size);
}

void clear_reg(KnownReg regs[32], u8 reg) {
    regs[reg].known = 0;
    regs[reg].value = 0;
}

void set_reg(KnownReg regs[32], u8 reg, u32 value) {
    regs[reg].known = 1;
    regs[reg].value = value;
}

void update_known_regs(KnownReg regs[32], const PPCInst* inst) {
    u32 ea = 0;

    switch (inst->op) {
    case PPC_OP_ADDI:
        if (inst->rA == 0) {
            set_reg(regs, inst->rD, (u32)(s32)inst->simm);
        } else if (regs[inst->rA].known) {
            set_reg(regs, inst->rD, regs[inst->rA].value + (u32)(s32)inst->simm);
        } else {
            clear_reg(regs, inst->rD);
        }
        break;

    case PPC_OP_ADDIS:
        if (inst->rA == 0) {
            set_reg(regs, inst->rD, ((u32)inst->simm) << 16);
        } else if (regs[inst->rA].known) {
            set_reg(regs, inst->rD, regs[inst->rA].value + (((u32)inst->simm) << 16));
        } else {
            clear_reg(regs, inst->rD);
        }
        break;

    case PPC_OP_ORI:
        if (regs[inst->rS].known)
            set_reg(regs, inst->rA, regs[inst->rS].value | inst->uimm);
        else
            clear_reg(regs, inst->rA);
        break;

    case PPC_OP_ORIS:
        if (regs[inst->rS].known)
            set_reg(regs, inst->rA, regs[inst->rS].value | ((u32)inst->uimm << 16));
        else
            clear_reg(regs, inst->rA);
        break;

    case PPC_OP_XORI:
        if (regs[inst->rS].known)
            set_reg(regs, inst->rA, regs[inst->rS].value ^ inst->uimm);
        else
            clear_reg(regs, inst->rA);
        break;

    case PPC_OP_XORIS:
        if (regs[inst->rS].known)
            set_reg(regs, inst->rA, regs[inst->rS].value ^ ((u32)inst->uimm << 16));
        else
            clear_reg(regs, inst->rA);
        break;

    case PPC_OP_LWZ:
    case PPC_OP_LBZ:
    case PPC_OP_LHZ:
    case PPC_OP_LHA:
    case PPC_OP_LWZX:
    case PPC_OP_LBZX:
    case PPC_OP_LHZX:
    case PPC_OP_LHAX:
    case PPC_OP_LWBRX:
    case PPC_OP_LHBRX:
    case PPC_OP_LWARX:
        clear_reg(regs, inst->rD);
        break;

    case PPC_OP_LWZU:
    case PPC_OP_LBZU:
    case PPC_OP_LHZU:
    case PPC_OP_LHAU:
        if (known_dform_ea(regs, inst, &ea))
            set_reg(regs, inst->rA, ea);
        else
            clear_reg(regs, inst->rA);
        clear_reg(regs, inst->rD);
        break;

    case PPC_OP_LWZUX:
    case PPC_OP_LBZUX:
    case PPC_OP_LHZUX:
    case PPC_OP_LHAUX:
        if (known_indexed_ea(regs, inst, &ea))
            set_reg(regs, inst->rA, ea);
        else
            clear_reg(regs, inst->rA);
        clear_reg(regs, inst->rD);
        break;

    case PPC_OP_STWU:
    case PPC_OP_STBU:
    case PPC_OP_STHU:
        if (known_dform_ea(regs, inst, &ea))
            set_reg(regs, inst->rA, ea);
        else
            clear_reg(regs, inst->rA);
        break;

    case PPC_OP_STWUX:
    case PPC_OP_STBUX:
    case PPC_OP_STHUX:
    case PPC_OP_STFSUX:
    case PPC_OP_STFDUX:
    case PPC_OP_PSQ_STUX:
        if (known_indexed_ea(regs, inst, &ea))
            set_reg(regs, inst->rA, ea);
        else
            clear_reg(regs, inst->rA);
        break;

    case PPC_OP_MFCR:
    case PPC_OP_MFSPR:
    case PPC_OP_MFTB:
    case PPC_OP_MFMSR:
    case PPC_OP_MFSR:
    case PPC_OP_MFSRIN:
    case PPC_OP_MULLI:
    case PPC_OP_SUBFIC:
    case PPC_OP_ADD:
    case PPC_OP_ADDO:
    case PPC_OP_ADDC:
    case PPC_OP_ADDCO:
    case PPC_OP_ADDE:
    case PPC_OP_ADDEO:
    case PPC_OP_ADDME:
    case PPC_OP_ADDMEO:
    case PPC_OP_ADDZE:
    case PPC_OP_ADDZEO:
    case PPC_OP_SUBF:
    case PPC_OP_SUBFO:
    case PPC_OP_SUBFC:
    case PPC_OP_SUBFCO:
    case PPC_OP_SUBFE:
    case PPC_OP_SUBFEO:
    case PPC_OP_SUBFME:
    case PPC_OP_SUBFMEO:
    case PPC_OP_SUBFZE:
    case PPC_OP_SUBFZEO:
    case PPC_OP_NEG:
    case PPC_OP_NEGO:
    case PPC_OP_MULLW:
    case PPC_OP_MULLWO:
    case PPC_OP_MULHW:
    case PPC_OP_MULHWU:
    case PPC_OP_DIVW:
    case PPC_OP_DIVWO:
    case PPC_OP_DIVWU:
    case PPC_OP_DIVWUO:
        clear_reg(regs, inst->rD);
        break;

    case PPC_OP_AND:
    case PPC_OP_ANDC:
    case PPC_OP_OR:
    case PPC_OP_ORC:
    case PPC_OP_XOR:
    case PPC_OP_NAND:
    case PPC_OP_NOR:
    case PPC_OP_EQV:
    case PPC_OP_CNTLZW:
    case PPC_OP_EXTSB:
    case PPC_OP_EXTSH:
    case PPC_OP_SLW:
    case PPC_OP_SRW:
    case PPC_OP_SRAW:
    case PPC_OP_SRAWI:
    case PPC_OP_RLWINM:
    case PPC_OP_RLWNM:
    case PPC_OP_RLWIMI:
        clear_reg(regs, inst->rA);
        break;

    case PPC_OP_LMW:
        for (u32 r = inst->rD; r < 32; r++)
            clear_reg(regs, (u8)r);
        break;

    default:
        break;
    }
}

void analyze_smc_section(const LoadedCodeSection* sections,
                                u32 section_count, const PPCInst* insts,
                                u32 count, SMCAnalysis* smc) {
    KnownReg regs[32];
    memset(regs, 0, sizeof(regs));

    for (u32 i = 0; i < count; i++) {
        const PPCInst* inst = &insts[i];
        if (inst->embedded_data || inst->op == PPC_OP_UNKNOWN)
            continue;

        if (inst->op == PPC_OP_ICBI ||
            smc_inst_targets_code(sections, section_count, inst, regs)) {
            smc_note(smc, inst->address);
        }

        update_known_regs(regs, inst);
    }
}

