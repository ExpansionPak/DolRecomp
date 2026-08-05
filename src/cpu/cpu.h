#ifndef DOLRECOMP_CPU_H
#define DOLRECOMP_CPU_H

#include "common/types.h"

#define GC_MAIN_RAM_SIZE    (24 * 1024 * 1024)
#define GC_RAM_BASE         0x80000000u
#define GC_RAM_UNCACHED     0xC0000000u

#define WII_MEM2_SIZE       (64 * 1024 * 1024)
#define WII_MEM2_BASE       0x90000000u
#define WII_MEM2_UNCACHED   0xD0000000u

#define PPC_EXC_PROGRAM       0x00000001u
#define PPC_EXC_DSI           0x00000002u
#define PPC_EXC_ALIGNMENT     0x00000004u
#define PPC_EXC_SYSTEM_CALL   0x00000008u
#define PPC_EXC_MACHINE_CHECK 0x00000010u
#define PPC_EXC_FP_UNAVAILABLE 0x00000020u

#define PPC_PROGRAM_FP        0x00100000u
#define PPC_PROGRAM_ILLEGAL   0x00080000u
#define PPC_PROGRAM_PRIV      0x00040000u
#define PPC_PROGRAM_TRAP      0x00020000u

#define PPC_DSI_EAR_DISABLED  0x00100000u

#define PPC_VECTOR_MACHINE_CHECK 0x00200u
#define PPC_VECTOR_DSI           0x00300u
#define PPC_VECTOR_ALIGNMENT     0x00600u
#define PPC_VECTOR_PROGRAM       0x00700u
#define PPC_VECTOR_FP_UNAVAILABLE 0x00800u
#define PPC_VECTOR_SYSTEM_CALL   0x00C00u

#define PPC_HID2_LSQE   0x80000000u
#define PPC_HID2_PSE    0x20000000u
#define PPC_HID2_LCE    0x10000000u
#define PPC_HID2_DCHERR 0x00800000u
#define PPC_HID2_DCHEE  0x00080000u

#define PPC_GEKKO_PVR 0x00083214u

typedef struct CPUState CPUState;
typedef u64 (*PPCExternalRead)(CPUState* cpu, u32 ea, u8 size);
typedef void (*PPCExternalWrite)(CPUState* cpu, u32 ea, u64 value, u8 size);
typedef u32 (*PPCExternalRead32)(CPUState* cpu, u32 ea, u8 rid);
typedef void (*PPCExternalWrite32)(CPUState* cpu, u32 ea, u32 value, u8 rid);
typedef void* (*PPCExternalPointer)(CPUState* cpu, u32 ea, u32 size);
typedef void (*PPCInstructionFallback)(CPUState* cpu, u32 raw, u32 cia);
typedef bool (*PPCHostCall)(CPUState* cpu, u32 address);
typedef void (*PPCCacheControl)(CPUState* cpu, u8 operation, u32 ea, u32 cia);

enum {
    PPC_CACHE_DCBST,
    PPC_CACHE_DCBF,
    PPC_CACHE_DCBI,
    PPC_CACHE_ICBI,
};

struct CPUState {
    u32 gpr[32];
    f64 fpr[32];
    f64 ps1[32];
    u32 pc;
    u32 lr;
    u32 ctr;
    u32 cr;
    u32 xer;
    u32 fpscr;
    u32 msr;
    u32 srr0;
    u32 srr1;
    u32 dar;
    u32 dsisr;
    u32 ear;
    u32 hid2;
    u64 timebase;
    u32 sr[16];
    u32 gqr[8];
    u32 exception;
    u32 program_exception;
    u32 tlb_last_vps;
    u32 tlb_last_index;
    u32 tlb_invalidate_count;
    u32 external_addr;
    u32 external_value;
    u8 external_rid;
    u8 external_read_count;
    u8 external_write_count;
    u32 reserve_addr;
    bool reserve_valid;
    u32 locked_cache_tag[512];
    bool locked_cache_valid[512];
    PPCExternalRead external_read;
    PPCExternalWrite external_write;
    PPCExternalRead32 external_read32;
    PPCExternalWrite32 external_write32;
    PPCInstructionFallback instruction_fallback;
    PPCHostCall host_call;
    void* external_user_data;

    u8* ram;
    u32 ram_size;
    PPCExternalPointer external_pointer;
    s64 downcount;
    union {
        u8* exram;
        u8* mem2;
    };
    union {
        u32 exram_size;
        u32 mem2_size;
    };

    u32 spr[1024];
    PPCCacheControl cache_control;
};

typedef void (*PPCMemWriteJournal)(u32 offset, u32 size, void* user);
extern PPCMemWriteJournal g_mem_write_journal;
extern void* g_mem_write_journal_user;
void ppc_set_mem_write_journal(PPCMemWriteJournal fn, void* user);

bool cpu_init(CPUState* cpu);
bool cpu_alloc_mem2(CPUState* cpu, u32 size); //mem 2 only exists after first aloc
void cpu_free(CPUState* cpu);
void cpu_reset(CPUState* cpu);

u64  mem_read64(CPUState* cpu, u32 addr);
void mem_write64(CPUState* cpu, u32 addr, u64 value);
u32  mem_read32(CPUState* cpu, u32 addr);
void mem_write32(CPUState* cpu, u32 addr, u32 value);
u16  mem_read16(CPUState* cpu, u32 addr);
void mem_write16(CPUState* cpu, u32 addr, u16 value);
u8   mem_read8(CPUState* cpu, u32 addr);
void mem_write8(CPUState* cpu, u32 addr, u8 value);

f64 ppc_approx_reciprocal(f64 value);
f64 ppc_approx_rsqrt(f64 value);
bool ppc_fres(CPUState* cpu, f64 value, f64* result);
bool ppc_frsqrte(CPUState* cpu, f64 value, f64* result);
void ppc_ps_res(CPUState* cpu, f64 a, f64 b, f64* result_a, f64* result_b);
void ppc_ps_rsqrte(CPUState* cpu, f64 a, f64 b, f64* result_a, f64* result_b);
bool ppc_fma(CPUState* cpu, f64 a, f64 c, f64 b, bool single,
             bool subtract, bool negative, f64* output);
bool ppc_fctiw(CPUState* cpu, f64 value, bool toward_zero, u64* result);
void ppc_fcmp(CPUState* cpu, u8 crfd, f64 a, f64 b, bool ordered);
void ppc_fadds(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fsubs(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fmuls(CPUState* cpu, u8 d, u8 a, u8 c);
void ppc_fdivs(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fadd(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fsub(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fmul(CPUState* cpu, u8 d, u8 a, u8 c);
void ppc_fdiv(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_frsp(CPUState* cpu, u8 d, u8 b);
void ppc_ps_add_op(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_ps_sub_op(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_ps_mul_op(CPUState* cpu, u8 d, u8 a, u8 c);
void ppc_ps_div_op(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_ps_madd_op(CPUState* cpu, u8 d, u8 a, u8 c, u8 b,
                    bool subtract, bool negative);
void ppc_ps_madds0(CPUState* cpu, u8 d, u8 a, u8 c, u8 b);
void ppc_ps_madds1(CPUState* cpu, u8 d, u8 a, u8 c, u8 b);
void ppc_ps_sum0(CPUState* cpu, u8 d, u8 a, u8 c, u8 b);
void ppc_ps_sum1(CPUState* cpu, u8 d, u8 a, u8 c, u8 b);
void ppc_ps_muls0(CPUState* cpu, u8 d, u8 a, u8 c);
void ppc_ps_muls1(CPUState* cpu, u8 d, u8 a, u8 c);
void ppc_ps_res_op(CPUState* cpu, u8 d, u8 b);
void ppc_ps_rsqrte_op(CPUState* cpu, u8 d, u8 b);
void ppc_stwcx_op(CPUState* cpu, u8 s, u32 ea, u32 cia);
bool ppc_add_overflowed(u32 a, u32 b, u32 result);
bool ppc_trap_condition(u8 to, u32 a, u32 b);
void ppc_set_xer_ov(CPUState* cpu, bool ov);
void ppc_take_exception(CPUState* cpu, u32 exception, u32 vector, u32 srr0, u32 srr1_info);
void ppc_program_exception(CPUState* cpu, u32 cause, u32 cia);
bool ppc_fp_available(CPUState* cpu, u32 cia);

/* MSR[FP] spelled out rather than via PPC_MSR_FP: that macro is defined in
   cpu.c, not here, and defining it in the header would collide with it. */
static inline bool ppc_fp_available_inline(CPUState* cpu, u32 cia) {
    if (cpu->msr & 0x00002000u) /* MSR[FP], PPC bit 18 */
        return true;
    return ppc_fp_available(cpu, cia);
}
void ppc_fallback_instruction(CPUState* cpu, u32 raw, u32 cia);
bool ppc_host_call(CPUState* cpu, u32 address);
void ppc_system_call_exception(CPUState* cpu, u32 cia);
void ppc_dsi_exception(CPUState* cpu, u32 ea, u32 cia, u32 dsisr);
void ppc_alignment_exception(CPUState* cpu, u32 ea, u32 cia);
u32 ppc_mftb(CPUState* cpu, u16 tbr, u32 cia);
u32 ppc_mfspr(CPUState* cpu, u16 spr, u32 cia);
void ppc_mtspr(CPUState* cpu, u16 spr, u32 value, u32 cia);
void ppc_rfi(CPUState* cpu, u32 cia);
void ppc_dcbz_l(CPUState* cpu, u32 ea, u32 cia);
bool ppc_psq_load(CPUState* cpu, u8 frD, u32 ea, bool w, u8 gqr, bool indexed, u32 cia);
bool ppc_psq_store(CPUState* cpu, u8 frS, u32 ea, bool w, u8 gqr, bool indexed, u32 cia);

/* The emitter emits the _inline forms so the hosting runtime can provide a fast
   path (GXRuntime does, for the unquantised GQR type-0 case). This standalone
   runtime backs the tests rather than a shipping port, so it simply forwards. */
static inline bool ppc_psq_load_inline(CPUState* cpu, u8 frD, u32 ea, bool w,
                                       u8 gqr, bool indexed, u32 cia) {
    return ppc_psq_load(cpu, frD, ea, w, gqr, indexed, cia);
}

static inline bool ppc_psq_store_inline(CPUState* cpu, u8 frS, u32 ea, bool w,
                                        u8 gqr, bool indexed, u32 cia) {
    return ppc_psq_store(cpu, frS, ea, w, gqr, indexed, cia);
}
u32 ppc_eciwx(CPUState* cpu, u32 ea, u32 cia);
void ppc_ecowx(CPUState* cpu, u32 ea, u32 value, u32 cia);
void ppc_tlbie(CPUState* cpu, u32 ea, u32 cia);
void ppc_fpscr_updated(CPUState* cpu);
void ppc_fpscr_control_updated(CPUState* cpu);
void ppc_mtfsb0_op(CPUState* cpu, u8 bit);
void ppc_mtfsb1_op(CPUState* cpu, u8 bit);
void ppc_lswx(CPUState* cpu, u8 rD, u8 rA, u8 rB, u32 cia);
void ppc_cache_control(CPUState* cpu, u8 operation, u32 ea, u32 cia);
void ppc_memory_fence(void);

#endif /* DOLRECOMP_CPU_H */
