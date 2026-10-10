#include "backend/dispatch.h"
#include <stdlib.h>
#include <string.h>

typedef enum {
    DISPATCH_LOOKUP_LINEAR = 0,
    DISPATCH_LOOKUP_INDEXED = 1
} DispatchLookupMode;

#define DISPATCH_PAGE_SHIFT 12u
#define DISPATCH_MAX_INDEX_PAGES 65536u

static DispatchLookupMode dispatch_lookup_mode(void) {
    const char* configured = getenv("DOLRECOMP_DISPATCH_LOOKUP");
    if (!configured || !configured[0] || !strcmp(configured, "linear"))
        return DISPATCH_LOOKUP_LINEAR;
    if (!strcmp(configured, "indexed"))
        return DISPATCH_LOOKUP_INDEXED;
    fprintf(stderr,
            "warning: DOLRECOMP_DISPATCH_LOOKUP must be linear|indexed; using "
            "linear\n");
    return DISPATCH_LOOKUP_LINEAR;
}

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
    return function_list_add_hashed(list, start, end, 0);
}

int function_list_add_hashed(FunctionList* list, u32 start, u32 end, u64 hash) {
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
    list->ranges[list->count].hash = hash;
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
                            u32 first, u32 end, const char* symbol_suffix) {
    const FunctionRange* first_range = &funcs->ranges[first];

    if (end == first + 1u || first_range->start >= first_range->end) {
        fprintf(out,
                "    if (address >= 0x%08Xu && address < 0x%08Xu && "
                "((address - 0x%08Xu) & 3u) == 0u) return func_%08X%s;\n",
                first_range->start, first_range->end,
                first_range->start, first_range->start, symbol_suffix);
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
        fprintf(out, "                func_%08X%s,\n",
                funcs->ranges[i].start, symbol_suffix);
    }
    fprintf(out, "            };\n");
    fprintf(out, "            return chunk_functions[offset / 0x%08Xu];\n",
            stride);
    fprintf(out, "        }\n");
    fprintf(out, "    }\n");
}

static int range_compare(const void* a, const void* b) {
    const FunctionRange* left = (const FunctionRange*)a;
    const FunctionRange* right = (const FunctionRange*)b;
    if (left->start != right->start)
        return left->start < right->start ? -1 : 1;
    if (left->end != right->end)
        return left->end < right->end ? -1 : 1;
    return 0;
}

static int emit_lookup_indexed(FILE* out, const FunctionList* funcs) {
    FunctionRange* sorted = NULL;
    u32* run_first = NULL;
    u32* page_first = NULL;
    u32 sorted_count = 0;
    u32 run_count = 0;
    u32 page_count;
    u32 base;
    u32 limit;
    u32 max_per_page = 0;
    FunctionList view = {0};

    if (funcs->count == 0)
        return 0;

    sorted = (FunctionRange*)malloc(funcs->count * sizeof(*sorted));
    if (!sorted)
        return 0;
    for (u32 i = 0; i < funcs->count; i++) {
        if (funcs->ranges[i].start < funcs->ranges[i].end)
            sorted[sorted_count++] = funcs->ranges[i];
    }
    if (sorted_count == 0) {
        free(sorted);
        return 0;
    }
    qsort(sorted, sorted_count, sizeof(*sorted), range_compare);

    for (u32 i = 1; i < sorted_count; i++) {
        if (sorted[i].start < sorted[i - 1].end) {
            fprintf(stderr,
                    "warning: indexed dispatch needs disjoint chunk ranges; "
                    "using linear\n");
            free(sorted);
            return 0;
        }
    }

    base = sorted[0].start & ~((1u << DISPATCH_PAGE_SHIFT) - 1u);
    limit = sorted[sorted_count - 1].end;
    page_count = ((limit - base) >> DISPATCH_PAGE_SHIFT) + 1u;
    if (page_count > DISPATCH_MAX_INDEX_PAGES) {
        free(sorted);
        return 0;
    }

    view.ranges = sorted;
    view.count = sorted_count;
    view.capacity = sorted_count;
    run_first = (u32*)malloc((sorted_count + 1u) * sizeof(*run_first));
    page_first = (u32*)malloc(page_count * sizeof(*page_first));
    if (!run_first || !page_first) {
        free(run_first);
        free(page_first);
        free(sorted);
        return 0;
    }

    for (u32 first = 0; first < sorted_count;) {
        run_first[run_count++] = first;
        first = uniform_run_end(&view, first);
    }
    run_first[run_count] = sorted_count;

    {
        u32 run = 0;
        for (u32 page = 0; page < page_count; page++) {
            u32 page_start = base + (page << DISPATCH_PAGE_SHIFT);
            while (run < run_count &&
                   sorted[run_first[run + 1u] - 1u].end <= page_start)
                run++;
            page_first[page] = run;
        }
    }

    for (u32 page = 0; page < page_count; page++) {
        u32 page_end = base + ((page + 1u) << DISPATCH_PAGE_SHIFT);
        u32 here = 0;
        for (u32 run = page_first[page];
             run < run_count && sorted[run_first[run]].start < page_end;
             run++)
            here++;
        if (here > max_per_page)
            max_per_page = here;
    }

    fprintf(out, "\n#define DOLRECOMP_LOOKUP_RUNS %uu\n", run_count);
    fprintf(out, "#define DOLRECOMP_LOOKUP_BASE 0x%08Xu\n", base);
    fprintf(out, "#define DOLRECOMP_LOOKUP_PAGES %uu\n", page_count);
    fprintf(out, "#define DOLRECOMP_LOOKUP_PAGE_SHIFT %uu\n",
            DISPATCH_PAGE_SHIFT);

    fprintf(out,
            "\nstatic const u32 dolrecomp_run_start[DOLRECOMP_LOOKUP_RUNS] "
            "DOLRECOMP_UNUSED = {\n");
    for (u32 i = 0; i < run_count; i++)
        fprintf(out, "    0x%08Xu,\n", sorted[run_first[i]].start);
    fprintf(out, "};\n");

    fprintf(out,
            "\nstatic const u32 dolrecomp_run_end[DOLRECOMP_LOOKUP_RUNS] "
            "DOLRECOMP_UNUSED = {\n");
    for (u32 i = 0; i < run_count; i++)
        fprintf(out, "    0x%08Xu,\n", sorted[run_first[i + 1u] - 1u].end);
    fprintf(out, "};\n");

    fprintf(out,
            "\nstatic const u32 dolrecomp_run_stride[DOLRECOMP_LOOKUP_RUNS] "
            "DOLRECOMP_UNUSED = {\n");
    for (u32 i = 0; i < run_count; i++) {
        const FunctionRange* head = &sorted[run_first[i]];
        fprintf(out, "    0x%08Xu,\n", head->end - head->start);
    }
    fprintf(out, "};\n");

    fprintf(out,
            "\nstatic const u32 dolrecomp_run_base[DOLRECOMP_LOOKUP_RUNS] "
            "DOLRECOMP_UNUSED = {\n");
    for (u32 i = 0; i < run_count; i++)
        fprintf(out, "    %uu,\n", run_first[i]);
    fprintf(out, "};\n");

    fprintf(out,
            "\nstatic const DolRecompFunction "
            "dolrecomp_run_chunks[%uu] DOLRECOMP_UNUSED = {\n",
            sorted_count);
    for (u32 i = 0; i < sorted_count; i++)
        fprintf(out, "    func_%08X,\n", sorted[i].start);
    fprintf(out, "};\n");

    fprintf(out,
            "\nstatic const u32 dolrecomp_page_first[DOLRECOMP_LOOKUP_PAGES] "
            "DOLRECOMP_UNUSED = {\n");
    for (u32 i = 0; i < page_count; i++)
        fprintf(out, "    %uu,\n", page_first[i]);
    fprintf(out, "};\n");

    fprintf(out,
            "\nstatic inline DolRecompFunction "
            "dolrecomp_find_original(u32 address) {\n");
    fprintf(out, "    u32 page;\n");
    fprintf(out, "    u32 run;\n");
    fprintf(out, "    u32 offset;\n");
    fprintf(out, "    if (address < DOLRECOMP_LOOKUP_BASE) return NULL;\n");
    fprintf(out,
            "    page = (address - DOLRECOMP_LOOKUP_BASE) >> "
            "DOLRECOMP_LOOKUP_PAGE_SHIFT;\n");
    fprintf(out, "    if (page >= DOLRECOMP_LOOKUP_PAGES) return NULL;\n");
    fprintf(out, "    run = dolrecomp_page_first[page];\n");
    fprintf(out,
            "    while (run < DOLRECOMP_LOOKUP_RUNS && "
            "dolrecomp_run_end[run] <= address) run++;\n");
    fprintf(out,
            "    if (run >= DOLRECOMP_LOOKUP_RUNS || "
            "address < dolrecomp_run_start[run]) return NULL;\n");
    fprintf(out, "    offset = address - dolrecomp_run_start[run];\n");
    fprintf(out, "    if ((offset & 3u) != 0u) return NULL;\n");
    fprintf(out,
            "    return dolrecomp_run_chunks[dolrecomp_run_base[run] + "
            "offset / dolrecomp_run_stride[run]];\n");
    fprintf(out, "}\n");

    printf("  dispatch lookup: indexed, %u chunks in %u runs, %u pages, "
           "at most %u run%s walked per lookup\n",
           sorted_count, run_count, page_count, max_per_page,
           max_per_page == 1u ? "" : "s");

    free(run_first);
    free(page_first);
    free(sorted);
    return 1;
}

// Past a few uniform runs, binary search beats a linear chain of range checks.
#define DISPATCH_LINEAR_MAX_RUNS 16u

static int emit_lookup_search(FILE* out, const FunctionList* funcs,
                              const char* symbol_suffix) {
    FunctionRange* sorted =
        (FunctionRange*)malloc(funcs->count * sizeof(*sorted));
    if (!sorted)
        return 0;
    u32 count = 0;
    for (u32 i = 0; i < funcs->count; i++) {
        if (funcs->ranges[i].start < funcs->ranges[i].end)
            sorted[count++] = funcs->ranges[i];
    }
    if (count == 0) {
        free(sorted);
        return 0;
    }
    qsort(sorted, count, sizeof(*sorted), range_compare);
    // Overlapping ranges keep the linear form to preserve first-match order.
    for (u32 i = 1; i < count; i++) {
        if (sorted[i - 1u].end > sorted[i].start) {
            free(sorted);
            return 0;
        }
    }

    // Coverage guard; module-table generators scrape this form.
    fprintf(out, "    if (!(");
    for (u32 i = 0; i < count;) {
        u32 span_end = i + 1u;
        while (span_end < count && sorted[span_end - 1u].end == sorted[span_end].start)
            span_end++;
        fprintf(out, "%s\n          (address >= 0x%08Xu && address < 0x%08Xu)",
                i ? " ||" : "", sorted[i].start, sorted[span_end - 1u].end);
        i = span_end;
    }
    fprintf(out, "))\n        return NULL;\n");
    fprintf(out, "    static const u32 range_start[%u] = {\n", count);
    for (u32 i = 0; i < count; i++)
        fprintf(out, "        0x%08Xu,\n", sorted[i].start);
    fprintf(out, "    };\n");
    fprintf(out, "    static const u32 range_end[%u] = {\n", count);
    for (u32 i = 0; i < count; i++)
        fprintf(out, "        0x%08Xu,\n", sorted[i].end);
    fprintf(out, "    };\n");
    fprintf(out, "    static const DolRecompFunction range_function[%u] = {\n",
            count);
    for (u32 i = 0; i < count; i++)
        fprintf(out, "        func_%08X%s,\n", sorted[i].start, symbol_suffix);
    fprintf(out, "    };\n");
    fprintf(out, "    u32 low = 0u;\n");
    fprintf(out, "    u32 high = %uu;\n", count);
    fprintf(out, "    while (low < high) {\n");
    fprintf(out, "        u32 middle = low + ((high - low) >> 1);\n");
    fprintf(out, "        if (range_start[middle] <= address)\n");
    fprintf(out, "            low = middle + 1u;\n");
    fprintf(out, "        else\n");
    fprintf(out, "            high = middle;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    if (low == 0u)\n");
    fprintf(out, "        return NULL;\n");
    fprintf(out, "    low--;\n");
    fprintf(out,
            "    if (address < range_end[low] && "
            "((address - range_start[low]) & 3u) == 0u)\n");
    fprintf(out, "        return range_function[low];\n");
    fprintf(out, "    return NULL;\n");
    fprintf(out, "}\n");
    free(sorted);
    return 1;
}

void emit_function_lookup(FILE* out, const FunctionList* funcs,
                          const char* symbol_suffix) {
    fprintf(out,
            "\nstatic inline DolRecompFunction "
            "dolrecomp_find_original%s(u32 address) {\n",
            symbol_suffix);
    u32 run_count = 0;
    for (u32 first = 0; first < funcs->count; run_count++)
        first = uniform_run_end(funcs, first);
    if (run_count > DISPATCH_LINEAR_MAX_RUNS &&
        emit_lookup_search(out, funcs, symbol_suffix))
        return;
    for (u32 first = 0; first < funcs->count;) {
        u32 end = uniform_run_end(funcs, first);
        emit_lookup_run(out, funcs, first, end, symbol_suffix);
        first = end;
    }
    fprintf(out, "    return NULL;\n");
    fprintf(out, "}\n");
}

void emit_dispatch_helpers(FILE* out, const FunctionList* funcs, u32 entry_point) {
    fprintf(out, "\n#define DOLRECOMP_ENTRY_POINT 0x%08Xu\n", entry_point);
    fprintf(out, "\ntypedef void (*DolRecompFunction)(CPUState* ctx);\n");
    fprintf(out, "\n#if defined(__GNUC__) || defined(__clang__)\n");
    fprintf(out, "#define DOLRECOMP_UNUSED __attribute__((unused))\n");
    fprintf(out, "#else\n");
    fprintf(out, "#define DOLRECOMP_UNUSED\n");
    fprintf(out, "#endif\n");
    fprintf(out, "\n#if defined(DOLRECOMP_ENABLE_REPLACEMENTS)\n");
    fprintf(out, "int dolrecomp_dispatch_replacement(CPUState* ctx, u32 address);\n");
    fprintf(out, "#else\n");
    fprintf(out, "static inline int dolrecomp_dispatch_replacement(CPUState* ctx, u32 address) {\n");
    fprintf(out, "    (void)ctx;\n");
    fprintf(out, "    (void)address;\n");
    fprintf(out, "    return 0;\n");
    fprintf(out, "}\n");
    fprintf(out, "#endif\n");
    if (dispatch_lookup_mode() != DISPATCH_LOOKUP_INDEXED ||
        !emit_lookup_indexed(out, funcs))
        emit_function_lookup(out, funcs, "");
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
    fprintf(out, "    if (dolrecomp_dispatch_replacement(ctx, address)) return 1;\n");
    fprintf(out, "    if (ctx->host_call && ppc_host_call(ctx, address)) return 1;\n");
    fprintf(out, "    if (dolrecomp_call_original(ctx, address)) return 1;\n");
    fprintf(out, "    if (dolrecomp_physical_pc_alias(ctx, address, &alias)) {\n");
    fprintf(out, "        ctx->pc = alias;\n");
    fprintf(out, "        if (dolrecomp_dispatch_replacement(ctx, alias)) return 1;\n");
    fprintf(out, "        if (ctx->host_call && ppc_host_call(ctx, alias)) return 1;\n");
    fprintf(out, "        if (dolrecomp_call_original(ctx, alias)) return 1;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return 0;\n");
    fprintf(out, "}\n");
    fprintf(out, "\nstatic inline DOLRECOMP_UNUSED int dolrecomp_run_blocks(CPUState* ctx, u32 max_blocks) {\n");
    fprintf(out, "    u32 blocks = 0;\n");
    fprintf(out, "    while (max_blocks == 0u || blocks < max_blocks) {\n");
    fprintf(out, "        if (!dolrecomp_call(ctx, ctx->pc)) return 0;\n");
    fprintf(out, "        if (ctx->exception) return 0;\n");
    fprintf(out, "        blocks++;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return 1;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n#undef DOLRECOMP_UNUSED\n");
}
