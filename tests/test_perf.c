#include "common/perf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #x); return false; } } while (0)

static char g_dir[1024];

static void path_in_dir(char* out, size_t size, const char* leaf) {
    snprintf(out, size, "%s%c%s", g_dir, '/', leaf);
}

static char* read_all(const char* path) {
    FILE* in = fopen(path, "rb");
    if (!in)
        return NULL;
    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    rewind(in);
    if (size < 0) {
        fclose(in);
        return NULL;
    }
    char* text = (char*)malloc((size_t)size + 1u);
    if (!text) {
        fclose(in);
        return NULL;
    }
    size_t got = fread(text, 1, (size_t)size, in);
    text[got] = '\0';
    fclose(in);
    return text;
}

/* A counter added to the X-macro must appear in the struct, the JSON and the
   generated header without any further edit. Checking one from each population
   is enough to catch a list that has been extended in only one place. */
static bool test_counter_list_is_one_source_of_truth(void) {
    DolPerfReport report;
    memset(&report, 0, sizeof(report));

    report.counters.dispatcher_entries = 11;
    report.counters.mem1_fast_reads = 22;
    report.counters.regions_planned = 33;

    char path[1200];
    path_in_dir(path, sizeof(path), "counters.json");
    CHECK(dolperf_write_json(&report, path, stderr));

    char* text = read_all(path);
    CHECK(text != NULL);
    CHECK(strstr(text, "\"dispatcher_entries\": 11") != NULL);
    CHECK(strstr(text, "\"mem1_fast_reads\": 22") != NULL);
    CHECK(strstr(text, "\"regions_planned\": 33") != NULL);
    CHECK(strstr(text, "\"schema\": \"dolrecomp.perf/1\"") != NULL);
    free(text);
    return true;
}

static bool test_region_records_fold_into_counters(void) {
    DolPerfReport report;
    memset(&report, 0, sizeof(report));

    DolPerfRegion a = {0};
    a.region_id = 0;
    a.guest_start = 0x80003100u;
    a.guest_end = 0x80003200u;
    a.guest_instructions = 64;
    a.ir_instructions = 200;
    a.code_bytes = 512;
    a.optimize_ns = 1000;
    a.codegen_ns = 2000;
    a.cache_hit = 0;

    DolPerfRegion b = a;
    b.region_id = 1;
    b.guest_instructions = 36;
    b.cache_hit = 1;

    dolperf_add_region(&report, &a);
    dolperf_add_region(&report, &b);

    CHECK(report.region_count == 2);
    CHECK(report.counters.regions_planned == 2);
    CHECK(report.counters.region_guest_instructions == 100);
    CHECK(report.counters.region_ir_instructions == 400);
    CHECK(report.counters.llvm_optimize_ns == 2000);
    CHECK(report.counters.llvm_codegen_ns == 4000);
    CHECK(report.counters.artifact_cache_hits == 1);
    CHECK(report.counters.artifact_cache_misses == 1);

    char path[1200];
    path_in_dir(path, sizeof(path), "regions.json");
    CHECK(dolperf_write_json(&report, path, stderr));
    char* text = read_all(path);
    CHECK(text != NULL);
    CHECK(strstr(text, "\"start\": \"0x80003100\"") != NULL);
    CHECK(strstr(text, "\"cache_hit\": true") != NULL);
    CHECK(strstr(text, "\"cache_hit\": false") != NULL);
    free(text);

    dolperf_free(&report);
    return true;
}

/* The runtime half writes a counter dump that the compiler half reads back.
   Round-tripping through the parser is what keeps the benchmark harness able to
   merge a game run's counters into a build report. */
static bool test_runtime_counter_roundtrip(void) {
    DolPerfReport source;
    memset(&source, 0, sizeof(source));
    source.counters.dispatcher_entries = 5000;
    source.counters.blr_prediction_hits = 90;
    source.counters.blr_prediction_misses = 10;
    source.counters.slow_writes = 7;
    /* A compile-side counter must NOT survive the runtime round-trip: the
       generated module has no such counter to report. */
    source.counters.regions_planned = 999;

    char path[1200];
    path_in_dir(path, sizeof(path), "runtime.json");
    CHECK(dolperf_write_json(&source, path, stderr));

    DolPerfCounters parsed;
    CHECK(dolperf_read_runtime_json(path, &parsed, stderr));
    CHECK(parsed.dispatcher_entries == 5000);
    CHECK(parsed.blr_prediction_hits == 90);
    CHECK(parsed.blr_prediction_misses == 10);
    CHECK(parsed.slow_writes == 7);
    CHECK(parsed.regions_planned == 0);

    DolPerfReport merged;
    memset(&merged, 0, sizeof(merged));
    merged.counters.dispatcher_entries = 1;
    dolperf_merge_runtime(&merged, &parsed);
    CHECK(merged.counters.dispatcher_entries == 5001);
    CHECK(merged.counters.blr_prediction_hits == 90);
    CHECK(merged.counters.regions_planned == 0);
    return true;
}

/* The point of the generated header is that a module built without
   DOLRECOMP_PERF carries no counter stores at all. */
static bool test_generated_header_compiles_out(void) {
    char path[1200];
    path_in_dir(path, sizeof(path), "dolrecomp_perf.h");
    FILE* out = fopen(path, "wb");
    CHECK(out != NULL);
    dolperf_emit_runtime_header(out);
    CHECK(fclose(out) == 0);

    char* text = read_all(path);
    CHECK(text != NULL);
    CHECK(strstr(text, "#ifndef DOLRECOMP_GENERATED_PERF_H") != NULL);
    CHECK(strstr(text, "#define DOLRECOMP_PERF_INC(name) ((void)0)") != NULL);
    CHECK(strstr(text, "DOLRECOMP_PERF_SLOT dispatcher_entries;") != NULL);
    CHECK(strstr(text, "DOLRECOMP_PERF_SLOT mem2_fast_writes;") != NULL);
    /* Compile-side counters have no business in the guest module. */
    CHECK(strstr(text, "llvm_codegen_ns;") == NULL);
    free(text);
    return true;
}

static bool test_summary_hides_empty_groups(void) {
    DolPerfReport report;
    memset(&report, 0, sizeof(report));
    report.counters.regions_planned = 4;
    report.counters.region_guest_instructions = 400;

    char path[1200];
    path_in_dir(path, sizeof(path), "summary.txt");
    FILE* out = fopen(path, "wb");
    CHECK(out != NULL);
    dolperf_print_summary(&report, out);
    CHECK(fclose(out) == 0);

    char* text = read_all(path);
    CHECK(text != NULL);
    CHECK(strstr(text, "Compilation") != NULL);
    CHECK(strstr(text, "Regions planned") != NULL);
    /* Nothing executed guest code, so these sections must not appear. */
    CHECK(strstr(text, "Guest memory") == NULL);
    CHECK(strstr(text, "Dispatch and linking") == NULL);
    free(text);
    return true;
}

static bool test_clock_is_monotonic(void) {
    u64 first = dolperf_now_ns();
    u64 last = first;
    for (int i = 0; i < 1000; i++) {
        u64 now = dolperf_now_ns();
        CHECK(now >= last);
        last = now;
    }
    CHECK(last >= first);
    return true;
}

int main(int argc, char** argv) {
    if (argc > 1)
        snprintf(g_dir, sizeof(g_dir), "%s", argv[1]);
    else
        snprintf(g_dir, sizeof(g_dir), ".");

#if defined(_WIN32)
    _mkdir(g_dir);
#else
    mkdir(g_dir, 0777);
#endif

    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"counter_list_is_one_source_of_truth", test_counter_list_is_one_source_of_truth},
        {"region_records_fold_into_counters", test_region_records_fold_into_counters},
        {"runtime_counter_roundtrip", test_runtime_counter_roundtrip},
        {"generated_header_compiles_out", test_generated_header_compiles_out},
        {"summary_hides_empty_groups", test_summary_hides_empty_groups},
        {"clock_is_monotonic", test_clock_is_monotonic},
    };

    int failures = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (!tests[i].fn()) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            failures++;
        }
    }

    if (failures != 0) {
        fprintf(stderr, "%d perf test(s) failed\n", failures);
        return 1;
    }

    printf("perf tests passed\n");
    return 0;
}
