#ifndef DOLRECOMP_CPU_H
#define DOLRECOMP_CPU_H

#include "types.h"

// Minimal CPU support ABI for generated code and CPU tests.

#define GC_MAIN_RAM_SIZE    (24 * 1024 * 1024)
#define GC_RAM_BASE         0x80000000u
#define GC_RAM_UNCACHED     0xC0000000u

typedef struct {
    u32 gpr[32];
    f64 fpr[32];
    f64 ps1[32];
    u32 pc;
    u32 lr;
    u32 ctr;
    u32 cr;
    u32 xer;
    u32 fpscr;

    u8* ram;
    u32 ram_size;
} CPUState;

bool cpu_init(CPUState* cpu);
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

#endif /* DOLRECOMP_CPU_H */
