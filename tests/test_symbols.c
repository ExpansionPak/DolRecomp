#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analysis/code_section.h"
#include "analysis/symbol_map.h"
#include "backend/symbols.h"

static int pass_count;
static int fail_count;

static void check(int condition, const char* name) {
    printf("SYMBOLS,%s,%s\n", name, condition ? "PASS" : "FAIL");
    if (condition)
        pass_count++;
    else
        fail_count++;
}

static char* read_stream(FILE* stream) {
    long size;
    char* text;
    fflush(stream);
    size = ftell(stream);
    if (size < 0)
        return NULL;
    rewind(stream);
    text = (char*)malloc((size_t)size + 1u);
    if (!text)
        return NULL;
    size_t count = fread(text, 1, (size_t)size, stream);
    text[count] = '\0';
    return text;
}

int main(int argc, char** argv) {
    DolRecompSymbolMap map = {0};
    LoadedCodeSection section = {0};
    char identifier[64];
    FILE* output;
    char* text;

    if (argc != 2) {
        fprintf(stderr, "expected MAP fixture path\n");
        return 1;
    }

    check(symbol_map_load(&map, argv[1]), "loads CodeWarrior and simple rows");
    check(map.count == 5, "deduplicates and rejects unaligned entries");
    check(map.symbols[0].address == 0x80004000u &&
          strcmp(map.symbols[0].name, "Start") == 0,
          "sorts symbols by address");
    check(symbol_name_to_identifier("7 bad::name", identifier, sizeof(identifier)) &&
          strcmp(identifier, "_7_bad_name") == 0,
          "sanitizes C identifiers");

    section.address = 0x80004000u;
    section.size = 0x60u;
    check(count_code_symbols(&map, &section, 1) == 4,
          "filters symbols to executable sections");

    output = tmpfile();
    check(output != NULL, "opens generated symbol stream");
    if (!output) {
        symbol_map_free(&map);
        return 1;
    }
    check(emit_symbol_definitions(output, &map, &section, 1),
          "emits symbol definitions");
    text = read_stream(output);
    check(text != NULL, "reads generated symbol definitions");
    if (text) {
        check(strstr(text, "#ifndef DOLRECOMP_GENERATED_SYMBOLS_H") != NULL,
              "emits guarded symbol header");
        check(strstr(text, "#define DOLRECOMP_SYMBOL_Start 0x80004000u") != NULL,
              "emits named address");
        check(strstr(text, "#define DOLRECOMP_SYMBOL_Game_Update 0x80004020u") != NULL &&
              strstr(text,
                     "#define DOLRECOMP_SYMBOL_Game_Update_80004030 0x80004030u") != NULL,
              "disambiguates sanitized names");
        check(strstr(text, "#define DOLRECOMP_SYMBOL_SIZE_Tail 0x00000020u") != NULL,
              "infers missing size");
    }

    free(text);
    fclose(output);
    symbol_map_free(&map);
    printf("SYMBOLS,total,%d passed %d failed\n", pass_count, fail_count);
    return fail_count == 0 ? 0 : 1;
}
