#include "app/cli.h"
#include "platform/strutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void print_usage(const char* argv0) {
    (void)argv0;

    const char* prog = "dolrecomp.exe";

    fprintf(stderr, "Usage: %s [options] <input> [wii-title-id] [output.c | output-dir]\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -jN                            Use N worker jobs for split C output (e.g. -j14)\n");
    fprintf(stderr, "  --cpu gekko|broadway|espresso  Select CPU profile (default: broadway)\n");
    fprintf(stderr, "  --backend c|llvm               Select generated-code backend (default: c)\n");
    fprintf(stderr, "  --native-abi unrestricted|compact|off  Select LLVM native-call policy\n");
    fprintf(stderr, "  --runtime recompcore|moderngekko Select the generated module runtime\n");
    fprintf(stderr, "  --game-id <id>                  Set the six-character native module game ID\n");
    fprintf(stderr, "  --state-in-memory              Keep non-native guest state in CPUState\n");
    fprintf(stderr, "  --targets <set>                host, x86-64-v2, x86-64-v3, aarch64, aarch64-a57\n");
    fprintf(stderr, "  --semantics exact|fast         PowerPC floating-point semantics (default: exact)\n");
    fprintf(stderr, "  --instrumentation none|lockstep  Compile release or state-journal objects\n");
    fprintf(stderr, "  --profile-generate <path>      Emit counters for a later profile run\n");
    fprintf(stderr, "  --profile-use <path>           Optimize with an existing profile\n");
    fprintf(stderr, "  --range-profile <path>         Emit only hot LLVM ranges from runtime CSV\n");
    fprintf(stderr, "  --native-entry-points <path>   Force hex guest PCs as prepared AOT entries\n");
    fprintf(stderr, "  --config <path>                 Load patch configuration from TOML\n");
    fprintf(stderr, "  --range-profile-coverage <pct> Native-cycle coverage target (default: 99)\n");
    fprintf(stderr, "  --range-profile-min-samples <n> Ignore colder ranges below N samples\n");
    fprintf(stderr, "  --range-profile-miss-min-samples <n> Ignore colder module misses below N samples (default: range min)\n");
    fprintf(stderr, "  --range-profile-neighbors <n>  Include N adjacent ranges on each side\n");
    fprintf(stderr, "  --range-profile-call-closure-depth <n>  Follow direct calls to depth N (default: unlimited)\n");
    fprintf(stderr, "  --range-profile-successor-closure-depth <n>  Follow static CFG successors to depth N (default: 0)\n");
    fprintf(stderr, "  --partition-instructions <n>   Reproducible object partition size\n");
    fprintf(stderr, "  --partition-seed <n>           Stable partition naming seed\n");
    fprintf(stderr, "  --gamecube                     GameCube mode (no title ID required)\n");
    fprintf(stderr, "  --rel-base <addr>              Override first virtual load address for REL codegen\n");
    fprintf(stderr, "  --map <path>                   Load optional function names from a linker MAP\n");
    fprintf(stderr, "  --setup                        Download titles database and optionally install wit\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  GameCube:     %s --gamecube <input.dol> build\n", prog);
    fprintf(stderr, "  Wii DOL:      %s <input.dol> SUKE01 build\n", prog);
    fprintf(stderr, "  REL module:   %s <input.rel | rel_folder> SUKE01 build\n", prog);
    fprintf(stderr, "  Wii U RPX:    %s --cpu espresso <input.rpx> build\n", prog);
    fprintf(stderr, "  Extract ISO:  %s extract game.iso output_folder\n", prog);
    fprintf(stderr, "  Extract WBFS: %s extract game.wbfs output_folder\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Output rules:\n");
    fprintf(stderr, "  output.c      Writes that exact split C set\n");
    fprintf(stderr, "  output-dir    Wii: writes output-dir/<title-id>_generated/<title-id>.c\n");
    fprintf(stderr, "                GameCube/Wii U: writes output-dir/generated/generated.c\n");
    fprintf(stderr, "  (none)        Writes generated code under the current directory\n");
}

int is_title_id(const char* text) {
    size_t len = strlen(text);
    if (len != 6)
        return 0;

    for (size_t i = 0; i < len; i++) {
        char ch = text[i];
        if (!((ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9'))) {
            return 0;
        }
    }

    return 1;
}

int is_title_id_length_valid(const char* text) {
    return strlen(text) == 6;
}

int parse_cpu_name(const char* text, DolRecompCPU* cpu) {
    if (ascii_case_equal(text, "gekko") || ascii_case_equal(text, "gamecube")) {
        *cpu = DOLRECOMP_CPU_GEKKO;
        return 1;
    }

    if (ascii_case_equal(text, "broadway") || ascii_case_equal(text, "wii")) {
        *cpu = DOLRECOMP_CPU_BROADWAY;
        return 1;
    }

    if (ascii_case_equal(text, "espresso") || ascii_case_equal(text, "wiiu") ||
        ascii_case_equal(text, "wii-u")) {
        *cpu = DOLRECOMP_CPU_ESPRESSO;
        return 1;
    }

    return 0;
}

const char* cpu_display_name(DolRecompCPU cpu) {
    switch (cpu) {
    case DOLRECOMP_CPU_BROADWAY:
        return "Broadway (Wii)";
    case DOLRECOMP_CPU_ESPRESSO:
        return "Espresso (Wii U)";
    case DOLRECOMP_CPU_GEKKO:
    default:
        return "Gekko (GameCube)";
    }
}

void copy_title_id(char* out, size_t out_size, const char* title_id) {
    size_t len = strlen(title_id);
    if (len >= out_size)
        len = out_size - 1;

    for (size_t i = 0; i < len; i++)
        out[i] = (char)ascii_upper((unsigned char)title_id[i]);
    out[len] = '\0';
}

int parse_job_count(const char* text, u32* jobs) {
    char* end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value == 0 || value > 256) {
        fprintf(stderr, "error: job count must be 1..256\n");
        return 0;
    }

    *jobs = (u32)value;
    return 1;
}

int parse_u32_arg(const char* text, const char* name, u32* value_out) {
    char* end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 0);
    if (errno != 0 || !end || *end != '\0' || value > 0xFFFFFFFFul) {
        fprintf(stderr, "error: %s must be a 32-bit address\n", name);
        return 0;
    }

    *value_out = (u32)value;
    return 1;
}

static int valid_target_set(const char* text) {
    const char* start = text;
    while (start && *start) {
        const char* end = strchr(start, ',');
        size_t length = end ? (size_t)(end - start) : strlen(start);
        const char* names[] = {
            "host", "x86-64-v2", "x86-64-v3", "aarch64", "aarch64-a57",
        };
        int found = 0;
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
            found |= strlen(names[i]) == length &&
                     strncmp(start, names[i], length) == 0;
        if (!found)
            return 0;
        start = end ? end + 1 : NULL;
    }
    return text && text[0];
}

static int parse_u64_value(const char* text, const char* name, u64* result) {
    char* end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 0);
    if (errno || !end || *end) {
        fprintf(stderr, "error: %s must be an unsigned integer\n", name);
        return 0;
    }
    *result = (u64)value;
    return 1;
}

int parse_cli(int argc, char** argv, CliOptions* opts) {
    const char* positional[3];
    int positional_count = 0;
    int range_profile_miss_min_samples_explicit = 0;

    memset(opts, 0, sizeof(*opts));
    opts->cpu = DOLRECOMP_CPU_GEKKO;
    opts->backend = DOLRECOMP_BACKEND_C;
    opts->jobs = 1;
    opts->llvm_targets = "host";
    opts->range_profile_coverage = 99.0;
    opts->range_profile_min_samples = 1u;
    opts->range_profile_miss_min_samples = 1u;
    opts->range_profile_neighbors = 1u;
    opts->range_profile_call_closure_depth = UINT32_MAX;
    opts->range_profile_successor_closure_depth = 0u;

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(argv[0]);
            opts->show_help = 1;
            return 1;
        }

        if (strcmp(arg, "--setup") == 0) {
            opts->setup_mode = 1;
            continue;
        }

        if (strcmp(arg, "--gamecube") == 0 || strcmp(arg, "-gc") == 0) {
            opts->gamecube_mode = 1;
            continue;
        }

        if (strcmp(arg, "--state-in-memory") == 0) {
            opts->state_in_memory = 1;
            continue;
        }

        if (strcmp(arg, "--runtime") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --runtime needs recompcore or moderngekko\n");
                return 0;
            }
            const char* runtime = argv[++i];
            if (ascii_case_equal(runtime, "recompcore"))
                opts->llvm_runtime = DOLLLVM_RUNTIME_RECOMPCORE;
            else if (ascii_case_equal(runtime, "moderngekko"))
                opts->llvm_runtime = DOLLLVM_RUNTIME_MODERNGEKKO;
            else {
                fprintf(stderr, "error: unknown runtime '%s'\n", runtime);
                return 0;
            }
            continue;
        }

        if (strncmp(arg, "--runtime=", 10) == 0) {
            const char* runtime = arg + 10;
            if (ascii_case_equal(runtime, "recompcore"))
                opts->llvm_runtime = DOLLLVM_RUNTIME_RECOMPCORE;
            else if (ascii_case_equal(runtime, "moderngekko"))
                opts->llvm_runtime = DOLLLVM_RUNTIME_MODERNGEKKO;
            else {
                fprintf(stderr, "error: unknown runtime '%s'\n", runtime);
                return 0;
            }
            continue;
        }

        if (strcmp(arg, "--game-id") == 0 || strncmp(arg, "--game-id=", 10) == 0) {
            const char* game_id = arg[9] == '=' ? arg + 10 : (i + 1 < argc ? argv[++i] : NULL);
            if (!game_id || !is_title_id(game_id)) {
                fprintf(stderr, "error: --game-id needs six letters or numbers\n");
                return 0;
            }
            opts->game_id = game_id;
            continue;
        }

        if (strcmp(arg, "--no-state-in-memory") == 0) {
            opts->state_in_memory = 0;
            continue;
        }

        if (strcmp(arg, "--backend") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --backend needs c or llvm\n");
                return 0;
            }
            arg = argv[++i];
            if (ascii_case_equal(arg, "c"))
                opts->backend = DOLRECOMP_BACKEND_C;
            else if (ascii_case_equal(arg, "llvm"))
                opts->backend = DOLRECOMP_BACKEND_LLVM;
            else {
                fprintf(stderr, "error: unknown backend '%s'\n", arg);
                return 0;
            }
            continue;
        }

        if (strncmp(arg, "--backend=", 10) == 0) {
            const char* name = arg + 10;
            if (ascii_case_equal(name, "c"))
                opts->backend = DOLRECOMP_BACKEND_C;
            else if (ascii_case_equal(name, "llvm"))
                opts->backend = DOLRECOMP_BACKEND_LLVM;
            else {
                fprintf(stderr, "error: unknown backend '%s'\n", name);
                return 0;
            }
            continue;
        }

        if (strcmp(arg, "--targets") == 0) {
            if (i + 1 >= argc || !valid_target_set(argv[i + 1])) {
                fprintf(stderr, "error: --targets needs a comma-separated target set\n");
                return 0;
            }
            opts->llvm_targets = argv[++i];
            continue;
        }

        if (strncmp(arg, "--targets=", 10) == 0) {
            if (!valid_target_set(arg + 10)) {
                fprintf(stderr, "error: invalid LLVM target set '%s'\n", arg + 10);
                return 0;
            }
            opts->llvm_targets = arg + 10;
            continue;
        }

        if (strcmp(arg, "--semantics") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --semantics needs exact or fast\n");
                return 0;
            }
            arg = argv[++i];
            if (ascii_case_equal(arg, "exact")) opts->fast_semantics = 0;
            else if (ascii_case_equal(arg, "fast")) opts->fast_semantics = 1;
            else {
                fprintf(stderr, "error: --semantics needs exact or fast\n");
                return 0;
            }
            continue;
        }

        if (strncmp(arg, "--semantics=", 12) == 0) {
            const char* mode = arg + 12;
            if (ascii_case_equal(mode, "exact")) opts->fast_semantics = 0;
            else if (ascii_case_equal(mode, "fast")) opts->fast_semantics = 1;
            else {
                fprintf(stderr, "error: --semantics needs exact or fast\n");
                return 0;
            }
            continue;
        }

        if (strcmp(arg, "--instrumentation") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --instrumentation needs none or lockstep\n");
                return 0;
            }
            arg = argv[++i];
            if (ascii_case_equal(arg, "none")) opts->lockstep_instrumentation = 0;
            else if (ascii_case_equal(arg, "lockstep")) opts->lockstep_instrumentation = 1;
            else {
                fprintf(stderr, "error: --instrumentation needs none or lockstep\n");
                return 0;
            }
            continue;
        }

        if (strncmp(arg, "--instrumentation=", 18) == 0) {
            const char* mode = arg + 18;
            if (ascii_case_equal(mode, "none")) opts->lockstep_instrumentation = 0;
            else if (ascii_case_equal(mode, "lockstep")) opts->lockstep_instrumentation = 1;
            else {
                fprintf(stderr, "error: --instrumentation needs none or lockstep\n");
                return 0;
            }
            continue;
        }

        if (strcmp(arg, "--profile-generate") == 0 ||
            strcmp(arg, "--profile-use") == 0) {
            int generate = strcmp(arg, "--profile-generate") == 0;
            if (i + 1 >= argc) {
                fprintf(stderr, "error: %s needs a path\n", arg);
                return 0;
            }
            if (generate) opts->profile_generate_path = argv[++i];
            else opts->profile_use_path = argv[++i];
            continue;
        }

        if (strncmp(arg, "--profile-generate=", 19) == 0) {
            opts->profile_generate_path = arg + 19;
            continue;
        }

        if (strncmp(arg, "--profile-use=", 14) == 0) {
            opts->profile_use_path = arg + 14;
            continue;
        }

        if (strcmp(arg, "--range-profile") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --range-profile needs a path\n");
                return 0;
            }
            opts->range_profile_path = argv[++i];
            continue;
        }

        if (strncmp(arg, "--range-profile=", 16) == 0) {
            if (!arg[16]) {
                fprintf(stderr, "error: --range-profile needs a path\n");
                return 0;
            }
            opts->range_profile_path = arg + 16;
            continue;
        }

        if (strcmp(arg, "--native-entry-points") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --native-entry-points needs a path\n");
                return 0;
            }
            opts->native_entry_points_path = argv[++i];
            continue;
        }

        if (strncmp(arg, "--native-entry-points=", 22) == 0) {
            if (!arg[22]) {
                fprintf(stderr, "error: --native-entry-points needs a path\n");
                return 0;
            }
            opts->native_entry_points_path = arg + 22;
            continue;
        }

        if (strcmp(arg, "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --config needs a path\n");
                return 0;
            }
            opts->config_path = argv[++i];
            continue;
        }

        if (strncmp(arg, "--config=", 9) == 0) {
            if (!arg[9]) {
                fprintf(stderr, "error: --config needs a path\n");
                return 0;
            }
            opts->config_path = arg + 9;
            continue;
        }

        if (strcmp(arg, "--range-profile-coverage") == 0 ||
            strncmp(arg, "--range-profile-coverage=", 25) == 0) {
            const char* value = arg[24] == '=' ? arg + 25
                                                : (i + 1 < argc ? argv[++i] : NULL);
            char* end = NULL;
            if (!value) {
                fprintf(stderr, "error: --range-profile-coverage needs 0..100\n");
                return 0;
            }
            errno = 0;
            double coverage = strtod(value, &end);
            if (errno || !end || *end || coverage <= 0.0 || coverage > 100.0) {
                fprintf(stderr, "error: --range-profile-coverage needs 0..100\n");
                return 0;
            }
            opts->range_profile_coverage = coverage;
            continue;
        }

        if (strcmp(arg, "--range-profile-min-samples") == 0 ||
            strncmp(arg, "--range-profile-min-samples=", 28) == 0) {
            const char* value = arg[27] == '=' ? arg + 28
                                                : (i + 1 < argc ? argv[++i] : NULL);
            if (!value ||
                !parse_u32_arg(value, "--range-profile-min-samples",
                               &opts->range_profile_min_samples))
                return 0;
            continue;
        }

        if (strcmp(arg, "--range-profile-miss-min-samples") == 0 ||
            strncmp(arg, "--range-profile-miss-min-samples=", 33) == 0) {
            const char* value = arg[32] == '=' ? arg + 33
                                                : (i + 1 < argc ? argv[++i] : NULL);
            if (!value ||
                !parse_u32_arg(value, "--range-profile-miss-min-samples",
                               &opts->range_profile_miss_min_samples))
                return 0;
            range_profile_miss_min_samples_explicit = 1;
            continue;
        }

        if (strcmp(arg, "--range-profile-neighbors") == 0 ||
            strncmp(arg, "--range-profile-neighbors=", 26) == 0) {
            const char* value = arg[25] == '=' ? arg + 26
                                                : (i + 1 < argc ? argv[++i] : NULL);
            if (!value ||
                !parse_u32_arg(value, "--range-profile-neighbors",
                               &opts->range_profile_neighbors) ||
                opts->range_profile_neighbors > 64u) {
                fprintf(stderr, "error: --range-profile-neighbors must be 0..64\n");
                return 0;
            }
            continue;
        }

        if (strcmp(arg, "--range-profile-call-closure-depth") == 0 ||
            strncmp(arg, "--range-profile-call-closure-depth=", 35) == 0) {
            const char* value = arg[34] == '=' ? arg + 35
                                                : (i + 1 < argc ? argv[++i] : NULL);
            if (!value ||
                !parse_u32_arg(value, "--range-profile-call-closure-depth",
                               &opts->range_profile_call_closure_depth))
                return 0;
            continue;
        }

        if (strcmp(arg, "--range-profile-successor-closure-depth") == 0 ||
            strncmp(arg, "--range-profile-successor-closure-depth=", 40) == 0) {
            const char* value = arg[39] == '=' ? arg + 40
                                                : (i + 1 < argc ? argv[++i] : NULL);
            if (!value ||
                !parse_u32_arg(value, "--range-profile-successor-closure-depth",
                               &opts->range_profile_successor_closure_depth))
                return 0;
            continue;
        }

        if (strcmp(arg, "--partition-instructions") == 0) {
            if (i + 1 >= argc ||
                !parse_u32_arg(argv[++i], "--partition-instructions",
                               &opts->partition_instructions) ||
                opts->partition_instructions < 64u ||
                opts->partition_instructions > 4096u) {
                fprintf(stderr, "error: partition instructions must be 64..4096\n");
                return 0;
            }
            continue;
        }

        if (strcmp(arg, "--native-abi") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                        "error: --native-abi needs unrestricted, compact, or off\n");
                return 0;
            }
            const char* mode = argv[++i];
            if (strcmp(mode, "unrestricted") == 0)
                opts->llvm_native_abi = DOLLLVM_NATIVE_ABI_UNRESTRICTED;
            else if (strcmp(mode, "compact") == 0)
                opts->llvm_native_abi = DOLLLVM_NATIVE_ABI_COMPACT;
            else if (strcmp(mode, "off") == 0)
                opts->llvm_native_abi = DOLLLVM_NATIVE_ABI_DISABLED;
            else {
                fprintf(stderr, "error: unknown native ABI policy '%s'\n", mode);
                return 0;
            }
            continue;
        }

        if (strncmp(arg, "--native-abi=", 13) == 0) {
            const char* mode = arg + 13;
            if (strcmp(mode, "unrestricted") == 0)
                opts->llvm_native_abi = DOLLLVM_NATIVE_ABI_UNRESTRICTED;
            else if (strcmp(mode, "compact") == 0)
                opts->llvm_native_abi = DOLLLVM_NATIVE_ABI_COMPACT;
            else if (strcmp(mode, "off") == 0)
                opts->llvm_native_abi = DOLLLVM_NATIVE_ABI_DISABLED;
            else {
                fprintf(stderr, "error: unknown native ABI policy '%s'\n", mode);
                return 0;
            }
            continue;
        }

        if (strcmp(arg, "--partition-seed") == 0) {
            if (i + 1 >= argc ||
                !parse_u64_value(argv[++i], "--partition-seed",
                                 &opts->partition_seed))
                return 0;
            continue;
        }

        if (strcmp(arg, "--cpu") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --cpu needs gekko, broadway, or espresso\n");
                return 0;
            }
            if (!parse_cpu_name(argv[++i], &opts->cpu)) {
                fprintf(stderr, "error: unknown cpu '%s'\n", argv[i]);
                return 0;
            }
            opts->cpu_explicit = 1;
            continue;
        }

        if (strncmp(arg, "--cpu=", 6) == 0) {
            if (!parse_cpu_name(arg + 6, &opts->cpu)) {
                fprintf(stderr, "error: unknown cpu '%s'\n", arg + 6);
                return 0;
            }
            opts->cpu_explicit = 1;
            continue;
        }

        if (strcmp(arg, "--gekko") == 0) {
            opts->cpu = DOLRECOMP_CPU_GEKKO;
            opts->cpu_explicit = 1;
            continue;
        }

        if (strcmp(arg, "--broadway") == 0) {
            opts->cpu = DOLRECOMP_CPU_BROADWAY;
            opts->cpu_explicit = 1;
            continue;
        }

        if (strcmp(arg, "--espresso") == 0 || strcmp(arg, "--wiiu-cpu") == 0) {
            opts->cpu = DOLRECOMP_CPU_ESPRESSO;
            opts->cpu_explicit = 1;
            continue;
        }

        if (strcmp(arg, "-j") == 0 || strcmp(arg, "--jobs") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: -j needs a number\n");
                return 0;
            }
            if (!parse_job_count(argv[++i], &opts->jobs))
                return 0;
            continue;
        }

        if (strncmp(arg, "-j", 2) == 0 && arg[2] != '\0') {
            if (!parse_job_count(arg + 2, &opts->jobs))
                return 0;
            continue;
        }

        if (strncmp(arg, "--jobs=", 7) == 0) {
            if (!parse_job_count(arg + 7, &opts->jobs))
                return 0;
            continue;
        }

        if (strcmp(arg, "--rel-base") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --rel-base needs an address\n");
                return 0;
            }
            if (!parse_u32_arg(argv[++i], "--rel-base", &opts->rel_base))
                return 0;
            opts->rel_base_set = 1;
            continue;
        }

        if (strncmp(arg, "--rel-base=", 11) == 0) {
            if (!parse_u32_arg(arg + 11, "--rel-base", &opts->rel_base))
                return 0;
            opts->rel_base_set = 1;
            continue;
        }

        if (strcmp(arg, "--map") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --map needs a path\n");
                return 0;
            }
            opts->map_path = argv[++i];
            continue;
        }

        if (strncmp(arg, "--map=", 6) == 0) {
            if (arg[6] == '\0') {
                fprintf(stderr, "error: --map needs a path\n");
                return 0;
            }
            opts->map_path = arg + 6;
            continue;
        }

        if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "error: unknown option '%s'\n", arg);
            return 0;
        }

        if (positional_count >= 3) {
            print_usage(argv[0]);
            return 0;
        }
        positional[positional_count++] = arg;
    }

    if (!range_profile_miss_min_samples_explicit)
        opts->range_profile_miss_min_samples = opts->range_profile_min_samples;

    if (positional_count == 0) {
        if (opts->setup_mode)
            return 1;
        print_usage(argv[0]);
        return 0;
    }

    if (opts->setup_mode) {
        print_usage(argv[0]);
        return 0;
    }

    if (opts->gamecube_mode && opts->cpu == DOLRECOMP_CPU_ESPRESSO) {
        fprintf(stderr, "error: --gamecube cannot be used with espresso\n");
        return 0;
    }

#ifndef DOLRECOMP_ENABLE_LLVM
    if (opts->backend == DOLRECOMP_BACKEND_LLVM) {
        fprintf(stderr, "error: LLVM backend is not built; configure with -DDOLRECOMP_ENABLE_LLVM=ON\n");
        return 0;
    }
#endif

    if (opts->range_profile_path &&
        (opts->backend != DOLRECOMP_BACKEND_LLVM ||
         opts->llvm_runtime != DOLLLVM_RUNTIME_MODERNGEKKO)) {
        fprintf(stderr,
                "error: --range-profile requires --backend=llvm "
                "--runtime=moderngekko\n");
        return 0;
    }

    if (opts->native_entry_points_path &&
        (opts->backend != DOLRECOMP_BACKEND_LLVM ||
         opts->llvm_runtime != DOLLLVM_RUNTIME_MODERNGEKKO)) {
        fprintf(stderr,
                "error: --native-entry-points requires --backend=llvm "
                "--runtime=moderngekko\n");
        return 0;
    }

    if (opts->config_path &&
        (opts->backend != DOLRECOMP_BACKEND_LLVM ||
         opts->llvm_runtime != DOLLLVM_RUNTIME_MODERNGEKKO)) {
        fprintf(stderr,
                "error: --config requires --backend=llvm "
                "--runtime=moderngekko\n");
        return 0;
    }

    if (opts->profile_generate_path && opts->profile_use_path) {
        fprintf(stderr, "error: profile generation and use are mutually exclusive\n");
        return 0;
    }

    opts->input_path = positional[0];

    if (opts->gamecube_mode || opts->cpu == DOLRECOMP_CPU_ESPRESSO) {
        opts->title_id_arg = "generated";
        opts->output_arg = positional_count > 1 ? positional[1] : NULL;
        if (positional_count > 2) {
            print_usage(argv[0]);
            return 0;
        }
    } else {
        opts->title_id_arg = positional_count > 1 ? positional[1] : NULL;
        opts->output_arg = positional_count > 2 ? positional[2] : NULL;
    }

    return 1;
}
