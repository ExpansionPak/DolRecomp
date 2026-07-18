#include "backend/symbols.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const DolRecompSymbol* symbol;
    u32 size;
    char identifier[256];
} EmittedSymbol;

typedef struct {
    u32* slots;
    size_t capacity;
} IdentifierSet;

static const LoadedCodeSection* find_section(const LoadedCodeSection* sections,
                                             u32 section_count, u32 address) {
    for (u32 i = 0; i < section_count; i++) {
        u64 end = (u64)sections[i].address + sections[i].size;
        if (address >= sections[i].address && (u64)address < end)
            return &sections[i];
    }
    return NULL;
}

static u32 resolved_size(const DolRecompSymbolMap* map, u32 index,
                         const LoadedCodeSection* section) {
    const DolRecompSymbol* symbol = &map->symbols[index];
    u32 section_end = section->address + section->size;
    if (symbol->size != 0) {
        if (symbol->size > section_end - symbol->address)
            return 0;
        return symbol->size;
    }
    for (u32 i = index + 1u; i < map->count; i++) {
        u32 next = map->symbols[i].address;
        if (next > symbol->address && next < section_end)
            return next - symbol->address;
        if (next >= section_end)
            break;
    }
    return section_end - symbol->address;
}

u32 count_code_symbols(const DolRecompSymbolMap* map,
                       const LoadedCodeSection* sections, u32 section_count) {
    u32 count = 0;
    if (!map)
        return 0;
    for (u32 i = 0; i < map->count; i++) {
        const LoadedCodeSection* section =
            find_section(sections, section_count, map->symbols[i].address);
        if (section && resolved_size(map, i, section) != 0)
            count++;
    }
    return count;
}

static u64 identifier_hash(const char* identifier) {
    u64 hash = 0xcbf29ce484222325ull;
    for (size_t i = 0; identifier[i] != '\0'; i++) {
        hash ^= (unsigned char)identifier[i];
        hash *= 0x100000001b3ull;
    }
    return hash;
}

static int identifier_set_init(IdentifierSet* set, u32 symbol_count) {
    size_t target = (size_t)symbol_count * 2u;
    set->capacity = 16;
    while (set->capacity < target) {
        if (set->capacity > (size_t)-1 / 2u)
            return 0;
        set->capacity *= 2u;
    }
    set->slots = (u32*)calloc(set->capacity, sizeof(*set->slots));
    return set->slots != NULL;
}

static int identifier_set_contains(const IdentifierSet* set,
                                   const EmittedSymbol* symbols,
                                   const char* identifier) {
    size_t slot = (size_t)identifier_hash(identifier) & (set->capacity - 1u);
    while (set->slots[slot] != 0) {
        if (strcmp(symbols[set->slots[slot] - 1u].identifier, identifier) == 0)
            return 1;
        slot = (slot + 1u) & (set->capacity - 1u);
    }
    return 0;
}

static void identifier_set_insert(IdentifierSet* set,
                                  const EmittedSymbol* symbols, u32 index) {
    size_t slot = (size_t)identifier_hash(symbols[index].identifier) &
                  (set->capacity - 1u);
    while (set->slots[slot] != 0)
        slot = (slot + 1u) & (set->capacity - 1u);
    set->slots[slot] = index + 1u;
}

int emit_symbol_definitions(FILE* output, const DolRecompSymbolMap* map,
                            const LoadedCodeSection* sections, u32 section_count) {
    u32 symbol_count = count_code_symbols(map, sections, section_count);
    EmittedSymbol* emitted;
    IdentifierSet identifiers = {0};
    u32 emitted_count = 0;

    if (!map)
        return 1;
    if (symbol_count == 0)
        return 0;
    emitted = (EmittedSymbol*)calloc(symbol_count, sizeof(*emitted));
    if (!emitted)
        return 0;
    if (!identifier_set_init(&identifiers, symbol_count)) {
        free(emitted);
        return 0;
    }

    for (u32 i = 0; i < map->count; i++) {
        const DolRecompSymbol* symbol = &map->symbols[i];
        const LoadedCodeSection* section =
            find_section(sections, section_count, symbol->address);
        char base[112];
        u32 size;
        if (!section)
            continue;
        size = resolved_size(map, i, section);
        if (size == 0)
            continue;
        if (!symbol_name_to_identifier(symbol->name, base, sizeof(base)))
            continue;

        EmittedSymbol* item = &emitted[emitted_count];
        item->symbol = symbol;
        item->size = size;
        snprintf(item->identifier, sizeof(item->identifier), "%s", base);
        if (identifier_set_contains(&identifiers, emitted, item->identifier)) {
            u32 suffix = 0;
            do {
                int written;
                if (suffix == 0) {
                    written = snprintf(item->identifier, sizeof(item->identifier),
                                       "%s_%08X", base, symbol->address);
                } else {
                    written = snprintf(item->identifier, sizeof(item->identifier),
                                       "%s_%08X_%u", base, symbol->address, suffix);
                }
                if (written < 0 || written >= (int)sizeof(item->identifier)) {
                    item->identifier[0] = '\0';
                    break;
                }
                suffix++;
            } while (identifier_set_contains(&identifiers, emitted, item->identifier));
            if (item->identifier[0] == '\0')
                continue;
        }
        identifier_set_insert(&identifiers, emitted, emitted_count);
        emitted_count++;
    }

    fprintf(output, "#ifndef DOLRECOMP_GENERATED_SYMBOLS_H\n");
    fprintf(output, "#define DOLRECOMP_GENERATED_SYMBOLS_H\n\n");
    fprintf(output, "#define DOLRECOMP_SYMBOL_COUNT %uu\n", emitted_count);
    for (u32 i = 0; i < emitted_count; i++) {
        fprintf(output, "#define DOLRECOMP_SYMBOL_%s 0x%08Xu\n",
                emitted[i].identifier, emitted[i].symbol->address);
        fprintf(output, "#define DOLRECOMP_SYMBOL_SIZE_%s 0x%08Xu\n",
                emitted[i].identifier, emitted[i].size);
    }
    fprintf(output, "\n#endif\n");

    free(identifiers.slots);
    free(emitted);
    return 1;
}
