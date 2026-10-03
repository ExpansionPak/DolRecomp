#include "app/patch_config.h"
#include "backend/patch_version.h"

#include "tomlc17.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int patch_symbol_valid(const char* symbol) {
    if (!symbol || !(symbol[0] == '_' ||
                     (symbol[0] >= 'A' && symbol[0] <= 'Z') ||
                     (symbol[0] >= 'a' && symbol[0] <= 'z')))
        return 0;
    for (const char* p = symbol + 1; *p; p++) {
        if (!(*p == '_' || (*p >= '0' && *p <= '9') ||
              (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')))
            return 0;
    }
    return strncmp(symbol, "func_", 5) != 0 &&
           strncmp(symbol, "moderngekko_", 12) != 0;
}

static char* copy_string(const char* value) {
    size_t size = strlen(value) + 1u;
    char* copy = (char*)malloc(size);
    if (copy)
        memcpy(copy, value, size);
    return copy;
}

static int parse_expected_hash(toml_datum_t value, u64* hash) {
    if (value.type == TOML_INT64) {
        if (value.u.int64 < 0)
            return 0;
        *hash = (u64)value.u.int64;
        return 1;
    }
    if (value.type != TOML_STRING || !value.u.s || !value.u.s[0])
        return 0;
    char* end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(value.u.s, &end, 16);
    if (errno || !end || *end)
        return 0;
    *hash = (u64)parsed;
    return 1;
}

void dolrecomp_patch_config_free(DolRecompPatchConfig* config) {
    if (!config)
        return;
    for (u32 i = 0; i < config->count; i++)
        free(config->entries[i].symbol);
    free(config->entries);
    memset(config, 0, sizeof(*config));
}

int dolrecomp_patch_config_load(const char* path, DolRecompPatchConfig* config,
                                char* error, size_t error_size) {
    memset(config, 0, sizeof(*config));
    toml_result_t result = toml_parse_file_ex(path);
    if (!result.ok) {
        snprintf(error, error_size, "%s", result.errmsg);
        toml_free(result);
        return 0;
    }

    toml_datum_t patches = toml_get(result.toptab, "patches");
    if (patches.type == TOML_UNKNOWN) {
        toml_free(result);
        return 1;
    }
    if (patches.type != TOML_TABLE) {
        snprintf(error, error_size, "[patches] must be a table");
        toml_free(result);
        return 0;
    }

    toml_datum_t abi = toml_get(patches, "abi_version");
    if (abi.type != TOML_UNKNOWN &&
        (abi.type != TOML_INT64 || abi.u.int64 != DOLRECOMP_PATCH_ABI_VERSION)) {
        snprintf(error, error_size, "patches.abi_version must be %d",
                 DOLRECOMP_PATCH_ABI_VERSION);
        toml_free(result);
        return 0;
    }

    toml_datum_t funcs = toml_get(patches, "func");
    if (funcs.type == TOML_UNKNOWN) {
        toml_free(result);
        return 1;
    }
    if (funcs.type != TOML_ARRAY) {
        snprintf(error, error_size, "[[patches.func]] must be an array of tables");
        toml_free(result);
        return 0;
    }

    if (funcs.u.arr.size > 0) {
        config->entries = (DolRecompPatchConfigEntry*)calloc(
            (size_t)funcs.u.arr.size, sizeof(*config->entries));
        if (!config->entries) {
            snprintf(error, error_size, "out of memory");
            toml_free(result);
            return 0;
        }
    }

    for (int i = 0; i < funcs.u.arr.size; i++) {
        toml_datum_t func = funcs.u.arr.elem[i];
        if (func.type != TOML_TABLE) {
            snprintf(error, error_size, "[[patches.func]] #%d must be a table", i + 1);
            goto fail;
        }
        toml_datum_t start = toml_get(func, "start");
        toml_datum_t end = toml_get(func, "end");
        toml_datum_t symbol = toml_get(func, "symbol");
        toml_datum_t source = toml_get(func, "source");
        if (start.type != TOML_INT64 || end.type != TOML_INT64 ||
            start.u.int64 < 0 || start.u.int64 > UINT32_MAX ||
            end.u.int64 < 0 || end.u.int64 > UINT32_MAX ||
            end.u.int64 <= start.u.int64 ||
            (((u64)start.u.int64 | (u64)end.u.int64) & 3u) != 0) {
            snprintf(error, error_size,
                     "[[patches.func]] #%d has an invalid aligned range", i + 1);
            goto fail;
        }
        if (symbol.type != TOML_STRING || !patch_symbol_valid(symbol.u.s)) {
            snprintf(error, error_size,
                     "[[patches.func]] #%d has an invalid patch symbol", i + 1);
            goto fail;
        }
        if (source.type != TOML_UNKNOWN && source.type != TOML_STRING) {
            snprintf(error, error_size,
                     "[[patches.func]] #%d source must be a string", i + 1);
            goto fail;
        }
        DolRecompPatchConfigEntry* entry = &config->entries[config->count];
        entry->start = (u32)start.u.int64;
        entry->end = (u32)end.u.int64;
        entry->symbol = copy_string(symbol.u.s);
        if (!entry->symbol) {
            snprintf(error, error_size, "out of memory");
            goto fail;
        }
        toml_datum_t expected = toml_get(func, "expected_fnv64");
        if (expected.type != TOML_UNKNOWN) {
            if (!parse_expected_hash(expected, &entry->expected_fnv64)) {
                snprintf(error, error_size,
                         "[[patches.func]] #%d has an invalid expected_fnv64", i + 1);
                goto fail;
            }
            entry->has_expected_fnv64 = 1;
        }
        config->count++;
    }

    toml_free(result);
    return 1;

fail:
    toml_free(result);
    dolrecomp_patch_config_free(config);
    return 0;
}
