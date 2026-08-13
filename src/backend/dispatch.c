#include "backend/dispatch.h"
#include "common/options.h"
#include "cpu/cpu.h"
#include <stdlib.h>
#include <string.h>

// How dolrecomp_find_original resolves a guest address to a generated chunk.
//
// "linear" is the historical emission and stays the default: a chain of range
// tests, with consecutive equal-stride chunks collapsed into one indexed jump
// table. That is O(1) when every chunk is the same size -- a fixed-128 plan on
// this title emits exactly 2 tests -- and O(chunks) when they are not. The
// control-flow-aligned plans of E008 emitted 2,089 (func/128/512) and 5,224
// (func/32/256) tests, taken on every dispatch into the module, and those two
// arms rank by chain length exactly as they rank by measured slowness. So no
// throughput number from an irregular-boundary plan is interpretable under
// this lookup. See docs/LLVM-EXPERIMENTS.md W001, "Ops finding".
//
// "indexed" removes that confound: a 4 KiB page index over the covered guest
// range selects a small window of runs, and the window is walked forward. The
// walk is bounded by the number of runs that intersect one page, so the lookup
// is O(1) in the number of chunks regardless of how irregular the plan is.
//
// The default is unchanged because the shipping C module is pinned by hash and
// must stay byte-identical; this selects an experiment, not a new default.
typedef enum {
    DISPATCH_LOOKUP_LINEAR = 0,
    DISPATCH_LOOKUP_INDEXED = 1,
    /* Decide from the plan's shape. See dispatch_lookup_mode(). */
    DISPATCH_LOOKUP_AUTO = 2
} DispatchLookupMode;

/* Above this many runs, the linear chain stops being a chain worth walking.
 *
 * A uniform plan collapses to a handful of runs -- fixed 128-instruction chunks
 * produce four -- and the linear form is then a couple of range tests plus a
 * table index, which nothing beats. An irregular plan does not collapse: Mario
 * Kart's 4,017 planned regions emitted 8,284 address comparisons, and walking
 * those on every dispatch made the region build ~8x slower than the fixed one
 * at an identical dispatcher rate. The page index costs one u32 per 4 KiB page
 * and a short bounded walk.
 *
 * 64 is far above what any uniform plan produces and far below the thousands an
 * irregular one does, so the choice is never close. */
#define DISPATCH_LINEAR_RUN_LIMIT 64u

// A 4 KiB page is small enough that a page holds only a handful of runs even
// under the most irregular plan measured here (E008a's mean chunk was 87
// instructions, so ~11 per page), and the whole index is one u32 per page.
#define DISPATCH_PAGE_SHIFT 12u

// Refuse the page index if the code sections are scattered far enough apart
// that the table would dwarf the thing it indexes. 256 MiB of guest address
// space is 65,536 pages, 256 KiB of table; MEM1 is 24 MiB, so this never fires
// on a GameCube title and exists so a bad input degrades rather than explodes.
#define DISPATCH_MAX_INDEX_PAGES 65536u

static DispatchLookupMode dispatch_lookup_mode(void) {
    const char* configured = getenv("DOLRECOMP_DISPATCH_LOOKUP");
    if (!configured || !configured[0])
        return DISPATCH_LOOKUP_AUTO;
    if (!strcmp(configured, "auto"))
        return DISPATCH_LOOKUP_AUTO;
    if (!strcmp(configured, "indexed"))
        return DISPATCH_LOOKUP_INDEXED;
    if (!strcmp(configured, "linear"))
        return DISPATCH_LOOKUP_LINEAR;
    fprintf(stderr,
            "warning: DOLRECOMP_DISPATCH_LOOKUP must be linear|indexed|auto; "
            "using auto\n");
    return DISPATCH_LOOKUP_AUTO;
}

static u32 uniform_run_end(const FunctionList* funcs, u32 first);

/* How many runs the linear chain would emit. Runs, not ranges: consecutive
   equal-width ranges collapse into one table, which is why a uniform plan stays
   cheap however many chunks it has. */
static u32 linear_run_count(const FunctionList* funcs) {
    u32 runs = 0;
    for (u32 first = 0; first < funcs->count; runs++)
        first = uniform_run_end(funcs, first);
    return runs;
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

static int range_compare(const void* a, const void* b) {
    const FunctionRange* left = (const FunctionRange*)a;
    const FunctionRange* right = (const FunctionRange*)b;
    if (left->start != right->start)
        return left->start < right->start ? -1 : 1;
    if (left->end != right->end)
        return left->end < right->end ? -1 : 1;
    return 0;
}

// Emit the page-indexed lookup. Returns 0 if this plan cannot be indexed, in
// which case the caller falls back to the linear chain -- correctness never
// depends on which one is emitted.
static int emit_lookup_indexed(FILE* out, const FunctionList* funcs) {
    FunctionRange* sorted = NULL;
    u32* run_first = NULL;
    u32* page_first = NULL;
    u32 sorted_count = 0;
    u32 run_count = 0;
    u32 page_count = 0;
    u32 base = 0;
    u32 limit = 0;
    u32 max_per_page = 0;
    FunctionList view = {0};

    if (funcs->count == 0)
        return 0;

    sorted = (FunctionRange*)malloc(funcs->count * sizeof(*sorted));
    if (!sorted)
        return 0;

    // An empty range can never match, and it would divide by zero below.
    for (u32 i = 0; i < funcs->count; i++) {
        if (funcs->ranges[i].start < funcs->ranges[i].end)
            sorted[sorted_count++] = funcs->ranges[i];
    }
    if (sorted_count == 0) {
        free(sorted);
        return 0;
    }
    qsort(sorted, sorted_count, sizeof(*sorted), range_compare);

    // The walk below assumes ranges are disjoint, so that the first run whose
    // end passes the address is the only run that can contain it. The linear
    // chain instead returns the first range that matches in list order, so on
    // an overlapping plan the two would disagree. Refuse rather than differ.
    for (u32 i = 1; i < sorted_count; i++) {
        if (sorted[i].start < sorted[i - 1].end) {
            fprintf(stderr,
                    "warning: DOLRECOMP_DISPATCH_LOOKUP=indexed needs disjoint "
                    "chunk ranges (0x%08X overlaps 0x%08X); using linear\n",
                    sorted[i].start, sorted[i - 1].start);
            free(sorted);
            return 0;
        }
    }

    base = sorted[0].start & ~((1u << DISPATCH_PAGE_SHIFT) - 1u);
    limit = sorted[sorted_count - 1].end;
    page_count = ((limit - base) >> DISPATCH_PAGE_SHIFT) + 1u;
    if (page_count > DISPATCH_MAX_INDEX_PAGES) {
        fprintf(stderr,
                "warning: DOLRECOMP_DISPATCH_LOOKUP=indexed needs the code "
                "sections within %u pages (got %u); using linear\n",
                DISPATCH_MAX_INDEX_PAGES, page_count);
        free(sorted);
        return 0;
    }

    // Reuse uniform_run_end so the runs -- and therefore the emitted function
    // table -- are exactly the ones the linear chain would have produced.
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

    // page_first[p] is the first run whose end passes the start of page p, so
    // the forward walk from it can only skip runs that also intersect the page.
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

    // The walk length is bounded by the runs intersecting one page. Report it,
    // because it is the number that makes the lookup O(1) rather than O(runs).
    {
        u32 run = 0;
        for (u32 page = 0; page < page_count; page++) {
            u32 page_end = base + ((page + 1u) << DISPATCH_PAGE_SHIFT);
            u32 here = 0;
            for (run = page_first[page];
                 run < run_count && sorted[run_first[run]].start < page_end;
                 run++)
                here++;
            if (here > max_per_page)
                max_per_page = here;
        }
    }

    /* Coverage manifest.
     *
     * The module template's gen_module_tables.py recovers a module's address
     * coverage by grepping this header for the dispatcher's own range tests.
     * The page index does not emit those tests, so switching to it left the
     * tool with nothing to find and the module build failed with "no coverage
     * ranges found".
     *
     * Restating the ranges here in the offset-table form that tool already
     * recognises keeps the contract without coupling the two repositories over
     * it. The regex scans raw text, so a comment satisfies it: none of this is
     * compiled, and the emitted lookup below is unaffected. */
    fprintf(out,
            "\n/* Coverage manifest for gen_module_tables.py. Not compiled --\n"
            " * the page index below is the actual lookup. Restated in the\n"
            " * offset-table form that tool greps for.\n");
    for (u32 i = 0; i < run_count; i++) {
        u32 start = sorted[run_first[i]].start;
        u32 end = sorted[run_first[i + 1u] - 1u].end;
        fprintf(out, " * u32 offset = address - 0x%08Xu; if (offset < 0x%08Xu)\n",
                start, end - start);
    }
    fprintf(out, " */\n");

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

    // A run of one chunk gets its own width as the stride, so the division
    // always yields index 0 and no branch is needed to special-case it.
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

    fprintf(out, "\nstatic inline DolRecompFunction dolrecomp_find_original(u32 address) {\n");
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

static void emit_lookup_linear(FILE* out, const FunctionList* funcs) {
    fprintf(out, "\nstatic inline DolRecompFunction dolrecomp_find_original(u32 address) {\n");
    for (u32 first = 0; first < funcs->count;) {
        u32 end = uniform_run_end(funcs, first);
        emit_lookup_run(out, funcs, first, end);
        first = end;
    }
    fprintf(out, "    return NULL;\n");
    fprintf(out, "}\n");
}

/* The fast memory mode bakes two runtime facts into every load and store (see
   memoryModeFast() in llvm_memory_lowering.cpp). Baking in an assumption that
   silently stops holding is how a recompiler corrupts guest memory, so the
   generated code checks both once and refuses to run natively if either fails:
   returning 0 from dolrecomp_call leaves the chassis interpreting, which is
   slow but right.

   Checked once rather than per access. The flag is written only after a
   successful check, so the cost on the hot path is one predictable branch on a
   value that never changes. */
static void emit_memory_mode_guard(FILE* out, int uses_fast_memory) {
    if (!uses_fast_memory) {
        fprintf(out, "\nstatic inline int dolrecomp_memory_mode_ok(CPUState* ctx) {\n");
        fprintf(out, "    (void)ctx;\n");
        fprintf(out, "    return 1;\n");
        fprintf(out, "}\n");
        return;
    }
    fprintf(out, "\n/* Built with --memory-mode fast. */\n");
    /* Only in fast mode: the guard is the sole user of stdio in generated
       output, and a default build's header must stay byte-identical to what it
       emitted before this option existed. */
    fprintf(out, "#include <stdio.h>\n");
    fprintf(out, "static int dolrecomp_memory_mode_state = 0;\n");
    fprintf(out, "static int dolrecomp_memory_mode_check(CPUState* ctx) {\n");
    fprintf(out, "    if (ctx->ram_size != %uu) {\n", (unsigned)GC_MAIN_RAM_SIZE);
    fprintf(out, "        fprintf(stderr, \"dolrecomp: --memory-mode fast expects ram_size %%u,\"\n");
    fprintf(out, "                        \" runtime has %%u; falling back to the interpreter\\n\",\n");
    fprintf(out, "                %uu, ctx->ram_size);\n", (unsigned)GC_MAIN_RAM_SIZE);
    fprintf(out, "        return -1;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    if (g_mem_write_journal) {\n");
    fprintf(out, "        fprintf(stderr, \"dolrecomp: --memory-mode fast omits the write journal,\"\n");
    fprintf(out, "                        \" but one is installed; falling back to the interpreter\\n\");\n");
    fprintf(out, "        return -1;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return 1;\n");
    fprintf(out, "}\n");
    fprintf(out, "static inline int dolrecomp_memory_mode_ok(CPUState* ctx) {\n");
    fprintf(out, "    if (dolrecomp_memory_mode_state == 0)\n");
    fprintf(out, "        dolrecomp_memory_mode_state = dolrecomp_memory_mode_check(ctx);\n");
    fprintf(out, "    return dolrecomp_memory_mode_state > 0;\n");
    fprintf(out, "}\n");
}

void emit_dispatch_helpers(FILE* out, const FunctionList* funcs, u32 entry_point,
                           int uses_fast_memory) {
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
    {
        DispatchLookupMode mode = dispatch_lookup_mode();
        int want_indexed = mode == DISPATCH_LOOKUP_INDEXED;
        if (mode == DISPATCH_LOOKUP_AUTO) {
            u32 runs = linear_run_count(funcs);
            want_indexed = runs > DISPATCH_LINEAR_RUN_LIMIT;
            if (want_indexed) {
                printf("  dispatch: %u lookup runs, using page index\n", runs);
            }
        }
        /* Correctness never depends on which one is emitted: the index refuses
           plans it cannot represent and falls back here. */
        if (!want_indexed || !emit_lookup_indexed(out, funcs))
            emit_lookup_linear(out, funcs);
    }
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
    emit_memory_mode_guard(out, uses_fast_memory);
    fprintf(out, "\nstatic inline int dolrecomp_call(CPUState* ctx, u32 address) {\n");
    fprintf(out, "    u32 alias;\n");
    fprintf(out, "    ctx->pc = address;\n");
    /* Before any generated body runs, so a runtime that violates the fast
       mode's assumptions never executes code built on them. */
    fprintf(out, "    if (!dolrecomp_memory_mode_ok(ctx)) return 0;\n");
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
