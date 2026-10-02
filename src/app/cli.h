#ifndef DOLRECOMP_APP_CLI_H
#define DOLRECOMP_APP_CLI_H

#include <stddef.h>
#include "common/types.h"
#include "backend/emitter.h"
#include "backend/llvm/llvm_backend.h"

typedef enum {
    DOLRECOMP_BACKEND_C,
    DOLRECOMP_BACKEND_LLVM,
} DolRecompBackend;

typedef struct {
    const char* input_path;
    const char* title_id_arg;
    const char* output_arg;
    const char* map_path;
    const char* llvm_targets;
    const char* profile_generate_path;
    const char* profile_use_path;
    const char* range_profile_path;
    const char* game_id;
    DolRecompCPU cpu;
    DolRecompBackend backend;
    u32 jobs;
    u32 rel_base;
    u32 partition_instructions;
    u64 partition_seed;
    double range_profile_coverage;
    u32 range_profile_min_samples;
    u32 range_profile_miss_min_samples;
    u32 range_profile_neighbors;
    u32 range_profile_call_closure_depth;
    DolLLVMNativeABIPolicy llvm_native_abi;
    DolLLVMRuntime llvm_runtime;
    int gamecube_mode;
    int cpu_explicit;
    int rel_base_set;
    int setup_mode;
    int show_help;
    int fast_semantics;
    int state_in_memory;
    int lockstep_instrumentation;
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
