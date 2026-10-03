#ifndef DOLRECOMP_APP_PATCH_CONFIG_H
#define DOLRECOMP_APP_PATCH_CONFIG_H

#include "common/types.h"

#include <stddef.h>

typedef struct {
    u32 start;
    u32 end;
    char* symbol;
    u64 expected_fnv64;
    int has_expected_fnv64;
} DolRecompPatchConfigEntry;

typedef struct {
    DolRecompPatchConfigEntry* entries;
    u32 count;
} DolRecompPatchConfig;

int dolrecomp_patch_config_load(const char* path, DolRecompPatchConfig* config,
                                char* error, size_t error_size);
void dolrecomp_patch_config_free(DolRecompPatchConfig* config);

#endif
