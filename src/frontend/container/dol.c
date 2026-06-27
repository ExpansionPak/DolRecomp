// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 ExpansionPak

#include "dol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int range_fits(u32 offset, u32 size, u32 total) {
    return offset <= total && size <= total - offset;
}

static int address_in_range(u32 address, u32 base, u32 size) {
    return size != 0 && address >= base && address - base < size;
}

static int validate_section_range(const char* label, int index,
                                  u32 offset, u32 size, u32 file_size) {
    if (size == 0)
        return 1;

    if (!range_fits(offset, size, file_size)) {
        fprintf(stderr,
                "error: DOL %s section %d is outside the file\n",
                label, index);
        return 0;
    }

    return 1;
}

static int validate_dol_layout(const DOLFile* dol) {
    int entry_in_text = 0;

    if ((dol->header.entry_point & 3u) != 0) {
        fprintf(stderr, "error: DOL entry point is not instruction-aligned\n");
        return 0;
    }

    for (int i = 0; i < DOL_NUM_TEXT; i++) {
        u32 offset = dol->header.text_offsets[i];
        u32 address = dol->header.text_addresses[i];
        u32 size = dol->header.text_sizes[i];

        if (!validate_section_range("text", i, offset, size, dol->file_size))
            return 0;

        if ((size & 3u) != 0) {
            fprintf(stderr,
                    "error: DOL text section %d size is not instruction-aligned\n",
                    i);
            return 0;
        }

        if (address_in_range(dol->header.entry_point, address, size))
            entry_in_text = 1;
    }

    for (int i = 0; i < DOL_NUM_DATA; i++) {
        if (!validate_section_range("data", i, dol->header.data_offsets[i],
                                    dol->header.data_sizes[i],
                                    dol->file_size))
            return 0;
    }

    if (!entry_in_text) {
        fprintf(stderr,
                "error: DOL entry point 0x%08X is not inside a text section\n",
                dol->header.entry_point);
        return 0;
    }

    return 1;
}

bool dol_load(DOLFile* dol, const char* path) {
    memset(dol, 0, sizeof(*dol));

    FILE* f = NULL;
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: can't open '%s'\n", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < DOL_HEADER_SIZE) {
        fprintf(stderr, "error: file too small to be a DOL (%ld bytes)\n", size);
        fclose(f);
        return false;
    }

    dol->file_data = (u8*)malloc(size);
    if (!dol->file_data) {
        fprintf(stderr, "error: out of memory (%ld bytes)\n", size);
        fclose(f);
        return false;
    }

    dol->file_size = (u32)size;
    if (fread(dol->file_data, 1, size, f) != (size_t)size) {
        fprintf(stderr, "error: failed to read file\n");
        free(dol->file_data);
        dol->file_data = NULL;
        fclose(f);
        return false;
    }
    fclose(f);

    // parse header (all BE u32)
    const u8* h = dol->file_data;
    int i;

    for (i = 0; i < DOL_NUM_TEXT; i++)
        dol->header.text_offsets[i] = read_be32(h + 0x00 + i * 4);

    for (i = 0; i < DOL_NUM_DATA; i++)
        dol->header.data_offsets[i] = read_be32(h + 0x1C + i * 4);

    for (i = 0; i < DOL_NUM_TEXT; i++)
        dol->header.text_addresses[i] = read_be32(h + 0x48 + i * 4);

    for (i = 0; i < DOL_NUM_DATA; i++)
        dol->header.data_addresses[i] = read_be32(h + 0x64 + i * 4);

    for (i = 0; i < DOL_NUM_TEXT; i++)
        dol->header.text_sizes[i] = read_be32(h + 0x90 + i * 4);

    for (i = 0; i < DOL_NUM_DATA; i++)
        dol->header.data_sizes[i] = read_be32(h + 0xAC + i * 4);

    dol->header.bss_address = read_be32(h + 0xD8);
    dol->header.bss_size    = read_be32(h + 0xDC);
    dol->header.entry_point = read_be32(h + 0xE0);

    if (!validate_dol_layout(dol)) {
        dol_free(dol);
        return false;
    }

    return true;
}

void dol_free(DOLFile* dol) {
    if (dol->file_data) {
        free(dol->file_data);
        dol->file_data = NULL;
    }
    dol->file_size = 0;
}

void dol_print_info(const DOLFile* dol, const char* game_name) {
    int i;

    printf("=== DOL Info ===\n");
    printf("entry point: 0x%08X\n", dol->header.entry_point);
    if (game_name && game_name[0] != '\0')
        printf("game: %s\n", game_name);
    printf("BSS: 0x%08X (0x%X bytes)\n", dol->header.bss_address, dol->header.bss_size);
    printf("\n");

    printf("text sections:\n");
    for (i = 0; i < DOL_NUM_TEXT; i++) {
        if (dol->header.text_sizes[i] == 0) continue;
        printf("  [%d] file:0x%08X -> addr:0x%08X  size:0x%08X\n",
               i,
               dol->header.text_offsets[i],
               dol->header.text_addresses[i],
               dol->header.text_sizes[i]);
    }

    printf("data sections:\n");
    for (i = 0; i < DOL_NUM_DATA; i++) {
        if (dol->header.data_sizes[i] == 0) continue;
        printf("  [%d] file:0x%08X -> addr:0x%08X  size:0x%08X\n",
               i,
               dol->header.data_offsets[i],
               dol->header.data_addresses[i],
               dol->header.data_sizes[i]);
    }
}

const u8* dol_get_text_section(const DOLFile* dol, int index) {
    if (index < 0 || index >= DOL_NUM_TEXT) return NULL;
    if (dol->header.text_sizes[index] == 0) return NULL;
    if (!range_fits(dol->header.text_offsets[index],
                    dol->header.text_sizes[index],
                    dol->file_size))
        return NULL;
    return dol->file_data + dol->header.text_offsets[index];
}

const u8* dol_get_data_section(const DOLFile* dol, int index) {
    if (index < 0 || index >= DOL_NUM_DATA) return NULL;
    if (dol->header.data_sizes[index] == 0) return NULL;
    if (!range_fits(dol->header.data_offsets[index],
                    dol->header.data_sizes[index],
                    dol->file_size))
        return NULL;
    return dol->file_data + dol->header.data_offsets[index];
}
