#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/backend/dispatch.h"

#define BASE 0x80003000u

static int pass_count = 0;
static int fail_count = 0;

static void check(int condition, const char* name) {
    printf("DISPATCH,%s,%s\n", name, condition ? "PASS" : "FAIL");
    if (condition)
        pass_count++;
    else
        fail_count++;
}

// MSVC has no setenv, and _putenv_s is not portable back the other way.
static int set_lookup_mode(const char* value) {
#if defined(_WIN32)
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "DOLRECOMP_DISPATCH_LOOKUP=%s",
             value ? value : "");
    return _putenv(buffer) == 0;
#else
    if (!value)
        return unsetenv("DOLRECOMP_DISPATCH_LOOKUP") == 0;
    return setenv("DOLRECOMP_DISPATCH_LOOKUP", value, 1) == 0;
#endif
}

// The four ranges below cover the three shapes the lookup has to get right:
// a contiguous equal-stride run (0x3000..0x3080), a short chunk closing that
// run (0x3080..0x30A0), and an isolated chunk a page away (0x4000..0x4020).
static char* emit_dispatch_to_string(void) {
    FunctionList funcs = {0};
    FILE* f = NULL;
    char* buf = NULL;

    if (!function_list_add(&funcs, BASE, BASE + 0x40u) ||
        !function_list_add(&funcs, BASE + 0x40u, BASE + 0x80u) ||
        !function_list_add(&funcs, BASE + 0x80u, BASE + 0xA0u) ||
        !function_list_add(&funcs, BASE + 0x1000u, BASE + 0x1020u)) {
        function_list_free(&funcs);
        return NULL;
    }

    f = tmpfile();
    if (!f) {
        function_list_free(&funcs);
        return NULL;
    }

    emit_chunk_prototype(f, BASE);
    emit_chunk_prototype(f, BASE + 0x40u);
    emit_chunk_prototype(f, BASE + 0x80u);
    emit_chunk_prototype(f, BASE + 0x1000u);
    emit_dispatch_helpers(f, &funcs, BASE, 0);
    function_list_free(&funcs);
    fflush(f);

    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    buf = (char*)malloc((size_t)size + 1u);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

int main(void) {
    char* code = emit_dispatch_to_string();
    if (!code) {
        check(0, "emit dispatch helpers");
        printf("DISPATCH,total,%d passed %d failed\n", pass_count, fail_count);
        return 1;
    }

    check(strstr(code, "dolrecomp_find_original") != NULL,
          "emits original lookup helper");
    check(strstr(code, "dolrecomp_call_original") != NULL,
          "emits original call helper");
    check(strstr(code, "func_80003000,") != NULL &&
          strstr(code, "func_80003040,") != NULL &&
          strstr(code, "func_80003080,") != NULL &&
          strstr(code, "return func_80004000;") != NULL,
          "original lookup covers generated chunks");
    check(strstr(code, "static const DolRecompFunction chunk_functions[]") != NULL &&
          strstr(code, "return chunk_functions[offset / 0x00000040u];") != NULL,
          "contiguous chunks use indexed dispatch");
    check(strstr(code, "ctx->pc = address;") != NULL,
          "call helpers set the entry pc");
    check(strstr(code, "#if defined(DOLRECOMP_ENABLE_REPLACEMENTS)") != NULL &&
          strstr(code,
                 "if (dolrecomp_dispatch_replacement(ctx, address)) return 1;") != NULL,
          "public dispatcher supports module replacements");
    check(strstr(code,
                 "if (ctx->host_call && ppc_host_call(ctx, address)) return 1;") != NULL,
          "public dispatcher checks installed host replacements first");
    check(strstr(code, "dolrecomp_physical_pc_alias") != NULL &&
          strstr(code,
                 "if (ctx->host_call && ppc_host_call(ctx, alias)) return 1;") != NULL &&
          strstr(code, "if (dolrecomp_call_original(ctx, alias)) return 1;") != NULL,
          "public dispatcher retries physical MEM1 aliases");
    check(strstr(code, "if (dolrecomp_call_original(ctx, address)) return 1;") != NULL,
          "public dispatcher can fall back to original code");

    free(code);

    // DOLRECOMP_DISPATCH_LOOKUP=indexed. The linear chain is O(chunks) on an
    // irregular plan and that confounded E008; the indexed form must replace
    // it without changing which chunk an address resolves to.
    if (set_lookup_mode("indexed")) {
        char* indexed = emit_dispatch_to_string();
        if (!indexed) {
            check(0, "indexed: emit dispatch helpers");
        } else {
            check(strstr(indexed, "dolrecomp_page_first[DOLRECOMP_LOOKUP_PAGES]") != NULL &&
                  strstr(indexed, "run = dolrecomp_page_first[page];") != NULL,
                  "indexed: page index selects the run window");
            check(strstr(indexed, "if (address >= 0x80003000u && address < 0x80003040u") == NULL,
                  "indexed: no linear range-test chain remains");
            check(strstr(indexed, "#define DOLRECOMP_LOOKUP_RUNS 2u") != NULL,
                  "indexed: collapses the contiguous chunks into one run");
            // 0x80003000..0x800040a0 spans two 4 KiB pages plus the boundary page.
            check(strstr(indexed, "#define DOLRECOMP_LOOKUP_BASE 0x80003000u") != NULL &&
                  strstr(indexed, "#define DOLRECOMP_LOOKUP_PAGES 2u") != NULL,
                  "indexed: page table covers exactly the emitted code");
            check(strstr(indexed, "func_80003000,") != NULL &&
                  strstr(indexed, "func_80003040,") != NULL &&
                  strstr(indexed, "func_80003080,") != NULL &&
                  strstr(indexed, "func_80004000,") != NULL,
                  "indexed: chunk table covers generated chunks");
            check(strstr(indexed, "if ((offset & 3u) != 0u) return NULL;") != NULL,
                  "indexed: keeps the instruction-alignment check");
            check(strstr(indexed, "ctx->pc = address;") != NULL &&
                  strstr(indexed, "dolrecomp_physical_pc_alias") != NULL,
                  "indexed: leaves the rest of the dispatcher alone");
            free(indexed);
        }
        set_lookup_mode(NULL);
    } else {
        check(0, "indexed: set DOLRECOMP_DISPATCH_LOOKUP");
    }

    printf("DISPATCH,total,%d passed %d failed\n", pass_count, fail_count);
    return fail_count == 0 ? 0 : 1;
}
