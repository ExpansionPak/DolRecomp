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

void func_81000000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000000u: goto label_81000000;
    default: return;
    }
label_81000000:
    ctx->pc = 0x81000000u;
    // 81000000: add   r10, r11, r12
    {
        u32 a = ctx->gpr[11];
        u32 b = ctx->gpr[12];
        u32 res = a + b;
        ctx->gpr[10] = res;
    }

    ctx->pc = 0x81000004u;
}

void func_81000100(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000100u: goto label_81000100;
    default: return;
    }
label_81000100:
    ctx->pc = 0x81000100u;
    // 81000100: mulli   r3, r4, -7
    ctx->gpr[3] = (u32)((s64)(s32)ctx->gpr[4] * (s64)(s32)-7);

    ctx->pc = 0x81000104u;
}

void func_81000200(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000200u: goto label_81000200;
    default: return;
    }
label_81000200:
    ctx->pc = 0x81000200u;
    // 81000200: srawi r4, r5, 7
    {
        u32 sh = 7u;
        u32 value = ctx->gpr[5];
        bool ca = false;
        if (sh == 0) {
            ctx->gpr[4] = value;
        } else if (sh > 31) {
            ctx->gpr[4] = (value & 0x80000000u) ? 0xFFFFFFFFu : 0u;
            ca = (value & 0x80000000u) != 0;
        } else {
            ctx->gpr[4] = (u32)((s32)value >> sh);
            ca = (value & 0x80000000u) && ((value << (32u - sh)) != 0);
        }
        ctx->xer = (ctx->xer & ~0x20000000u) | (ca ? 0x20000000u : 0u);
    }

    ctx->pc = 0x81000204u;
}

void func_81000300(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000300u: goto label_81000300;
    default: return;
    }
label_81000300:
    ctx->pc = 0x81000300u;
    // 81000300: addic   r4, r4, -1
    {
        u64 a = ctx->gpr[4];
        u64 b = (u32)(s32)(-1);
        u64 res = a + b;
        ctx->gpr[4] = (u32)res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(res >> 32) & 1u) << 29);
    }

    ctx->pc = 0x81000304u;
}

void func_81000400(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000400u: goto label_81000400;
    default: return;
    }
label_81000400:
    ctx->pc = 0x81000400u;
    // 81000400: fadds   f1, f2, f3
    ctx->fpr[1] = (f64)(f32)(ctx->fpr[2] + ctx->fpr[3]);

    ctx->pc = 0x81000404u;
}

void func_81000500(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000500u: goto label_81000500;
    default: return;
    }
label_81000500:
    ctx->pc = 0x81000500u;
    // 81000500: fmuls   f7, f8, f9
    ctx->fpr[7] = (f64)(f32)(ctx->fpr[8] * ctx->fpr[9]);

    ctx->pc = 0x81000504u;
}

void func_81000600(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000600u: goto label_81000600;
    default: return;
    }
label_81000600:
    ctx->pc = 0x81000600u;
    // 81000600: fadd   f13, f14, f15
    ctx->fpr[13] = ctx->fpr[14] + ctx->fpr[15];

    ctx->pc = 0x81000604u;
}

void func_81000700(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000700u: goto label_81000700;
    default: return;
    }
label_81000700:
    ctx->pc = 0x81000700u;
    // 81000700: lfs     f1, 0(r4)
    {
        u32 ea = ctx->gpr[4] + (u32)(s32)(0);
        f64 value = (f64)dolrecomp_f32_from_bits(mem_read32(ctx, ea));
        ctx->fpr[1] = value;
        ctx->ps1[1] = value;
    }

    ctx->pc = 0x81000704u;
}

void func_81000800(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000800u: goto label_81000800;
    default: return;
    }
label_81000800:
    ctx->pc = 0x81000800u;
    // 81000800: fadds   f1, f2, f3
    ctx->fpr[1] = (f64)(f32)(ctx->fpr[2] + ctx->fpr[3]);

    ctx->pc = 0x81000804u;
}

void func_81000900(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000900u: goto label_81000900;
    default: return;
    }
label_81000900:
    ctx->pc = 0x81000900u;
    // 81000900: ps_add  f1, f2, f3
    {
        ctx->fpr[1] = dolrecomp_ps_round((f32)ctx->fpr[2] + (f32)ctx->fpr[3]);
        ctx->ps1[1] = dolrecomp_ps_round((f32)ctx->ps1[2] + (f32)ctx->ps1[3]);
    }

    ctx->pc = 0x81000904u;
}

void func_81000A00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000A00u: goto label_81000A00;
    default: return;
    }
label_81000A00:
    ctx->pc = 0x81000A00u;
    // 81000A00: subfic  r4, r5, 1
    {
        u64 res = (u64)(u32)(s32)(1) + (u64)(~ctx->gpr[5]) + 1u;
        ctx->gpr[4] = (u32)res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(res >> 32) & 1u) << 29);
    }

    ctx->pc = 0x81000A04u;
}

void func_81000B00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000B00u: goto label_81000B00;
    default: return;
    }
label_81000B00:
    ctx->pc = 0x81000B00u;
    // 81000B00: lis     r5, 4660
    ctx->gpr[5] = ((u32)(s32)(4660) << 16);

    ctx->pc = 0x81000B04u;
}

void func_81000C00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000C00u: goto label_81000C00;
    default: return;
    }
label_81000C00:
    ctx->pc = 0x81000C00u;
    // 81000C00: and   r19, r20, r21
    {
        ctx->gpr[19] = ctx->gpr[20] & ctx->gpr[21];
    }

    ctx->pc = 0x81000C04u;
}

void func_81000D00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000D00u: goto label_81000D00;
    default: return;
    }
label_81000D00:
    ctx->pc = 0x81000D00u;
    // 81000D00: andc   r20, r21, r22
    {
        ctx->gpr[20] = ctx->gpr[21] & ~ctx->gpr[22];
    }

    ctx->pc = 0x81000D04u;
}

void func_81000E00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000E00u: goto label_81000E00;
    default: return;
    }
label_81000E00:
    ctx->pc = 0x81000E00u;
    // 81000E00: or   r21, r22, r23
    {
        ctx->gpr[21] = ctx->gpr[22] | ctx->gpr[23];
    }

    ctx->pc = 0x81000E04u;
}

void func_81000F00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81000F00u: goto label_81000F00;
    default: return;
    }
label_81000F00:
    ctx->pc = 0x81000F00u;
    // 81000F00: xor   r23, r24, r25
    {
        ctx->gpr[23] = ctx->gpr[24] ^ ctx->gpr[25];
    }

    ctx->pc = 0x81000F04u;
}

void func_81001000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001000u: goto label_81001000;
    default: return;
    }
label_81001000:
    ctx->pc = 0x81001000u;
    // 81001000: nand   r24, r25, r26
    {
        ctx->gpr[24] = ~(ctx->gpr[25] & ctx->gpr[26]);
    }

    ctx->pc = 0x81001004u;
}

void func_81001100(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001100u: goto label_81001100;
    default: return;
    }
label_81001100:
    ctx->pc = 0x81001100u;
    // 81001100: nor   r25, r26, r27
    {
        ctx->gpr[25] = ~(ctx->gpr[26] | ctx->gpr[27]);
    }

    ctx->pc = 0x81001104u;
}

void func_81001200(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001200u: goto label_81001200;
    default: return;
    }
label_81001200:
    ctx->pc = 0x81001200u;
    // 81001200: eqv   r26, r27, r28
    {
        ctx->gpr[26] = ~(ctx->gpr[27] ^ ctx->gpr[28]);
    }

    ctx->pc = 0x81001204u;
}

void func_81001300(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001300u: goto label_81001300;
    default: return;
    }
label_81001300:
    ctx->pc = 0x81001300u;
    // 81001300: cntlzw r27, r28
    {
        u32 v = ctx->gpr[28];
        u32 n = 0;
        while (n < 32 && ((v & (0x80000000u >> n)) == 0)) n++;
        ctx->gpr[27] = n;
    }

    ctx->pc = 0x81001304u;
}

void func_81001400(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001400u: goto label_81001400;
    default: return;
    }
label_81001400:
    ctx->pc = 0x81001400u;
    // 81001400: extsb r28, r29
    {
        ctx->gpr[28] = (u32)(s32)(s8)ctx->gpr[29];
    }

    ctx->pc = 0x81001404u;
}

void func_81001500(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001500u: goto label_81001500;
    default: return;
    }
label_81001500:
    ctx->pc = 0x81001500u;
    // 81001500: extsh r29, r30
    {
        ctx->gpr[29] = (u32)(s32)(s16)ctx->gpr[30];
    }

    ctx->pc = 0x81001504u;
}

void func_81001600(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001600u: goto label_81001600;
    default: return;
    }
label_81001600:
    ctx->pc = 0x81001600u;
    // 81001600: slw   r30, r31, r3
    {
        u32 sh = ctx->gpr[3] & 0x3Fu;
        ctx->gpr[30] = sh > 31 ? 0u : (ctx->gpr[31] << sh);
    }

    ctx->pc = 0x81001604u;
}

void func_81001700(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001700u: goto label_81001700;
    default: return;
    }
label_81001700:
    ctx->pc = 0x81001700u;
    // 81001700: slw   r30, r31, r3
    {
        u32 sh = ctx->gpr[3] & 0x3Fu;
        ctx->gpr[30] = sh > 31 ? 0u : (ctx->gpr[31] << sh);
    }

    ctx->pc = 0x81001704u;
}

void func_81001800(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001800u: goto label_81001800;
    default: return;
    }
label_81001800:
    ctx->pc = 0x81001800u;
    // 81001800: srw   r31, r3, r4
    {
        u32 sh = ctx->gpr[4] & 0x3Fu;
        ctx->gpr[31] = sh > 31 ? 0u : (ctx->gpr[3] >> sh);
    }

    ctx->pc = 0x81001804u;
}

void func_81001900(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001900u: goto label_81001900;
    default: return;
    }
label_81001900:
    ctx->pc = 0x81001900u;
    // 81001900: srw   r31, r3, r4
    {
        u32 sh = ctx->gpr[4] & 0x3Fu;
        ctx->gpr[31] = sh > 31 ? 0u : (ctx->gpr[3] >> sh);
    }

    ctx->pc = 0x81001904u;
}

void func_81001A00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001A00u: goto label_81001A00;
    default: return;
    }
label_81001A00:
    ctx->pc = 0x81001A00u;
    // 81001A00: sraw   r3, r4, r5
    {
        u32 sh = ctx->gpr[5] & 0x3Fu;
        u32 value = ctx->gpr[4];
        bool ca = false;
        if (sh == 0) {
            ctx->gpr[3] = value;
        } else if (sh > 31) {
            ctx->gpr[3] = (value & 0x80000000u) ? 0xFFFFFFFFu : 0u;
            ca = (value & 0x80000000u) != 0;
        } else {
            ctx->gpr[3] = (u32)((s32)value >> sh);
            ca = (value & 0x80000000u) && ((value << (32u - sh)) != 0);
        }
        ctx->xer = (ctx->xer & ~0x20000000u) | (ca ? 0x20000000u : 0u);
    }

    ctx->pc = 0x81001A04u;
}

void func_81001B00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001B00u: goto label_81001B00;
    default: return;
    }
label_81001B00:
    ctx->pc = 0x81001B00u;
    // 81001B00: sraw   r3, r4, r5
    {
        u32 sh = ctx->gpr[5] & 0x3Fu;
        u32 value = ctx->gpr[4];
        bool ca = false;
        if (sh == 0) {
            ctx->gpr[3] = value;
        } else if (sh > 31) {
            ctx->gpr[3] = (value & 0x80000000u) ? 0xFFFFFFFFu : 0u;
            ca = (value & 0x80000000u) != 0;
        } else {
            ctx->gpr[3] = (u32)((s32)value >> sh);
            ca = (value & 0x80000000u) && ((value << (32u - sh)) != 0);
        }
        ctx->xer = (ctx->xer & ~0x20000000u) | (ca ? 0x20000000u : 0u);
    }

    ctx->pc = 0x81001B04u;
}

void func_81001C00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001C00u: goto label_81001C00;
    default: return;
    }
label_81001C00:
    ctx->pc = 0x81001C00u;
    // 81001C00: rlwinm r5, r6, 5, 8, 23
    {
        ctx->gpr[5] = dolrecomp_rotl32(ctx->gpr[6], 5u) & 0x00FFFF00u;
    }

    ctx->pc = 0x81001C04u;
}

void func_81001D00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001D00u: goto label_81001D00;
    default: return;
    }
label_81001D00:
    ctx->pc = 0x81001D00u;
    // 81001D00: rlwnm r6, r7, r8, 4, 27
    {
        ctx->gpr[6] = dolrecomp_rotl32(ctx->gpr[7], ctx->gpr[8]) & 0x0FFFFFF0u;
    }

    ctx->pc = 0x81001D04u;
}

void func_81001E00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001E00u: goto label_81001E00;
    default: return;
    }
label_81001E00:
    ctx->pc = 0x81001E00u;
    // 81001E00: rlwimi r7, r8, 8, 8, 15
    {
        u32 rot = dolrecomp_rotl32(ctx->gpr[8], 8u);
        ctx->gpr[7] = (ctx->gpr[7] & ~0x00FF0000u) | (rot & 0x00FF0000u);
    }

    ctx->pc = 0x81001E04u;
}

void func_81001F00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81001F00u: goto label_81001F00;
    default: return;
    }
label_81001F00:
    ctx->pc = 0x81001F00u;
    // 81001F00: cmpwi   r3, -1
    {
        s32 val_a = (s32)(ctx->gpr[3]);
        s32 val_b = (s32)(-1);
        u32 cr_bits = 0;
        if (val_a < val_b)  cr_bits |= 0x8u;
        if (val_a > val_b)  cr_bits |= 0x4u;
        if (val_a == val_b) cr_bits |= 0x2u;
        cr_bits |= (ctx->xer >> 31) & 1u;
        ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_bits << 28);
    }

    ctx->pc = 0x81001F04u;
}

void func_81002000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002000u: goto label_81002000;
    default: return;
    }
label_81002000:
    ctx->pc = 0x81002000u;
    // 81002000: cmplwi  r3, 0x8000
    {
        u32 val_a = (u32)(ctx->gpr[3]);
        u32 val_b = (u32)(0x8000u);
        u32 cr_bits = 0;
        if (val_a < val_b)  cr_bits |= 0x8u;
        if (val_a > val_b)  cr_bits |= 0x4u;
        if (val_a == val_b) cr_bits |= 0x2u;
        cr_bits |= (ctx->xer >> 31) & 1u;
        ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_bits << 28);
    }

    ctx->pc = 0x81002004u;
}

void func_81002100(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002100u: goto label_81002100;
    default: return;
    }
label_81002100:
    ctx->pc = 0x81002100u;
    // 81002100: cmpw    cr1, r3, r4
    {
        s32 val_a = (s32)(ctx->gpr[3]);
        s32 val_b = (s32)(ctx->gpr[4]);
        u32 cr_bits = 0;
        if (val_a < val_b)  cr_bits |= 0x8u;
        if (val_a > val_b)  cr_bits |= 0x4u;
        if (val_a == val_b) cr_bits |= 0x2u;
        cr_bits |= (ctx->xer >> 31) & 1u;
        ctx->cr = (ctx->cr & ~(0xFu << 24)) | (cr_bits << 24);
    }

    ctx->pc = 0x81002104u;
}

void func_81002200(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002200u: goto label_81002200;
    default: return;
    }
label_81002200:
    ctx->pc = 0x81002200u;
    // 81002200: cmplw   cr2, r3, r4
    {
        u32 val_a = (u32)(ctx->gpr[3]);
        u32 val_b = (u32)(ctx->gpr[4]);
        u32 cr_bits = 0;
        if (val_a < val_b)  cr_bits |= 0x8u;
        if (val_a > val_b)  cr_bits |= 0x4u;
        if (val_a == val_b) cr_bits |= 0x2u;
        cr_bits |= (ctx->xer >> 31) & 1u;
        ctx->cr = (ctx->cr & ~(0xFu << 20)) | (cr_bits << 20);
    }

    ctx->pc = 0x81002204u;
}

void func_81002300(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002300u: goto label_81002300;
    default: return;
    }
label_81002300:
    ctx->pc = 0x81002300u;
    // 81002300: cror    2, 3, 4
    {
        u32 a = (ctx->cr >> (31u - 3u)) & 1u;
        u32 b = (ctx->cr >> (31u - 4u)) & 1u;
        u32 mask = 0x80000000u >> 2;
        u32 value = (a | b) & 1u;
        ctx->cr = (ctx->cr & ~mask) | (value ? mask : 0u);
    }

    ctx->pc = 0x81002304u;
}

void func_81002400(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002400u: goto label_81002400;
    default: return;
    }
label_81002400:
    ctx->pc = 0x81002400u;
    // 81002400: crand   2, 3, 4
    {
        u32 a = (ctx->cr >> (31u - 3u)) & 1u;
        u32 b = (ctx->cr >> (31u - 4u)) & 1u;
        u32 mask = 0x80000000u >> 2;
        u32 value = (a & b) & 1u;
        ctx->cr = (ctx->cr & ~mask) | (value ? mask : 0u);
    }

    ctx->pc = 0x81002404u;
}

void func_81002500(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002500u: goto label_81002500;
    default: return;
    }
label_81002500:
    ctx->pc = 0x81002500u;
    // 81002500: crxor   2, 3, 4
    {
        u32 a = (ctx->cr >> (31u - 3u)) & 1u;
        u32 b = (ctx->cr >> (31u - 4u)) & 1u;
        u32 mask = 0x80000000u >> 2;
        u32 value = (a ^ b) & 1u;
        ctx->cr = (ctx->cr & ~mask) | (value ? mask : 0u);
    }

    ctx->pc = 0x81002504u;
}

void func_81002600(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002600u: goto label_81002600;
    default: return;
    }
label_81002600:
    ctx->pc = 0x81002600u;
    // 81002600: mcrf    cr2, cr3
    {
        u32 bits = (ctx->cr >> 16) & 0xFu;
        ctx->cr = (ctx->cr & ~(0xFu << 20)) | (bits << 20);
    }

    ctx->pc = 0x81002604u;
}

void func_81002700(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002700u: goto label_81002700;
    default: return;
    }
label_81002700:
    ctx->pc = 0x81002700u;
    // 81002700: mtcr    r10
    ctx->cr = (ctx->cr & ~0xFFFFFFFFu) | (ctx->gpr[10] & 0xFFFFFFFFu);

    ctx->pc = 0x81002704u;
}

void func_81002800(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002800u: goto label_81002800;
    default: return;
    }
label_81002800:
    ctx->pc = 0x81002800u;
    // 81002800: mfcr    r10
    ctx->gpr[10] = ctx->cr;

    ctx->pc = 0x81002804u;
}

void func_81002900(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002900u: goto label_81002900;
    default: return;
    }
label_81002900:
    ctx->pc = 0x81002900u;
    // 81002900: addc   r11, r12, r13
    {
        u32 a = ctx->gpr[12];
        u32 b = ctx->gpr[13];
        u64 wide = (u64)a + (u64)b;
        u32 res = (u32)wide;
        ctx->gpr[11] = res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);
    }

    ctx->pc = 0x81002904u;
}

void func_81002A00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002A00u: goto label_81002A00;
    default: return;
    }
label_81002A00:
    ctx->pc = 0x81002A00u;
    // 81002A00: adde   r12, r13, r14
    {
        u32 carry = (ctx->xer >> 29) & 1u;
        u32 a = ctx->gpr[13];
        u32 b = ctx->gpr[14];
        u64 wide = (u64)a + (u64)b + carry;
        u32 res = (u32)wide;
        ctx->gpr[12] = res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);
    }

    ctx->pc = 0x81002A04u;
}

void func_81002B00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002B00u: goto label_81002B00;
    default: return;
    }
label_81002B00:
    ctx->pc = 0x81002B00u;
    // 81002B00: addze  r13, r14
    {
        u32 a = ctx->gpr[14];
        u64 wide = (u64)a + ((ctx->xer >> 29) & 1u);
        u32 res = (u32)wide;
        ctx->gpr[13] = res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);
    }

    ctx->pc = 0x81002B04u;
}

void func_81002C00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002C00u: goto label_81002C00;
    default: return;
    }
label_81002C00:
    ctx->pc = 0x81002C00u;
    // 81002C00: subfc   r15, r16, r17
    {
        u32 a = ~ctx->gpr[16];
        u32 b = ctx->gpr[17];
        u64 wide = (u64)b + (u64)a + 1u;
        u32 res = (u32)wide;
        ctx->gpr[15] = res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);
    }

    ctx->pc = 0x81002C04u;
}

void func_81002D00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002D00u: goto label_81002D00;
    default: return;
    }
label_81002D00:
    ctx->pc = 0x81002D00u;
    // 81002D00: subfe   r16, r17, r18
    {
        u32 a = ~ctx->gpr[17];
        u32 b = ctx->gpr[18];
        u32 carry = (ctx->xer >> 29) & 1u;
        u64 wide = (u64)a + (u64)b + carry;
        u32 res = (u32)wide;
        ctx->gpr[16] = res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);
    }

    ctx->pc = 0x81002D04u;
}

void func_81002E00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002E00u: goto label_81002E00;
    default: return;
    }
label_81002E00:
    ctx->pc = 0x81002E00u;
    // 81002E00: subfze  r17, r18
    {
        u32 a = ~ctx->gpr[18];
        u64 wide = (u64)a + ((ctx->xer >> 29) & 1u);
        u32 res = (u32)wide;
        ctx->gpr[17] = res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);
    }

    ctx->pc = 0x81002E04u;
}

void func_81002F00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81002F00u: goto label_81002F00;
    default: return;
    }
label_81002F00:
    ctx->pc = 0x81002F00u;
    // 81002F00: neg  r18, r19
    {
        u32 a = ctx->gpr[19];
        ctx->gpr[18] = (~a) + 1u;
    }

    ctx->pc = 0x81002F04u;
}

void func_81003000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003000u: goto label_81003000;
    default: return;
    }
label_81003000:
    ctx->pc = 0x81003000u;
    // 81003000: mulhw   r6, r7, r8
    {
        s64 product = (s64)(s32)ctx->gpr[7] * (s64)(s32)ctx->gpr[8];
        ctx->gpr[6] = (u32)(product >> 32);
    }

    ctx->pc = 0x81003004u;
}

void func_81003100(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003100u: goto label_81003100;
    default: return;
    }
label_81003100:
    ctx->pc = 0x81003100u;
    // 81003100: mulhwu   r9, r10, r11
    {
        u64 product = (u64)ctx->gpr[10] * (u64)ctx->gpr[11];
        ctx->gpr[9] = (u32)(product >> 32);
    }

    ctx->pc = 0x81003104u;
}

void func_81003200(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003200u: goto label_81003200;
    default: return;
    }
label_81003200:
    ctx->pc = 0x81003200u;
    // 81003200: divw   r12, r13, r14
    {
        s32 dividend = (s32)ctx->gpr[13];
        s32 divisor = (s32)ctx->gpr[14];
        bool ov = divisor == 0 || ((u32)dividend == 0x80000000u && divisor == -1);
        ctx->gpr[12] = ov ? ((dividend < 0) ? 0xFFFFFFFFu : 0u) : (u32)(dividend / divisor);
    }

    ctx->pc = 0x81003204u;
}

void func_81003300(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003300u: goto label_81003300;
    default: return;
    }
label_81003300:
    ctx->pc = 0x81003300u;
    // 81003300: divwu   r15, r16, r17
    {
        u32 divisor = ctx->gpr[17];
        ctx->gpr[15] = divisor == 0 ? 0u : ctx->gpr[16] / divisor;
    }

    ctx->pc = 0x81003304u;
}

void func_81003400(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003400u: goto label_81003400;
    default: return;
    }
label_81003400:
    ctx->pc = 0x81003400u;
    // 81003400: addo   r10, r11, r12
    {
        u32 a = ctx->gpr[11];
        u32 b = ctx->gpr[12];
        u32 res = a + b;
        ctx->gpr[10] = res;
        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, b, res));
    }

    ctx->pc = 0x81003404u;
}

void func_81003500(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003500u: goto label_81003500;
    default: return;
    }
label_81003500:
    ctx->pc = 0x81003500u;
    // 81003500: divwo   r22, r23, r24
    {
        s32 dividend = (s32)ctx->gpr[23];
        s32 divisor = (s32)ctx->gpr[24];
        bool ov = divisor == 0 || ((u32)dividend == 0x80000000u && divisor == -1);
        ctx->gpr[22] = ov ? ((dividend < 0) ? 0xFFFFFFFFu : 0u) : (u32)(dividend / divisor);
        ppc_set_xer_ov(ctx, ov);
    }

    ctx->pc = 0x81003504u;
}

void func_81003600(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003600u: goto label_81003600;
    default: return;
    }
label_81003600:
    ctx->pc = 0x81003600u;
    // 81003600: addme  r3, r4
    {
        u32 input = ctx->gpr[4];
        u32 carry = (ctx->xer >> 29) & 1u;
        u64 res = (u64)input + 0xFFFFFFFFull + carry;
        ctx->gpr[3] = (u32)res;
        ctx->xer = (ctx->xer & ~0x20000000u) | ((res >> 32) ? 0x20000000u : 0u);
    }

    ctx->pc = 0x81003604u;
}

void func_81003700(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003700u: goto label_81003700;
    default: return;
    }
label_81003700:
    ctx->pc = 0x81003700u;
    // 81003700: subfme  r5, r6
    {
        u32 input = ~ctx->gpr[6];
        u32 carry = (ctx->xer >> 29) & 1u;
        u64 res = (u64)input + 0xFFFFFFFFull + carry;
        ctx->gpr[5] = (u32)res;
        ctx->xer = (ctx->xer & ~0x20000000u) | ((res >> 32) ? 0x20000000u : 0u);
    }

    ctx->pc = 0x81003704u;
}

void func_81003800(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003800u: goto label_81003800;
    default: return;
    }
label_81003800:
    ctx->pc = 0x81003800u;
    // 81003800: fsubs   f4, f5, f6
    ctx->fpr[4] = (f64)(f32)(ctx->fpr[5] - ctx->fpr[6]);

    ctx->pc = 0x81003804u;
}

void func_81003900(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003900u: goto label_81003900;
    default: return;
    }
label_81003900:
    ctx->pc = 0x81003900u;
    // 81003900: fdivs   f10, f11, f12
    ctx->fpr[10] = (f64)(f32)(ctx->fpr[11] / ctx->fpr[12]);

    ctx->pc = 0x81003904u;
}

void func_81003A00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003A00u: goto label_81003A00;
    default: return;
    }
label_81003A00:
    ctx->pc = 0x81003A00u;
    // 81003A00: fsub   f16, f17, f18
    ctx->fpr[16] = ctx->fpr[17] - ctx->fpr[18];

    ctx->pc = 0x81003A04u;
}

void func_81003B00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003B00u: goto label_81003B00;
    default: return;
    }
label_81003B00:
    ctx->pc = 0x81003B00u;
    // 81003B00: fmul   f19, f20, f21
    ctx->fpr[19] = ctx->fpr[20] * ctx->fpr[21];

    ctx->pc = 0x81003B04u;
}

void func_81003C00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003C00u: goto label_81003C00;
    default: return;
    }
label_81003C00:
    ctx->pc = 0x81003C00u;
    // 81003C00: fdiv   f22, f23, f24
    ctx->fpr[22] = ctx->fpr[23] / ctx->fpr[24];

    ctx->pc = 0x81003C04u;
}

void func_81003D00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003D00u: goto label_81003D00;
    default: return;
    }
label_81003D00:
    ctx->pc = 0x81003D00u;
    // 81003D00: fneg    f27, f28
    ctx->fpr[27] = dolrecomp_f64_from_bits(dolrecomp_f64_to_bits(ctx->fpr[28]) ^ 0x8000000000000000ull);

    ctx->pc = 0x81003D04u;
}

void func_81003E00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003E00u: goto label_81003E00;
    default: return;
    }
label_81003E00:
    ctx->pc = 0x81003E00u;
    // 81003E00: fabs    f29, f30
    ctx->fpr[29] = dolrecomp_f64_from_bits(dolrecomp_f64_to_bits(ctx->fpr[30]) & 0x7FFFFFFFFFFFFFFFull);

    ctx->pc = 0x81003E04u;
}

void func_81003F00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81003F00u: goto label_81003F00;
    default: return;
    }
label_81003F00:
    ctx->pc = 0x81003F00u;
    // 81003F00: fnabs    f31, f0
    ctx->fpr[31] = dolrecomp_f64_from_bits(dolrecomp_f64_to_bits(ctx->fpr[0]) | 0x8000000000000000ull);

    ctx->pc = 0x81003F04u;
}

void func_81004000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004000u: goto label_81004000;
    default: return;
    }
label_81004000:
    ctx->pc = 0x81004000u;
    // 81004000: fsel   f1, f2, f3, f4
    {
        ctx->fpr[1] = (ctx->fpr[2] >= 0.0) ? ctx->fpr[3] : ctx->fpr[4];
    }

    ctx->pc = 0x81004004u;
}

void func_81004100(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004100u: goto label_81004100;
    default: return;
    }
label_81004100:
    ctx->pc = 0x81004100u;
    // 81004100: fmr    f25, f26
    ctx->fpr[25] = ctx->fpr[26];

    ctx->pc = 0x81004104u;
}

void func_81004200(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004200u: goto label_81004200;
    default: return;
    }
label_81004200:
    ctx->pc = 0x81004200u;
    // 81004200: fctiw    f9, f10
    { u64 result; if (ppc_fctiw(ctx, ctx->fpr[10], false, &result)) ctx->fpr[9] = dolrecomp_f64_from_bits(result); }

    ctx->pc = 0x81004204u;
}

void func_81004300(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004300u: goto label_81004300;
    default: return;
    }
label_81004300:
    ctx->pc = 0x81004300u;
    // 81004300: fctiwz    f11, f12
    { u64 result; if (ppc_fctiw(ctx, ctx->fpr[12], true, &result)) ctx->fpr[11] = dolrecomp_f64_from_bits(result); }

    ctx->pc = 0x81004304u;
}

void func_81004400(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004400u: goto label_81004400;
    default: return;
    }
label_81004400:
    ctx->pc = 0x81004400u;
    // 81004400: fmadds f17, f18, f19, f20
    {
        f64 result;
        if (ppc_fma(ctx, ctx->fpr[18], ctx->fpr[19], ctx->fpr[20], true, false, false, &result))
            ctx->fpr[17] = ctx->ps1[17] = result;
    }

    ctx->pc = 0x81004404u;
}

void func_81004500(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004500u: goto label_81004500;
    default: return;
    }
label_81004500:
    ctx->pc = 0x81004500u;
    // 81004500: fmadds f17, f18, f19, f20
    {
        f64 result;
        if (ppc_fma(ctx, ctx->fpr[18], ctx->fpr[19], ctx->fpr[20], true, false, false, &result))
            ctx->fpr[17] = ctx->ps1[17] = result;
    }

    ctx->pc = 0x81004504u;
}

void func_81004600(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004600u: goto label_81004600;
    default: return;
    }
label_81004600:
    ctx->pc = 0x81004600u;
    // 81004600: fmsubs f25, f26, f27, f28
    {
        f64 result;
        if (ppc_fma(ctx, ctx->fpr[26], ctx->fpr[27], ctx->fpr[28], true, true, false, &result))
            ctx->fpr[25] = ctx->ps1[25] = result;
    }

    ctx->pc = 0x81004604u;
}

void func_81004700(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004700u: goto label_81004700;
    default: return;
    }
label_81004700:
    ctx->pc = 0x81004700u;
    // 81004700: fnmadds f1, f2, f3, f4
    {
        f64 result;
        if (ppc_fma(ctx, ctx->fpr[2], ctx->fpr[3], ctx->fpr[4], true, false, true, &result))
            ctx->fpr[1] = ctx->ps1[1] = result;
    }

    ctx->pc = 0x81004704u;
}

void func_81004800(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004800u: goto label_81004800;
    default: return;
    }
label_81004800:
    ctx->pc = 0x81004800u;
    // 81004800: fnmsubs f9, f10, f11, f12
    {
        f64 result;
        if (ppc_fma(ctx, ctx->fpr[10], ctx->fpr[11], ctx->fpr[12], true, true, true, &result))
            ctx->fpr[9] = ctx->ps1[9] = result;
    }

    ctx->pc = 0x81004804u;
}

void func_81004900(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004900u: goto label_81004900;
    default: return;
    }
label_81004900:
    ctx->pc = 0x81004900u;
    // 81004900: fmadd f13, f14, f15, f16
    {
        f64 result;
        if (ppc_fma(ctx, ctx->fpr[14], ctx->fpr[15], ctx->fpr[16], false, false, false, &result))
            ctx->fpr[13] = result;
    }

    ctx->pc = 0x81004904u;
}

void func_81004A00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004A00u: goto label_81004A00;
    default: return;
    }
label_81004A00:
    ctx->pc = 0x81004A00u;
    // 81004A00: fmadd f13, f14, f15, f16
    {
        f64 result;
        if (ppc_fma(ctx, ctx->fpr[14], ctx->fpr[15], ctx->fpr[16], false, false, false, &result))
            ctx->fpr[13] = result;
    }

    ctx->pc = 0x81004A04u;
}

void func_81004B00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004B00u: goto label_81004B00;
    default: return;
    }
label_81004B00:
    ctx->pc = 0x81004B00u;
    // 81004B00: fres    f1, f2
    { f64 result; if (ppc_fres(ctx, ctx->fpr[2], &result)) ctx->fpr[1] = ctx->ps1[1] = result; }

    ctx->pc = 0x81004B04u;
}

void func_81004C00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004C00u: goto label_81004C00;
    default: return;
    }
label_81004C00:
    ctx->pc = 0x81004C00u;
    // 81004C00: frsqrte    f3, f4
    { f64 result; if (ppc_frsqrte(ctx, ctx->fpr[4], &result)) ctx->fpr[3] = result; }

    ctx->pc = 0x81004C04u;
}

void func_81004D00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004D00u: goto label_81004D00;
    default: return;
    }
label_81004D00:
    ctx->pc = 0x81004D00u;
    // 81004D00: ps_res f5, f6
    { f64 a, b; ppc_ps_res(ctx, ctx->fpr[6], ctx->ps1[6], &a, &b); ctx->fpr[5] = dolrecomp_ps_round(a); ctx->ps1[5] = dolrecomp_ps_round(b); }

    ctx->pc = 0x81004D04u;
}

void func_81004E00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004E00u: goto label_81004E00;
    default: return;
    }
label_81004E00:
    ctx->pc = 0x81004E00u;
    // 81004E00: ps_rsqrte f7, f8
    { f64 a, b; ppc_ps_rsqrte(ctx, ctx->fpr[8], ctx->ps1[8], &a, &b); ctx->fpr[7] = dolrecomp_ps_round(a); ctx->ps1[7] = dolrecomp_ps_round(b); }

    ctx->pc = 0x81004E04u;
}

void func_81004F00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81004F00u: goto label_81004F00;
    default: return;
    }
label_81004F00:
    ctx->pc = 0x81004F00u;
    // 81004F00: frsp    f1, f2
    ctx->fpr[1] = (f64)(f32)ctx->fpr[2];

    ctx->pc = 0x81004F04u;
}

void func_81005000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005000u: goto label_81005000;
    default: return;
    }
label_81005000:
    ctx->pc = 0x81005000u;
    // 81005000: fmadds f17, f18, f19, f20
    {
        f64 result;
        if (ppc_fma(ctx, ctx->fpr[18], ctx->fpr[19], ctx->fpr[20], true, false, false, &result))
            ctx->fpr[17] = ctx->ps1[17] = result;
    }

    ctx->pc = 0x81005004u;
}

void func_81005100(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005100u: goto label_81005100;
    default: return;
    }
label_81005100:
    ctx->pc = 0x81005100u;
    // 81005100: lwz     r3, 0(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(0);
        ctx->gpr[3] = mem_read32(ctx, ea);
    }

    ctx->pc = 0x81005104u;
}

void func_81005200(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005200u: goto label_81005200;
    default: return;
    }
label_81005200:
    ctx->pc = 0x81005200u;
    // 81005200: lwzu     r4, 4(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(4);
        ctx->gpr[4] = mem_read32(ctx, ea);
        ctx->gpr[1] = ea;
    }

    ctx->pc = 0x81005204u;
}

void func_81005300(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005300u: goto label_81005300;
    default: return;
    }
label_81005300:
    ctx->pc = 0x81005300u;
    // 81005300: lbz     r5, 8(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(8);
        ctx->gpr[5] = mem_read8(ctx, ea);
    }

    ctx->pc = 0x81005304u;
}

void func_81005400(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005400u: goto label_81005400;
    default: return;
    }
label_81005400:
    ctx->pc = 0x81005400u;
    // 81005400: lha     r9, -4(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(-4);
        ctx->gpr[9] = (u32)(s32)(s16)mem_read16(ctx, ea);
    }

    ctx->pc = 0x81005404u;
}

void func_81005500(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005500u: goto label_81005500;
    default: return;
    }
label_81005500:
    ctx->pc = 0x81005500u;
    // 81005500: stw     r3, 28(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(28);
        mem_write32(ctx, ea, (u32)ctx->gpr[3]);
    }

    ctx->pc = 0x81005504u;
}

void func_81005600(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005600u: goto label_81005600;
    default: return;
    }
label_81005600:
    ctx->pc = 0x81005600u;
    // 81005600: stwu     r4, 32(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(32);
        mem_write32(ctx, ea, (u32)ctx->gpr[4]);
        ctx->gpr[1] = ea;
    }

    ctx->pc = 0x81005604u;
}

void func_81005700(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005700u: goto label_81005700;
    default: return;
    }
label_81005700:
    ctx->pc = 0x81005700u;
    // 81005700: lwbrx    r3, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        ctx->gpr[3] = bswap32(mem_read32(ctx, ea));
    }

    ctx->pc = 0x81005704u;
}

void func_81005800(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005800u: goto label_81005800;
    default: return;
    }
label_81005800:
    ctx->pc = 0x81005800u;
    // 81005800: sthbrx    r12, r13, r14
    {
        u32 ea = ctx->gpr[13] + ctx->gpr[14];
        mem_write16(ctx, ea, bswap16((u16)ctx->gpr[12]));
    }

    ctx->pc = 0x81005804u;
}

void func_81005900(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005900u: goto label_81005900;
    default: return;
    }
label_81005900:
    ctx->pc = 0x81005900u;
    // 81005900: stfiwx    f23, r24, r25
    {
        u32 ea = ctx->gpr[24] + ctx->gpr[25];
        mem_write32(ctx, ea, (u32)dolrecomp_f64_to_bits(ctx->fpr[23]));
    }

    ctx->pc = 0x81005904u;
}

void func_81005A00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005A00u: goto label_81005A00;
    default: return;
    }
label_81005A00:
    ctx->pc = 0x81005A00u;
    // 81005A00: psq_l   f1, 0(r4), 0, 0
    {
        u32 ea = ctx->gpr[4] + (u32)(s32)(0);
        ppc_psq_load(ctx, 1u, ea, false, 0u, false, 0x81005A00u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x81005A04u;
}

void func_81005B00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005B00u: goto label_81005B00;
    default: return;
    }
label_81005B00:
    ctx->pc = 0x81005B00u;
    // 81005B00: psq_st   f5, 16(r4), 0, 0
    {
        u32 ea = ctx->gpr[4] + (u32)(s32)(16);
        ppc_psq_store(ctx, 5u, ea, false, 0u, false, 0x81005B00u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x81005B04u;
}

void func_81005C00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005C00u: goto label_81005C00;
    default: return;
    }
label_81005C00:
    ctx->pc = 0x81005C00u;
    // 81005C00: lwarx    r17, r18, r19
    {
        u32 ea = ctx->gpr[18] + ctx->gpr[19];
        ctx->gpr[17] = mem_read32(ctx, ea);
        ctx->reserve_addr = ea;
        ctx->reserve_valid = true;
    }

    ctx->pc = 0x81005C04u;
}

void func_81005D00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005D00u: goto label_81005D00;
    default: return;
    }
label_81005D00:
    ctx->pc = 0x81005D00u;
    // 81005D00: mcrxr   cr2
    {
        u32 bits = (ctx->xer >> 28) & 0xFu;
        ctx->cr = (ctx->cr & ~(0xFu << 20)) | (bits << 20);
        ctx->xer &= ~0xE0000000u;
    }

    ctx->pc = 0x81005D04u;
}

void func_81005E00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005E00u: goto label_81005E00;
    default: return;
    }
label_81005E00:
    ctx->pc = 0x81005E00u;
    // 81005E00: sync
    ppc_memory_fence();

    ctx->pc = 0x81005E04u;
}

void func_81005F00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81005F00u: goto label_81005F00;
    case 0x81005F04u: goto label_81005F04;
    default: return;
    }
label_81005F00:
    ctx->pc = 0x81005F00u;
    // 81005F00: lfs     f1, 0(r4)
    {
        u32 ea = ctx->gpr[4] + (u32)(s32)(0);
        f64 value = (f64)dolrecomp_f32_from_bits(mem_read32(ctx, ea));
        ctx->fpr[1] = value;
        ctx->ps1[1] = value;
    }

label_81005F04:
    ctx->pc = 0x81005F04u;
    // 81005F04: psq_st   f1, 0(r5), 0, 0
    {
        u32 ea = ctx->gpr[5] + (u32)(s32)(0);
        ppc_psq_store(ctx, 1u, ea, false, 0u, false, 0x81005F04u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x81005F08u;
}

void func_81006000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006000u: goto label_81006000;
    case 0x81006004u: goto label_81006004;
    default: return;
    }
label_81006000:
    ctx->pc = 0x81006000u;
    // 81006000: frsp    f1, f2
    ctx->fpr[1] = (f64)(f32)ctx->fpr[2];

label_81006004:
    ctx->pc = 0x81006004u;
    // 81006004: psq_st   f1, 0(r5), 0, 0
    {
        u32 ea = ctx->gpr[5] + (u32)(s32)(0);
        ppc_psq_store(ctx, 1u, ea, false, 0u, false, 0x81006004u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x81006008u;
}

void func_81006100(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006100u: goto label_81006100;
    case 0x81006104u: goto label_81006104;
    default: return;
    }
label_81006100:
    ctx->pc = 0x81006100u;
    // 81006100: fmadds f17, f18, f19, f20
    {
        f64 result;
        if (ppc_fma(ctx, ctx->fpr[18], ctx->fpr[19], ctx->fpr[20], true, false, false, &result))
            ctx->fpr[17] = ctx->ps1[17] = result;
    }

label_81006104:
    ctx->pc = 0x81006104u;
    // 81006104: psq_st   f17, 0(r5), 0, 0
    {
        u32 ea = ctx->gpr[5] + (u32)(s32)(0);
        ppc_psq_store(ctx, 17u, ea, false, 0u, false, 0x81006104u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x81006108u;
}

void func_81006200(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006200u: goto label_81006200;
    case 0x81006204u: goto label_81006204;
    default: return;
    }
label_81006200:
    ctx->pc = 0x81006200u;
    // 81006200: addic   r4, r4, -1
    {
        u64 a = ctx->gpr[4];
        u64 b = (u32)(s32)(-1);
        u64 res = a + b;
        ctx->gpr[4] = (u32)res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(res >> 32) & 1u) << 29);
    }

label_81006204:
    ctx->pc = 0x81006204u;
    // 81006204: adde   r5, r5, r6
    {
        u32 carry = (ctx->xer >> 29) & 1u;
        u32 a = ctx->gpr[5];
        u32 b = ctx->gpr[6];
        u64 wide = (u64)a + (u64)b + carry;
        u32 res = (u32)wide;
        ctx->gpr[5] = res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);
    }

    ctx->pc = 0x81006208u;
}

void func_81006300(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006300u: goto label_81006300;
    case 0x81006304u: goto label_81006304;
    case 0x81006308u: goto label_81006308;
    case 0x8100630Cu: goto label_8100630C;
    default: return;
    }
label_81006300:
    ctx->pc = 0x81006300u;
    // 81006300: cmpwi   r3, 0
    {
        s32 val_a = (s32)(ctx->gpr[3]);
        s32 val_b = (s32)(0);
        u32 cr_bits = 0;
        if (val_a < val_b)  cr_bits |= 0x8u;
        if (val_a > val_b)  cr_bits |= 0x4u;
        if (val_a == val_b) cr_bits |= 0x2u;
        cr_bits |= (ctx->xer >> 31) & 1u;
        ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_bits << 28);
    }

label_81006304:
    ctx->pc = 0x81006304u;
    // 81006304: bc    12, 2, 0x8100630C
    {
        bool ctr_ok = true;
        bool cr_ok = (((ctx->cr & 0x20000000u) != 0) == true);
        if (ctr_ok && cr_ok) {
            goto label_8100630C;
        }
    }

label_81006308:
    ctx->pc = 0x81006308u;
    // 81006308: li      r7, 1
    ctx->gpr[7] = (u32)(s32)(1);

label_8100630C:
    ctx->pc = 0x8100630Cu;
    // 8100630C: li      r8, 2
    ctx->gpr[8] = (u32)(s32)(2);

    ctx->pc = 0x81006310u;
}

void func_81006400(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006400u: goto label_81006400;
    case 0x81006404u: goto label_81006404;
    default: return;
    }
label_81006400:
    ctx->pc = 0x81006400u;
    // 81006400: lwzu     r4, 4(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(4);
        ctx->gpr[4] = mem_read32(ctx, ea);
        ctx->gpr[1] = ea;
    }

label_81006404:
    ctx->pc = 0x81006404u;
    // 81006404: stw     r4, 4(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(4);
        mem_write32(ctx, ea, (u32)ctx->gpr[4]);
    }

    ctx->pc = 0x81006408u;
}

void func_81006500(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006500u: goto label_81006500;
    case 0x81006504u: goto label_81006504;
    default: return;
    }
label_81006500:
    ctx->pc = 0x81006500u;
    // 81006500: fadds   f1, f2, f3
    ctx->fpr[1] = (f64)(f32)(ctx->fpr[2] + ctx->fpr[3]);

label_81006504:
    ctx->pc = 0x81006504u;
    // 81006504: stfs     f1, 0(r5)
    {
        u32 ea = ctx->gpr[5] + (u32)(s32)(0);
        mem_write32(ctx, ea, dolrecomp_f32_to_bits((f32)ctx->fpr[1]));
    }

    ctx->pc = 0x81006508u;
}

void func_81006600(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006600u: goto label_81006600;
    case 0x81006604u: goto label_81006604;
    default: return;
    }
label_81006600:
    ctx->pc = 0x81006600u;
    // 81006600: ps_add  f1, f2, f3
    {
        ctx->fpr[1] = dolrecomp_ps_round((f32)ctx->fpr[2] + (f32)ctx->fpr[3]);
        ctx->ps1[1] = dolrecomp_ps_round((f32)ctx->ps1[2] + (f32)ctx->ps1[3]);
    }

label_81006604:
    ctx->pc = 0x81006604u;
    // 81006604: psq_st   f1, 0(r5), 0, 0
    {
        u32 ea = ctx->gpr[5] + (u32)(s32)(0);
        ppc_psq_store(ctx, 1u, ea, false, 0u, false, 0x81006604u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x81006608u;
}

void func_81006700(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006700u: goto label_81006700;
    default: return;
    }
label_81006700:
    ctx->pc = 0x81006700u;
    // 81006700: addic.  r5, r5, -1
    {
        u64 a = ctx->gpr[5];
        u64 b = (u32)(s32)(-1);
        u64 res = a + b;
        ctx->gpr[5] = (u32)res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(res >> 32) & 1u) << 29);
        u32 cr_bits = 0;
        s32 cr_value = (s32)ctx->gpr[5];
        if (cr_value < 0)  cr_bits |= 0x8u;
        if (cr_value > 0)  cr_bits |= 0x4u;
        if (cr_value == 0) cr_bits |= 0x2u;
        cr_bits |= (ctx->xer >> 31) & 1u;
        ctx->cr = (ctx->cr & 0x0FFFFFFFu) | (cr_bits << 28);
    }

    ctx->pc = 0x81006704u;
}

void func_81006800(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006800u: goto label_81006800;
    default: return;
    }
label_81006800:
    ctx->pc = 0x81006800u;
    // 81006800: ori     r4, r3, 0xFF00
    ctx->gpr[4] = ctx->gpr[3] | 0xFF00u;

    ctx->pc = 0x81006804u;
}

void func_81006900(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006900u: goto label_81006900;
    default: return;
    }
label_81006900:
    ctx->pc = 0x81006900u;
    // 81006900: oris    r5, r4, 0x1234
    ctx->gpr[5] = ctx->gpr[4] | (0x1234u << 16);

    ctx->pc = 0x81006904u;
}

void func_81006A00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006A00u: goto label_81006A00;
    default: return;
    }
label_81006A00:
    ctx->pc = 0x81006A00u;
    // 81006A00: xori    r6, r5, 0xFFFF
    ctx->gpr[6] = ctx->gpr[5] ^ 0xFFFFu;

    ctx->pc = 0x81006A04u;
}

void func_81006B00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006B00u: goto label_81006B00;
    default: return;
    }
label_81006B00:
    ctx->pc = 0x81006B00u;
    // 81006B00: xoris   r7, r6, 0x8000
    ctx->gpr[7] = ctx->gpr[6] ^ (0x8000u << 16);

    ctx->pc = 0x81006B04u;
}

void func_81006C00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006C00u: goto label_81006C00;
    default: return;
    }
label_81006C00:
    ctx->pc = 0x81006C00u;
    // 81006C00: andi.   r8, r7, 0x00FF
    {
        ctx->gpr[8] = ctx->gpr[7] & 0x00FFu;
        u32 cr_bits = 0;
        s32 cr_value = (s32)ctx->gpr[8];
        if (cr_value < 0)  cr_bits |= 0x8u;
        if (cr_value > 0)  cr_bits |= 0x4u;
        if (cr_value == 0) cr_bits |= 0x2u;
        cr_bits |= (ctx->xer >> 31) & 1u;
        ctx->cr = (ctx->cr & 0x0FFFFFFFu) | (cr_bits << 28);
    }

    ctx->pc = 0x81006C04u;
}

void func_81006D00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006D00u: goto label_81006D00;
    default: return;
    }
label_81006D00:
    ctx->pc = 0x81006D00u;
    // 81006D00: andis.  r9, r7, 0x00FF
    {
        ctx->gpr[9] = ctx->gpr[7] & (0x00FFu << 16);
        u32 cr_bits = 0;
        s32 cr_value = (s32)ctx->gpr[9];
        if (cr_value < 0)  cr_bits |= 0x8u;
        if (cr_value > 0)  cr_bits |= 0x4u;
        if (cr_value == 0) cr_bits |= 0x2u;
        cr_bits |= (ctx->xer >> 31) & 1u;
        ctx->cr = (ctx->cr & 0x0FFFFFFFu) | (cr_bits << 28);
    }

    ctx->pc = 0x81006D04u;
}

void func_81006E00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006E00u: goto label_81006E00;
    default: return;
    }
label_81006E00:
    ctx->pc = 0x81006E00u;
    // 81006E00: lbzu     r6, 12(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(12);
        ctx->gpr[6] = mem_read8(ctx, ea);
        ctx->gpr[1] = ea;
    }

    ctx->pc = 0x81006E04u;
}

void func_81006F00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81006F00u: goto label_81006F00;
    default: return;
    }
label_81006F00:
    ctx->pc = 0x81006F00u;
    // 81006F00: lhz     r7, 16(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(16);
        ctx->gpr[7] = mem_read16(ctx, ea);
    }

    ctx->pc = 0x81006F04u;
}

void func_81007000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007000u: goto label_81007000;
    default: return;
    }
label_81007000:
    ctx->pc = 0x81007000u;
    // 81007000: lhzu     r8, 20(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(20);
        ctx->gpr[8] = mem_read16(ctx, ea);
        ctx->gpr[1] = ea;
    }

    ctx->pc = 0x81007004u;
}

void func_81007100(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007100u: goto label_81007100;
    default: return;
    }
label_81007100:
    ctx->pc = 0x81007100u;
    // 81007100: lhau     r10, 24(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(24);
        ctx->gpr[10] = (u32)(s32)(s16)mem_read16(ctx, ea);
        ctx->gpr[1] = ea;
    }

    ctx->pc = 0x81007104u;
}

void func_81007200(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007200u: goto label_81007200;
    default: return;
    }
label_81007200:
    ctx->pc = 0x81007200u;
    // 81007200: stb     r5, 36(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(36);
        mem_write8(ctx, ea, (u8)ctx->gpr[5]);
    }

    ctx->pc = 0x81007204u;
}

void func_81007300(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007300u: goto label_81007300;
    default: return;
    }
label_81007300:
    ctx->pc = 0x81007300u;
    // 81007300: stbu     r6, 40(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(40);
        mem_write8(ctx, ea, (u8)ctx->gpr[6]);
        ctx->gpr[1] = ea;
    }

    ctx->pc = 0x81007304u;
}

void func_81007400(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007400u: goto label_81007400;
    default: return;
    }
label_81007400:
    ctx->pc = 0x81007400u;
    // 81007400: sth     r7, 44(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(44);
        mem_write16(ctx, ea, (u16)ctx->gpr[7]);
    }

    ctx->pc = 0x81007404u;
}

void func_81007500(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007500u: goto label_81007500;
    default: return;
    }
label_81007500:
    ctx->pc = 0x81007500u;
    // 81007500: sthu     r8, 48(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(48);
        mem_write16(ctx, ea, (u16)ctx->gpr[8]);
        ctx->gpr[1] = ea;
    }

    ctx->pc = 0x81007504u;
}

void func_81007600(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007600u: goto label_81007600;
    default: return;
    }
label_81007600:
    ctx->pc = 0x81007600u;
    // 81007600: lmw     r20, 52(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(52);
        for (u32 r = 20; r < 32; r++, ea += 4) ctx->gpr[r] = mem_read32(ctx, ea);
    }

    ctx->pc = 0x81007604u;
}

void func_81007700(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007700u: goto label_81007700;
    default: return;
    }
label_81007700:
    ctx->pc = 0x81007700u;
    // 81007700: stmw     r20, 100(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(100);
        for (u32 r = 20; r < 32; r++, ea += 4) mem_write32(ctx, ea, ctx->gpr[r]);
    }

    ctx->pc = 0x81007704u;
}

void func_81007800(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007800u: goto label_81007800;
    default: return;
    }
label_81007800:
    ctx->pc = 0x81007800u;
    // 81007800: crandc  2, 3, 4
    {
        u32 a = (ctx->cr >> (31u - 3u)) & 1u;
        u32 b = (ctx->cr >> (31u - 4u)) & 1u;
        u32 mask = 0x80000000u >> 2;
        u32 value = (a & ~b) & 1u;
        ctx->cr = (ctx->cr & ~mask) | (value ? mask : 0u);
    }

    ctx->pc = 0x81007804u;
}

void func_81007900(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007900u: goto label_81007900;
    default: return;
    }
label_81007900:
    ctx->pc = 0x81007900u;
    // 81007900: creqv   2, 3, 4
    {
        u32 a = (ctx->cr >> (31u - 3u)) & 1u;
        u32 b = (ctx->cr >> (31u - 4u)) & 1u;
        u32 mask = 0x80000000u >> 2;
        u32 value = (~(a ^ b)) & 1u;
        ctx->cr = (ctx->cr & ~mask) | (value ? mask : 0u);
    }

    ctx->pc = 0x81007904u;
}

void func_81007A00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007A00u: goto label_81007A00;
    default: return;
    }
label_81007A00:
    ctx->pc = 0x81007A00u;
    // 81007A00: crnand  2, 3, 4
    {
        u32 a = (ctx->cr >> (31u - 3u)) & 1u;
        u32 b = (ctx->cr >> (31u - 4u)) & 1u;
        u32 mask = 0x80000000u >> 2;
        u32 value = (~(a & b)) & 1u;
        ctx->cr = (ctx->cr & ~mask) | (value ? mask : 0u);
    }

    ctx->pc = 0x81007A04u;
}

void func_81007B00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007B00u: goto label_81007B00;
    default: return;
    }
label_81007B00:
    ctx->pc = 0x81007B00u;
    // 81007B00: crnor   2, 3, 4
    {
        u32 a = (ctx->cr >> (31u - 3u)) & 1u;
        u32 b = (ctx->cr >> (31u - 4u)) & 1u;
        u32 mask = 0x80000000u >> 2;
        u32 value = (~(a | b)) & 1u;
        ctx->cr = (ctx->cr & ~mask) | (value ? mask : 0u);
    }

    ctx->pc = 0x81007B04u;
}

void func_81007C00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007C00u: goto label_81007C00;
    default: return;
    }
label_81007C00:
    ctx->pc = 0x81007C00u;
    // 81007C00: crorc   2, 3, 4
    {
        u32 a = (ctx->cr >> (31u - 3u)) & 1u;
        u32 b = (ctx->cr >> (31u - 4u)) & 1u;
        u32 mask = 0x80000000u >> 2;
        u32 value = (a | ~b) & 1u;
        ctx->cr = (ctx->cr & ~mask) | (value ? mask : 0u);
    }

    ctx->pc = 0x81007C04u;
}

void func_81007D00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007D00u: goto label_81007D00;
    default: return;
    }
label_81007D00:
    ctx->pc = 0x81007D00u;
    // 81007D00: mflr    r10
    ctx->gpr[10] = ppc_mfspr(ctx, 8u, 0x81007D00u);
    if (ctx->exception) return;

    ctx->pc = 0x81007D04u;
}

void func_81007E00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007E00u: goto label_81007E00;
    default: return;
    }
label_81007E00:
    ctx->pc = 0x81007E00u;
    // 81007E00: mtlr    r10
    ppc_mtspr(ctx, 8u, ctx->gpr[10], 0x81007E00u);
    if (ctx->exception) return;

    ctx->pc = 0x81007E04u;
}

void func_81007F00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81007F00u: goto label_81007F00;
    default: return;
    }
label_81007F00:
    ctx->pc = 0x81007F00u;
    // 81007F00: subf   r14, r15, r16
    {
        u32 a = ~ctx->gpr[15];
        u32 b = ctx->gpr[16];
        u32 res = a + b + 1u;
        ctx->gpr[14] = res;
    }

    ctx->pc = 0x81007F04u;
}

void func_81008000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008000u: goto label_81008000;
    default: return;
    }
label_81008000:
    ctx->pc = 0x81008000u;
    // 81008000: orc   r22, r23, r24
    {
        ctx->gpr[22] = ctx->gpr[23] | ~ctx->gpr[24];
    }

    ctx->pc = 0x81008004u;
}

void func_81008100(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008100u: goto label_81008100;
    default: return;
    }
label_81008100:
    ctx->pc = 0x81008100u;
    // 81008100: lwzx    r3, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        ctx->gpr[3] = mem_read32(ctx, ea);
    }

    ctx->pc = 0x81008104u;
}

void func_81008200(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008200u: goto label_81008200;
    default: return;
    }
label_81008200:
    ctx->pc = 0x81008200u;
    // 81008200: lwzux    r6, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        ctx->gpr[6] = mem_read32(ctx, ea);
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x81008204u;
}

void func_81008300(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008300u: goto label_81008300;
    default: return;
    }
label_81008300:
    ctx->pc = 0x81008300u;
    // 81008300: lbzx    r7, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        ctx->gpr[7] = mem_read8(ctx, ea);
    }

    ctx->pc = 0x81008304u;
}

void func_81008400(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008400u: goto label_81008400;
    default: return;
    }
label_81008400:
    ctx->pc = 0x81008400u;
    // 81008400: lbzux    r8, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        ctx->gpr[8] = mem_read8(ctx, ea);
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x81008404u;
}

void func_81008500(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008500u: goto label_81008500;
    default: return;
    }
label_81008500:
    ctx->pc = 0x81008500u;
    // 81008500: lhzx    r9, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        ctx->gpr[9] = mem_read16(ctx, ea);
    }

    ctx->pc = 0x81008504u;
}

void func_81008600(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008600u: goto label_81008600;
    default: return;
    }
label_81008600:
    ctx->pc = 0x81008600u;
    // 81008600: lhzux    r10, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        ctx->gpr[10] = mem_read16(ctx, ea);
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x81008604u;
}

void func_81008700(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008700u: goto label_81008700;
    default: return;
    }
label_81008700:
    ctx->pc = 0x81008700u;
    // 81008700: lhax    r11, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        ctx->gpr[11] = (u32)(s32)(s16)mem_read16(ctx, ea);
    }

    ctx->pc = 0x81008704u;
}

void func_81008800(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008800u: goto label_81008800;
    default: return;
    }
label_81008800:
    ctx->pc = 0x81008800u;
    // 81008800: lhaux    r12, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        ctx->gpr[12] = (u32)(s32)(s16)mem_read16(ctx, ea);
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x81008804u;
}

void func_81008900(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008900u: goto label_81008900;
    default: return;
    }
label_81008900:
    ctx->pc = 0x81008900u;
    // 81008900: lhbrx    r6, r7, r8
    {
        u32 ea = ctx->gpr[7] + ctx->gpr[8];
        ctx->gpr[6] = bswap16(mem_read16(ctx, ea));
    }

    ctx->pc = 0x81008904u;
}

void func_81008A00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008A00u: goto label_81008A00;
    default: return;
    }
label_81008A00:
    ctx->pc = 0x81008A00u;
    // 81008A00: stwx    r3, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        mem_write32(ctx, ea, (u32)ctx->gpr[3]);
    }

    ctx->pc = 0x81008A04u;
}

void func_81008B00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008B00u: goto label_81008B00;
    default: return;
    }
label_81008B00:
    ctx->pc = 0x81008B00u;
    // 81008B00: stwux    r6, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        mem_write32(ctx, ea, (u32)ctx->gpr[6]);
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x81008B04u;
}

void func_81008C00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008C00u: goto label_81008C00;
    default: return;
    }
label_81008C00:
    ctx->pc = 0x81008C00u;
    // 81008C00: stbx    r7, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        mem_write8(ctx, ea, (u8)ctx->gpr[7]);
    }

    ctx->pc = 0x81008C04u;
}

void func_81008D00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008D00u: goto label_81008D00;
    default: return;
    }
label_81008D00:
    ctx->pc = 0x81008D00u;
    // 81008D00: stbux    r8, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        mem_write8(ctx, ea, (u8)ctx->gpr[8]);
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x81008D04u;
}

void func_81008E00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008E00u: goto label_81008E00;
    default: return;
    }
label_81008E00:
    ctx->pc = 0x81008E00u;
    // 81008E00: sthx    r9, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        mem_write16(ctx, ea, (u16)ctx->gpr[9]);
    }

    ctx->pc = 0x81008E04u;
}

void func_81008F00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81008F00u: goto label_81008F00;
    default: return;
    }
label_81008F00:
    ctx->pc = 0x81008F00u;
    // 81008F00: sthux    r10, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        mem_write16(ctx, ea, (u16)ctx->gpr[10]);
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x81008F04u;
}

void func_81009000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009000u: goto label_81009000;
    default: return;
    }
label_81009000:
    ctx->pc = 0x81009000u;
    // 81009000: stwbrx    r9, r10, r11
    {
        u32 ea = ctx->gpr[10] + ctx->gpr[11];
        mem_write32(ctx, ea, bswap32(ctx->gpr[9]));
    }

    ctx->pc = 0x81009004u;
}

void func_81009100(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009100u: goto label_81009100;
    default: return;
    }
label_81009100:
    ctx->pc = 0x81009100u;
    // 81009100: dcbz    r15, r16
    {
        u32 ea = ctx->gpr[15] + ctx->gpr[16];
        ea &= ~31u;
        for (u32 i = 0; i < 32; i += 4) mem_write32(ctx, ea + i, 0);
    }

    ctx->pc = 0x81009104u;
}

void func_81009200(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009200u: goto label_81009200;
    default: return;
    }
label_81009200:
    ctx->pc = 0x81009200u;
    // 81009200: lfsu     f2, 4(r4)
    {
        u32 ea = ctx->gpr[4] + (u32)(s32)(4);
        f64 value = (f64)dolrecomp_f32_from_bits(mem_read32(ctx, ea));
        ctx->fpr[2] = value;
        ctx->ps1[2] = value;
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x81009204u;
}

void func_81009300(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009300u: goto label_81009300;
    default: return;
    }
label_81009300:
    ctx->pc = 0x81009300u;
    // 81009300: lfd     f3, 8(r4)
    {
        u32 ea = ctx->gpr[4] + (u32)(s32)(8);
        ctx->fpr[3] = dolrecomp_f64_from_bits(mem_read64(ctx, ea));
    }

    ctx->pc = 0x81009304u;
}

void func_81009400(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009400u: goto label_81009400;
    default: return;
    }
label_81009400:
    ctx->pc = 0x81009400u;
    // 81009400: lfdu     f4, 16(r4)
    {
        u32 ea = ctx->gpr[4] + (u32)(s32)(16);
        ctx->fpr[4] = dolrecomp_f64_from_bits(mem_read64(ctx, ea));
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x81009404u;
}

void func_81009500(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009500u: goto label_81009500;
    default: return;
    }
label_81009500:
    ctx->pc = 0x81009500u;
    // 81009500: stfsu     f6, 24(r4)
    {
        u32 ea = ctx->gpr[4] + (u32)(s32)(24);
        mem_write32(ctx, ea, dolrecomp_f32_to_bits((f32)ctx->fpr[6]));
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x81009504u;
}

void func_81009600(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009600u: goto label_81009600;
    default: return;
    }
label_81009600:
    ctx->pc = 0x81009600u;
    // 81009600: stfd     f7, 32(r4)
    {
        u32 ea = ctx->gpr[4] + (u32)(s32)(32);
        mem_write64(ctx, ea, dolrecomp_f64_to_bits(ctx->fpr[7]));
    }

    ctx->pc = 0x81009604u;
}

void func_81009700(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009700u: goto label_81009700;
    default: return;
    }
label_81009700:
    ctx->pc = 0x81009700u;
    // 81009700: stfdu     f8, 40(r4)
    {
        u32 ea = ctx->gpr[4] + (u32)(s32)(40);
        mem_write64(ctx, ea, dolrecomp_f64_to_bits(ctx->fpr[8]));
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x81009704u;
}

void func_81009800(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009800u: goto label_81009800;
    default: return;
    }
label_81009800:
    ctx->pc = 0x81009800u;
    // 81009800: lfsx    f9, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        f64 value = (f64)dolrecomp_f32_from_bits(mem_read32(ctx, ea));
        ctx->fpr[9] = value;
        ctx->ps1[9] = value;
    }

    ctx->pc = 0x81009804u;
}

void func_81009900(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009900u: goto label_81009900;
    default: return;
    }
label_81009900:
    ctx->pc = 0x81009900u;
    // 81009900: lfsux    f10, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        f64 value = (f64)dolrecomp_f32_from_bits(mem_read32(ctx, ea));
        ctx->fpr[10] = value;
        ctx->ps1[10] = value;
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x81009904u;
}

void func_81009A00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009A00u: goto label_81009A00;
    default: return;
    }
label_81009A00:
    ctx->pc = 0x81009A00u;
    // 81009A00: lfdx    f11, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        ctx->fpr[11] = dolrecomp_f64_from_bits(mem_read64(ctx, ea));
    }

    ctx->pc = 0x81009A04u;
}

void func_81009B00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009B00u: goto label_81009B00;
    default: return;
    }
label_81009B00:
    ctx->pc = 0x81009B00u;
    // 81009B00: lfdux    f12, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        ctx->fpr[12] = dolrecomp_f64_from_bits(mem_read64(ctx, ea));
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x81009B04u;
}

void func_81009C00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009C00u: goto label_81009C00;
    default: return;
    }
label_81009C00:
    ctx->pc = 0x81009C00u;
    // 81009C00: stfsx    f13, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        mem_write32(ctx, ea, dolrecomp_f32_to_bits((f32)ctx->fpr[13]));
    }

    ctx->pc = 0x81009C04u;
}

void func_81009D00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009D00u: goto label_81009D00;
    default: return;
    }
label_81009D00:
    ctx->pc = 0x81009D00u;
    // 81009D00: stfsux    f14, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        mem_write32(ctx, ea, dolrecomp_f32_to_bits((f32)ctx->fpr[14]));
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x81009D04u;
}

void func_81009E00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009E00u: goto label_81009E00;
    default: return;
    }
label_81009E00:
    ctx->pc = 0x81009E00u;
    // 81009E00: stfdx    f15, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        mem_write64(ctx, ea, dolrecomp_f64_to_bits(ctx->fpr[15]));
    }

    ctx->pc = 0x81009E04u;
}

void func_81009F00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x81009F00u: goto label_81009F00;
    default: return;
    }
label_81009F00:
    ctx->pc = 0x81009F00u;
    // 81009F00: stfdux    f16, r4, r5
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        mem_write64(ctx, ea, dolrecomp_f64_to_bits(ctx->fpr[16]));
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x81009F04u;
}

void func_8100A000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100A000u: goto label_8100A000;
    default: return;
    }
label_8100A000:
    ctx->pc = 0x8100A000u;
    // 8100A000: fcmpu   cr2, f3, f4
    {
        f64 val_a = ctx->fpr[3];
        f64 val_b = ctx->fpr[4];
        u32 cr_bits = 0;
        if (val_a < val_b)       cr_bits = 0x8u;
        else if (val_a > val_b)  cr_bits = 0x4u;
        else if (val_a == val_b) cr_bits = 0x2u;
        else                     cr_bits = 0x1u;
        ctx->cr = (ctx->cr & ~(0xFu << 20)) | (cr_bits << 20);
    }

    ctx->pc = 0x8100A004u;
}

void func_8100A100(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100A100u: goto label_8100A100;
    default: return;
    }
label_8100A100:
    ctx->pc = 0x8100A100u;
    // 8100A100: fcmpo   cr3, f5, f6
    {
        f64 val_a = ctx->fpr[5];
        f64 val_b = ctx->fpr[6];
        u32 cr_bits = 0;
        if (val_a < val_b)       cr_bits = 0x8u;
        else if (val_a > val_b)  cr_bits = 0x4u;
        else if (val_a == val_b) cr_bits = 0x2u;
        else                     cr_bits = 0x1u;
        ctx->cr = (ctx->cr & ~(0xFu << 16)) | (cr_bits << 16);
    }

    ctx->pc = 0x8100A104u;
}

void func_8100A200(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100A200u: goto label_8100A200;
    default: return;
    }
label_8100A200:
    ctx->pc = 0x8100A200u;
    // 8100A200: mtfsb0  31
    {
        u32 mask = 0x80000000u >> 31;
        if (31 != 1 && 31 != 2) ctx->fpscr &= ~mask;
    }

    ctx->pc = 0x8100A204u;
}

void func_8100A300(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100A300u: goto label_8100A300;
    default: return;
    }
label_8100A300:
    ctx->pc = 0x8100A300u;
    // 8100A300: mtfsb1  31
    {
        u32 mask = 0x80000000u >> 31;
        if (31 != 1 && 31 != 2) ctx->fpscr |= mask;
    }

    ctx->pc = 0x8100A304u;
}

void func_8100A400(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100A400u: goto label_8100A400;
    default: return;
    }
label_8100A400:
    ctx->pc = 0x8100A400u;
    // 8100A400: psq_lu   f3, 8(r4), 0, 0
    {
        u32 ea = ctx->gpr[4] + (u32)(s32)(8);
        ppc_psq_load(ctx, 3u, ea, false, 0u, false, 0x8100A400u);
        if (ctx->exception) return;
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x8100A404u;
}

void func_8100A500(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100A500u: goto label_8100A500;
    default: return;
    }
label_8100A500:
    ctx->pc = 0x8100A500u;
    // 8100A500: psq_stu   f7, 24(r4), 0, 0
    {
        u32 ea = ctx->gpr[4] + (u32)(s32)(24);
        ppc_psq_store(ctx, 7u, ea, false, 0u, false, 0x8100A500u);
        if (ctx->exception) return;
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x8100A504u;
}

void func_8100A600(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100A600u: goto label_8100A600;
    default: return;
    }
label_8100A600:
    ctx->pc = 0x8100A600u;
    // 8100A600: psq_lx   f9, r4, r5, 0, 0
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        ppc_psq_load(ctx, 9u, ea, false, 0u, true, 0x8100A600u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x8100A604u;
}

void func_8100A700(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100A700u: goto label_8100A700;
    default: return;
    }
label_8100A700:
    ctx->pc = 0x8100A700u;
    // 8100A700: psq_lux   f11, r4, r5, 0, 0
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        ppc_psq_load(ctx, 11u, ea, false, 0u, true, 0x8100A700u);
        if (ctx->exception) return;
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x8100A704u;
}

void func_8100A800(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100A800u: goto label_8100A800;
    default: return;
    }
label_8100A800:
    ctx->pc = 0x8100A800u;
    // 8100A800: psq_stx   f13, r4, r5, 0, 0
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        ppc_psq_store(ctx, 13u, ea, false, 0u, true, 0x8100A800u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x8100A804u;
}

void func_8100A900(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100A900u: goto label_8100A900;
    default: return;
    }
label_8100A900:
    ctx->pc = 0x8100A900u;
    // 8100A900: psq_stux   f15, r4, r5, 0, 0
    {
        u32 ea = ctx->gpr[4] + ctx->gpr[5];
        ppc_psq_store(ctx, 15u, ea, false, 0u, true, 0x8100A900u);
        if (ctx->exception) return;
        ctx->gpr[4] = ea;
    }

    ctx->pc = 0x8100A904u;
}

void func_8100AA00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100AA00u: goto label_8100AA00;
    default: return;
    }
label_8100AA00:
    ctx->pc = 0x8100AA00u;
    // 8100AA00: ps_sub  f4, f5, f6
    {
        ctx->fpr[4] = dolrecomp_ps_round((f32)ctx->fpr[5] - (f32)ctx->fpr[6]);
        ctx->ps1[4] = dolrecomp_ps_round((f32)ctx->ps1[5] - (f32)ctx->ps1[6]);
    }

    ctx->pc = 0x8100AA04u;
}

void func_8100AB00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100AB00u: goto label_8100AB00;
    default: return;
    }
label_8100AB00:
    ctx->pc = 0x8100AB00u;
    // 8100AB00: ps_mul  f7, f8, f9
    {
        ctx->fpr[7] = dolrecomp_ps_round((f32)ctx->fpr[8] * (f32)ctx->fpr[9]);
        ctx->ps1[7] = dolrecomp_ps_round((f32)ctx->ps1[8] * (f32)ctx->ps1[9]);
    }

    ctx->pc = 0x8100AB04u;
}

void func_8100AC00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100AC00u: goto label_8100AC00;
    default: return;
    }
label_8100AC00:
    ctx->pc = 0x8100AC00u;
    // 8100AC00: ps_div  f10, f11, f12
    {
        ctx->fpr[10] = dolrecomp_ps_round((f32)ctx->fpr[11] / (f32)ctx->fpr[12]);
        ctx->ps1[10] = dolrecomp_ps_round((f32)ctx->ps1[11] / (f32)ctx->ps1[12]);
    }

    ctx->pc = 0x8100AC04u;
}

void func_8100AD00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100AD00u: goto label_8100AD00;
    default: return;
    }
label_8100AD00:
    ctx->pc = 0x8100AD00u;
    // 8100AD00: ps_madd  f13, f14, f15, f16
    {
        f32 ps0 = (f32)ctx->fpr[14] * (f32)ctx->fpr[15];
        f32 ps1 = (f32)ctx->ps1[14] * (f32)ctx->ps1[15];
        ps0 += (f32)ctx->fpr[16];
        ps1 += (f32)ctx->ps1[16];
        ctx->fpr[13] = dolrecomp_ps_round(ps0);
        ctx->ps1[13] = dolrecomp_ps_round(ps1);
    }

    ctx->pc = 0x8100AD04u;
}

void func_8100AE00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100AE00u: goto label_8100AE00;
    default: return;
    }
label_8100AE00:
    ctx->pc = 0x8100AE00u;
    // 8100AE00: ps_msub  f17, f18, f19, f20
    {
        f32 ps0 = (f32)ctx->fpr[18] * (f32)ctx->fpr[19];
        f32 ps1 = (f32)ctx->ps1[18] * (f32)ctx->ps1[19];
        ps0 -= (f32)ctx->fpr[20];
        ps1 -= (f32)ctx->ps1[20];
        ctx->fpr[17] = dolrecomp_ps_round(ps0);
        ctx->ps1[17] = dolrecomp_ps_round(ps1);
    }

    ctx->pc = 0x8100AE04u;
}

void func_8100AF00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100AF00u: goto label_8100AF00;
    default: return;
    }
label_8100AF00:
    ctx->pc = 0x8100AF00u;
    // 8100AF00: ps_nmadd  f21, f22, f23, f24
    {
        f32 ps0 = (f32)ctx->fpr[22] * (f32)ctx->fpr[23];
        f32 ps1 = (f32)ctx->ps1[22] * (f32)ctx->ps1[23];
        ps0 += (f32)ctx->fpr[24];
        ps1 += (f32)ctx->ps1[24];
        ps0 = -ps0;
        ps1 = -ps1;
        ctx->fpr[21] = dolrecomp_ps_round(ps0);
        ctx->ps1[21] = dolrecomp_ps_round(ps1);
    }

    ctx->pc = 0x8100AF04u;
}

void func_8100B000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100B000u: goto label_8100B000;
    default: return;
    }
label_8100B000:
    ctx->pc = 0x8100B000u;
    // 8100B000: ps_nmsub  f25, f26, f27, f28
    {
        f32 ps0 = (f32)ctx->fpr[26] * (f32)ctx->fpr[27];
        f32 ps1 = (f32)ctx->ps1[26] * (f32)ctx->ps1[27];
        ps0 -= (f32)ctx->fpr[28];
        ps1 -= (f32)ctx->ps1[28];
        ps0 = -ps0;
        ps1 = -ps1;
        ctx->fpr[25] = dolrecomp_ps_round(ps0);
        ctx->ps1[25] = dolrecomp_ps_round(ps1);
    }

    ctx->pc = 0x8100B004u;
}

void func_8100B100(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100B100u: goto label_8100B100;
    default: return;
    }
label_8100B100:
    ctx->pc = 0x8100B100u;
    // 8100B100: ps_neg  f1, f2
    ctx->fpr[1] = dolrecomp_ps_from_bits(dolrecomp_ps_to_bits(ctx->fpr[2]) ^ 0x80000000u);
    ctx->ps1[1] = dolrecomp_ps_from_bits(dolrecomp_ps_to_bits(ctx->ps1[2]) ^ 0x80000000u);

    ctx->pc = 0x8100B104u;
}

void func_8100B200(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100B200u: goto label_8100B200;
    default: return;
    }
label_8100B200:
    ctx->pc = 0x8100B200u;
    // 8100B200: ps_abs  f3, f4
    ctx->fpr[3] = dolrecomp_ps_from_bits(dolrecomp_ps_to_bits(ctx->fpr[4]) & 0x7FFFFFFFu);
    ctx->ps1[3] = dolrecomp_ps_from_bits(dolrecomp_ps_to_bits(ctx->ps1[4]) & 0x7FFFFFFFu);

    ctx->pc = 0x8100B204u;
}

void func_8100B300(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100B300u: goto label_8100B300;
    default: return;
    }
label_8100B300:
    ctx->pc = 0x8100B300u;
    // 8100B300: ps_nabs  f5, f6
    ctx->fpr[5] = dolrecomp_ps_from_bits(dolrecomp_ps_to_bits(ctx->fpr[6]) | 0x80000000u);
    ctx->ps1[5] = dolrecomp_ps_from_bits(dolrecomp_ps_to_bits(ctx->ps1[6]) | 0x80000000u);

    ctx->pc = 0x8100B304u;
}

void func_8100B400(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100B400u: goto label_8100B400;
    default: return;
    }
label_8100B400:
    ctx->pc = 0x8100B400u;
    // 8100B400: ps_mr  f7, f8
    ctx->fpr[7] = ctx->fpr[8];
    ctx->ps1[7] = ctx->ps1[8];

    ctx->pc = 0x8100B404u;
}

void func_8100B500(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100B500u: goto label_8100B500;
    default: return;
    }
label_8100B500:
    ctx->pc = 0x8100B500u;
    // 8100B500: ps_sum0  f9, f10, f11, f12
    ctx->fpr[9] = dolrecomp_ps_round((f32)ctx->fpr[10] + (f32)ctx->ps1[12]);
    ctx->ps1[9] = dolrecomp_ps_round(ctx->ps1[11]);

    ctx->pc = 0x8100B504u;
}

void func_8100B600(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100B600u: goto label_8100B600;
    default: return;
    }
label_8100B600:
    ctx->pc = 0x8100B600u;
    // 8100B600: ps_sum1  f13, f14, f15, f16
    ctx->fpr[13] = dolrecomp_ps_round(ctx->fpr[15]);
    ctx->ps1[13] = dolrecomp_ps_round((f32)ctx->fpr[14] + (f32)ctx->ps1[16]);

    ctx->pc = 0x8100B604u;
}

void func_8100B700(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100B700u: goto label_8100B700;
    default: return;
    }
label_8100B700:
    ctx->pc = 0x8100B700u;
    // 8100B700: ps_muls0  f17, f18, f19
    ctx->fpr[17] = dolrecomp_ps_round((f32)ctx->fpr[18] * (f32)ctx->fpr[19]);
    ctx->ps1[17] = dolrecomp_ps_round((f32)ctx->ps1[18] * (f32)ctx->fpr[19]);

    ctx->pc = 0x8100B704u;
}

void func_8100B800(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100B800u: goto label_8100B800;
    default: return;
    }
label_8100B800:
    ctx->pc = 0x8100B800u;
    // 8100B800: ps_muls1  f20, f21, f22
    ctx->fpr[20] = dolrecomp_ps_round((f32)ctx->fpr[21] * (f32)ctx->ps1[22]);
    ctx->ps1[20] = dolrecomp_ps_round((f32)ctx->ps1[21] * (f32)ctx->ps1[22]);

    ctx->pc = 0x8100B804u;
}

void func_8100B900(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100B900u: goto label_8100B900;
    default: return;
    }
label_8100B900:
    ctx->pc = 0x8100B900u;
    // 8100B900: ps_madds0  f23, f24, f25, f26
    ctx->fpr[23] = dolrecomp_ps_round((f32)ctx->fpr[24] * (f32)ctx->fpr[25] + (f32)ctx->fpr[26]);
    ctx->ps1[23] = dolrecomp_ps_round((f32)ctx->ps1[24] * (f32)ctx->fpr[25] + (f32)ctx->ps1[26]);

    ctx->pc = 0x8100B904u;
}

void func_8100BA00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100BA00u: goto label_8100BA00;
    default: return;
    }
label_8100BA00:
    ctx->pc = 0x8100BA00u;
    // 8100BA00: ps_madds1  f27, f28, f29, f30
    ctx->fpr[27] = dolrecomp_ps_round((f32)ctx->fpr[28] * (f32)ctx->ps1[29] + (f32)ctx->fpr[30]);
    ctx->ps1[27] = dolrecomp_ps_round((f32)ctx->ps1[28] * (f32)ctx->ps1[29] + (f32)ctx->ps1[30]);

    ctx->pc = 0x8100BA04u;
}

void func_8100BB00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100BB00u: goto label_8100BB00;
    default: return;
    }
label_8100BB00:
    ctx->pc = 0x8100BB00u;
    // 8100BB00: ps_merge00  f1, f2, f3
    ctx->fpr[1] = dolrecomp_ps_round(ctx->fpr[2]);
    ctx->ps1[1] = dolrecomp_ps_round(ctx->fpr[3]);

    ctx->pc = 0x8100BB04u;
}

void func_8100BC00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100BC00u: goto label_8100BC00;
    default: return;
    }
label_8100BC00:
    ctx->pc = 0x8100BC00u;
    // 8100BC00: ps_merge01  f4, f5, f6
    ctx->fpr[4] = dolrecomp_ps_round(ctx->fpr[5]);
    ctx->ps1[4] = dolrecomp_ps_round(ctx->ps1[6]);

    ctx->pc = 0x8100BC04u;
}

void func_8100BD00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100BD00u: goto label_8100BD00;
    default: return;
    }
label_8100BD00:
    ctx->pc = 0x8100BD00u;
    // 8100BD00: ps_merge10  f7, f8, f9
    ctx->fpr[7] = dolrecomp_ps_round(ctx->ps1[8]);
    ctx->ps1[7] = dolrecomp_ps_round(ctx->fpr[9]);

    ctx->pc = 0x8100BD04u;
}

void func_8100BE00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100BE00u: goto label_8100BE00;
    default: return;
    }
label_8100BE00:
    ctx->pc = 0x8100BE00u;
    // 8100BE00: ps_merge11  f10, f11, f12
    ctx->fpr[10] = dolrecomp_ps_round(ctx->ps1[11]);
    ctx->ps1[10] = dolrecomp_ps_round(ctx->ps1[12]);

    ctx->pc = 0x8100BE04u;
}

void func_8100BF00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100BF00u: goto label_8100BF00;
    default: return;
    }
label_8100BF00:
    ctx->pc = 0x8100BF00u;
    // 8100BF00: ps_cmpu0 cr2, f13, f14
    {
        f32 val_a = (f32)ctx->fpr[13];
        f32 val_b = (f32)ctx->fpr[14];
        u32 cr_bits = 0;
        if (val_a < val_b)       cr_bits = 0x8u;
        else if (val_a > val_b)  cr_bits = 0x4u;
        else if (val_a == val_b) cr_bits = 0x2u;
        else                     cr_bits = 0x1u;
        ctx->cr = (ctx->cr & ~(0xFu << 20)) | (cr_bits << 20);
    }

    ctx->pc = 0x8100BF04u;
}

void func_8100C000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100C000u: goto label_8100C000;
    default: return;
    }
label_8100C000:
    ctx->pc = 0x8100C000u;
    // 8100C000: ps_cmpo0 cr3, f15, f16
    {
        f32 val_a = (f32)ctx->fpr[15];
        f32 val_b = (f32)ctx->fpr[16];
        u32 cr_bits = 0;
        if (val_a < val_b)       cr_bits = 0x8u;
        else if (val_a > val_b)  cr_bits = 0x4u;
        else if (val_a == val_b) cr_bits = 0x2u;
        else                     cr_bits = 0x1u;
        ctx->cr = (ctx->cr & ~(0xFu << 16)) | (cr_bits << 16);
    }

    ctx->pc = 0x8100C004u;
}

void func_8100C100(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100C100u: goto label_8100C100;
    default: return;
    }
label_8100C100:
    ctx->pc = 0x8100C100u;
    // 8100C100: ps_cmpu1 cr4, f17, f18
    {
        f32 val_a = (f32)ctx->ps1[17];
        f32 val_b = (f32)ctx->ps1[18];
        u32 cr_bits = 0;
        if (val_a < val_b)       cr_bits = 0x8u;
        else if (val_a > val_b)  cr_bits = 0x4u;
        else if (val_a == val_b) cr_bits = 0x2u;
        else                     cr_bits = 0x1u;
        ctx->cr = (ctx->cr & ~(0xFu << 12)) | (cr_bits << 12);
    }

    ctx->pc = 0x8100C104u;
}

void func_8100C200(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100C200u: goto label_8100C200;
    default: return;
    }
label_8100C200:
    ctx->pc = 0x8100C200u;
    // 8100C200: ps_cmpo1 cr5, f19, f20
    {
        f32 val_a = (f32)ctx->ps1[19];
        f32 val_b = (f32)ctx->ps1[20];
        u32 cr_bits = 0;
        if (val_a < val_b)       cr_bits = 0x8u;
        else if (val_a > val_b)  cr_bits = 0x4u;
        else if (val_a == val_b) cr_bits = 0x2u;
        else                     cr_bits = 0x1u;
        ctx->cr = (ctx->cr & ~(0xFu << 8)) | (cr_bits << 8);
    }

    ctx->pc = 0x8100C204u;
}

void func_8100C300(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100C300u: goto label_8100C300;
    default: return;
    }
label_8100C300:
    ctx->pc = 0x8100C300u;
    // 8100C300: ps_sel  f21, f22, f23, f24
    ctx->fpr[21] = ((f32)ctx->fpr[22] >= 0.0f) ? ctx->fpr[23] : ctx->fpr[24];
    ctx->ps1[21] = ((f32)ctx->ps1[22] >= 0.0f) ? ctx->ps1[23] : ctx->ps1[24];

    ctx->pc = 0x8100C304u;
}

void func_8100C400(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100C400u: goto label_8100C400;
    default: return;
    }
label_8100C400:
    ctx->pc = 0x8100C400u;
    // 8100C400: mullw   r3, r4, r5
    {
        s64 product = (s64)(s32)ctx->gpr[4] * (s64)(s32)ctx->gpr[5];
        ctx->gpr[3] = (u32)product;
    }

    ctx->pc = 0x8100C404u;
}

void func_8100C500(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100C500u: goto label_8100C500;
    default: return;
    }
label_8100C500:
    ctx->pc = 0x8100C500u;
    // 8100C500: lswi    r7, r12, 13
    {
        u32 ea = ctx->gpr[12];
        u32 count = 13u;
        for (u32 n = 0; n < count; n++) {
            u32 reg = (7u + n / 4u) & 31u;
            if ((n & 3u) == 0) ctx->gpr[reg] = 0;
            ctx->gpr[reg] |= (u32)mem_read8(ctx, ea + n) << (24u - 8u * (n & 3u));
        }
    }

    ctx->pc = 0x8100C504u;
}

void func_8100C600(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100C600u: goto label_8100C600;
    default: return;
    }
label_8100C600:
    ctx->pc = 0x8100C600u;
    // 8100C600: lswx    r9, r20, r21
    {
        u32 ea = ctx->gpr[21];
        ea += ctx->gpr[20];
        u32 count = ctx->xer & 0x7Fu;
        u32 reg_count = (count + 3u) / 4u;
        for (u32 r = 0; r < reg_count; r++) {
            u32 reg = (9u + r) & 31u;
            if (reg == 20u || reg == 21u) {
                ppc_program_exception(ctx, PPC_PROGRAM_ILLEGAL, 0x8100C600u);
                return;
            }
        }
        for (u32 n = 0; n < count; n++) {
            u32 reg = (9u + n / 4u) & 31u;
            if ((n & 3u) == 0) ctx->gpr[reg] = 0;
            ctx->gpr[reg] |= (u32)mem_read8(ctx, ea + n) << (24u - 8u * (n & 3u));
        }
    }

    ctx->pc = 0x8100C604u;
}

void func_8100C700(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100C700u: goto label_8100C700;
    default: return;
    }
label_8100C700:
    ctx->pc = 0x8100C700u;
    // 8100C700: stswi   r12, r13, 17
    {
        u32 ea = ctx->gpr[13];
        u32 count = 17u;
        for (u32 n = 0; n < count; n++) {
            u32 reg = (12u + n / 4u) & 31u;
            u8 value = (u8)(ctx->gpr[reg] >> (24u - 8u * (n & 3u)));
            mem_write8(ctx, ea + n, value);
        }
    }

    ctx->pc = 0x8100C704u;
}

void func_8100C800(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100C800u: goto label_8100C800;
    default: return;
    }
label_8100C800:
    ctx->pc = 0x8100C800u;
    // 8100C800: stswx    r14, r15, r16
    {
        u32 ea = ctx->gpr[16] + ctx->gpr[15];
        u32 count = ctx->xer & 0x7Fu;
        for (u32 n = 0; n < count; n++) {
            u32 reg = (14u + n / 4u) & 31u;
            u8 value = (u8)(ctx->gpr[reg] >> (24u - 8u * (n & 3u)));
            mem_write8(ctx, ea + n, value);
        }
    }

    ctx->pc = 0x8100C804u;
}

void func_8100C900(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100C900u: goto label_8100C900;
    default: return;
    }
label_8100C900:
    ctx->pc = 0x8100C900u;
    // 8100C900: stwcx.    r20, r21, r22
    {
        u32 ea = ctx->gpr[21] + ctx->gpr[22];
        bool success = ctx->reserve_valid;
        ctx->reserve_valid = false;
        if (success) mem_write32(ctx, ea, ctx->gpr[20]);
        ctx->cr = (ctx->cr & 0x0FFFFFFFu) | ((success ? 2u : 0u) << 28) | ((ctx->xer >> 3) & 0x10000000u);
    }

    ctx->pc = 0x8100C904u;
}

void func_8100CA00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100CA00u: goto label_8100CA00;
    default: return;
    }
label_8100CA00:
    ctx->pc = 0x8100CA00u;
    // 8100CA00: fmsub f21, f22, f23, f24
    {
        f64 result;
        if (ppc_fma(ctx, ctx->fpr[22], ctx->fpr[23], ctx->fpr[24], false, true, false, &result))
            ctx->fpr[21] = result;
    }

    ctx->pc = 0x8100CA04u;
}

void func_8100CB00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100CB00u: goto label_8100CB00;
    default: return;
    }
label_8100CB00:
    ctx->pc = 0x8100CB00u;
    // 8100CB00: fnmadd f29, f30, f31, f0
    {
        f64 result;
        if (ppc_fma(ctx, ctx->fpr[30], ctx->fpr[31], ctx->fpr[0], false, false, true, &result))
            ctx->fpr[29] = result;
    }

    ctx->pc = 0x8100CB04u;
}

void func_8100CC00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100CC00u: goto label_8100CC00;
    default: return;
    }
label_8100CC00:
    ctx->pc = 0x8100CC00u;
    // 8100CC00: fnmsub f5, f6, f7, f8
    {
        f64 result;
        if (ppc_fma(ctx, ctx->fpr[6], ctx->fpr[7], ctx->fpr[8], false, true, true, &result))
            ctx->fpr[5] = result;
    }

    ctx->pc = 0x8100CC04u;
}

void func_8100CD00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100CD00u: goto label_8100CD00;
    default: return;
    }
label_8100CD00:
    ctx->pc = 0x8100CD00u;
    // 8100CD00: mffs   f13
    ctx->fpr[13] = dolrecomp_f64_from_bits(0xFFF8000000000000ull | ctx->fpscr);

    ctx->pc = 0x8100CD04u;
}

void func_8100CE00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100CE00u: goto label_8100CE00;
    default: return;
    }
label_8100CE00:
    ctx->pc = 0x8100CE00u;
    // 8100CE00: mcrfs   cr2, cr3
    {
        u32 field = (ctx->fpscr >> 16) & 0xFu;
        ctx->fpscr &= ~((0xFu << 16) & 0x83F80700u);
        ppc_fpscr_updated(ctx);
        ctx->cr = (ctx->cr & ~(0xFu << 20)) | (field << 20);
    }

    ctx->pc = 0x8100CE04u;
}

void func_8100CF00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100CF00u: goto label_8100CF00;
    default: return;
    }
label_8100CF00:
    ctx->pc = 0x8100CF00u;
    // 8100CF00: mtfsfi 4, 10
    ctx->fpscr = (ctx->fpscr & ~(0xFu << 12)) | (0xAu << 12);
    ppc_fpscr_updated(ctx);

    ctx->pc = 0x8100CF04u;
}

void func_8100D000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100D000u: goto label_8100D000;
    default: return;
    }
label_8100D000:
    ctx->pc = 0x8100D000u;
    // 8100D000: mtfsf  0x5A, f14
    {
        u32 mask = 0;
        for (u32 i = 0; i < 8; i++) if (0x5Au & (1u << i)) mask |= 0xFu << (i * 4);
        u32 source = (u32)dolrecomp_f64_to_bits(ctx->fpr[14]);
        ctx->fpscr = (ctx->fpscr & ~mask) | (source & mask);
        ppc_fpscr_updated(ctx);
    }

    ctx->pc = 0x8100D004u;
}

void func_8100D100(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100D100u: goto label_8100D100;
    default: return;
    }
label_8100D100:
    ctx->pc = 0x8100D100u;
    // 8100D100: eieio
    ppc_memory_fence();

    ctx->pc = 0x8100D104u;
}

void func_8100D200(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100D200u: goto label_8100D200;
    default: return;
    }
label_8100D200:
    ctx->pc = 0x8100D200u;
    // 8100D200: isync
    ppc_memory_fence();

    ctx->pc = 0x8100D204u;
}

void func_8100D300(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100D300u: goto label_8100D300;
    default: return;
    }
label_8100D300:
    ctx->pc = 0x8100D300u;
    // 8100D300: addco   r11, r12, r13
    {
        u32 a = ctx->gpr[12];
        u32 b = ctx->gpr[13];
        u64 wide = (u64)a + (u64)b;
        u32 res = (u32)wide;
        ctx->gpr[11] = res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);
        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, b, res));
    }

    ctx->pc = 0x8100D304u;
}

void func_8100D400(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100D400u: goto label_8100D400;
    default: return;
    }
label_8100D400:
    ctx->pc = 0x8100D400u;
    // 8100D400: addeo   r12, r13, r14
    {
        u32 carry = (ctx->xer >> 29) & 1u;
        u32 a = ctx->gpr[13];
        u32 b = ctx->gpr[14];
        u64 wide = (u64)a + (u64)b + carry;
        u32 res = (u32)wide;
        ctx->gpr[12] = res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);
        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, b, res));
    }

    ctx->pc = 0x8100D404u;
}

void func_8100D500(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100D500u: goto label_8100D500;
    default: return;
    }
label_8100D500:
    ctx->pc = 0x8100D500u;
    // 8100D500: addmeo  r13, r14
    {
        u32 input = ctx->gpr[14];
        u32 carry = (ctx->xer >> 29) & 1u;
        u64 res = (u64)input + 0xFFFFFFFFull + carry;
        ctx->gpr[13] = (u32)res;
        ctx->xer = (ctx->xer & ~0x20000000u) | ((res >> 32) ? 0x20000000u : 0u);
        ppc_set_xer_ov(ctx, ppc_add_overflowed(input, 0xFFFFFFFFu, (u32)res));
    }

    ctx->pc = 0x8100D504u;
}

void func_8100D600(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100D600u: goto label_8100D600;
    default: return;
    }
label_8100D600:
    ctx->pc = 0x8100D600u;
    // 8100D600: addzeo  r14, r15
    {
        u32 a = ctx->gpr[15];
        u64 wide = (u64)a + ((ctx->xer >> 29) & 1u);
        u32 res = (u32)wide;
        ctx->gpr[14] = res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);
        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, 0u, res));
    }

    ctx->pc = 0x8100D604u;
}

void func_8100D700(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100D700u: goto label_8100D700;
    default: return;
    }
label_8100D700:
    ctx->pc = 0x8100D700u;
    // 8100D700: subfo   r15, r16, r17
    {
        u32 a = ~ctx->gpr[16];
        u32 b = ctx->gpr[17];
        u32 res = a + b + 1u;
        ctx->gpr[15] = res;
        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, b, res));
    }

    ctx->pc = 0x8100D704u;
}

void func_8100D800(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100D800u: goto label_8100D800;
    default: return;
    }
label_8100D800:
    ctx->pc = 0x8100D800u;
    // 8100D800: subfco   r16, r17, r18
    {
        u32 a = ~ctx->gpr[17];
        u32 b = ctx->gpr[18];
        u64 wide = (u64)b + (u64)a + 1u;
        u32 res = (u32)wide;
        ctx->gpr[16] = res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);
        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, b, res));
    }

    ctx->pc = 0x8100D804u;
}

void func_8100D900(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100D900u: goto label_8100D900;
    default: return;
    }
label_8100D900:
    ctx->pc = 0x8100D900u;
    // 8100D900: subfeo   r17, r18, r19
    {
        u32 a = ~ctx->gpr[18];
        u32 b = ctx->gpr[19];
        u32 carry = (ctx->xer >> 29) & 1u;
        u64 wide = (u64)a + (u64)b + carry;
        u32 res = (u32)wide;
        ctx->gpr[17] = res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);
        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, b, res));
    }

    ctx->pc = 0x8100D904u;
}

void func_8100DA00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100DA00u: goto label_8100DA00;
    default: return;
    }
label_8100DA00:
    ctx->pc = 0x8100DA00u;
    // 8100DA00: subfmeo  r18, r19
    {
        u32 input = ~ctx->gpr[19];
        u32 carry = (ctx->xer >> 29) & 1u;
        u64 res = (u64)input + 0xFFFFFFFFull + carry;
        ctx->gpr[18] = (u32)res;
        ctx->xer = (ctx->xer & ~0x20000000u) | ((res >> 32) ? 0x20000000u : 0u);
        ppc_set_xer_ov(ctx, ppc_add_overflowed(input, 0xFFFFFFFFu, (u32)res));
    }

    ctx->pc = 0x8100DA04u;
}

void func_8100DB00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100DB00u: goto label_8100DB00;
    default: return;
    }
label_8100DB00:
    ctx->pc = 0x8100DB00u;
    // 8100DB00: subfzeo  r19, r20
    {
        u32 a = ~ctx->gpr[20];
        u64 wide = (u64)a + ((ctx->xer >> 29) & 1u);
        u32 res = (u32)wide;
        ctx->gpr[19] = res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);
        ppc_set_xer_ov(ctx, ppc_add_overflowed(a, 0u, res));
    }

    ctx->pc = 0x8100DB04u;
}

void func_8100DC00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100DC00u: goto label_8100DC00;
    default: return;
    }
label_8100DC00:
    ctx->pc = 0x8100DC00u;
    // 8100DC00: nego  r20, r21
    {
        u32 a = ctx->gpr[21];
        ctx->gpr[20] = (~a) + 1u;
        ppc_set_xer_ov(ctx, a == 0x80000000u);
    }

    ctx->pc = 0x8100DC04u;
}

void func_8100DD00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100DD00u: goto label_8100DD00;
    default: return;
    }
label_8100DD00:
    ctx->pc = 0x8100DD00u;
    // 8100DD00: mullwo   r21, r22, r23
    {
        s64 product = (s64)(s32)ctx->gpr[22] * (s64)(s32)ctx->gpr[23];
        ctx->gpr[21] = (u32)product;
        ppc_set_xer_ov(ctx, product < -0x80000000ll || product > 0x7fffffffll);
    }

    ctx->pc = 0x8100DD04u;
}

void func_8100DE00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100DE00u: goto label_8100DE00;
    default: return;
    }
label_8100DE00:
    ctx->pc = 0x8100DE00u;
    // 8100DE00: divwuo   r23, r24, r25
    {
        u32 divisor = ctx->gpr[25];
        ctx->gpr[23] = divisor == 0 ? 0u : ctx->gpr[24] / divisor;
        ppc_set_xer_ov(ctx, divisor == 0);
    }

    ctx->pc = 0x8100DE04u;
}

void func_8100DF00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100DF00u: goto label_8100DF00;
    case 0x8100DF04u: goto label_8100DF04;
    default: return;
    }
label_8100DF00:
    ctx->pc = 0x8100DF00u;
    // 8100DF00: lfs     f1, 0(r4)
    {
        u32 ea = ctx->gpr[4] + (u32)(s32)(0);
        f64 value = (f64)dolrecomp_f32_from_bits(mem_read32(ctx, ea));
        ctx->fpr[1] = value;
        ctx->ps1[1] = value;
    }

label_8100DF04:
    ctx->pc = 0x8100DF04u;
    // 8100DF04: psq_st   f1, 0(r5), 0, 0
    {
        u32 ea = ctx->gpr[5] + (u32)(s32)(0);
        ppc_psq_store(ctx, 1u, ea, false, 0u, false, 0x8100DF04u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x8100DF08u;
}

void func_8100E000(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100E000u: goto label_8100E000;
    case 0x8100E004u: goto label_8100E004;
    default: return;
    }
label_8100E000:
    ctx->pc = 0x8100E000u;
    // 8100E000: lfs     f1, 0(r4)
    {
        u32 ea = ctx->gpr[4] + (u32)(s32)(0);
        f64 value = (f64)dolrecomp_f32_from_bits(mem_read32(ctx, ea));
        ctx->fpr[1] = value;
        ctx->ps1[1] = value;
    }

label_8100E004:
    ctx->pc = 0x8100E004u;
    // 8100E004: psq_st   f1, 0(r5), 0, 0
    {
        u32 ea = ctx->gpr[5] + (u32)(s32)(0);
        ppc_psq_store(ctx, 1u, ea, false, 0u, false, 0x8100E004u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x8100E008u;
}

void func_8100E100(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100E100u: goto label_8100E100;
    case 0x8100E104u: goto label_8100E104;
    default: return;
    }
label_8100E100:
    ctx->pc = 0x8100E100u;
    // 8100E100: frsp    f1, f2
    ctx->fpr[1] = (f64)(f32)ctx->fpr[2];

label_8100E104:
    ctx->pc = 0x8100E104u;
    // 8100E104: psq_st   f1, 0(r5), 0, 0
    {
        u32 ea = ctx->gpr[5] + (u32)(s32)(0);
        ppc_psq_store(ctx, 1u, ea, false, 0u, false, 0x8100E104u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x8100E108u;
}

void func_8100E200(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100E200u: goto label_8100E200;
    case 0x8100E204u: goto label_8100E204;
    default: return;
    }
label_8100E200:
    ctx->pc = 0x8100E200u;
    // 8100E200: frsp    f1, f2
    ctx->fpr[1] = (f64)(f32)ctx->fpr[2];

label_8100E204:
    ctx->pc = 0x8100E204u;
    // 8100E204: psq_st   f1, 0(r5), 0, 0
    {
        u32 ea = ctx->gpr[5] + (u32)(s32)(0);
        ppc_psq_store(ctx, 1u, ea, false, 0u, false, 0x8100E204u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x8100E208u;
}

void func_8100E300(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100E300u: goto label_8100E300;
    case 0x8100E304u: goto label_8100E304;
    default: return;
    }
label_8100E300:
    ctx->pc = 0x8100E300u;
    // 8100E300: fmadds f17, f18, f19, f20
    {
        f64 result;
        if (ppc_fma(ctx, ctx->fpr[18], ctx->fpr[19], ctx->fpr[20], true, false, false, &result))
            ctx->fpr[17] = ctx->ps1[17] = result;
    }

label_8100E304:
    ctx->pc = 0x8100E304u;
    // 8100E304: psq_st   f17, 0(r5), 0, 0
    {
        u32 ea = ctx->gpr[5] + (u32)(s32)(0);
        ppc_psq_store(ctx, 17u, ea, false, 0u, false, 0x8100E304u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x8100E308u;
}

void func_8100E400(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100E400u: goto label_8100E400;
    case 0x8100E404u: goto label_8100E404;
    default: return;
    }
label_8100E400:
    ctx->pc = 0x8100E400u;
    // 8100E400: fmadds f17, f18, f19, f20
    {
        f64 result;
        if (ppc_fma(ctx, ctx->fpr[18], ctx->fpr[19], ctx->fpr[20], true, false, false, &result))
            ctx->fpr[17] = ctx->ps1[17] = result;
    }

label_8100E404:
    ctx->pc = 0x8100E404u;
    // 8100E404: psq_st   f17, 0(r5), 0, 0
    {
        u32 ea = ctx->gpr[5] + (u32)(s32)(0);
        ppc_psq_store(ctx, 17u, ea, false, 0u, false, 0x8100E404u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x8100E408u;
}

void func_8100E500(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100E500u: goto label_8100E500;
    case 0x8100E504u: goto label_8100E504;
    default: return;
    }
label_8100E500:
    ctx->pc = 0x8100E500u;
    // 8100E500: addic   r4, r4, -1
    {
        u64 a = ctx->gpr[4];
        u64 b = (u32)(s32)(-1);
        u64 res = a + b;
        ctx->gpr[4] = (u32)res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(res >> 32) & 1u) << 29);
    }

label_8100E504:
    ctx->pc = 0x8100E504u;
    // 8100E504: adde   r5, r5, r6
    {
        u32 carry = (ctx->xer >> 29) & 1u;
        u32 a = ctx->gpr[5];
        u32 b = ctx->gpr[6];
        u64 wide = (u64)a + (u64)b + carry;
        u32 res = (u32)wide;
        ctx->gpr[5] = res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);
    }

    ctx->pc = 0x8100E508u;
}

void func_8100E600(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100E600u: goto label_8100E600;
    case 0x8100E604u: goto label_8100E604;
    default: return;
    }
label_8100E600:
    ctx->pc = 0x8100E600u;
    // 8100E600: addic   r4, r4, -1
    {
        u64 a = ctx->gpr[4];
        u64 b = (u32)(s32)(-1);
        u64 res = a + b;
        ctx->gpr[4] = (u32)res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(res >> 32) & 1u) << 29);
    }

label_8100E604:
    ctx->pc = 0x8100E604u;
    // 8100E604: adde   r5, r5, r6
    {
        u32 carry = (ctx->xer >> 29) & 1u;
        u32 a = ctx->gpr[5];
        u32 b = ctx->gpr[6];
        u64 wide = (u64)a + (u64)b + carry;
        u32 res = (u32)wide;
        ctx->gpr[5] = res;
        ctx->xer = (ctx->xer & ~0x20000000u) | (((u32)(wide >> 32) & 1u) << 29);
    }

    ctx->pc = 0x8100E608u;
}

void func_8100E700(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100E700u: goto label_8100E700;
    case 0x8100E704u: goto label_8100E704;
    case 0x8100E708u: goto label_8100E708;
    case 0x8100E70Cu: goto label_8100E70C;
    default: return;
    }
label_8100E700:
    ctx->pc = 0x8100E700u;
    // 8100E700: cmpwi   r3, 0
    {
        s32 val_a = (s32)(ctx->gpr[3]);
        s32 val_b = (s32)(0);
        u32 cr_bits = 0;
        if (val_a < val_b)  cr_bits |= 0x8u;
        if (val_a > val_b)  cr_bits |= 0x4u;
        if (val_a == val_b) cr_bits |= 0x2u;
        cr_bits |= (ctx->xer >> 31) & 1u;
        ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_bits << 28);
    }

label_8100E704:
    ctx->pc = 0x8100E704u;
    // 8100E704: bc    12, 2, 0x8100E70C
    {
        bool ctr_ok = true;
        bool cr_ok = (((ctx->cr & 0x20000000u) != 0) == true);
        if (ctr_ok && cr_ok) {
            goto label_8100E70C;
        }
    }

label_8100E708:
    ctx->pc = 0x8100E708u;
    // 8100E708: li      r7, 1
    ctx->gpr[7] = (u32)(s32)(1);

label_8100E70C:
    ctx->pc = 0x8100E70Cu;
    // 8100E70C: li      r8, 2
    ctx->gpr[8] = (u32)(s32)(2);

    ctx->pc = 0x8100E710u;
}

void func_8100E800(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100E800u: goto label_8100E800;
    case 0x8100E804u: goto label_8100E804;
    case 0x8100E808u: goto label_8100E808;
    case 0x8100E80Cu: goto label_8100E80C;
    default: return;
    }
label_8100E800:
    ctx->pc = 0x8100E800u;
    // 8100E800: cmpwi   r3, 0
    {
        s32 val_a = (s32)(ctx->gpr[3]);
        s32 val_b = (s32)(0);
        u32 cr_bits = 0;
        if (val_a < val_b)  cr_bits |= 0x8u;
        if (val_a > val_b)  cr_bits |= 0x4u;
        if (val_a == val_b) cr_bits |= 0x2u;
        cr_bits |= (ctx->xer >> 31) & 1u;
        ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_bits << 28);
    }

label_8100E804:
    ctx->pc = 0x8100E804u;
    // 8100E804: bc    12, 2, 0x8100E80C
    {
        bool ctr_ok = true;
        bool cr_ok = (((ctx->cr & 0x20000000u) != 0) == true);
        if (ctr_ok && cr_ok) {
            goto label_8100E80C;
        }
    }

label_8100E808:
    ctx->pc = 0x8100E808u;
    // 8100E808: li      r7, 1
    ctx->gpr[7] = (u32)(s32)(1);

label_8100E80C:
    ctx->pc = 0x8100E80Cu;
    // 8100E80C: li      r8, 2
    ctx->gpr[8] = (u32)(s32)(2);

    ctx->pc = 0x8100E810u;
}

void func_8100E900(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100E900u: goto label_8100E900;
    case 0x8100E904u: goto label_8100E904;
    default: return;
    }
label_8100E900:
    ctx->pc = 0x8100E900u;
    // 8100E900: lwzu     r4, 4(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(4);
        ctx->gpr[4] = mem_read32(ctx, ea);
        ctx->gpr[1] = ea;
    }

label_8100E904:
    ctx->pc = 0x8100E904u;
    // 8100E904: stw     r4, 4(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(4);
        mem_write32(ctx, ea, (u32)ctx->gpr[4]);
    }

    ctx->pc = 0x8100E908u;
}

void func_8100EA00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100EA00u: goto label_8100EA00;
    case 0x8100EA04u: goto label_8100EA04;
    default: return;
    }
label_8100EA00:
    ctx->pc = 0x8100EA00u;
    // 8100EA00: lwzu     r4, 4(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(4);
        ctx->gpr[4] = mem_read32(ctx, ea);
        ctx->gpr[1] = ea;
    }

label_8100EA04:
    ctx->pc = 0x8100EA04u;
    // 8100EA04: stw     r4, 4(r1)
    {
        u32 ea = ctx->gpr[1] + (u32)(s32)(4);
        mem_write32(ctx, ea, (u32)ctx->gpr[4]);
    }

    ctx->pc = 0x8100EA08u;
}

void func_8100EB00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100EB00u: goto label_8100EB00;
    case 0x8100EB04u: goto label_8100EB04;
    default: return;
    }
label_8100EB00:
    ctx->pc = 0x8100EB00u;
    // 8100EB00: fadds   f1, f2, f3
    ctx->fpr[1] = (f64)(f32)(ctx->fpr[2] + ctx->fpr[3]);

label_8100EB04:
    ctx->pc = 0x8100EB04u;
    // 8100EB04: stfs     f1, 0(r5)
    {
        u32 ea = ctx->gpr[5] + (u32)(s32)(0);
        mem_write32(ctx, ea, dolrecomp_f32_to_bits((f32)ctx->fpr[1]));
    }

    ctx->pc = 0x8100EB08u;
}

void func_8100EC00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100EC00u: goto label_8100EC00;
    case 0x8100EC04u: goto label_8100EC04;
    default: return;
    }
label_8100EC00:
    ctx->pc = 0x8100EC00u;
    // 8100EC00: fadds   f1, f2, f3
    ctx->fpr[1] = (f64)(f32)(ctx->fpr[2] + ctx->fpr[3]);

label_8100EC04:
    ctx->pc = 0x8100EC04u;
    // 8100EC04: stfs     f1, 0(r5)
    {
        u32 ea = ctx->gpr[5] + (u32)(s32)(0);
        mem_write32(ctx, ea, dolrecomp_f32_to_bits((f32)ctx->fpr[1]));
    }

    ctx->pc = 0x8100EC08u;
}

void func_8100ED00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100ED00u: goto label_8100ED00;
    case 0x8100ED04u: goto label_8100ED04;
    default: return;
    }
label_8100ED00:
    ctx->pc = 0x8100ED00u;
    // 8100ED00: ps_add  f1, f2, f3
    {
        ctx->fpr[1] = dolrecomp_ps_round((f32)ctx->fpr[2] + (f32)ctx->fpr[3]);
        ctx->ps1[1] = dolrecomp_ps_round((f32)ctx->ps1[2] + (f32)ctx->ps1[3]);
    }

label_8100ED04:
    ctx->pc = 0x8100ED04u;
    // 8100ED04: psq_st   f1, 0(r5), 0, 0
    {
        u32 ea = ctx->gpr[5] + (u32)(s32)(0);
        ppc_psq_store(ctx, 1u, ea, false, 0u, false, 0x8100ED04u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x8100ED08u;
}

void func_8100EE00(CPUState* ctx) {
    switch (ctx->pc) {
    case 0x8100EE00u: goto label_8100EE00;
    case 0x8100EE04u: goto label_8100EE04;
    default: return;
    }
label_8100EE00:
    ctx->pc = 0x8100EE00u;
    // 8100EE00: ps_add  f1, f2, f3
    {
        ctx->fpr[1] = dolrecomp_ps_round((f32)ctx->fpr[2] + (f32)ctx->fpr[3]);
        ctx->ps1[1] = dolrecomp_ps_round((f32)ctx->ps1[2] + (f32)ctx->ps1[3]);
    }

label_8100EE04:
    ctx->pc = 0x8100EE04u;
    // 8100EE04: psq_st   f1, 0(r5), 0, 0
    {
        u32 ea = ctx->gpr[5] + (u32)(s32)(0);
        ppc_psq_store(ctx, 1u, ea, false, 0u, false, 0x8100EE04u);
        if (ctx->exception) return;
    }

    ctx->pc = 0x8100EE08u;
}

void (*const host_oracle_funcs[])(CPUState*) = {
    func_81000000,
    func_81000100,
    func_81000200,
    func_81000300,
    func_81000400,
    func_81000500,
    func_81000600,
    func_81000700,
    func_81000800,
    func_81000900,
    func_81000A00,
    func_81000B00,
    func_81000C00,
    func_81000D00,
    func_81000E00,
    func_81000F00,
    func_81001000,
    func_81001100,
    func_81001200,
    func_81001300,
    func_81001400,
    func_81001500,
    func_81001600,
    func_81001700,
    func_81001800,
    func_81001900,
    func_81001A00,
    func_81001B00,
    func_81001C00,
    func_81001D00,
    func_81001E00,
    func_81001F00,
    func_81002000,
    func_81002100,
    func_81002200,
    func_81002300,
    func_81002400,
    func_81002500,
    func_81002600,
    func_81002700,
    func_81002800,
    func_81002900,
    func_81002A00,
    func_81002B00,
    func_81002C00,
    func_81002D00,
    func_81002E00,
    func_81002F00,
    func_81003000,
    func_81003100,
    func_81003200,
    func_81003300,
    func_81003400,
    func_81003500,
    func_81003600,
    func_81003700,
    func_81003800,
    func_81003900,
    func_81003A00,
    func_81003B00,
    func_81003C00,
    func_81003D00,
    func_81003E00,
    func_81003F00,
    func_81004000,
    func_81004100,
    func_81004200,
    func_81004300,
    func_81004400,
    func_81004500,
    func_81004600,
    func_81004700,
    func_81004800,
    func_81004900,
    func_81004A00,
    func_81004B00,
    func_81004C00,
    func_81004D00,
    func_81004E00,
    func_81004F00,
    func_81005000,
    func_81005100,
    func_81005200,
    func_81005300,
    func_81005400,
    func_81005500,
    func_81005600,
    func_81005700,
    func_81005800,
    func_81005900,
    func_81005A00,
    func_81005B00,
    func_81005C00,
    func_81005D00,
    func_81005E00,
    func_81005F00,
    func_81006000,
    func_81006100,
    func_81006200,
    func_81006300,
    func_81006400,
    func_81006500,
    func_81006600,
    func_81006700,
    func_81006800,
    func_81006900,
    func_81006A00,
    func_81006B00,
    func_81006C00,
    func_81006D00,
    func_81006E00,
    func_81006F00,
    func_81007000,
    func_81007100,
    func_81007200,
    func_81007300,
    func_81007400,
    func_81007500,
    func_81007600,
    func_81007700,
    func_81007800,
    func_81007900,
    func_81007A00,
    func_81007B00,
    func_81007C00,
    func_81007D00,
    func_81007E00,
    func_81007F00,
    func_81008000,
    func_81008100,
    func_81008200,
    func_81008300,
    func_81008400,
    func_81008500,
    func_81008600,
    func_81008700,
    func_81008800,
    func_81008900,
    func_81008A00,
    func_81008B00,
    func_81008C00,
    func_81008D00,
    func_81008E00,
    func_81008F00,
    func_81009000,
    func_81009100,
    func_81009200,
    func_81009300,
    func_81009400,
    func_81009500,
    func_81009600,
    func_81009700,
    func_81009800,
    func_81009900,
    func_81009A00,
    func_81009B00,
    func_81009C00,
    func_81009D00,
    func_81009E00,
    func_81009F00,
    func_8100A000,
    func_8100A100,
    func_8100A200,
    func_8100A300,
    func_8100A400,
    func_8100A500,
    func_8100A600,
    func_8100A700,
    func_8100A800,
    func_8100A900,
    func_8100AA00,
    func_8100AB00,
    func_8100AC00,
    func_8100AD00,
    func_8100AE00,
    func_8100AF00,
    func_8100B000,
    func_8100B100,
    func_8100B200,
    func_8100B300,
    func_8100B400,
    func_8100B500,
    func_8100B600,
    func_8100B700,
    func_8100B800,
    func_8100B900,
    func_8100BA00,
    func_8100BB00,
    func_8100BC00,
    func_8100BD00,
    func_8100BE00,
    func_8100BF00,
    func_8100C000,
    func_8100C100,
    func_8100C200,
    func_8100C300,
    func_8100C400,
    func_8100C500,
    func_8100C600,
    func_8100C700,
    func_8100C800,
    func_8100C900,
    func_8100CA00,
    func_8100CB00,
    func_8100CC00,
    func_8100CD00,
    func_8100CE00,
    func_8100CF00,
    func_8100D000,
    func_8100D100,
    func_8100D200,
    func_8100D300,
    func_8100D400,
    func_8100D500,
    func_8100D600,
    func_8100D700,
    func_8100D800,
    func_8100D900,
    func_8100DA00,
    func_8100DB00,
    func_8100DC00,
    func_8100DD00,
    func_8100DE00,
    func_8100DF00,
    func_8100E000,
    func_8100E100,
    func_8100E200,
    func_8100E300,
    func_8100E400,
    func_8100E500,
    func_8100E600,
    func_8100E700,
    func_8100E800,
    func_8100E900,
    func_8100EA00,
    func_8100EB00,
    func_8100EC00,
    func_8100ED00,
    func_8100EE00,
};

// end
