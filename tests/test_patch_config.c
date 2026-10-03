#include "app/patch_config.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void) {
    const char* path = "patch_config_test.toml";
    FILE* file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fputs(
        "[patches]\n"
        "abi_version = 1\n"
        "func = [\n"
        "  { start = 0x8000_1000, end = 0x80001020, symbol = 'patch_inline', "
        "source = 'inline.c', expected_fnv64 = 10 },\n"
        "  { start = 0x80002000, end = 0x80002004, symbol = \"patch_string\", "
        "source = \"string.c\", expected_fnv64 = \"FEDCBA9876543210\" },\n"
        "]\n", file) >= 0);
    CHECK(fclose(file) == 0);

    DolRecompPatchConfig config;
    char error[256];
    CHECK(dolrecomp_patch_config_load(path, &config, error, sizeof(error)));
    CHECK(config.count == 2u);
    CHECK(config.entries[0].start == 0x80001000u);
    CHECK(config.entries[0].end == 0x80001020u);
    CHECK(strcmp(config.entries[0].symbol, "patch_inline") == 0);
    CHECK(config.entries[0].has_expected_fnv64);
    CHECK(config.entries[0].expected_fnv64 == 10u);
    CHECK(config.entries[1].expected_fnv64 == UINT64_C(0xFEDCBA9876543210));
    dolrecomp_patch_config_free(&config);
    remove(path);
    return 0;
}
