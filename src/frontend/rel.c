#include "rel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    REL_HEADER_MIN_SIZE = 0x40,
    REL_SECTION_ENTRY_SIZE = 8,
    REL_IMPORT_ENTRY_SIZE = 8,
    REL_RELOCATION_ENTRY_SIZE = 8,
    REL_MAX_SECTION_COUNT = 4096,

    R_PPC_NONE = 0,
    R_PPC_ADDR32 = 1,
    R_PPC_ADDR24 = 2,
    R_PPC_ADDR16 = 3,
    R_PPC_ADDR16_LO = 4,
    R_PPC_ADDR16_HI = 5,
    R_PPC_ADDR16_HA = 6,
    R_PPC_ADDR14 = 7,
    R_PPC_ADDR14_BRTAKEN = 8,
    R_PPC_ADDR14_BRNTAKEN = 9,
    R_PPC_REL24 = 10,
    R_PPC_REL14 = 11,
    R_DOLPHIN_NOP = 201,
    R_DOLPHIN_SECTION = 202,
    R_DOLPHIN_END = 203,
};

static int range_fits(u32 offset, u32 size, u32 total) {
    return offset <= total && size <= total - offset;
}

static u32 align_up_u32(u32 value, u32 alignment, int* ok) {
    u64 result;

    if (alignment <= 1)
        return value;

    result = ((u64)value + alignment - 1u) / alignment * alignment;
    if (result > 0xFFFFFFFFu) {
        *ok = 0;
        return 0;
    }

    return (u32)result;
}

static int add_u32_checked(u32 a, u32 b, u32* out) {
    if (b > 0xFFFFFFFFu - a)
        return 0;
    *out = a + b;
    return 1;
}

static int read_file(RELFile* rel, const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "error: can't open '%s'\n", path);
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        fprintf(stderr, "error: can't measure '%s'\n", path);
        return 0;
    }

    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        fprintf(stderr, "error: can't measure '%s'\n", path);
        return 0;
    }

    if (size < REL_HEADER_MIN_SIZE) {
        fclose(file);
        fprintf(stderr, "error: file too small to be a REL (%ld bytes)\n", size);
        return 0;
    }

    rel->file_data = (u8*)malloc((size_t)size);
    if (!rel->file_data) {
        fclose(file);
        fprintf(stderr, "error: out of memory\n");
        return 0;
    }

    rel->file_size = (u32)size;
    if (fread(rel->file_data, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        rel_free(rel);
        fprintf(stderr, "error: failed to read '%s'\n", path);
        return 0;
    }

    fclose(file);
    return 1;
}

static int section_has_file_data(const RELSection* section) {
    return section->size != 0 && !section->bss;
}

static int section_address_at(const RELFile* rel, u32 section_index,
                              u32 offset, u32* out) {
    if (section_index >= rel->section_count) {
        fprintf(stderr, "error: REL relocation references bad section %u\n",
                section_index);
        return 0;
    }

    const RELSection* section = &rel->sections[section_index];
    if (section->size == 0 || offset > section->size) {
        fprintf(stderr,
                "error: REL relocation references section %u outside its range\n",
                section_index);
        return 0;
    }

    return add_u32_checked(section->address, offset, out);
}

static int validate_entry(const RELFile* rel, u32 section_index, u32 offset,
                          const char* name) {
    if (section_index == 0)
        return 1;
    if (section_index >= rel->section_count) {
        fprintf(stderr, "error: REL %s section index %u is invalid\n",
                name, section_index);
        return 0;
    }

    const RELSection* section = &rel->sections[section_index];
    if (!section->executable || section->bss || !section->data) {
        fprintf(stderr, "error: REL %s is not inside executable code\n", name);
        return 0;
    }
    if ((offset & 3u) != 0 || offset >= section->size) {
        fprintf(stderr, "error: REL %s offset is not a valid instruction\n", name);
        return 0;
    }

    return 1;
}

static const RELFile* find_module(const RELModuleMap* modules, u32 module_id) {
    if (!modules)
        return NULL;

    for (u32 i = 0; i < modules->count; i++) {
        if (modules->entries[i].module_id == module_id)
            return modules->entries[i].rel;
    }

    return NULL;
}

static int relocation_target(const RELFile* rel, const RELModuleMap* modules,
                             u32 import_module,
                             u8 section_index, u32 symbol_offset, u32* target) {
    if (import_module == 0) {
        *target = symbol_offset;
        return 1;
    }

    if (import_module != rel->module_id) {
        const RELFile* imported = find_module(modules, import_module);
        if (!imported) {
            fprintf(stderr,
                    "error: REL imports module %u, but that module was not loaded\n",
                    import_module);
            return 0;
        }
        return section_address_at(imported, section_index, symbol_offset, target);
    }

    return section_address_at(rel, section_index, symbol_offset, target);
}

static int require_patch_range(const RELSection* section, u32 offset) {
    if (!section_has_file_data(section) || offset > section->size ||
        section->size - offset < 4) {
        fprintf(stderr, "error: REL relocation patch is outside section %u\n",
                section->index);
        return 0;
    }

    return 1;
}

static int check_rel_delta(s64 delta, s64 min, s64 max, const char* type) {
    if ((delta & 3) != 0 || delta < min || delta > max) {
        fprintf(stderr, "error: REL %s relocation target is out of range\n", type);
        return 0;
    }
    return 1;
}

static int apply_relocation(RELSection* section, u32 offset, u32 patch_address,
                            u8 type, u32 target) {
    if (type == R_PPC_NONE)
        return 1;

    if (!require_patch_range(section, offset))
        return 0;

    u8* patch = section->owned_data + offset;
    u32 word = read_be32(patch);

    switch (type) {
    case R_PPC_ADDR32:
        write_be32(patch, target);
        return 1;
    case R_PPC_ADDR24:
        if ((target & 3u) != 0) {
            fprintf(stderr, "error: REL ADDR24 target is not aligned\n");
            return 0;
        }
        write_be32(patch, (word & 0xFC000003u) | (target & 0x03FFFFFCu));
        return 1;
    case R_PPC_ADDR16:
    case R_PPC_ADDR16_LO:
        write_be16(patch + 2, (u16)target);
        return 1;
    case R_PPC_ADDR16_HI:
        write_be16(patch + 2, (u16)(target >> 16));
        return 1;
    case R_PPC_ADDR16_HA:
        write_be16(patch + 2, (u16)((target + 0x8000u) >> 16));
        return 1;
    case R_PPC_ADDR14:
    case R_PPC_ADDR14_BRTAKEN:
    case R_PPC_ADDR14_BRNTAKEN:
        if ((target & 3u) != 0) {
            fprintf(stderr, "error: REL ADDR14 target is not aligned\n");
            return 0;
        }
        write_be32(patch, (word & 0xFFFF0003u) | (target & 0x0000FFFCu));
        return 1;
    case R_PPC_REL24: {
        s64 delta = (s64)(s32)(target - patch_address);
        if (!check_rel_delta(delta, -0x02000000ll, 0x01FFFFFCll, "REL24"))
            return 0;
        write_be32(patch, (word & 0xFC000003u) | ((u32)delta & 0x03FFFFFCu));
        return 1;
    }
    case R_PPC_REL14: {
        s64 delta = (s64)(s32)(target - patch_address);
        if (!check_rel_delta(delta, -0x8000ll, 0x7FFCll, "REL14"))
            return 0;
        write_be32(patch, (word & 0xFFFF0003u) | ((u32)delta & 0x0000FFFCu));
        return 1;
    }
    default:
        fprintf(stderr, "error: unsupported REL relocation type %u\n", type);
        return 0;
    }
}

bool rel_apply_relocations(RELFile* rel, const RELModuleMap* modules) {
    u32 relocation_offset;
    u32 import_offset;
    u32 import_size;

    if (!rel)
        return false;

    relocation_offset = rel->relocation_offset;
    import_offset = rel->import_offset;
    import_size = rel->import_size;
    rel->relocation_count = 0;

    if (import_size == 0)
        return true;
    if ((import_size % REL_IMPORT_ENTRY_SIZE) != 0 ||
        !range_fits(import_offset, import_size, rel->file_size)) {
        fprintf(stderr, "error: REL import table is outside the file\n");
        return false;
    }

    rel->import_count = import_size / REL_IMPORT_ENTRY_SIZE;
    for (u32 import_index = 0; import_index < rel->import_count; import_index++) {
        const u8* import = rel->file_data + import_offset +
                           import_index * REL_IMPORT_ENTRY_SIZE;
        u32 import_module = read_be32(import);
        u32 cursor = read_be32(import + 4);
        u32 current_section_index = 0;
        u32 current_offset = 0;
        int saw_end = 0;

        if (!range_fits(cursor, REL_RELOCATION_ENTRY_SIZE, rel->file_size) ||
            cursor < relocation_offset) {
            fprintf(stderr, "error: REL import %u points outside relocations\n",
                    import_index);
            return 0;
        }

        while (range_fits(cursor, REL_RELOCATION_ENTRY_SIZE, rel->file_size)) {
            const u8* entry = rel->file_data + cursor;
            u16 offset_delta = read_be16(entry);
            u8 type = entry[2];
            u8 section_index = entry[3];
            u32 symbol_offset = read_be32(entry + 4);
            cursor += REL_RELOCATION_ENTRY_SIZE;

            if (type == R_DOLPHIN_SECTION) {
                current_section_index = section_index;
                current_offset = 0;
                if (current_section_index >= rel->section_count) {
                    fprintf(stderr, "error: REL relocation selects bad section %u\n",
                            current_section_index);
                    return 0;
                }
                continue;
            }

            if (type == R_DOLPHIN_END) {
                saw_end = 1;
                break;
            }

            if (!add_u32_checked(current_offset, offset_delta, &current_offset)) {
                fprintf(stderr, "error: REL relocation offset overflow\n");
                return 0;
            }

            if (type == R_DOLPHIN_NOP)
                continue;

            if (current_section_index >= rel->section_count) {
                fprintf(stderr, "error: REL relocation has no target section\n");
                return 0;
            }

            RELSection* patch_section = &rel->sections[current_section_index];
            u32 target;
            u32 patch_address;
            if (!relocation_target(rel, modules, import_module, section_index,
                                   symbol_offset, &target) ||
                !section_address_at(rel, current_section_index, current_offset,
                                    &patch_address) ||
                !apply_relocation(patch_section, current_offset, patch_address,
                                  type, target)) {
                return false;
            }
            rel->relocation_count++;
        }

        if (!saw_end) {
            fprintf(stderr, "error: REL import %u relocation list has no end\n",
                    import_index);
            return false;
        }
    }

    return true;
}

bool rel_load_image(RELFile* rel, const char* path, u32 base_address) {
    memset(rel, 0, sizeof(*rel));

    if ((base_address & 3u) != 0) {
        fprintf(stderr, "error: REL base address is not instruction-aligned\n");
        return false;
    }

    if (!read_file(rel, path))
        return false;

    const u8* h = rel->file_data;
    rel->module_id = read_be32(h + 0x00);
    rel->section_count = read_be32(h + 0x0C);
    u32 section_info_offset = read_be32(h + 0x10);
    rel->version = read_be32(h + 0x1C);
    rel->bss_size = read_be32(h + 0x20);
    rel->relocation_offset = read_be32(h + 0x24);
    rel->import_offset = read_be32(h + 0x28);
    rel->import_size = read_be32(h + 0x2C);
    rel->prolog_section = h[0x30];
    rel->epilog_section = h[0x31];
    rel->unresolved_section = h[0x32];
    rel->prolog_offset = read_be32(h + 0x34);
    rel->epilog_offset = read_be32(h + 0x38);
    rel->unresolved_offset = read_be32(h + 0x3C);
    u32 section_alignment = rel->file_size >= 0x44 ? read_be32(h + 0x40) : 4;
    u32 bss_alignment = rel->file_size >= 0x48 ? read_be32(h + 0x44) : 4;
    rel->base_address = base_address;

    if (rel->section_count == 0 ||
        rel->section_count > REL_MAX_SECTION_COUNT ||
        !range_fits(section_info_offset,
                    rel->section_count * REL_SECTION_ENTRY_SIZE,
                    rel->file_size)) {
        fprintf(stderr, "error: REL section table is outside the file\n");
        rel_free(rel);
        return false;
    }

    rel->sections = (RELSection*)calloc(rel->section_count, sizeof(RELSection));
    if (!rel->sections) {
        fprintf(stderr, "error: out of memory\n");
        rel_free(rel);
        return false;
    }

    int ok = 1;
    u32 bss_start;
    if (!add_u32_checked(base_address, rel->file_size, &bss_start)) {
        fprintf(stderr, "error: REL BSS address overflow\n");
        rel_free(rel);
        return false;
    }
    u32 bss_cursor = align_up_u32(bss_start,
                                  bss_alignment ? bss_alignment : 4, &ok);
    if (!ok) {
        fprintf(stderr, "error: REL BSS address overflow\n");
        rel_free(rel);
        return false;
    }

    u32 executable_count = 0;
    for (u32 i = 0; i < rel->section_count; i++) {
        const u8* entry = rel->file_data + section_info_offset +
                          i * REL_SECTION_ENTRY_SIZE;
        u32 raw_offset = read_be32(entry);
        u32 size = read_be32(entry + 4);
        u32 offset = raw_offset & ~1u;
        int executable = (raw_offset & 1u) != 0;
        RELSection* section = &rel->sections[i];

        section->index = i;
        section->offset = offset;
        section->size = size;
        section->executable = executable;
        section->bss = size != 0 && offset == 0 && !executable;

        if (size == 0)
            continue;

        if (section->bss) {
            section->address = bss_cursor;
            if (!add_u32_checked(bss_cursor, size, &bss_cursor)) {
                fprintf(stderr, "error: REL BSS address overflow\n");
                rel_free(rel);
                return false;
            }
            bss_cursor = align_up_u32(bss_cursor,
                                      bss_alignment ? bss_alignment : 4, &ok);
            if (!ok) {
                fprintf(stderr, "error: REL BSS address overflow\n");
                rel_free(rel);
                return false;
            }
            continue;
        }

        if (!range_fits(offset, size, rel->file_size)) {
            fprintf(stderr, "error: REL section %u is outside the file\n", i);
            rel_free(rel);
            return false;
        }

        if (executable && ((size & 3u) != 0 || (offset & 3u) != 0)) {
            fprintf(stderr,
                    "error: REL executable section %u is not instruction-aligned\n",
                    i);
            rel_free(rel);
            return false;
        }

        if (!add_u32_checked(base_address, offset, &section->address)) {
            fprintf(stderr, "error: REL section address overflow\n");
            rel_free(rel);
            return false;
        }
        if (section_alignment > 1 && executable &&
            (section->address % section_alignment) != 0) {
            fprintf(stderr, "error: REL executable section %u is not aligned\n", i);
            rel_free(rel);
            return false;
        }

        section->owned_data = (u8*)malloc(size);
        if (!section->owned_data) {
            fprintf(stderr, "error: out of memory\n");
            rel_free(rel);
            return false;
        }
        memcpy(section->owned_data, rel->file_data + offset, size);
        section->data = section->owned_data;
        if (executable)
            executable_count++;
    }

    if (executable_count == 0) {
        fprintf(stderr, "error: REL has no executable sections\n");
        rel_free(rel);
        return false;
    }

    if (!validate_entry(rel, rel->prolog_section, rel->prolog_offset, "prolog") ||
        !validate_entry(rel, rel->epilog_section, rel->epilog_offset, "epilog") ||
        !validate_entry(rel, rel->unresolved_section, rel->unresolved_offset,
                        "unresolved")) {
        rel_free(rel);
        return false;
    }

    if (rel->prolog_section != 0) {
        if (!section_address_at(rel, rel->prolog_section, rel->prolog_offset,
                                &rel->entry_point)) {
            rel_free(rel);
            return false;
        }
    }

    return true;
}

bool rel_load(RELFile* rel, const char* path, u32 base_address) {
    if (!rel_load_image(rel, path, base_address))
        return false;

    RELModuleMapEntry self_entry = { rel->module_id, rel };
    RELModuleMap map = { &self_entry, 1 };
    if (!rel_apply_relocations(rel, &map)) {
        rel_free(rel);
        return false;
    }

    return true;
}

void rel_free(RELFile* rel) {
    if (rel->sections) {
        for (u32 i = 0; i < rel->section_count; i++)
            free(rel->sections[i].owned_data);
    }
    free(rel->sections);
    free(rel->file_data);
    memset(rel, 0, sizeof(*rel));
}

void rel_print_info(const RELFile* rel, const char* game_name) {
    printf("=== REL Info ===\n");
    printf("module id: %u\n", rel->module_id);
    printf("version: %u\n", rel->version);
    printf("base: 0x%08X\n", rel->base_address);
    if (rel->entry_point != 0)
        printf("entry point: 0x%08X\n", rel->entry_point);
    else
        printf("entry point: <none>\n");
    if (game_name && game_name[0] != '\0')
        printf("game: %s\n", game_name);
    printf("imports: %u\n", rel->import_count);
    printf("relocations applied: %u\n", rel->relocation_count);
    printf("BSS: 0x%X bytes\n", rel->bss_size);
    printf("\n");

    printf("sections:\n");
    for (u32 i = 0; i < rel->section_count; i++) {
        const RELSection* section = &rel->sections[i];
        if (section->size == 0)
            continue;
        printf("  [%u] file:0x%08X -> addr:0x%08X  size:0x%08X%s%s\n",
               i, section->offset, section->address, section->size,
               section->executable ? " executable" : "",
               section->bss ? " bss" : "");
    }
}
