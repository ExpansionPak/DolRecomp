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
    fprintf(stderr, "  --gamecube                     GameCube mode (no title ID required)\n");
    fprintf(stderr, "  --rel-base <addr>              Override first virtual load address for REL codegen\n");
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

    memset(opts, 0, sizeof(*opts));
    opts->cpu = DOLRECOMP_CPU_GEKKO;
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
