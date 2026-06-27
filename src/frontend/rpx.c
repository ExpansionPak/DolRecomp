// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 ExpansionPak

#include "rpx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DOLRECOMP_HAVE_ZLIB
#include <zlib.h>
#endif

enum {
    ELF_HEADER_SIZE = 52,
    ELF_SECTION_SIZE = 40,
    ELF_CLASS_32 = 1,
    ELF_DATA_BE = 2,
    ELF_VERSION_CURRENT = 1,
    ELF_MACHINE_PPC = 20,
    ELF_TYPE_RPL = 0xfe01,
    SHF_EXECINSTR = 0x00000004,
    SHF_RPL_ZLIB = 0x08000000,
    SHT_PROGBITS = 1,
    SHT_NOBITS = 8,
};

typedef struct {
    u32 name_offset;
    u32 type;
    u32 flags;
    u32 address;
    u32 offset;
    u32 size;
    u32 link;
    u32 info;
    u32 alignment;
    u32 entry_size;
} RPXSectionHeader;

static int range_fits(u32 offset, u32 size, u32 total) {
    return offset <= total && size <= total - offset;
}

static int address_in_range(u32 address, u32 base, u32 size) {
    return size != 0 && address >= base && address - base < size;
}

static const char* section_name_at(const u8* names, u32 names_size, u32 offset) {
    if (offset >= names_size)
        return NULL;

    for (u32 i = offset; i < names_size; i++) {
        if (names[i] == '\0')
            return (const char*)names + offset;
    }

    return NULL;
}

static void copy_section_name(char* out, size_t out_size, const char* name) {
    if (!name || name[0] == '\0')
        name = "<anonymous>";

    snprintf(out, out_size, "%s", name);
}

static int decode_section_bytes(const RPXFile* rpx, const RPXSectionHeader* section,
                                const char* name, const u8** out_data,
                                u8** out_owned, u32* out_size) {
    *out_data = NULL;
    *out_owned = NULL;
    *out_size = 0;

    if (section->type == SHT_NOBITS) {
        *out_size = section->size;
        return 1;
    }

    if (!range_fits(section->offset, section->size, rpx->file_size)) {
        fprintf(stderr, "error: RPX section '%s' is outside the file\n",
                name ? name : "<unknown>");
        return 0;
    }

    if ((section->flags & SHF_RPL_ZLIB) == 0) {
        *out_data = rpx->file_data + section->offset;
        *out_size = section->size;
        return 1;
    }

    if (section->size < 4) {
        fprintf(stderr, "error: compressed RPX section '%s' is missing its size word\n",
                name ? name : "<unknown>");
        return 0;
    }

    u32 decoded_size = read_be32(rpx->file_data + section->offset);
    u8* decoded = (u8*)malloc(decoded_size ? decoded_size : 1);
    if (!decoded) {
        fprintf(stderr, "error: out of memory\n");
        return 0;
    }

#ifdef DOLRECOMP_HAVE_ZLIB
    uLongf dest_len = (uLongf)decoded_size;
    int status = uncompress(decoded, &dest_len,
                            rpx->file_data + section->offset + 4,
                            (uLong)section->size - 4u);
    if (status != Z_OK || dest_len != decoded_size) {
        free(decoded);
        fprintf(stderr, "error: can't decompress RPX section '%s' (zlib error %d)\n",
                name ? name : "<unknown>", status);
        return 0;
    }

    *out_data = decoded;
    *out_owned = decoded;
    *out_size = decoded_size;
    return 1;
#else
    free(decoded);
    fprintf(stderr,
            "error: RPX section '%s' is compressed; rebuild with zlib or decompress the RPX first\n",
            name ? name : "<unknown>");
    return 0;
#endif
}

static int read_section_header(const RPXFile* rpx, u32 table_offset,
                               u16 entry_size, u16 index,
                               RPXSectionHeader* out) {
    const u8* header = rpx->file_data + table_offset + (u32)index * entry_size;
    out->name_offset = read_be32(header + 0);
    out->type = read_be32(header + 4);
    out->flags = read_be32(header + 8);
    out->address = read_be32(header + 12);
    out->offset = read_be32(header + 16);
    out->size = read_be32(header + 20);
    out->link = read_be32(header + 24);
    out->info = read_be32(header + 28);
    out->alignment = read_be32(header + 32);
    out->entry_size = read_be32(header + 36);
    return 1;
}

bool rpx_load(RPXFile* rpx, const char* path) {
    memset(rpx, 0, sizeof(*rpx));

    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "error: can't open '%s'\n", path);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        fprintf(stderr, "error: can't measure '%s'\n", path);
        return false;
    }

    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        fprintf(stderr, "error: can't measure '%s'\n", path);
        return false;
    }

    if (size < ELF_HEADER_SIZE) {
        fclose(file);
        fprintf(stderr, "error: file too small to be an RPX (%ld bytes)\n", size);
        return false;
    }

    rpx->file_data = (u8*)malloc((size_t)size);
    if (!rpx->file_data) {
        fclose(file);
        fprintf(stderr, "error: out of memory\n");
        return false;
    }

    rpx->file_size = (u32)size;
    if (fread(rpx->file_data, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        rpx_free(rpx);
        fprintf(stderr, "error: failed to read '%s'\n", path);
        return false;
    }
    fclose(file);

    const u8* h = rpx->file_data;
    if (memcmp(h, "\x7f" "ELF", 4) != 0 ||
        h[4] != ELF_CLASS_32 ||
        h[5] != ELF_DATA_BE ||
        h[6] != ELF_VERSION_CURRENT) {
        fprintf(stderr, "error: '%s' is not a big-endian ELF32 RPX\n", path);
        rpx_free(rpx);
        return false;
    }

    if (read_be16(h + 16) != ELF_TYPE_RPL ||
        read_be16(h + 18) != ELF_MACHINE_PPC) {
        fprintf(stderr, "error: '%s' is not a Wii U PowerPC RPX/RPL\n", path);
        rpx_free(rpx);
        return false;
    }

    rpx->entry_point = read_be32(h + 24);
    if ((rpx->entry_point & 3u) != 0) {
        fprintf(stderr, "error: RPX entry point is not instruction-aligned\n");
        rpx_free(rpx);
        return false;
    }

    u32 section_offset = read_be32(h + 32);
    u16 section_entry_size = read_be16(h + 46);
    rpx->section_count = read_be16(h + 48);
    u16 section_names_index = read_be16(h + 50);

    if (section_entry_size != ELF_SECTION_SIZE ||
        section_names_index >= rpx->section_count) {
        fprintf(stderr, "error: unsupported RPX section table\n");
        rpx_free(rpx);
        return false;
    }

    u32 table_size = (u32)section_entry_size * rpx->section_count;
    if (!range_fits(section_offset, table_size, rpx->file_size)) {
        fprintf(stderr, "error: RPX section table is outside the file\n");
        rpx_free(rpx);
        return false;
    }

    RPXSectionHeader* sections =
        (RPXSectionHeader*)calloc(rpx->section_count, sizeof(RPXSectionHeader));
    if (!sections) {
        fprintf(stderr, "error: out of memory\n");
        rpx_free(rpx);
        return false;
    }

    for (u16 i = 0; i < rpx->section_count; i++) {
        read_section_header(rpx, section_offset, section_entry_size, i, &sections[i]);
    }

    const u8* names = NULL;
    u8* owned_names = NULL;
    u32 names_size = 0;
    if (!decode_section_bytes(rpx, &sections[section_names_index],
                              "<section-names>", &names, &owned_names,
                              &names_size)) {
        free(sections);
        rpx_free(rpx);
        return false;
    }

    for (u16 i = 0; i < rpx->section_count; i++) {
        RPXSectionHeader* section = &sections[i];
        const char* name = section_name_at(names, names_size, section->name_offset);

        if (section->type != SHT_NOBITS && section->size &&
            !range_fits(section->offset, section->size, rpx->file_size)) {
            fprintf(stderr, "error: RPX section %u is outside the file\n", i);
            free(owned_names);
            free(sections);
            rpx_free(rpx);
            return false;
        }

        if (section->type == SHT_PROGBITS &&
            (section->flags & SHF_EXECINSTR) &&
            section->size != 0) {
            if (rpx->code_section_count >= RPX_MAX_CODE_SECTIONS) {
                fprintf(stderr, "error: too many RPX executable sections\n");
                free(owned_names);
                free(sections);
                rpx_free(rpx);
                return false;
            }

            const u8* data = NULL;
            u8* owned = NULL;
            u32 decoded_size = 0;
            if (!decode_section_bytes(rpx, section, name, &data, &owned,
                                      &decoded_size)) {
                free(owned_names);
                free(sections);
                rpx_free(rpx);
                return false;
            }

            if ((decoded_size & 3u) != 0) {
                fprintf(stderr,
                        "error: executable RPX section '%s' size is not instruction-aligned\n",
                        name ? name : "<unknown>");
                free(owned);
                free(owned_names);
                free(sections);
                rpx_free(rpx);
                return false;
            }

            RPXCodeSection* code = &rpx->code_sections[rpx->code_section_count++];
            copy_section_name(code->name, sizeof(code->name), name);
            code->offset = section->offset;
            code->address = section->address;
            code->size = decoded_size;
            code->flags = section->flags;
            code->compressed = (section->flags & SHF_RPL_ZLIB) != 0;
            code->data = data;
            code->owned_data = owned;
        }
    }

    free(owned_names);
    free(sections);

    if (rpx->code_section_count == 0) {
        fprintf(stderr, "error: RPX has no executable code sections\n");
        rpx_free(rpx);
        return false;
    }

    int entry_in_code = 0;
    for (u32 i = 0; i < rpx->code_section_count; i++) {
        const RPXCodeSection* section = &rpx->code_sections[i];
        if (address_in_range(rpx->entry_point, section->address, section->size)) {
            entry_in_code = 1;
            break;
        }
    }
    if (!entry_in_code) {
        fprintf(stderr,
                "error: RPX entry point 0x%08X is not inside an executable section\n",
                rpx->entry_point);
        rpx_free(rpx);
        return false;
    }

    return true;
}

void rpx_free(RPXFile* rpx) {
    for (u32 i = 0; i < rpx->code_section_count; i++) {
        free(rpx->code_sections[i].owned_data);
    }
    free(rpx->file_data);
    memset(rpx, 0, sizeof(*rpx));
}

void rpx_print_info(const RPXFile* rpx, const char* game_name) {
    printf("=== RPX Info ===\n");
    printf("entry point: 0x%08X\n", rpx->entry_point);
    if (game_name && game_name[0] != '\0')
        printf("game: %s\n", game_name);
    printf("sections: %u\n", rpx->section_count);
    printf("\n");

    printf("code sections:\n");
    for (u32 i = 0; i < rpx->code_section_count; i++) {
        const RPXCodeSection* section = &rpx->code_sections[i];
        printf("  [%u] %-20s file:0x%08X -> addr:0x%08X  size:0x%08X%s\n",
               i, section->name, section->offset, section->address,
               section->size, section->compressed ? " compressed" : "");
    }
}
