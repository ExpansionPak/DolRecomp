#ifndef DOLRECOMP_SYMBOL_MAP_H
#define DOLRECOMP_SYMBOL_MAP_H

#include "common/types.h"
#include <stddef.h>

typedef struct {
    u32 address;
    u32 size;
    char* name;
} DolRecompSymbol;

typedef struct {
    DolRecompSymbol* symbols;
    u32 count;
    u32 capacity;
} DolRecompSymbolMap;

int symbol_map_load(DolRecompSymbolMap* map, const char* path);
void symbol_map_free(DolRecompSymbolMap* map);
int symbol_name_to_identifier(const char* name, char* output, size_t output_size);

#endif
