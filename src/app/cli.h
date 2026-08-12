#ifndef DOLRECOMP_APP_CLI_H
#define DOLRECOMP_APP_CLI_H

#include <stddef.h>
#include "common/types.h"
#include "backend/emitter.h"

typedef enum {
    DOLRECOMP_BACKEND_C,
    DOLRECOMP_BACKEND_LLVM,
    /* CFG-planned regions instead of fixed chunks. Additive: the fixed LLVM
       path above stays available until this reaches correctness and
       performance parity. */
    DOLRECOMP_BACKEND_LLVM_AOT,
} DolRecompBackend;

typedef struct {
    const char* input_path;
    const char* title_id_arg;
    const char* output_arg;
    const char* map_path;
    /* NULL disables reporting entirely; instrumentation stays collected but
       unwritten, which is what keeps --perf-report free when unused. */
    const char* perf_report_path;
    const char* region_report_path;
    const char* region_mode_arg;
    u32 region_max_instructions;
    u32 region_max_ir;
    DolRecompCPU cpu;
    DolRecompBackend backend;
    u32 jobs;
    u32 rel_base;
    int gamecube_mode;
    int cpu_explicit;
    int rel_base_set;
    int setup_mode;
    int show_help;
} CliOptions;

void print_usage(const char* argv0);
int is_title_id(const char* text);
int is_title_id_length_valid(const char* text);
int parse_cpu_name(const char* text, DolRecompCPU* cpu);
const char* cpu_display_name(DolRecompCPU cpu);
void copy_title_id(char* out, size_t out_size, const char* title_id);
int parse_job_count(const char* text, u32* jobs);
int parse_u32_arg(const char* text, const char* name, u32* value_out);
int parse_cli(int argc, char** argv, CliOptions* opts);

#endif
