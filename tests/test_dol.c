#include "frontend/container/dol.h"
#include "common/types.h"
#include <stdio.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        return 0; \
    } \
} while (0)

static int write_sample_dol(const char* path, u32 entry_point,
                            u32 text_offset, u32 text_size) {
    u8 file[0x108];
    memset(file, 0, sizeof(file));

    write_be32(file + 0x00, text_offset);
    write_be32(file + 0x48, 0x80003100u);
    write_be32(file + 0x90, text_size);
    write_be32(file + 0xE0, entry_point);

    write_be32(file + 0x100, 0x38600001u);
    write_be32(file + 0x104, 0x4E800020u);

    FILE* out = fopen(path, "wb");
    if (!out)
        return 0;
    int ok = fwrite(file, 1, sizeof(file), out) == sizeof(file);
    ok = fclose(out) == 0 && ok;
    return ok;
}

static int test_valid_dol(void) {
    const char* path = "test_valid.dol";
    CHECK(write_sample_dol(path, 0x80003100u, 0x100, 8),
          "failed to write sample DOL");

    DOLFile dol;
    CHECK(dol_load(&dol, path), "failed to load sample DOL");
    CHECK(dol.header.entry_point == 0x80003100u,
          "bad entry 0x%08X", dol.header.entry_point);
    CHECK(dol_get_text_section(&dol, 0) != NULL, "missing text section");

    dol_free(&dol);
    remove(path);
    return 1;
}

static int test_entry_outside_text_rejected(void) {
    const char* path = "test_entry_outside.dol";
    CHECK(write_sample_dol(path, 0x80004000u, 0x100, 8),
          "failed to write sample DOL");

    DOLFile dol;
    int loaded = dol_load(&dol, path);
    if (loaded)
        dol_free(&dol);
    remove(path);
    CHECK(!loaded, "DOL entry outside text should be rejected");
    return 1;
}

static int test_unaligned_entry_rejected(void) {
    const char* path = "test_unaligned_entry.dol";
    CHECK(write_sample_dol(path, 0x80003102u, 0x100, 8),
          "failed to write sample DOL");

    DOLFile dol;
    int loaded = dol_load(&dol, path);
    if (loaded)
        dol_free(&dol);
    remove(path);
    CHECK(!loaded, "unaligned DOL entry should be rejected");
    return 1;
}

static int test_text_range_rejected(void) {
    const char* path = "test_bad_range.dol";
    CHECK(write_sample_dol(path, 0x80003100u, 0x104, 8),
          "failed to write sample DOL");

    DOLFile dol;
    int loaded = dol_load(&dol, path);
    if (loaded)
        dol_free(&dol);
    remove(path);
    CHECK(!loaded, "DOL text section outside file should be rejected");
    return 1;
}

static int test_unaligned_text_size_rejected(void) {
    const char* path = "test_unaligned_text.dol";
    CHECK(write_sample_dol(path, 0x80003100u, 0x100, 6),
          "failed to write sample DOL");

    DOLFile dol;
    int loaded = dol_load(&dol, path);
    if (loaded)
        dol_free(&dol);
    remove(path);
    CHECK(!loaded, "unaligned DOL text size should be rejected");
    return 1;
}

int main(void) {
    int ok = 1;
    ok &= test_valid_dol();
    ok &= test_entry_outside_text_rejected();
    ok &= test_unaligned_entry_rejected();
    ok &= test_text_range_rejected();
    ok &= test_unaligned_text_size_rejected();

    if (!ok)
        return 1;

    printf("DOL frontend tests passed\n");
    return 0;
}
