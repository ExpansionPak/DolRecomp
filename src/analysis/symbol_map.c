#include "analysis/symbol_map.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_hex(const char* text, u32* value) {
    const char* digits = text;
    size_t length;
    char* end = NULL;
    unsigned long parsed;

    if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
        digits += 2;
    length = strlen(digits);
    if (length == 0 || length > 8)
        return 0;
    for (size_t i = 0; i < length; i++) {
        if (!isxdigit((unsigned char)digits[i]))
            return 0;
    }

    errno = 0;
    parsed = strtoul(digits, &end, 16);
    if (errno != 0 || !end || *end != '\0' || parsed > 0xFFFFFFFFul)
        return 0;
    *value = (u32)parsed;
    return 1;
}

static int valid_name(const char* name) {
    if (!name || name[0] == '\0' || name[0] == '.' || name[0] == '*')
        return 0;
    if (strcmp(name, "UNUSED") == 0 || strcmp(name, "...UNUSED...") == 0)
        return 0;
    return 1;
}

static char* duplicate_string(const char* text) {
    size_t size = strlen(text) + 1u;
    char* copy = (char*)malloc(size);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

static int symbol_compare(const void* lhs, const void* rhs) {
    const DolRecompSymbol* a = (const DolRecompSymbol*)lhs;
    const DolRecompSymbol* b = (const DolRecompSymbol*)rhs;
    if (a->address < b->address)
        return -1;
    if (a->address > b->address)
        return 1;
    return strcmp(a->name, b->name);
}

static int symbol_map_add(DolRecompSymbolMap* map, u32 address, u32 size,
                          const char* name) {
    if (map->count == map->capacity) {
        u32 capacity = map->capacity ? map->capacity * 2u : 256u;
        DolRecompSymbol* symbols =
            (DolRecompSymbol*)realloc(map->symbols, capacity * sizeof(*symbols));
        if (!symbols)
            return 0;
        map->symbols = symbols;
        map->capacity = capacity;
    }

    char* copy = duplicate_string(name);
    if (!copy)
        return 0;
    map->symbols[map->count].address = address;
    map->symbols[map->count].size = size;
    map->symbols[map->count].name = copy;
    map->count++;
    return 1;
}

static u32 split_tokens(char* line, char** tokens, u32 capacity) {
    u32 count = 0;
    char* cursor = line;
    while (*cursor != '\0' && count < capacity) {
        while (isspace((unsigned char)*cursor))
            cursor++;
        if (*cursor == '\0')
            break;
        tokens[count++] = cursor;
        while (*cursor != '\0' && !isspace((unsigned char)*cursor))
            cursor++;
        if (*cursor != '\0')
            *cursor++ = '\0';
    }
    return count;
}

static int parse_line(char* line, u32* address, u32* size, const char** name) {
    char* tokens[16];
    u32 values[5];
    u32 count = split_tokens(line, tokens, 16);

    if (count >= 6 && parse_hex(tokens[0], &values[0]) &&
        parse_hex(tokens[1], &values[1]) && parse_hex(tokens[2], &values[2]) &&
        parse_hex(tokens[3], &values[3]) && parse_hex(tokens[4], &values[4])) {
        if (valid_name(tokens[5])) {
            *address = values[2];
            *size = values[1];
            *name = tokens[5];
            return 1;
        }
        return 0;
    }

    if (count >= 5 && parse_hex(tokens[0], &values[0]) &&
        parse_hex(tokens[1], &values[1]) && parse_hex(tokens[2], &values[2]) &&
        parse_hex(tokens[3], &values[3]) && valid_name(tokens[4])) {
        *address = values[2];
        *size = values[1];
        *name = tokens[4];
        return 1;
    }

    if (count >= 3 && parse_hex(tokens[0], &values[0]) &&
        parse_hex(tokens[1], &values[1]) && valid_name(tokens[2])) {
        *address = values[0];
        *size = values[1];
        *name = tokens[2];
        return 1;
    }

    if (count >= 2 && parse_hex(tokens[0], &values[0]) && valid_name(tokens[1])) {
        *address = values[0];
        *size = 0;
        *name = tokens[1];
        return 1;
    }

    return 0;
}

static void symbol_map_deduplicate(DolRecompSymbolMap* map) {
    u32 write = 0;
    for (u32 read = 0; read < map->count; read++) {
        DolRecompSymbol* current = &map->symbols[read];
        if (write != 0 && map->symbols[write - 1u].address == current->address &&
            strcmp(map->symbols[write - 1u].name, current->name) == 0) {
            if (map->symbols[write - 1u].size == 0)
                map->symbols[write - 1u].size = current->size;
            free(current->name);
            continue;
        }
        if (write != read)
            map->symbols[write] = *current;
        write++;
    }
    map->count = write;
}

int symbol_map_load(DolRecompSymbolMap* map, const char* path) {
    char line[4096];
    FILE* input;

    memset(map, 0, sizeof(*map));
    input = fopen(path, "r");
    if (!input) {
        fprintf(stderr, "error: can't open symbol map '%s'\n", path);
        return 0;
    }

    while (fgets(line, sizeof(line), input)) {
        u32 address;
        u32 size;
        const char* name;
        if (!parse_line(line, &address, &size, &name))
            continue;
        if ((address & 3u) != 0u)
            continue;
        if (!symbol_map_add(map, address, size, name)) {
            fprintf(stderr, "error: out of memory while reading symbol map\n");
            fclose(input);
            symbol_map_free(map);
            return 0;
        }
    }

    if (ferror(input)) {
        fprintf(stderr, "error: failed reading symbol map '%s'\n", path);
        fclose(input);
        symbol_map_free(map);
        return 0;
    }
    fclose(input);

    if (map->count == 0) {
        fprintf(stderr, "error: no symbols found in '%s'\n", path);
        symbol_map_free(map);
        return 0;
    }

    qsort(map->symbols, map->count, sizeof(*map->symbols), symbol_compare);
    symbol_map_deduplicate(map);
    return 1;
}

void symbol_map_free(DolRecompSymbolMap* map) {
    for (u32 i = 0; i < map->count; i++)
        free(map->symbols[i].name);
    free(map->symbols);
    memset(map, 0, sizeof(*map));
}

int symbol_name_to_identifier(const char* name, char* output, size_t output_size) {
    size_t written = 0;
    int previous_underscore = 0;

    if (!output || output_size == 0)
        return 0;
    for (size_t i = 0; name[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)name[i];
        char emitted = (isalnum(ch) || ch == '_') ? (char)ch : '_';
        if (written == 0 && isdigit((unsigned char)emitted)) {
            if (written + 1u >= output_size)
                break;
            output[written++] = '_';
        }
        if (emitted == '_' && previous_underscore)
            continue;
        if (written + 1u >= output_size)
            break;
        output[written++] = emitted;
        previous_underscore = emitted == '_';
    }
    while (written > 1u && output[written - 1u] == '_')
        written--;
    if (written == 0)
        return 0;
    output[written] = '\0';
    return 1;
}
