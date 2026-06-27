#include "backend/dispatch.h"
#include <stdlib.h>
#include <string.h>

void emit_chunk_prototype(FILE* out, u32 func_addr) {
    fprintf(out, "void func_%08X(CPUState* ctx);\n", func_addr);
}

void function_list_free(FunctionList* list) {
    free(list->ranges);
    list->ranges = NULL;
    list->count = 0;
    list->capacity = 0;
}

int function_list_add(FunctionList* list, u32 start, u32 end) {
    if (list->count == list->capacity) {
        u32 new_capacity = list->capacity ? list->capacity * 2u : 64u;
        FunctionRange* new_ranges =
            (FunctionRange*)realloc(list->ranges, new_capacity * sizeof(*new_ranges));
        if (!new_ranges) {
            fprintf(stderr, "error: out of memory\n");
            return 0;
        }
        list->ranges = new_ranges;
        list->capacity = new_capacity;
    }

    list->ranges[list->count].start = start;
    list->ranges[list->count].end = end;
    list->count++;
    return 1;
}

void emit_dispatch_helpers(FILE* out, const FunctionList* funcs, u32 entry_point) {
    fprintf(out, "\n#define DOLRECOMP_ENTRY_POINT 0x%08Xu\n", entry_point);
    fprintf(out, "\ntypedef void (*DolRecompFunction)(CPUState* ctx);\n");
    fprintf(out, "\nstatic inline int dolrecomp_call(CPUState* ctx, u32 address) {\n");
    fprintf(out, "    if (ppc_host_call(ctx, address)) return 1;\n");
    for (u32 i = 0; i < funcs->count; i++) {
        fprintf(out,
                "    if (address >= 0x%08Xu && address < 0x%08Xu && "
                "((address - 0x%08Xu) & 3u) == 0u) { func_%08X(ctx); return 1; }\n",
                funcs->ranges[i].start, funcs->ranges[i].end,
                funcs->ranges[i].start, funcs->ranges[i].start);
    }
    fprintf(out, "    return 0;\n");
    fprintf(out, "}\n");
    fprintf(out, "\nstatic inline int dolrecomp_run_blocks(CPUState* ctx, u32 max_blocks) {\n");
    fprintf(out, "    u32 blocks = 0;\n");
    fprintf(out, "    while (max_blocks == 0u || blocks < max_blocks) {\n");
    fprintf(out, "        if (!dolrecomp_call(ctx, ctx->pc)) return 0;\n");
    fprintf(out, "        if (ctx->exception) return 0;\n");
    fprintf(out, "        blocks++;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return 1;\n");
    fprintf(out, "}\n");
}

