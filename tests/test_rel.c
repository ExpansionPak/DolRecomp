#include "frontend/rel.h"
#include "core/types.h"
#include <stdio.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        return 0; \
    } \
} while (0)

enum {
    R_PPC_ADDR32 = 1,
    R_PPC_REL24 = 10,
    R_DOLPHIN_SECTION = 202,
    R_DOLPHIN_END = 203,
};

static void write_relocation(u8* p, u16 offset, u8 type, u8 section,
                             u32 symbol_offset) {
    write_be16(p + 0, offset);
    p[2] = type;
    p[3] = section;
    write_be32(p + 4, symbol_offset);
}

static int write_sample_rel_with_id(const char* path, u32 module_id,
                            u32 import_module,
                            u8 reloc_type, u8 symbol_section,
                            u32 symbol_offset, u32 text_size) {
    u8 file[0x120];
    memset(file, 0, sizeof(file));

    write_be32(file + 0x00, module_id);
    write_be32(file + 0x0C, 3);
    write_be32(file + 0x10, 0x50);
    write_be32(file + 0x1C, 3);
    write_be32(file + 0x24, 0x80);
    write_be32(file + 0x28, 0xA0);
    write_be32(file + 0x2C, 8);
    file[0x30] = 1;
    write_be32(file + 0x34, 4);
    write_be32(file + 0x40, 4);
    write_be32(file + 0x44, 4);

    write_be32(file + 0x50 + 8, 0x100 | 1u);
    write_be32(file + 0x50 + 12, text_size);
    write_be32(file + 0x50 + 16, 0x108);
    write_be32(file + 0x50 + 20, 4);

    write_relocation(file + 0x80, 0, R_DOLPHIN_SECTION, 1, 0);
    write_relocation(file + 0x88, 0, reloc_type, symbol_section, symbol_offset);
    write_relocation(file + 0x90, 0, R_DOLPHIN_END, 0, 0);

    write_be32(file + 0xA0, import_module);
    write_be32(file + 0xA4, 0x80);

    write_be32(file + 0x100, reloc_type == R_PPC_REL24 ? 0x48000000u : 0);
    write_be32(file + 0x104, 0x4E800020u);
    write_be32(file + 0x108, 0x12345678u);

    FILE* out = fopen(path, "wb");
    if (!out)
        return 0;
    int ok = fwrite(file, 1, sizeof(file), out) == sizeof(file);
    ok = fclose(out) == 0 && ok;
    return ok;
}

static int write_sample_rel(const char* path, u32 import_module,
                            u8 reloc_type, u8 symbol_section,
                            u32 symbol_offset, u32 text_size) {
    return write_sample_rel_with_id(path, 1, import_module, reloc_type,
                                    symbol_section, symbol_offset, text_size);
}

static int test_self_relocation(void) {
    const char* path = "test_self.rel";
    CHECK(write_sample_rel(path, 1, R_PPC_ADDR32, 2, 0, 8),
          "failed to write sample REL");

    RELFile rel;
    CHECK(rel_load(&rel, path, 0x80500000u), "failed to load sample REL");
    CHECK(rel.module_id == 1, "bad module id %u", rel.module_id);
    CHECK(rel.section_count == 3, "bad section count %u", rel.section_count);
    CHECK(rel.entry_point == 0x80500104u, "bad entry 0x%08X", rel.entry_point);
    CHECK(rel.relocation_count == 1, "bad relocation count %u",
          rel.relocation_count);
    CHECK(read_be32(rel.sections[1].data) == 0x80500108u,
          "self ADDR32 relocation was not applied");

    rel_free(&rel);
    remove(path);
    return 1;
}

static int test_dol_rel24_relocation(void) {
    const char* path = "test_dol_rel24.rel";
    CHECK(write_sample_rel(path, 0, R_PPC_REL24, 0, 0x80003100u, 8),
          "failed to write sample REL");

    RELFile rel;
    CHECK(rel_load(&rel, path, 0x80500000u), "failed to load sample REL");
    u32 inst = read_be32(rel.sections[1].data);
    s32 delta = sign_extend(inst & 0x03FFFFFCu, 26);
    CHECK(0x80500100u + (u32)delta == 0x80003100u,
          "DOL REL24 relocation target was not applied");

    rel_free(&rel);
    remove(path);
    return 1;
}

static int test_external_import_rejected(void) {
    const char* path = "test_external.rel";
    CHECK(write_sample_rel(path, 2, R_PPC_ADDR32, 2, 0, 8),
          "failed to write sample REL");

    RELFile rel;
    int loaded = rel_load(&rel, path, 0x80500000u);
    if (loaded)
        rel_free(&rel);
    remove(path);
    CHECK(!loaded, "external REL import should be rejected");
    return 1;
}

static int test_external_import_with_map(void) {
    const char* importer_path = "test_importer.rel";
    const char* target_path = "test_target.rel";
    CHECK(write_sample_rel_with_id(importer_path, 1, 2, R_PPC_ADDR32, 2, 0, 8),
          "failed to write importer REL");
    CHECK(write_sample_rel_with_id(target_path, 2, 2, R_PPC_ADDR32, 2, 0, 8),
          "failed to write target REL");

    RELFile importer;
    RELFile target;
    CHECK(rel_load_image(&importer, importer_path, 0x80500000u),
          "failed to load importer REL");
    CHECK(rel_load_image(&target, target_path, 0x80510000u),
          "failed to load target REL");

    RELModuleMapEntry entries[] = {
        { importer.module_id, &importer },
        { target.module_id, &target },
    };
    RELModuleMap map = { entries, 2 };

    CHECK(rel_apply_relocations(&target, &map),
          "failed to relocate target REL");
    CHECK(rel_apply_relocations(&importer, &map),
          "failed to relocate importer REL");
    CHECK(read_be32(importer.sections[1].data) == target.sections[2].address,
          "external REL import was not resolved");

    rel_free(&target);
    rel_free(&importer);
    remove(importer_path);
    remove(target_path);
    return 1;
}

static int test_unaligned_text_rejected(void) {
    const char* path = "test_unaligned.rel";
    CHECK(write_sample_rel(path, 1, R_PPC_ADDR32, 2, 0, 6),
          "failed to write sample REL");

    RELFile rel;
    int loaded = rel_load(&rel, path, 0x80500000u);
    if (loaded)
        rel_free(&rel);
    remove(path);
    CHECK(!loaded, "unaligned REL executable section should be rejected");
    return 1;
}

int main(void) {
    int ok = 1;
    ok &= test_self_relocation();
    ok &= test_dol_rel24_relocation();
    ok &= test_external_import_rejected();
    ok &= test_external_import_with_map();
    ok &= test_unaligned_text_rejected();

    if (!ok)
        return 1;

    printf("REL frontend tests passed\n");
    return 0;
}
