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

static u32 uniform_run_end(const FunctionList* funcs, u32 first) {
    const FunctionRange* first_range = &funcs->ranges[first];
    if (first_range->start >= first_range->end)
        return first + 1u;

    u32 stride = first_range->end - first_range->start;
    if ((stride & 3u) != 0u)
        return first + 1u;

    // Split sections use equal-sized chunks followed by at most one short chunk.
    u32 end = first + 1u;
    while (end < funcs->count) {
        const FunctionRange* previous = &funcs->ranges[end - 1u];
        const FunctionRange* current = &funcs->ranges[end];
        if (previous->end != current->start ||
            current->start >= current->end)
            break;

        u32 width = current->end - current->start;
        if (width > stride)
            break;

        end++;
        if (width != stride)
            break;
    }

    return end;
}

static void emit_lookup_run(FILE* out, const FunctionList* funcs,
                            u32 first, u32 end) {
    const FunctionRange* first_range = &funcs->ranges[first];

    if (end == first + 1u || first_range->start >= first_range->end) {
        fprintf(out,
                "    if (address >= 0x%08Xu && address < 0x%08Xu && "
                "((address - 0x%08Xu) & 3u) == 0u) return func_%08X;\n",
                first_range->start, first_range->end,
                first_range->start, first_range->start);
        return;
    }

    const FunctionRange* last_range = &funcs->ranges[end - 1u];
    u32 stride = first_range->end - first_range->start;
    u32 span = last_range->end - first_range->start;

    fprintf(out, "    {\n");
    fprintf(out, "        u32 offset = address - 0x%08Xu;\n",
            first_range->start);
    fprintf(out,
            "        if (offset < 0x%08Xu && (offset & 3u) == 0u) {\n",
            span);
    fprintf(out,
            "            static const DolRecompFunction chunk_functions[] = {\n");
    for (u32 i = first; i < end; i++) {
        fprintf(out, "                func_%08X,\n", funcs->ranges[i].start);
    }
    fprintf(out, "            };\n");
    fprintf(out, "            return chunk_functions[offset / 0x%08Xu];\n",
            stride);
    fprintf(out, "        }\n");
    fprintf(out, "    }\n");
}

void emit_dispatch_helpers(FILE* out, const FunctionList* funcs, u32 entry_point) {
    fprintf(out, "\n#define DOLRECOMP_ENTRY_POINT 0x%08Xu\n", entry_point);
    fprintf(out, "\ntypedef void (*DolRecompFunction)(CPUState* ctx);\n");
    fprintf(out, "\nstatic inline DolRecompFunction dolrecomp_find_original(u32 address) {\n");
    for (u32 first = 0; first < funcs->count;) {
        u32 end = uniform_run_end(funcs, first);
        emit_lookup_run(out, funcs, first, end);
        first = end;
    }
    fprintf(out, "    return NULL;\n");
    fprintf(out, "}\n");
    fprintf(out, "\nstatic inline int dolrecomp_call_original(CPUState* ctx, u32 address) {\n");
    fprintf(out, "    DolRecompFunction fn = dolrecomp_find_original(address);\n");
    fprintf(out, "    if (!fn) return 0;\n");
    fprintf(out, "    ctx->pc = address;\n");
    fprintf(out, "    fn(ctx);\n");
    fprintf(out, "    return 1;\n");
    fprintf(out, "}\n");
    fprintf(out, "\nstatic inline bool dolrecomp_physical_pc_alias(CPUState* ctx, u32 address, u32* alias_out) {\n");
    fprintf(out, "    if (address < ctx->ram_size) {\n");
    fprintf(out, "        *alias_out = address | GC_RAM_BASE;\n");
    fprintf(out, "        return *alias_out != address;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return false;\n");
    fprintf(out, "}\n");
    fprintf(out, "\nstatic inline int dolrecomp_call(CPUState* ctx, u32 address) {\n");
    fprintf(out, "    u32 alias;\n");
    fprintf(out, "    ctx->pc = address;\n");
    fprintf(out, "    if (ctx->host_call && ppc_host_call(ctx, address)) return 1;\n");
    fprintf(out, "    if (dolrecomp_call_original(ctx, address)) return 1;\n");
    fprintf(out, "    if (dolrecomp_physical_pc_alias(ctx, address, &alias)) {\n");
    fprintf(out, "        ctx->pc = alias;\n");
    fprintf(out, "        if (ctx->host_call && ppc_host_call(ctx, alias)) return 1;\n");
    fprintf(out, "        if (dolrecomp_call_original(ctx, alias)) return 1;\n");
    fprintf(out, "    }\n");
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
