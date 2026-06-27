#include "frontend/container/rpx.h"
#include "common/types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        return 0; \
    } \
} while (0)

enum {
    ELF_HEADER_SIZE = 52,
    ELF_SECTION_SIZE = 40,
    ELF_MACHINE_PPC = 20,
    ELF_TYPE_RPL = 0xfe01,
    SHF_ALLOC = 0x00000002,
    SHF_EXECINSTR = 0x00000004,
    SHF_RPL_ZLIB = 0x08000000,
    SHT_PROGBITS = 1,
    SHT_STRTAB = 3,
};

static void write_be16_local(u8* p, u16 value) {
    write_be16(p, value);
}

static void write_section(u8* p, u32 name, u32 type, u32 flags, u32 address,
                          u32 offset, u32 size, u32 align) {
    write_be32(p + 0, name);
    write_be32(p + 4, type);
    write_be32(p + 8, flags);
    write_be32(p + 12, address);
    write_be32(p + 16, offset);
    write_be32(p + 20, size);
    write_be32(p + 24, 0);
    write_be32(p + 28, 0);
    write_be32(p + 32, align);
    write_be32(p + 36, 0);
}

static int write_sample_rpx(const char* path, int compressed_text,
                            u32 entry_point) {
    u8 file[0x140];
    memset(file, 0, sizeof(file));

    memcpy(file, "\x7f" "ELF", 4);
    file[4] = 1;
    file[5] = 2;
    file[6] = 1;
    write_be16_local(file + 16, ELF_TYPE_RPL);
    write_be16_local(file + 18, ELF_MACHINE_PPC);
    write_be32(file + 20, 1);
    write_be32(file + 24, entry_point);
    write_be32(file + 32, 0x80);
    write_be16_local(file + 40, ELF_HEADER_SIZE);
    write_be16_local(file + 46, ELF_SECTION_SIZE);
    write_be16_local(file + 48, 3);
    write_be16_local(file + 50, 2);

    write_section(file + 0x80, 0, 0, 0, 0, 0, 0, 0);
    write_section(file + 0x80 + ELF_SECTION_SIZE, 1, SHT_PROGBITS,
                  SHF_ALLOC | SHF_EXECINSTR |
                      (compressed_text ? SHF_RPL_ZLIB : 0),
                  0x02000000u, 0x100, compressed_text ? 4 : 8, 4);
    write_section(file + 0x80 + 2 * ELF_SECTION_SIZE, 7, SHT_STRTAB, 0, 0,
                  0x120, 17, 1);

    if (compressed_text) {
        write_be32(file + 0x100, 8);
    } else {
        write_be32(file + 0x100, 0x38600001u);
        write_be32(file + 0x104, 0x4E800020u);
    }
    memcpy(file + 0x120, "\0.text\0.shstrtab\0", 17);

    FILE* out = fopen(path, "wb");
    if (!out)
        return 0;
    int ok = fwrite(file, 1, sizeof(file), out) == sizeof(file);
    ok = fclose(out) == 0 && ok;
    return ok;
}

static int test_uncompressed_rpx(void) {
    const char* path = "test_uncompressed.rpx";
    CHECK(write_sample_rpx(path, 0, 0x02000000u),
          "failed to write sample RPX");

    RPXFile rpx;
    CHECK(rpx_load(&rpx, path), "failed to load sample RPX");
    CHECK(rpx.entry_point == 0x02000000u, "bad entry 0x%08X",
          rpx.entry_point);
    CHECK(rpx.section_count == 3, "bad section count %u", rpx.section_count);
    CHECK(rpx.code_section_count == 1, "bad code section count %u",
          rpx.code_section_count);
    CHECK(strcmp(rpx.code_sections[0].name, ".text") == 0,
          "bad code section name '%s'", rpx.code_sections[0].name);
    CHECK(rpx.code_sections[0].address == 0x02000000u,
          "bad code address 0x%08X", rpx.code_sections[0].address);
    CHECK(rpx.code_sections[0].size == 8, "bad code size %u",
          rpx.code_sections[0].size);
    CHECK(read_be32(rpx.code_sections[0].data) == 0x38600001u,
          "bad first instruction");

    rpx_free(&rpx);
    remove(path);
    return 1;
}

static int test_compressed_rejects_bad_stream(void) {
    const char* path = "test_compressed_bad.rpx";
    CHECK(write_sample_rpx(path, 1, 0x02000000u),
          "failed to write compressed sample RPX");

    RPXFile rpx;
    int loaded = rpx_load(&rpx, path);
    if (loaded)
        rpx_free(&rpx);
    remove(path);
    CHECK(!loaded, "bad compressed stream should be rejected");
    return 1;
}

static int test_entry_outside_code_rejected(void) {
    const char* path = "test_entry_outside.rpx";
    CHECK(write_sample_rpx(path, 0, 0x02001000u),
          "failed to write sample RPX");

    RPXFile rpx;
    int loaded = rpx_load(&rpx, path);
    if (loaded)
        rpx_free(&rpx);
    remove(path);
    CHECK(!loaded, "RPX entry outside executable code should be rejected");
    return 1;
}

static int test_unaligned_entry_rejected(void) {
    const char* path = "test_unaligned_entry.rpx";
    CHECK(write_sample_rpx(path, 0, 0x02000002u),
          "failed to write sample RPX");

    RPXFile rpx;
    int loaded = rpx_load(&rpx, path);
    if (loaded)
        rpx_free(&rpx);
    remove(path);
    CHECK(!loaded, "unaligned RPX entry should be rejected");
    return 1;
}

int main(void) {
    int ok = 1;
    ok &= test_uncompressed_rpx();
    ok &= test_compressed_rejects_bad_stream();
    ok &= test_entry_outside_code_rejected();
    ok &= test_unaligned_entry_rejected();

    if (!ok)
        return 1;

    printf("RPX frontend tests passed\n");
    return 0;
}
