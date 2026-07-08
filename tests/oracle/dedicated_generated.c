// DolRecomp output
// cpu: gekko

#define DOLRECOMP_CPU_GEKKO 1
#define DOLRECOMP_CPU_NAME "gekko"

#include <string.h>
#include <math.h>
#include "cpu/cpu.h"

static inline u32 dolrecomp_rotl32(u32 value, u32 sh) {
    sh &= 31u;
    return sh ? ((value << sh) | (value >> (32u - sh))) : value;
}

static inline f32 dolrecomp_f32_from_bits(u32 bits) {
    f32 value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline u32 dolrecomp_f32_to_bits(f32 value) {
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static inline f64 dolrecomp_f64_from_bits(u64 bits) {
    f64 value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline u64 dolrecomp_f64_to_bits(f64 value) {
    u64 bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static inline f64 dolrecomp_ps_round(f64 value) {
    return (f64)(f32)value;
}

static inline f64 dolrecomp_ps_from_bits(u32 bits) {
    return (f64)dolrecomp_f32_from_bits(bits);
}

static inline u32 dolrecomp_ps_to_bits(f64 value) {
    return dolrecomp_f32_to_bits((f32)value);
}

void func_81010000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010000u: goto label_81010000;
    default: return;
    }
label_81010000:
    ctx->pc = 0x81010000u;
    // 81010000: bl      0x81010068
    {
            ctx->lr = 0x81010004u;
            ctx->pc = 0x81010068u;
            return;
    }

    ctx->pc = 0x81010004u;
}

void func_81010004(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010004u: goto label_81010004;
    default: return;
    }
label_81010004:
    ctx->pc = 0x81010004u;
    // 81010004: bc    12, 2, 0x81010068
    {
        bool ctr_ok = true;
        bool cr_ok = (((ctx->cr & 0x20000000u) != 0) == true);
        if (ctr_ok && cr_ok) {
            ctx->pc = 0x81010068u;
            return;
        }
    }

    ctx->pc = 0x81010008u;
}

void func_81010008(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010008u: goto label_81010008;
    default: return;
    }
label_81010008:
    ctx->pc = 0x81010008u;
    // 81010008: blr
    {
        u32 target = ctx->lr & ~3u;
        bool ctr_ok = true;
        bool cr_ok = true;
        if (ctr_ok && cr_ok) {
            ctx->pc = target;
            return;
        }
    }

    ctx->pc = 0x8101000Cu;
}

void func_8101000C(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8101000Cu: goto label_8101000C;
    default: return;
    }
label_8101000C:
    ctx->pc = 0x8101000Cu;
    // 8101000C: bctr
    {
        u32 target = ctx->ctr & ~3u;
        bool ctr_ok = true;
        bool cr_ok = true;
        if (ctr_ok && cr_ok) {
            ctx->pc = target;
            return;
        }
    }

    ctx->pc = 0x81010010u;
}

void func_81010010(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010010u: goto label_81010010;
    default: return;
    }
label_81010010:
    ctx->pc = 0x81010010u;
    // 81010010: tw      31, r3, r4
    if (ppc_trap_condition(31u, ctx->gpr[3], ctx->gpr[4])) {
        ppc_program_exception(ctx, PPC_PROGRAM_TRAP, 0x81010010u);
        return;
    }

    ctx->pc = 0x81010014u;
}

void func_81010014(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010014u: goto label_81010014;
    default: return;
    }
label_81010014:
    ctx->pc = 0x81010014u;
    // 81010014: twi     31, r3, -1
    if (ppc_trap_condition(31u, ctx->gpr[3], (u32)(s32)-1)) {
        ppc_program_exception(ctx, PPC_PROGRAM_TRAP, 0x81010014u);
        return;
    }

    ctx->pc = 0x81010018u;
}

void func_81010018(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010018u: goto label_81010018;
    default: return;
    }
label_81010018:
    ctx->pc = 0x81010018u;
    // 81010018: sc
    ppc_system_call_exception(ctx, 0x81010018u);
    return;

    ctx->pc = 0x8101001Cu;
}

void func_8101001C(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8101001Cu: goto label_8101001C;
    default: return;
    }
label_8101001C:
    ctx->pc = 0x8101001Cu;
    // 8101001C: rfi
    ppc_rfi(ctx, 0x8101001Cu);
    return;

    ctx->pc = 0x81010020u;
}

void func_81010020(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010020u: goto label_81010020;
    default: return;
    }
label_81010020:
    ctx->pc = 0x81010020u;
    // 81010020: mfmsr   r9
    ctx->gpr[9] = ctx->msr;

    ctx->pc = 0x81010024u;
}

void func_81010024(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010024u: goto label_81010024;
    default: return;
    }
label_81010024:
    ctx->pc = 0x81010024u;
    // 81010024: mtmsr   r10
    ctx->msr = ctx->gpr[10];

    ctx->pc = 0x81010028u;
}

void func_81010028(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010028u: goto label_81010028;
    default: return;
    }
label_81010028:
    ctx->pc = 0x81010028u;
    // 81010028: mfsr    r11, 3
    ctx->gpr[11] = ctx->sr[3];

    ctx->pc = 0x8101002Cu;
}

void func_8101002C(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8101002Cu: goto label_8101002C;
    default: return;
    }
label_8101002C:
    ctx->pc = 0x8101002Cu;
    // 8101002C: mfsrin  r12, r13
    ctx->gpr[12] = ctx->sr[(ctx->gpr[13] >> 28) & 0xFu];

    ctx->pc = 0x81010030u;
}

void func_81010030(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010030u: goto label_81010030;
    default: return;
    }
label_81010030:
    ctx->pc = 0x81010030u;
    // 81010030: mtsr    4, r14
    ctx->sr[4] = ctx->gpr[14];

    ctx->pc = 0x81010034u;
}

void func_81010034(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010034u: goto label_81010034;
    default: return;
    }
label_81010034:
    ctx->pc = 0x81010034u;
    // 81010034: mtsrin  r15, r16
    ctx->sr[(ctx->gpr[16] >> 28) & 0xFu] = ctx->gpr[15];

    ctx->pc = 0x81010038u;
}

void func_81010038(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010038u: goto label_81010038;
    default: return;
    }
label_81010038:
    ctx->pc = 0x81010038u;
    // 81010038: mftb    r17
    ctx->gpr[17] = ppc_mftb(ctx, 268u, 0x81010038u);
    if (ctx->exception) return;

    ctx->pc = 0x8101003Cu;
}

void func_8101003C(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8101003Cu: goto label_8101003C;
    default: return;
    }
label_8101003C:
    ctx->pc = 0x8101003Cu;
    // 8101003C: mftbu   r18
    ctx->gpr[18] = ppc_mftb(ctx, 269u, 0x8101003Cu);
    if (ctx->exception) return;

    ctx->pc = 0x81010040u;
}

void func_81010040(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010040u: goto label_81010040;
    default: return;
    }
label_81010040:
    ctx->pc = 0x81010040u;
    // 81010040: tlbie   r19
    ppc_tlbie(ctx, ctx->gpr[19], 0x81010040u);
    if (ctx->exception) return;

    ctx->pc = 0x81010044u;
}

void func_81010044(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010044u: goto label_81010044;
    default: return;
    }
label_81010044:
    ctx->pc = 0x81010044u;
    // 81010044: tlbsync
    ppc_memory_fence();

    ctx->pc = 0x81010048u;
}

void func_81010048(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010048u: goto label_81010048;
    default: return;
    }
label_81010048:
    ctx->pc = 0x81010048u;
    // 81010048: dcbz_l    r20, r21
    {
        u32 ea = ctx->gpr[20] + ctx->gpr[21];
        ppc_dcbz_l(ctx, ea, 0x81010048u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x8101004Cu;
}

void func_8101004C(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8101004Cu: goto label_8101004C;
    default: return;
    }
label_8101004C:
    ctx->pc = 0x8101004Cu;
    // 8101004C: dcbst    r20, r21
    (void)ctx;

    ctx->pc = 0x81010050u;
}

void func_81010050(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010050u: goto label_81010050;
    default: return;
    }
label_81010050:
    ctx->pc = 0x81010050u;
    // 81010050: dcbf    r20, r21
    (void)ctx;

    ctx->pc = 0x81010054u;
}

void func_81010054(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010054u: goto label_81010054;
    default: return;
    }
label_81010054:
    ctx->pc = 0x81010054u;
    // 81010054: dcbt    r20, r21
    (void)ctx;

    ctx->pc = 0x81010058u;
}

void func_81010058(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010058u: goto label_81010058;
    default: return;
    }
label_81010058:
    ctx->pc = 0x81010058u;
    // 81010058: dcbi    r20, r21
    (void)ctx;

    ctx->pc = 0x8101005Cu;
}

void func_8101005C(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8101005Cu: goto label_8101005C;
    default: return;
    }
label_8101005C:
    ctx->pc = 0x8101005Cu;
    // 8101005C: icbi    r20, r21
    (void)ctx;

    ctx->pc = 0x81010060u;
}

void func_81010060(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010060u: goto label_81010060;
    default: return;
    }
label_81010060:
    ctx->pc = 0x81010060u;
    // 81010060: eciwx   r22, r20, r21
    {
        u32 ea = ctx->gpr[20] + ctx->gpr[21];
        u32 value = ppc_eciwx(ctx, ea, 0x81010060u);
        if (ctx->exception) return;
        ctx->gpr[22] = value;
    }

    ctx->pc = 0x81010064u;
}

void func_81010064(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81010064u: goto label_81010064;
    default: return;
    }
label_81010064:
    ctx->pc = 0x81010064u;
    // 81010064: ecowx   r23, r20, r21
    {
        u32 ea = ctx->gpr[20] + ctx->gpr[21];
        ppc_ecowx(ctx, ea, ctx->gpr[23], 0x81010064u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x81010068u;
}

void (*const dedicated_funcs[])(CPUState*) = {
    func_81010000,
    func_81010004,
    func_81010008,
    func_8101000C,
    func_81010010,
    func_81010014,
    func_81010018,
    func_8101001C,
    func_81010020,
    func_81010024,
    func_81010028,
    func_8101002C,
    func_81010030,
    func_81010034,
    func_81010038,
    func_8101003C,
    func_81010040,
    func_81010044,
    func_81010048,
    func_8101004C,
    func_81010050,
    func_81010054,
    func_81010058,
    func_8101005C,
    func_81010060,
    func_81010064,
};

// end
