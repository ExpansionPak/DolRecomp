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
    fprintf(stderr, "  --backend c|llvm|llvm-aot      Select generated-code backend (default: c)\n");
    fprintf(stderr, "  --region-mode MODE             fixed|function|cfg|pgo for llvm-aot (default: cfg)\n");
    fprintf(stderr, "  --region-max-instructions N    Guest instructions per region\n");
    fprintf(stderr, "  --region-max-ir N              Estimated DolIR instructions per region\n");
    fprintf(stderr, "  --region-profile <path>        Execution weights for --region-mode pgo\n");
    fprintf(stderr, "  --emit-region-report <path>    Write the region plan as JSON\n");
    fprintf(stderr, "  --lto off|thin                 Emit ThinLTO bitcode beside each region object\n");
    fprintf(stderr, "  --gamecube                     GameCube mode (no title ID required)\n");
    fprintf(stderr, "  --rel-base <addr>              Override first virtual load address for REL codegen\n");
    fprintf(stderr, "  --map <path>                   Load optional function names from a linker MAP\n");
    fprintf(stderr, "  --perf-report <path>           Write a JSON build/runtime counter report\n");
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

int parse_cli(int argc, char** argv, CliOptions* opts) {
    const char* positional[3];
    int positional_count = 0;
    int backend_from_cli = 0;

    memset(opts, 0, sizeof(*opts));
    opts->cpu = DOLRECOMP_CPU_GEKKO;
    opts->backend = DOLRECOMP_BACKEND_C;
    opts->jobs = 1;

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

        if (strcmp(arg, "--backend") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --backend needs c, llvm, or llvm-aot\n");
                return 0;
            }
            arg = argv[++i];
            if (ascii_case_equal(arg, "c"))
                opts->backend = DOLRECOMP_BACKEND_C;
            else if (ascii_case_equal(arg, "llvm"))
                opts->backend = DOLRECOMP_BACKEND_LLVM;
            else if (ascii_case_equal(arg, "llvm-aot") ||
                     ascii_case_equal(arg, "llvm-regions"))
                opts->backend = DOLRECOMP_BACKEND_LLVM_AOT;
            else {
                fprintf(stderr, "error: unknown backend '%s'\n", arg);
                return 0;
            }
            backend_from_cli = 1;
            continue;
        }

        if (strncmp(arg, "--backend=", 10) == 0) {
            const char* name = arg + 10;
            if (ascii_case_equal(name, "c"))
                opts->backend = DOLRECOMP_BACKEND_C;
            else if (ascii_case_equal(name, "llvm"))
                opts->backend = DOLRECOMP_BACKEND_LLVM;
            else if (ascii_case_equal(name, "llvm-aot") ||
                     ascii_case_equal(name, "llvm-regions"))
                opts->backend = DOLRECOMP_BACKEND_LLVM_AOT;
            else {
                fprintf(stderr, "error: unknown backend '%s'\n", name);
                return 0;
            }
            backend_from_cli = 1;
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

        if (strcmp(arg, "--region-mode") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --region-mode needs fixed, function, cfg, or pgo\n");
                return 0;
            }
            opts->region_mode_arg = argv[++i];
            continue;
        }

        if (strncmp(arg, "--region-mode=", 14) == 0) {
            opts->region_mode_arg = arg + 14;
            continue;
        }

        if (strcmp(arg, "--region-max-instructions") == 0) {
            if (i + 1 >= argc ||
                !parse_u32_arg(argv[++i], "--region-max-instructions",
                               &opts->region_max_instructions))
                return 0;
            continue;
        }

        if (strcmp(arg, "--lto") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --lto needs off or thin\n");
                return 0;
            }
            opts->lto_mode_arg = argv[++i];
            if (strcmp(opts->lto_mode_arg, "off") &&
                strcmp(opts->lto_mode_arg, "thin")) {
                fprintf(stderr, "error: --lto must be off or thin\n");
                return 0;
            }
            continue;
        }

        if (strncmp(arg, "--lto=", 6) == 0) {
            opts->lto_mode_arg = arg + 6;
            if (strcmp(opts->lto_mode_arg, "off") &&
                strcmp(opts->lto_mode_arg, "thin")) {
                fprintf(stderr, "error: --lto must be off or thin\n");
                return 0;
            }
            continue;
        }

        if (strcmp(arg, "--region-profile") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --region-profile needs a path\n");
                return 0;
            }
            opts->region_profile_path = argv[++i];
            continue;
        }

        if (strncmp(arg, "--region-profile=", 17) == 0) {
            opts->region_profile_path = arg + 17;
            continue;
        }

        if (strcmp(arg, "--region-max-ir") == 0) {
            if (i + 1 >= argc ||
                !parse_u32_arg(argv[++i], "--region-max-ir", &opts->region_max_ir))
                return 0;
            continue;
        }

        if (strcmp(arg, "--emit-region-report") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --emit-region-report needs a path\n");
                return 0;
            }
            opts->region_report_path = argv[++i];
            continue;
        }

        if (strncmp(arg, "--emit-region-report=", 21) == 0) {
            opts->region_report_path = arg + 21;
            continue;
        }

        if (strcmp(arg, "--perf-report") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --perf-report needs a path\n");
                return 0;
            }
            opts->perf_report_path = argv[++i];
            continue;
        }

        if (strncmp(arg, "--perf-report=", 14) == 0) {
            if (arg[14] == '\0') {
                fprintf(stderr, "error: --perf-report needs a path\n");
                return 0;
            }
            opts->perf_report_path = arg + 14;
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

    /* Environment fallbacks.
     *
     * Applied only where the command line said nothing, so an explicit flag
     * always wins -- that is the documented precedence.
     *
     * These exist because moderngekko-port drives a sibling dolrecomp and only
     * forwards --backend=c|llvm, which it validates. Without an out-of-band
     * channel there is no way to build an AOT module through the existing port
     * tool, and patching ModernGekko to pass a flag through would couple the two
     * repositories for what is a benchmarking concern. */
    /* DOLRECOMP_FORCE_BACKEND overrides even an explicit flag.
     *
     * It exists for exactly one situation: a caller that hardcodes --backend
     * and validates it against its own list. moderngekko-port does both, so
     * the polite fallback below never fires for it. Naming the override
     * separately keeps the ordinary variable honest -- a script that sets
     * DOLRECOMP_BACKEND still cannot silently change what a build asked for. */
    const char* forced_backend = getenv("DOLRECOMP_FORCE_BACKEND");
    if (forced_backend && *forced_backend) {
        if (ascii_case_equal(forced_backend, "c")) {
            opts->backend = DOLRECOMP_BACKEND_C;
        } else if (ascii_case_equal(forced_backend, "llvm")) {
            opts->backend = DOLRECOMP_BACKEND_LLVM;
        } else if (ascii_case_equal(forced_backend, "llvm-aot") ||
                   ascii_case_equal(forced_backend, "llvm-regions")) {
            opts->backend = DOLRECOMP_BACKEND_LLVM_AOT;
        } else {
            fprintf(stderr, "error: unknown DOLRECOMP_FORCE_BACKEND '%s'\n",
                    forced_backend);
            return 0;
        }
        backend_from_cli = 1;
    }

    if (!backend_from_cli) {
        const char* env_backend = getenv("DOLRECOMP_BACKEND");
        if (env_backend && *env_backend) {
            if (ascii_case_equal(env_backend, "c")) {
                opts->backend = DOLRECOMP_BACKEND_C;
            } else if (ascii_case_equal(env_backend, "llvm")) {
                opts->backend = DOLRECOMP_BACKEND_LLVM;
            } else if (ascii_case_equal(env_backend, "llvm-aot") ||
                       ascii_case_equal(env_backend, "llvm-regions")) {
                opts->backend = DOLRECOMP_BACKEND_LLVM_AOT;
            } else {
                fprintf(stderr, "error: unknown DOLRECOMP_BACKEND '%s'\n", env_backend);
                return 0;
            }
        }
    }
    if (!opts->region_mode_arg) {
        const char* value = getenv("DOLRECOMP_REGION_MODE");
        if (value && *value)
            opts->region_mode_arg = value;
    }
    if (!opts->region_max_instructions) {
        const char* value = getenv("DOLRECOMP_REGION_MAX_INSTRUCTIONS");
        if (value && *value &&
            !parse_u32_arg(value, "DOLRECOMP_REGION_MAX_INSTRUCTIONS",
                           &opts->region_max_instructions))
            return 0;
    }
    if (!opts->region_max_ir) {
        const char* value = getenv("DOLRECOMP_REGION_MAX_IR");
        if (value && *value &&
            !parse_u32_arg(value, "DOLRECOMP_REGION_MAX_IR", &opts->region_max_ir))
            return 0;
    }
    if (!opts->lto_mode_arg) {
        const char* value = getenv("DOLRECOMP_LTO");
        if (value && *value)
            opts->lto_mode_arg = value;
    }
    if (!opts->region_profile_path) {
        const char* value = getenv("DOLRECOMP_REGION_PROFILE");
        if (value && *value)
            opts->region_profile_path = value;
    }
    if (!opts->region_report_path) {
        const char* value = getenv("DOLRECOMP_REGION_REPORT");
        if (value && *value)
            opts->region_report_path = value;
    }
    if (!opts->perf_report_path) {
        const char* value = getenv("DOLRECOMP_PERF_REPORT");
        if (value && *value)
            opts->perf_report_path = value;
    }

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
    if (opts->backend == DOLRECOMP_BACKEND_LLVM ||
        opts->backend == DOLRECOMP_BACKEND_LLVM_AOT) {
        fprintf(stderr, "error: LLVM backend is not built; configure with -DDOLRECOMP_ENABLE_LLVM=ON\n");
        return 0;
    }
#endif

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
