#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <stdatomic.h>
#endif

bool cpu_init(CPUState* cpu) {
    memset(cpu, 0, sizeof(*cpu));

    cpu->ram_size = GC_MAIN_RAM_SIZE;
    cpu->ram = (u8*)calloc(1, cpu->ram_size);
    if (!cpu->ram) {
        fprintf(stderr, "error: failed to allocate %u bytes for RAM\n", cpu->ram_size);
        return false;
    }

    return true;
}

void cpu_free(CPUState* cpu) {
    if (cpu->ram) {
        free(cpu->ram);
        cpu->ram = NULL;
    }
}

void cpu_reset(CPUState* cpu) {
    u8* ram = cpu->ram;
    u32 ram_size = cpu->ram_size;
    PPCExternalRead external_read = cpu->external_read;
    PPCExternalWrite external_write = cpu->external_write;
    PPCExternalRead32 external_read32 = cpu->external_read32;
    PPCExternalWrite32 external_write32 = cpu->external_write32;
    PPCInstructionFallback instruction_fallback = cpu->instruction_fallback;
    PPCHostCall host_call = cpu->host_call;
    void* external_user_data = cpu->external_user_data;

    memset(cpu, 0, sizeof(*cpu));
    cpu->ram = ram;
    cpu->ram_size = ram_size;
    cpu->external_read = external_read;
    cpu->external_write = external_write;
    cpu->external_read32 = external_read32;
    cpu->external_write32 = external_write32;
    cpu->instruction_fallback = instruction_fallback;
    cpu->host_call = host_call;
    cpu->external_user_data = external_user_data;

    if (cpu->ram)
        memset(cpu->ram, 0, cpu->ram_size);
}

static u32 translate_addr(u32 addr, u32 ram_size) {
    if (addr >= GC_RAM_BASE && addr < GC_RAM_BASE + ram_size)
        return addr - GC_RAM_BASE;

    if (addr >= GC_RAM_UNCACHED && addr < GC_RAM_UNCACHED + ram_size)
        return addr - GC_RAM_UNCACHED;

    return (u32)-1;
}

static void clear_matching_reservation(CPUState* cpu, u32 addr) {
    if (cpu->reserve_valid && ((cpu->reserve_addr ^ addr) & ~31u) == 0)
        cpu->reserve_valid = false;
}

#define PPC_BIT(n) (1u << (31u - (n)))
#define PPC_MSR_RFI_MASK 0x87C0FFFFu
#define PPC_MSR_POW PPC_BIT(13)
#define PPC_MSR_ILE PPC_BIT(15)
#define PPC_MSR_EE  PPC_BIT(16)
#define PPC_MSR_PR  PPC_BIT(17)
#define PPC_MSR_FP  PPC_BIT(18)
#define PPC_MSR_ME  PPC_BIT(19)
#define PPC_MSR_FE0 PPC_BIT(20)
#define PPC_MSR_SE  PPC_BIT(21)
#define PPC_MSR_BE  PPC_BIT(22)
#define PPC_MSR_FE1 PPC_BIT(23)
#define PPC_MSR_IP  PPC_BIT(25)
#define PPC_MSR_IR  PPC_BIT(26)
#define PPC_MSR_DR  PPC_BIT(27)
#define PPC_MSR_PM  PPC_BIT(29)
#define PPC_MSR_RI  PPC_BIT(30)
#define PPC_MSR_LE  PPC_BIT(31)

#define PPC_EAR_ENABLE 0x80000000u
#define PPC_SRR1_MACHINE_CHECK_DCBZL PPC_BIT(10)

static u32 exception_vector_address(u32 msr, u32 vector) {
    return ((msr & PPC_MSR_IP) ? 0xFFF00000u : 0u) + vector;
}

static u32 exception_msr(u32 old_msr, u32 exception) {
    u32 clear = PPC_MSR_POW | PPC_MSR_EE | PPC_MSR_PR | PPC_MSR_FP |
                PPC_MSR_FE0 | PPC_MSR_SE | PPC_MSR_BE | PPC_MSR_FE1 |
                PPC_MSR_IR | PPC_MSR_DR | PPC_MSR_PM | PPC_MSR_RI |
                PPC_MSR_LE;
    if (exception & PPC_EXC_MACHINE_CHECK)
        clear |= PPC_MSR_ME;

    u32 next = old_msr & ~clear;
    if (old_msr & PPC_MSR_ILE)
        next |= PPC_MSR_LE;
    return next;
}

u64 mem_read64(CPUState* cpu, u32 addr) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1 || offset + 8 > cpu->ram_size) {
        if (cpu->external_read)
            return cpu->external_read(cpu, addr, 8);
        fprintf(stderr, "warn: read64 from unmapped 0x%08X\n", addr);
        return 0;
    }
    return read_be64(cpu->ram + offset);
}

void mem_write64(CPUState* cpu, u32 addr, u64 value) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1 || offset + 8 > cpu->ram_size) {
        if (cpu->external_write) {
            cpu->external_write(cpu, addr, value, 8);
            return;
        }
        fprintf(stderr, "warn: write64 to unmapped 0x%08X\n", addr);
        return;
    }
    clear_matching_reservation(cpu, addr);
    write_be64(cpu->ram + offset, value);
}

u32 mem_read32(CPUState* cpu, u32 addr) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1 || offset + 4 > cpu->ram_size) {
        if (cpu->external_read)
            return (u32)cpu->external_read(cpu, addr, 4);
        fprintf(stderr, "warn: read32 from unmapped 0x%08X\n", addr);
        return 0;
    }
    return read_be32(cpu->ram + offset);
}

void mem_write32(CPUState* cpu, u32 addr, u32 value) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1 || offset + 4 > cpu->ram_size) {
        if (cpu->external_write) {
            cpu->external_write(cpu, addr, value, 4);
            return;
        }
        fprintf(stderr, "warn: write32 to unmapped 0x%08X\n", addr);
        return;
    }
    clear_matching_reservation(cpu, addr);
    write_be32(cpu->ram + offset, value);
}

u16 mem_read16(CPUState* cpu, u32 addr) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1 || offset + 2 > cpu->ram_size) {
        if (cpu->external_read)
            return (u16)cpu->external_read(cpu, addr, 2);
        fprintf(stderr, "warn: read16 from unmapped 0x%08X\n", addr);
        return 0;
    }
    return read_be16(cpu->ram + offset);
}

void mem_write16(CPUState* cpu, u32 addr, u16 value) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1 || offset + 2 > cpu->ram_size) {
        if (cpu->external_write) {
            cpu->external_write(cpu, addr, value, 2);
            return;
        }
        fprintf(stderr, "warn: write16 to unmapped 0x%08X\n", addr);
        return;
    }
    clear_matching_reservation(cpu, addr);
    write_be16(cpu->ram + offset, value);
}

u8 mem_read8(CPUState* cpu, u32 addr) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1) {
        if (cpu->external_read)
            return (u8)cpu->external_read(cpu, addr, 1);
        fprintf(stderr, "warn: read8 from unmapped 0x%08X\n", addr);
        return 0;
    }
    return cpu->ram[offset];
}

void mem_write8(CPUState* cpu, u32 addr, u8 value) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1) {
        if (cpu->external_write) {
            cpu->external_write(cpu, addr, value, 1);
            return;
        }
        fprintf(stderr, "warn: write8 to unmapped 0x%08X\n", addr);
        return;
    }
    clear_matching_reservation(cpu, addr);
    cpu->ram[offset] = value;
}

bool ppc_add_overflowed(u32 a, u32 b, u32 result) {
    return (((a ^ result) & (b ^ result)) >> 31) != 0;
}

void ppc_set_xer_ov(CPUState* cpu, bool ov) {
    cpu->xer = (cpu->xer & ~0x40000000u) | (ov ? 0x40000000u : 0u);
    if (ov)
        cpu->xer |= 0x80000000u;
}

void ppc_take_exception(CPUState* cpu, u32 exception, u32 vector, u32 srr0, u32 srr1_info) {
    u32 old_msr = cpu->msr;
    cpu->srr0 = srr0;
    cpu->srr1 = (old_msr & PPC_MSR_RFI_MASK) | srr1_info;
    cpu->exception |= exception;
    cpu->msr = exception_msr(old_msr, exception);
    cpu->pc = exception_vector_address(cpu->msr, vector);
}

void ppc_program_exception(CPUState* cpu, u32 cause, u32 cia) {
    cpu->program_exception |= cause;
    ppc_take_exception(cpu, PPC_EXC_PROGRAM, PPC_VECTOR_PROGRAM, cia, cause);
}

void ppc_fallback_instruction(CPUState* cpu, u32 raw, u32 cia) {
    if (cpu->instruction_fallback) {
        cpu->instruction_fallback(cpu, raw, cia);
        return;
    }

    (void)raw;
    ppc_program_exception(cpu, PPC_PROGRAM_ILLEGAL, cia);
}

bool ppc_host_call(CPUState* cpu, u32 address) {
    return cpu->host_call ? cpu->host_call(cpu, address) : false;
}

void ppc_system_call_exception(CPUState* cpu, u32 cia) {
    ppc_take_exception(cpu, PPC_EXC_SYSTEM_CALL, PPC_VECTOR_SYSTEM_CALL, cia + 4u, 0);
}

void ppc_dsi_exception(CPUState* cpu, u32 ea, u32 cia, u32 dsisr) {
    cpu->dar = ea;
    cpu->dsisr = dsisr;
    ppc_take_exception(cpu, PPC_EXC_DSI, PPC_VECTOR_DSI, cia, 0);
}

void ppc_alignment_exception(CPUState* cpu, u32 ea, u32 cia) {
    cpu->dar = ea;
    ppc_take_exception(cpu, PPC_EXC_ALIGNMENT, PPC_VECTOR_ALIGNMENT, cia, 0);
}

u32 ppc_mftb(CPUState* cpu, u16 tbr, u32 cia) {
    if (tbr == 268)
        return (u32)cpu->timebase;
    if (tbr == 269)
        return (u32)(cpu->timebase >> 32);

    ppc_program_exception(cpu, PPC_PROGRAM_ILLEGAL, cia);
    return 0;
}

static f32 f32_value(u32 bits) {
    f32 value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static u32 f32_bits(f32 value) {
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static bool f32_is_denormal(f32 value) {
    u32 bits = f32_bits(value);
    return (bits & 0x7F800000u) == 0 && (bits & 0x007FFFFFu) != 0;
}

static s32 gqr_scale(u32 value) {
    return sign_extend(value & 0x3Fu, 6);
}

static u32 psq_type_size(u8 type) {
    switch (type) {
    case 0: return 4;
    case 4:
    case 6: return 1;
    case 5:
    case 7: return 2;
    default: return 0;
    }
}

static bool psq_access_is_valid(CPUState* cpu, u8 type, u32 ea, u32 cia) {
    if (psq_type_size(type) == 0) {
        ppc_program_exception(cpu, PPC_PROGRAM_ILLEGAL, cia);
        return false;
    }

    if (type == 0 && (ea & 3u) != 0) {
        ppc_alignment_exception(cpu, ea, cia);
        return false;
    }

    return true;
}

static f64 psq_load_value(CPUState* cpu, u32 ea, u8 type, s32 scale) {
    switch (type) {
    case 0:
        return (f64)f32_value(mem_read32(cpu, ea));
    case 4:
        return (f64)(f32)ldexp((f64)mem_read8(cpu, ea), -scale);
    case 5:
        return (f64)(f32)ldexp((f64)mem_read16(cpu, ea), -scale);
    case 6:
        return (f64)(f32)ldexp((f64)(s8)mem_read8(cpu, ea), -scale);
    case 7:
        return (f64)(f32)ldexp((f64)(s16)mem_read16(cpu, ea), -scale);
    default:
        return 0.0;
    }
}

static s64 psq_quantize_int(f64 value, s64 min_value, s64 max_value, s32 scale) {
    if (isnan(value))
        return max_value;
    if (isinf(value))
        return value < 0.0 ? min_value : max_value;

    f64 scaled = trunc(ldexp(value, scale));
    if (scaled <= (f64)min_value)
        return min_value;
    if (scaled >= (f64)max_value)
        return max_value;
    return (s64)scaled;
}

static void psq_store_value(CPUState* cpu, u32 ea, u8 type, s32 scale, f64 value) {
    switch (type) {
    case 0: {
        f32 single = (f32)value;
        mem_write32(cpu, ea, f32_is_denormal(single) ? 0u : f32_bits(single));
        break;
    }
    case 4:
        mem_write8(cpu, ea, (u8)psq_quantize_int(value, 0, 255, scale));
        break;
    case 5:
        mem_write16(cpu, ea, (u16)psq_quantize_int(value, 0, 65535, scale));
        break;
    case 6:
        mem_write8(cpu, ea, (u8)(s8)psq_quantize_int(value, -128, 127, scale));
        break;
    case 7:
        mem_write16(cpu, ea, (u16)(s16)psq_quantize_int(value, -32768, 32767, scale));
        break;
    }
}

static bool psq_check_enabled(CPUState* cpu, bool indexed, u32 cia) {
    if ((cpu->hid2 & PPC_HID2_PSE) == 0 || (!indexed && (cpu->hid2 & PPC_HID2_LSQE) == 0)) {
        ppc_program_exception(cpu, PPC_PROGRAM_ILLEGAL, cia);
        return false;
    }
    return true;
}

void ppc_psq_load(CPUState* cpu, u8 frD, u32 ea, bool w, u8 gqr_index, bool indexed, u32 cia) {
    if (!psq_check_enabled(cpu, indexed, cia))
        return;

    u32 gqr = cpu->gqr[gqr_index & 7u];
    s32 scale = gqr_scale(gqr >> 24);
    u8 type = (u8)((gqr >> 16) & 7u);
    u32 size = psq_type_size(type);
    if (!psq_access_is_valid(cpu, type, ea, cia))
        return;

    cpu->fpr[frD] = psq_load_value(cpu, ea, type, scale);
    if (w) {
        cpu->ps1[frD] = 1.0;
    } else {
        u32 ps1_ea = ea + size;
        if (!psq_access_is_valid(cpu, type, ps1_ea, cia))
            return;
        cpu->ps1[frD] = psq_load_value(cpu, ps1_ea, type, scale);
    }
}

void ppc_psq_store(CPUState* cpu, u8 frS, u32 ea, bool w, u8 gqr_index, bool indexed, u32 cia) {
    if (!psq_check_enabled(cpu, indexed, cia))
        return;

    u32 gqr = cpu->gqr[gqr_index & 7u];
    s32 scale = gqr_scale(gqr >> 8);
    u8 type = (u8)(gqr & 7u);
    u32 size = psq_type_size(type);
    if (!psq_access_is_valid(cpu, type, ea, cia))
        return;

    psq_store_value(cpu, ea, type, scale, cpu->fpr[frS]);
    if (!w) {
        u32 ps1_ea = ea + size;
        if (!psq_access_is_valid(cpu, type, ps1_ea, cia))
            return;
        psq_store_value(cpu, ps1_ea, type, scale, cpu->ps1[frS]);
    }
}

void ppc_rfi(CPUState* cpu, u32 cia) {
    if (cpu->msr & PPC_MSR_PR) {
        ppc_program_exception(cpu, PPC_PROGRAM_PRIV, cia);
        return;
    }

    cpu->msr = (cpu->msr & ~PPC_MSR_RFI_MASK) | (cpu->srr1 & PPC_MSR_RFI_MASK);
    cpu->msr &= ~PPC_MSR_POW;
    cpu->pc = cpu->srr0 & ~3u;
}

void ppc_dcbz_l(CPUState* cpu, u32 ea, u32 cia) {
    if (cpu->msr & PPC_MSR_PR) {
        ppc_program_exception(cpu, PPC_PROGRAM_PRIV, cia);
        return;
    }

    if ((cpu->hid2 & PPC_HID2_LCE) == 0) {
        ppc_program_exception(cpu, PPC_PROGRAM_ILLEGAL, cia);
        return;
    }

    u32 block = ea & ~31u;
    u32 slot = (block >> 5) & 511u;
    bool hit = cpu->locked_cache_valid[slot] && cpu->locked_cache_tag[slot] == block;
    bool first_hit_error = hit && (cpu->hid2 & PPC_HID2_DCHERR) == 0;

    if (hit) {
        cpu->hid2 |= PPC_HID2_DCHERR;
        if (first_hit_error && (cpu->hid2 & PPC_HID2_DCHEE) &&
            (cpu->msr & PPC_MSR_EE) && (cpu->msr & PPC_MSR_ME)) {
            ppc_take_exception(cpu, PPC_EXC_MACHINE_CHECK, PPC_VECTOR_MACHINE_CHECK,
                               cia, PPC_SRR1_MACHINE_CHECK_DCBZL);
        }
    } else {
        cpu->locked_cache_valid[slot] = true;
        cpu->locked_cache_tag[slot] = block;
    }

    for (u32 i = 0; i < 32; i += 4)
        mem_write32(cpu, block + i, 0);
}

u32 ppc_eciwx(CPUState* cpu, u32 ea, u32 cia) {
    if ((cpu->ear & PPC_EAR_ENABLE) == 0) {
        ppc_dsi_exception(cpu, ea, cia, PPC_DSI_EAR_DISABLED);
        return 0;
    }

    if ((ea & 3u) != 0) {
        ppc_alignment_exception(cpu, ea, cia);
        return 0;
    }

    u8 rid = (u8)(cpu->ear & 0xFu);
    cpu->external_addr = ea;
    cpu->external_rid = rid;
    cpu->external_read_count++;
    if (cpu->external_read32)
        return cpu->external_read32(cpu, ea, rid);
    return 0;
}

void ppc_ecowx(CPUState* cpu, u32 ea, u32 value, u32 cia) {
    if ((cpu->ear & PPC_EAR_ENABLE) == 0) {
        ppc_dsi_exception(cpu, ea, cia, PPC_DSI_EAR_DISABLED);
        return;
    }

    if ((ea & 3u) != 0) {
        ppc_alignment_exception(cpu, ea, cia);
        return;
    }

    u8 rid = (u8)(cpu->ear & 0xFu);
    cpu->external_addr = ea;
    cpu->external_value = value;
    cpu->external_rid = rid;
    cpu->external_write_count++;
    if (cpu->external_write32)
        cpu->external_write32(cpu, ea, value, rid);
}

void ppc_tlbie(CPUState* cpu, u32 ea, u32 cia) {
    if (cpu->msr & PPC_MSR_PR) {
        ppc_program_exception(cpu, PPC_PROGRAM_PRIV, cia);
        return;
    }

    cpu->tlb_last_vps = (ea >> 12) & 0xFFFFu;
    cpu->tlb_last_index = (ea >> 12) & 0x3Fu;
    cpu->tlb_invalidate_count++;
}

bool ppc_trap_condition(u8 to, u32 a, u32 b) {
    s32 sa = (s32)a;
    s32 sb = (s32)b;

    return ((sa < sb) && (to & 0x10u)) ||
           ((sa > sb) && (to & 0x08u)) ||
           ((sa == sb) && (to & 0x04u)) ||
           ((a < b) && (to & 0x02u)) ||
           ((a > b) && (to & 0x01u));
}

typedef struct {
    s32 base;
    s32 dec;
} EstimateEntry;

/* Adapted from Dolphin Emulator's Common/FloatUtils.cpp.
 * Copyright 2018 Dolphin Emulator Project, GPL-2.0-or-later. */
static const EstimateEntry frsqrte_table[32] = {
    {0x1a7e800, -0x568}, {0x17cb800, -0x4f3}, {0x1552800, -0x48d}, {0x130c000, -0x435},
    {0x10f2000, -0x3e7}, {0x0eff000, -0x3a2}, {0x0d2e000, -0x365}, {0x0b7c000, -0x32e},
    {0x09e5000, -0x2fc}, {0x0867000, -0x2d0}, {0x06ff000, -0x2a8}, {0x05ab800, -0x283},
    {0x046a000, -0x261}, {0x0339800, -0x243}, {0x0218800, -0x226}, {0x0105800, -0x20b},
    {0x3ffa000, -0x7a4}, {0x3c29000, -0x700}, {0x38aa000, -0x670}, {0x3572000, -0x5f2},
    {0x3279000, -0x584}, {0x2fb7000, -0x524}, {0x2d26000, -0x4cc}, {0x2ac0000, -0x47e},
    {0x2881000, -0x43a}, {0x2665000, -0x3fa}, {0x2468000, -0x3c2}, {0x2287000, -0x38e},
    {0x20c1000, -0x35e}, {0x1f12000, -0x332}, {0x1d79000, -0x30a}, {0x1bf4000, -0x2e6},
};

static const EstimateEntry fres_table[32] = {
    {0x7ff800, 0x3e1}, {0x783800, 0x3a7}, {0x70ea00, 0x371}, {0x6a0800, 0x340},
    {0x638800, 0x313}, {0x5d6200, 0x2ea}, {0x579000, 0x2c4}, {0x520800, 0x2a0},
    {0x4cc800, 0x27f}, {0x47ca00, 0x261}, {0x430800, 0x245}, {0x3e8000, 0x22a},
    {0x3a2c00, 0x212}, {0x360800, 0x1fb}, {0x321400, 0x1e5}, {0x2e4a00, 0x1d1},
    {0x2aa800, 0x1be}, {0x272c00, 0x1ac}, {0x23d600, 0x19b}, {0x209e00, 0x18b},
    {0x1d8800, 0x17c}, {0x1a9000, 0x16e}, {0x17ae00, 0x15b}, {0x14f800, 0x15b},
    {0x124400, 0x143}, {0x0fbe00, 0x143}, {0x0d3800, 0x12d}, {0x0ade00, 0x12d},
    {0x088400, 0x11a}, {0x065000, 0x11a}, {0x041c00, 0x108}, {0x020c00, 0x106},
};

static u64 f64_bits(f64 value) {
    u64 bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static f64 f64_value(u64 bits) {
    f64 value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

f64 ppc_approx_rsqrt(f64 value) {
    u64 bits = f64_bits(value);
    u64 mantissa = bits & 0x000FFFFFFFFFFFFFull;
    u64 sign = bits & 0x8000000000000000ull;
    s64 exponent = (s64)(bits & 0x7FF0000000000000ull);

    if (mantissa == 0 && exponent == 0)
        return f64_value(sign | 0x7FF0000000000000ull);
    if (exponent == (s64)0x7FF0000000000000ull) {
        if (mantissa == 0)
            return sign ? f64_value(0x7FF8000000000000ull) : 0.0;
        return f64_value(bits | 0x0008000000000000ull);
    }
    if (sign)
        return f64_value(0x7FF8000000000000ull);

    if (exponent == 0) {
        do {
            exponent -= (s64)0x0010000000000000ull;
            mantissa <<= 1;
        } while ((mantissa & 0x0010000000000000ull) == 0);
        mantissa &= 0x000FFFFFFFFFFFFFull;
        exponent += (s64)0x0010000000000000ull;
    }

    u64 exponent_lsb = (u64)exponent & 0x0010000000000000ull;
    exponent = ((s64)0x3FF0000000000000ull -
                (exponent - (s64)0x3FE0000000000000ull) / 2) &
               (s64)0x7FF0000000000000ull;
    u32 i = (u32)((exponent_lsb | mantissa) >> 37);
    const EstimateEntry* entry = &frsqrte_table[i / 2048u];
    bits = (u64)exponent |
           ((u64)(entry->base + entry->dec * (s32)(i % 2048u)) << 26);
    return f64_value(bits);
}

f64 ppc_approx_reciprocal(f64 value) {
    u64 bits = f64_bits(value);
    u64 mantissa = bits & 0x000FFFFFFFFFFFFFull;
    u64 sign = bits & 0x8000000000000000ull;
    u64 exponent = bits & 0x7FF0000000000000ull;

    if (mantissa == 0 && exponent == 0)
        return f64_value(sign | 0x7FF0000000000000ull);
    if (exponent == 0x7FF0000000000000ull) {
        if (mantissa == 0)
            return f64_value(sign);
        return f64_value(bits | 0x0008000000000000ull);
    }
    if (exponent < (895ull << 52))
        return f64_value(sign | 0x47EFFFFFE0000000ull);
    if (exponent >= (1149ull << 52))
        return f64_value(sign);

    exponent = 0x7FD0000000000000ull - exponent;
    u32 i = (u32)(mantissa >> 37);
    const EstimateEntry* entry = &fres_table[i / 1024u];
    bits = sign | exponent |
           ((u64)(entry->base - (entry->dec * (s32)(i % 1024u) + 1) / 2) << 29);
    return f64_value(bits);
}

void ppc_fpscr_updated(CPUState* cpu) {
    const u32 vx_any = 0x01F80700u;
    const u32 any_e = 0x000000F8u;
    u32 fpscr = cpu->fpscr;
    fpscr = (fpscr & ~0x20000000u) | ((fpscr & vx_any) ? 0x20000000u : 0u);
    fpscr = (fpscr & ~0x40000000u) |
            ((((fpscr >> 22) & fpscr & any_e) != 0) ? 0x40000000u : 0u);
    cpu->fpscr = fpscr;
}

static void set_fp_exception(CPUState* cpu, u32 bit) {
    if ((cpu->fpscr & bit) != bit)
        cpu->fpscr |= 0x80000000u;
    cpu->fpscr |= bit;
    ppc_fpscr_updated(cpu);
}

static bool is_snan(f64 value) {
    u64 bits = f64_bits(value);
    u64 fraction = bits & 0x000FFFFFFFFFFFFFull;
    return (bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull &&
           fraction != 0 && (fraction & 0x0008000000000000ull) == 0;
}

static u32 classify_f64(f64 value) {
    u64 bits = f64_bits(value);
    u64 sign = bits >> 63;
    u64 exponent = bits & 0x7FF0000000000000ull;
    u64 fraction = bits & 0x000FFFFFFFFFFFFFull;
    if (exponent == 0x7FF0000000000000ull)
        return fraction ? 0x11u : (sign ? 0x09u : 0x05u);
    if (exponent == 0)
        return fraction ? (sign ? 0x18u : 0x14u) : (sign ? 0x12u : 0x02u);
    return sign ? 0x08u : 0x04u;
}

static u32 classify_f32(f32 value) {
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));
    u32 sign = bits >> 31;
    u32 exponent = bits & 0x7F800000u;
    u32 fraction = bits & 0x007FFFFFu;
    if (exponent == 0x7F800000u)
        return fraction ? 0x11u : (sign ? 0x09u : 0x05u);
    if (exponent == 0)
        return fraction ? (sign ? 0x18u : 0x14u) : (sign ? 0x12u : 0x02u);
    return sign ? 0x08u : 0x04u;
}

static void set_fprf(CPUState* cpu, u32 value) {
    cpu->fpscr = (cpu->fpscr & ~(0x1Fu << 12)) | ((value & 0x1Fu) << 12);
}

bool ppc_fres(CPUState* cpu, f64 value, f64* result) {
    if (value == 0.0) {
        set_fp_exception(cpu, 0x04000000u);
        cpu->fpscr &= ~0x00006000u;
        if (cpu->fpscr & 0x10u)
            return false;
    } else if (is_snan(value)) {
        set_fp_exception(cpu, 0x01000000u);
        cpu->fpscr &= ~0x00006000u;
        if (cpu->fpscr & 0x80u)
            return false;
    } else if (isnan(value) || isinf(value)) {
        cpu->fpscr &= ~0x00006000u;
    }

    *result = ppc_approx_reciprocal(value);
    set_fprf(cpu, classify_f32((f32)*result));
    return true;
}

bool ppc_frsqrte(CPUState* cpu, f64 value, f64* result) {
    if (value < 0.0) {
        set_fp_exception(cpu, 0x00000200u);
        cpu->fpscr &= ~0x00006000u;
        if (cpu->fpscr & 0x80u)
            return false;
    } else if (value == 0.0) {
        set_fp_exception(cpu, 0x04000000u);
        cpu->fpscr &= ~0x00006000u;
        if (cpu->fpscr & 0x10u)
            return false;
    } else if (is_snan(value)) {
        set_fp_exception(cpu, 0x01000000u);
        cpu->fpscr &= ~0x00006000u;
        if (cpu->fpscr & 0x80u)
            return false;
    } else if (isnan(value) || isinf(value)) {
        cpu->fpscr &= ~0x00006000u;
    }

    *result = ppc_approx_rsqrt(value);
    set_fprf(cpu, classify_f64(*result));
    return true;
}

void ppc_ps_res(CPUState* cpu, f64 a, f64 b, f64* result_a, f64* result_b) {
    if (a == 0.0 || b == 0.0) {
        set_fp_exception(cpu, 0x04000000u);
        cpu->fpscr &= ~0x00006000u;
    }
    if (is_snan(a) || is_snan(b))
        set_fp_exception(cpu, 0x01000000u);
    if (isnan(a) || isinf(a) || isnan(b) || isinf(b))
        cpu->fpscr &= ~0x00006000u;
    *result_a = ppc_approx_reciprocal(a);
    *result_b = ppc_approx_reciprocal(b);
    set_fprf(cpu, classify_f32((f32)*result_a));
}

void ppc_ps_rsqrte(CPUState* cpu, f64 a, f64 b, f64* result_a, f64* result_b) {
    if (a == 0.0 || b == 0.0) {
        set_fp_exception(cpu, 0x04000000u);
        cpu->fpscr &= ~0x00006000u;
    }
    if (a < 0.0 || b < 0.0) {
        set_fp_exception(cpu, 0x00000200u);
        cpu->fpscr &= ~0x00006000u;
    }
    if (is_snan(a) || is_snan(b))
        set_fp_exception(cpu, 0x01000000u);
    if (isnan(a) || isinf(a) || isnan(b) || isinf(b))
        cpu->fpscr &= ~0x00006000u;
    *result_a = ppc_approx_rsqrt(a);
    *result_b = ppc_approx_rsqrt(b);
    set_fprf(cpu, classify_f32((f32)*result_a));
}

static unsigned leading_zeroes_u64(u64 value) {
    unsigned count = 0;
    while ((value & 0x8000000000000000ull) == 0) {
        value <<= 1;
        count++;
    }
    return count;
}

static f64 force_25_bit(f64 value) {
    u64 bits = f64_bits(value);
    u64 fraction = bits & 0x000FFFFFFFFFFFFFull;
    u64 keep_mask = 0xFFFFFFFFF8000000ull;
    u64 round = 0x0000000008000000ull;

    if ((bits & 0x7FF0000000000000ull) == 0 && fraction != 0) {
        unsigned shift = leading_zeroes_u64(fraction) - 11;
        if (shift < 28) {
            keep_mask = ~((1ull << (27 - shift)) - 1);
            round >>= shift;
        } else {
            keep_mask = ~0ull;
            round = 0;
        }
    }

    bits = (bits & keep_mask) + (bits & round);
    return f64_value(bits);
}

bool ppc_fma(CPUState* cpu, f64 a, f64 c, f64 b, bool single,
             bool subtract, bool negative, f64* output) {
    f64 addend = subtract ? -b : b;
    f64 result;

    if (!single) {
        result = fma(a, c, addend);
    } else {
        f64 rounded_c = force_25_bit(c);
        result = fma(a, rounded_c, addend);
        u64 bits = f64_bits(result);
        if ((bits & 0x000000001FFFFFFFull) == 0x0000000010000000ull) {
            f64 a_prime = addend - result;
            f64 b_prime = result + a_prime;
            f64 error = fma(a, rounded_c, a_prime) + (addend - b_prime);
            if (error != 0.0) {
                if ((error > 0.0) == (result > 0.0)) bits++;
                else bits--;
                result = f64_value(bits);
            }
        }
        result = (f64)(f32)result;
    }

    if (isnan(result)) {
        u32 invalid = 0;
        if (is_snan(a) || is_snan(b) || is_snan(c))
            invalid |= 0x01000000u;

        cpu->fpscr &= ~0x00006000u;
        if (isnan(a)) {
            result = f64_value(f64_bits(a) | 0x0008000000000000ull);
        } else if (isnan(b)) {
            result = f64_value(f64_bits(b) | 0x0008000000000000ull);
        } else if (isnan(c)) {
            result = f64_value(f64_bits(c) | 0x0008000000000000ull);
        } else {
            bool invalid_multiply = (a == 0.0 && isinf(c)) ||
                                    (isinf(a) && c == 0.0);
            invalid |= invalid_multiply ? 0x00100000u : 0x00800000u;
            result = f64_value(0x7FF8000000000000ull);
        }

        if (invalid) {
            set_fp_exception(cpu, invalid);
            if (cpu->fpscr & 0x80u)
                return false;
        }
    } else if (isinf(a) || isinf(b) || isinf(c)) {
        cpu->fpscr &= ~0x00006000u;
    }

    if (negative && !isnan(result))
        result = -result;
    set_fprf(cpu, single ? classify_f32((f32)result) : classify_f64(result));
    *output = result;
    return true;
}

void ppc_memory_fence(void) {
#if defined(_MSC_VER)
    _ReadWriteBarrier();
#if defined(_M_IX86) || defined(_M_X64)
    _mm_mfence();
#endif
    _ReadWriteBarrier();
#else
    atomic_thread_fence(memory_order_seq_cst);
#endif
}

static f64 round_nearest_even(f64 value) {
    f64 lo = floor(value);
    f64 fraction = value - lo;
    if (fraction < 0.5)
        return lo;
    if (fraction > 0.5)
        return lo + 1.0;
    return fmod(lo, 2.0) == 0.0 ? lo : lo + 1.0;
}

bool ppc_fctiw(CPUState* cpu, f64 value, bool toward_zero, u64* output) {
    f64 rounded;
    switch (toward_zero ? 1u : (cpu->fpscr & 3u)) {
    case 1: rounded = trunc(value); break;
    case 2: rounded = ceil(value); break;
    case 3: rounded = floor(value); break;
    default: rounded = round_nearest_even(value); break;
    }

    u32 result;
    bool invalid = false;
    if (isnan(value)) {
        if (is_snan(value))
            set_fp_exception(cpu, 0x01000000u);
        result = 0x80000000u;
        invalid = true;
    } else if (rounded >= 2147483648.0) {
        result = 0x7FFFFFFFu;
        invalid = true;
    } else if (rounded < -2147483648.0) {
        result = 0x80000000u;
        invalid = true;
    } else {
        result = (u32)(s32)rounded;
    }

    cpu->fpscr &= ~0x00006000u;
    if (invalid) {
        set_fp_exception(cpu, 0x00000100u);
    } else if (rounded != value) {
        set_fp_exception(cpu, 0x02000000u);
        cpu->fpscr |= 0x00004000u;
        if (fabs(rounded) > fabs(value))
            cpu->fpscr |= 0x00002000u;
    }

    if (invalid && (cpu->fpscr & 0x80u))
        return false;

    *output = 0xFFF8000000000000ull | result |
              ((result == 0 && signbit(value)) ? 0x100000000ull : 0ull);
    return true;
}
