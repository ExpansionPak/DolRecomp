#include "common/perf.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

static DolPerfReport g_report;

DolPerfReport* dolperf_report(void) {
    return &g_report;
}

void dolperf_reset(DolPerfReport* report) {
    if (!report)
        return;

    DolPerfRegion* regions = report->regions;
    u32 capacity = report->region_capacity;
    memset(report, 0, sizeof(*report));
    report->regions = regions;
    report->region_capacity = capacity;
}

void dolperf_free(DolPerfReport* report) {
    if (!report)
        return;

    free(report->regions);
    report->regions = NULL;
    report->region_count = 0;
    report->region_capacity = 0;
}

u64 dolperf_now_ns(void) {
#if defined(_WIN32)
    static LARGE_INTEGER frequency;
    if (frequency.QuadPart == 0)
        QueryPerformanceFrequency(&frequency);

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    /* Split the division so a long-running process cannot overflow the
       multiply before the divide. */
    u64 ticks = (u64)now.QuadPart;
    u64 freq = (u64)frequency.QuadPart;
    if (freq == 0)
        return 0;
    return (ticks / freq) * 1000000000ull +
           ((ticks % freq) * 1000000000ull) / freq;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
#endif
}

void dolperf_add_region(DolPerfReport* report, const DolPerfRegion* region) {
    if (!report || !region)
        return;

    if (report->region_count == report->region_capacity) {
        u32 capacity = report->region_capacity ? report->region_capacity * 2u : 64u;
        DolPerfRegion* grown =
            (DolPerfRegion*)realloc(report->regions, capacity * sizeof(*grown));
        if (!grown)
            return; /* Instrumentation must never fail a build. */
        report->regions = grown;
        report->region_capacity = capacity;
    }

    report->regions[report->region_count++] = *region;

    report->counters.regions_planned++;
    report->counters.region_guest_instructions += region->guest_instructions;
    report->counters.region_ir_instructions += region->ir_instructions;
    report->counters.region_code_bytes += region->code_bytes;
    report->counters.llvm_optimize_ns += region->optimize_ns;
    report->counters.llvm_codegen_ns += region->codegen_ns;
    if (region->cache_hit)
        report->counters.artifact_cache_hits++;
    else
        report->counters.artifact_cache_misses++;
}

void dolperf_merge_runtime(DolPerfReport* report, const DolPerfCounters* from) {
    if (!report || !from)
        return;

#define DOLRECOMP_PERF_MERGE(field, json, label, group)                        \
    report->counters.field += from->field;
    DOLRECOMP_PERF_RUNTIME_COUNTERS(DOLRECOMP_PERF_MERGE)
#undef DOLRECOMP_PERF_MERGE
}

static void write_json_string(FILE* out, const char* text) {
    fputc('"', out);
    for (const char* p = text ? text : ""; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        switch (ch) {
        case '"':  fputs("\\\"", out); break;
        case '\\': fputs("\\\\", out); break;
        case '\n': fputs("\\n", out); break;
        case '\r': fputs("\\r", out); break;
        case '\t': fputs("\\t", out); break;
        default:
            if (ch < 0x20)
                fprintf(out, "\\u%04x", ch);
            else
                fputc((int)ch, out);
            break;
        }
    }
    fputc('"', out);
}

static void write_json_field(FILE* out, const char* name, const char* value) {
    fputs("    ", out);
    write_json_string(out, name);
    fputs(": ", out);
    write_json_string(out, value);
    fputs(",\n", out);
}

bool dolperf_write_json(const DolPerfReport* report, const char* path,
                        FILE* diagnostics) {
    if (!report || !path)
        return false;

    FILE* out = fopen(path, "wb");
    if (!out) {
        if (diagnostics)
            fprintf(diagnostics, "error: cannot write perf report '%s'\n", path);
        return false;
    }

    fputs("{\n", out);
    fputs("  \"schema\": \"dolrecomp.perf/1\",\n", out);

    fputs("  \"build\": {\n", out);
    write_json_field(out, "backend", report->backend);
    write_json_field(out, "region_mode", report->region_mode);
    write_json_field(out, "target_triple", report->target_triple);
    write_json_field(out, "target_cpu", report->target_cpu);
    write_json_field(out, "target_features", report->target_features);
    write_json_field(out, "lto", report->lto_mode);
    write_json_field(out, "pgo", report->pgo_mode);
    write_json_field(out, "pgo_profile_hash", report->pgo_profile_hash);
    write_json_field(out, "mod_policy", report->mod_policy);
    write_json_field(out, "memory_mode", report->memory_mode);
    write_json_field(out, "llvm_version", report->llvm_version);
    fprintf(out, "    \"wall_ns\": %llu\n", (unsigned long long)report->wall_ns);
    fputs("  },\n", out);

    fputs("  \"counters\": {\n", out);
    {
        int first = 1;
#define DOLRECOMP_PERF_JSON(field, json, label, group)                         \
        if (!first)                                                            \
            fputs(",\n", out);                                                 \
        first = 0;                                                             \
        fprintf(out, "    \"%s\": %llu", json,                                 \
                (unsigned long long)report->counters.field);
        DOLRECOMP_PERF_COUNTERS(DOLRECOMP_PERF_JSON)
#undef DOLRECOMP_PERF_JSON
        fputs("\n", out);
    }
    fputs("  },\n", out);

    fputs("  \"regions\": [\n", out);
    for (u32 i = 0; i < report->region_count; i++) {
        const DolPerfRegion* region = &report->regions[i];
        fprintf(out,
                "    {\"id\": %u, \"start\": \"0x%08X\", \"end\": \"0x%08X\", "
                "\"guest_instructions\": %u, \"ir_instructions\": %u, "
                "\"blocks\": %u, \"loops\": %u, \"code_bytes\": %u, "
                "\"optimize_ns\": %llu, \"codegen_ns\": %llu, "
                "\"cache_hit\": %s}%s\n",
                region->region_id, region->guest_start, region->guest_end,
                region->guest_instructions, region->ir_instructions,
                region->blocks, region->loops, region->code_bytes,
                (unsigned long long)region->optimize_ns,
                (unsigned long long)region->codegen_ns,
                region->cache_hit ? "true" : "false",
                (i + 1 < report->region_count) ? "," : "");
    }
    fputs("  ]\n", out);
    fputs("}\n", out);

    if (fclose(out) != 0) {
        if (diagnostics)
            fprintf(diagnostics, "error: failed to close perf report '%s'\n", path);
        return false;
    }

    return true;
}

void dolperf_print_summary(const DolPerfReport* report, FILE* out) {
    if (!report || !out)
        return;

    static const char* const groups[] = {
        DOLRECOMP_PERF_GROUP_EXEC,     DOLRECOMP_PERF_GROUP_DISPATCH,
        DOLRECOMP_PERF_GROUP_STATE,    DOLRECOMP_PERF_GROUP_INDIRECT,
        DOLRECOMP_PERF_GROUP_MEMORY,   DOLRECOMP_PERF_GROUP_COMPILE,
    };

    fputs("\nPerformance counters\n", out);

    for (size_t g = 0; g < sizeof(groups) / sizeof(groups[0]); g++) {
        const char* group = groups[g];

        /* A group whose counters are all zero is noise: the backend that owns
           them was not exercised in this run. */
        int any = 0;
#define DOLRECOMP_PERF_ANY(field, json, label, grp)                            \
        if (strcmp(grp, group) == 0 && report->counters.field != 0)            \
            any = 1;
        DOLRECOMP_PERF_COUNTERS(DOLRECOMP_PERF_ANY)
#undef DOLRECOMP_PERF_ANY
        if (!any)
            continue;

        fprintf(out, "  %s\n", group);
#define DOLRECOMP_PERF_ROW(field, json, label, grp)                            \
        if (strcmp(grp, group) == 0 && report->counters.field != 0)            \
            fprintf(out, "    %-42s %20llu\n", label,                          \
                    (unsigned long long)report->counters.field);
        DOLRECOMP_PERF_COUNTERS(DOLRECOMP_PERF_ROW)
#undef DOLRECOMP_PERF_ROW
    }

    if (report->counters.regions_planned != 0) {
        fprintf(out, "  Derived\n");
        fprintf(out, "    %-42s %20.1f\n", "Guest instructions per region",
                (double)report->counters.region_guest_instructions /
                    (double)report->counters.regions_planned);
        u64 compile_ns =
            report->counters.llvm_optimize_ns + report->counters.llvm_codegen_ns;
        fprintf(out, "    %-42s %20.3f\n", "LLVM time (ms)",
                (double)compile_ns / 1e6);
    }

    u64 mem_fast = report->counters.mem1_fast_reads +
                   report->counters.mem1_fast_writes +
                   report->counters.mem2_fast_reads +
                   report->counters.mem2_fast_writes +
                   report->counters.const_ram_accesses;
    u64 mem_slow = report->counters.slow_reads + report->counters.slow_writes;
    if (mem_fast + mem_slow != 0) {
        fprintf(out, "    %-42s %19.1f%%\n", "Guest memory ops on a fast path",
                100.0 * (double)mem_fast / (double)(mem_fast + mem_slow));
    }

    fputc('\n', out);
}

/* --- runtime counter dump parsing ---------------------------------------- */

static bool json_lookup_u64(const char* text, const char* name, u64* out) {
    char needle[128];
    int written = snprintf(needle, sizeof(needle), "\"%s\"", name);
    if (written <= 0 || (size_t)written >= sizeof(needle))
        return false;

    const char* cursor = text;
    while ((cursor = strstr(cursor, needle)) != NULL) {
        const char* p = cursor + written;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
        if (*p != ':') {
            cursor += written;
            continue;
        }
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
        if (*p < '0' || *p > '9') {
            cursor += written;
            continue;
        }
        *out = strtoull(p, NULL, 10);
        return true;
    }

    return false;
}

bool dolperf_read_runtime_json(const char* path, DolPerfCounters* out,
                               FILE* diagnostics) {
    if (!path || !out)
        return false;

    FILE* in = fopen(path, "rb");
    if (!in) {
        if (diagnostics)
            fprintf(diagnostics, "error: cannot read counter dump '%s'\n", path);
        return false;
    }

    if (fseek(in, 0, SEEK_END) != 0) {
        fclose(in);
        return false;
    }
    long size = ftell(in);
    if (size < 0) {
        fclose(in);
        return false;
    }
    rewind(in);

    char* text = (char*)malloc((size_t)size + 1u);
    if (!text) {
        fclose(in);
        return false;
    }
    size_t got = fread(text, 1, (size_t)size, in);
    text[got] = '\0';
    fclose(in);

    memset(out, 0, sizeof(*out));
#define DOLRECOMP_PERF_READ(field, json, label, group)                         \
    {                                                                          \
        u64 value = 0;                                                         \
        if (json_lookup_u64(text, json, &value))                               \
            out->field = value;                                                \
    }
    DOLRECOMP_PERF_RUNTIME_COUNTERS(DOLRECOMP_PERF_READ)
#undef DOLRECOMP_PERF_READ

    free(text);
    return true;
}

/* --- generated-code instrumentation header ------------------------------- */

void dolperf_emit_runtime_header(FILE* out) {
    if (!out)
        return;

    fputs(
        "/* Generated by DolRecomp. Do not edit.\n"
        " *\n"
        " * Runtime instrumentation for generated guest code.\n"
        " *\n"
        " * Every macro here compiles to nothing unless DOLRECOMP_PERF is defined,\n"
        " * so a shipping module carries no counter stores on its hot paths. Build\n"
        " * the module and the hosting runtime with -DDOLRECOMP_PERF=1 to collect.\n"
        " *\n"
        " * Threading: counters are plain u64 by default, which assumes the single\n"
        " * guest CPU thread DolRecomp generates. Define DOLRECOMP_PERF_ATOMIC to\n"
        " * make them _Atomic if a host drives generated code from several threads.\n"
        " */\n"
        "#ifndef DOLRECOMP_GENERATED_PERF_H\n"
        "#define DOLRECOMP_GENERATED_PERF_H\n"
        "\n"
        "#include <stdint.h>\n"
        "#include <stdio.h>\n"
        "\n"
        "#ifdef __cplusplus\n"
        "extern \"C\" {\n"
        "#endif\n"
        "\n"
        "#ifdef DOLRECOMP_PERF\n"
        "\n"
        "#if defined(DOLRECOMP_PERF_ATOMIC) && !defined(__cplusplus)\n"
        "#include <stdatomic.h>\n"
        "#define DOLRECOMP_PERF_SLOT _Atomic uint64_t\n"
        "#define DOLRECOMP_PERF_ADD(slot, amount)                                \\\n"
        "    atomic_fetch_add_explicit(&(slot), (uint64_t)(amount),              \\\n"
        "                              memory_order_relaxed)\n"
        "#else\n"
        "#define DOLRECOMP_PERF_SLOT uint64_t\n"
        "#define DOLRECOMP_PERF_ADD(slot, amount) ((slot) += (uint64_t)(amount))\n"
        "#endif\n"
        "\n"
        "typedef struct {\n",
        out);

#define DOLRECOMP_PERF_GEN_FIELD(field, json, label, group)                    \
    fprintf(out, "    DOLRECOMP_PERF_SLOT %s;\n", #field);
    DOLRECOMP_PERF_RUNTIME_COUNTERS(DOLRECOMP_PERF_GEN_FIELD)
#undef DOLRECOMP_PERF_GEN_FIELD

    fputs(
        "} DolRecompPerfCounters;\n"
        "\n"
        "extern DolRecompPerfCounters dolrecomp_perf_counters;\n"
        "\n"
        "#define DOLRECOMP_PERF_INC(name)                                        \\\n"
        "    DOLRECOMP_PERF_ADD(dolrecomp_perf_counters.name, 1u)\n"
        "#define DOLRECOMP_PERF_ADDN(name, amount)                               \\\n"
        "    DOLRECOMP_PERF_ADD(dolrecomp_perf_counters.name, (amount))\n"
        "\n"
        "void dolrecomp_perf_reset(void);\n"
        "/* Writes the counter block as JSON. dolperf_read_runtime_json() on the\n"
        "   DolRecomp side parses exactly this shape. */\n"
        "int dolrecomp_perf_write_json(const char* path);\n"
        "\n"
        "#else /* !DOLRECOMP_PERF */\n"
        "\n"
        "#define DOLRECOMP_PERF_INC(name) ((void)0)\n"
        "#define DOLRECOMP_PERF_ADDN(name, amount) ((void)0)\n"
        "#define dolrecomp_perf_reset() ((void)0)\n"
        "#define dolrecomp_perf_write_json(path) (0)\n"
        "\n"
        "#endif /* DOLRECOMP_PERF */\n"
        "\n"
        "#ifdef __cplusplus\n"
        "}\n"
        "#endif\n"
        "\n"
        "#endif /* DOLRECOMP_GENERATED_PERF_H */\n",
        out);
}
