#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    memset(cpu, 0, sizeof(*cpu));
    cpu->ram = ram;
    cpu->ram_size = ram_size;

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

u64 mem_read64(CPUState* cpu, u32 addr) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1 || offset + 8 > cpu->ram_size) {
        fprintf(stderr, "warn: read64 from unmapped 0x%08X\n", addr);
        return 0;
    }
    return read_be64(cpu->ram + offset);
}

void mem_write64(CPUState* cpu, u32 addr, u64 value) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1 || offset + 8 > cpu->ram_size) {
        fprintf(stderr, "warn: write64 to unmapped 0x%08X\n", addr);
        return;
    }
    write_be64(cpu->ram + offset, value);
}

u32 mem_read32(CPUState* cpu, u32 addr) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1 || offset + 4 > cpu->ram_size) {
        fprintf(stderr, "warn: read32 from unmapped 0x%08X\n", addr);
        return 0;
    }
    return read_be32(cpu->ram + offset);
}

void mem_write32(CPUState* cpu, u32 addr, u32 value) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1 || offset + 4 > cpu->ram_size) {
        fprintf(stderr, "warn: write32 to unmapped 0x%08X\n", addr);
        return;
    }
    write_be32(cpu->ram + offset, value);
}

u16 mem_read16(CPUState* cpu, u32 addr) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1 || offset + 2 > cpu->ram_size) {
        fprintf(stderr, "warn: read16 from unmapped 0x%08X\n", addr);
        return 0;
    }
    return read_be16(cpu->ram + offset);
}

void mem_write16(CPUState* cpu, u32 addr, u16 value) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1 || offset + 2 > cpu->ram_size) {
        fprintf(stderr, "warn: write16 to unmapped 0x%08X\n", addr);
        return;
    }
    write_be16(cpu->ram + offset, value);
}

u8 mem_read8(CPUState* cpu, u32 addr) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1) {
        fprintf(stderr, "warn: read8 from unmapped 0x%08X\n", addr);
        return 0;
    }
    return cpu->ram[offset];
}

void mem_write8(CPUState* cpu, u32 addr, u8 value) {
    u32 offset = translate_addr(addr, cpu->ram_size);
    if (offset == (u32)-1) {
        fprintf(stderr, "warn: write8 to unmapped 0x%08X\n", addr);
        return;
    }
    cpu->ram[offset] = value;
}
