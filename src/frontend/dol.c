#include "dol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool dol_load(DOLFile* dol, const char* path) {
    memset(dol, 0, sizeof(*dol));

    FILE* f = NULL;
#ifdef _MSC_VER
    if (fopen_s(&f, path, "rb") != 0 || !f) {
#else
    f = fopen(path, "rb");
    if (!f) {
#endif
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

    return true;
}

void dol_free(DOLFile* dol) {
    if (dol->file_data) {
        free(dol->file_data);
        dol->file_data = NULL;
    }
    dol->file_size = 0;
}

void dol_print_info(const DOLFile* dol) {
    int i;

    printf("=== DOL Info ===\n");
    printf("entry point: 0x%08X\n", dol->header.entry_point);
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
    if (dol->header.text_offsets[index] + dol->header.text_sizes[index] > dol->file_size)
        return NULL;
    return dol->file_data + dol->header.text_offsets[index];
}

const u8* dol_get_data_section(const DOLFile* dol, int index) {
    if (index < 0 || index >= DOL_NUM_DATA) return NULL;
    if (dol->header.data_sizes[index] == 0) return NULL;
    if (dol->header.data_offsets[index] + dol->header.data_sizes[index] > dol->file_size)
        return NULL;
    return dol->file_data + dol->header.data_offsets[index];
}
