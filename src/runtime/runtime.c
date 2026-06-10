#include "../frontend/dol.h"
#include "runtime.h"
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
    u8*  ram      = cpu->ram;
    u32  ram_size = cpu->ram_size;

    memset(cpu, 0, sizeof(*cpu));
    cpu->ram      = ram;
    cpu->ram_size = ram_size;

    if (cpu->ram)
        memset(cpu->ram, 0, cpu->ram_size);
}

// vaddr -> RAM offset, returns -1 if unmapped
static u32 translate_addr(u32 addr, u32 ram_size) {
    // cached: 0x80000000+
    if (addr >= GC_RAM_BASE && addr < GC_RAM_BASE + ram_size)
        return addr - GC_RAM_BASE;

    // uncached mirror: 0xC0000000+
    if (addr >= GC_RAM_UNCACHED && addr < GC_RAM_UNCACHED + ram_size)
        return addr - GC_RAM_UNCACHED;

    return (u32)-1;
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
    cpu->ram[offset]     = (u8)(value >> 8);
    cpu->ram[offset + 1] = (u8)(value);
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

bool cpu_load_dol(CPUState* cpu, const DOLFile* dol) {
    int i;

    for (i = 0; i < DOL_NUM_TEXT; i++) {
        if (dol->header.text_sizes[i] == 0) continue;

        const u8* src = dol_get_text_section(dol, i);
        if (!src) {
            fprintf(stderr, "error: text[%d] data out of bounds\n", i);
            return false;
        }

        u32 offset = translate_addr(dol->header.text_addresses[i], cpu->ram_size);
        if (offset == (u32)-1 || offset + dol->header.text_sizes[i] > cpu->ram_size) {
            fprintf(stderr, "error: text[%d] addr 0x%08X out of range\n",
                    i, dol->header.text_addresses[i]);
            return false;
        }

        memcpy(cpu->ram + offset, src, dol->header.text_sizes[i]);
        printf("  loaded text[%d]: 0x%08X (0x%X bytes)\n",
               i, dol->header.text_addresses[i], dol->header.text_sizes[i]);
    }

    for (i = 0; i < DOL_NUM_DATA; i++) {
        if (dol->header.data_sizes[i] == 0) continue;

        const u8* src = dol_get_data_section(dol, i);
        if (!src) {
            fprintf(stderr, "error: data[%d] data out of bounds\n", i);
            return false;
        }

        u32 offset = translate_addr(dol->header.data_addresses[i], cpu->ram_size);
        if (offset == (u32)-1 || offset + dol->header.data_sizes[i] > cpu->ram_size) {
            fprintf(stderr, "error: data[%d] addr 0x%08X out of range\n",
                    i, dol->header.data_addresses[i]);
            return false;
        }

        memcpy(cpu->ram + offset, src, dol->header.data_sizes[i]);
        printf("  loaded data[%d]: 0x%08X (0x%X bytes)\n",
               i, dol->header.data_addresses[i], dol->header.data_sizes[i]);
    }

    // zero BSS
    if (dol->header.bss_size > 0) {
        u32 offset = translate_addr(dol->header.bss_address, cpu->ram_size);
        if (offset != (u32)-1 && offset + dol->header.bss_size <= cpu->ram_size) {
            memset(cpu->ram + offset, 0, dol->header.bss_size);
            printf("  zeroed BSS: 0x%08X (0x%X bytes)\n",
                   dol->header.bss_address, dol->header.bss_size);
        }
    }

    return true;
}
