#ifndef DOLRECOMP_RUNTIME_H
#define DOLRECOMP_RUNTIME_H

#include "../core/types.h"

// gekko CPU state + memory access
// recompiled code links against this

#define GC_MAIN_RAM_SIZE    (24 * 1024 * 1024)
#define GC_RAM_BASE         0x80000000u
#define GC_RAM_UNCACHED     0xC0000000u

typedef struct {
    u32 gpr[32];
    f64 fpr[32];
    u32 pc;
    u32 lr;
    u32 ctr;
    u32 cr;
    u32 xer;
    u32 fpscr;

    u8* ram;
    u32 ram_size;
} CPUState;

// lifecycle
bool cpu_init(CPUState* cpu);
void cpu_free(CPUState* cpu);
void cpu_reset(CPUState* cpu);

// memory access (handles GC address map)
u32  mem_read32(CPUState* cpu, u32 addr);
void mem_write32(CPUState* cpu, u32 addr, u32 value);
u16  mem_read16(CPUState* cpu, u32 addr);
void mem_write16(CPUState* cpu, u32 addr, u16 value);
u8   mem_read8(CPUState* cpu, u32 addr);
void mem_write8(CPUState* cpu, u32 addr, u8 value);

// load DOL sections into RAM (include dol.h first)
#ifdef DOLRECOMP_DOL_H
bool cpu_load_dol(CPUState* cpu, const DOLFile* dol);
#endif

#endif /* DOLRECOMP_RUNTIME_H */
