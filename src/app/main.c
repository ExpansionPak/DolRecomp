#include "app/cli.h"
#include "app/setup.h"
#include "app/database.h"
#include "app/paths.h"
#include "app/pipeline.h"
#include "platform/fs.h"
#include "platform/strutil.h"
#include "frontend/container/dol.h"
#include "frontend/container/rel.h"
#include "frontend/container/rpx.h"
#include "frontend/container/disc_extract.h"
#include "backend/emitter.h"
#include "analysis/symbol_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "extract") == 0)
        return disc_extract_main(argc - 1, argv + 1);

    CliOptions opts;
    if (!parse_cli(argc, argv, &opts))
        return 1;
    if (opts.show_help)
        return 0;
    if (opts.setup_mode)
        return run_setup() ? 0 : 1;

    const char* input_path  = opts.input_path;
    const char* title_id_arg = opts.title_id_arg;
    const char* output_arg = opts.output_arg;
    char title_id[64];
    char game_name[256];
    char named_output_path[1200];
    const char* output_path = NULL;
    int local_chunks_dir = 0;
    int effective_gamecube_mode = opts.gamecube_mode;
    DolRecompCPU effective_cpu = opts.cpu;
    int titleless_mode = effective_gamecube_mode || effective_cpu == DOLRECOMP_CPU_ESPRESSO;
    int espresso_rpx_mode = effective_cpu == DOLRECOMP_CPU_ESPRESSO;
    int input_is_directory = path_is_directory(input_path);
    int rel_mode = has_rel_extension(input_path) || input_is_directory;
    u32 rel_start_base = opts.rel_base_set ? opts.rel_base : REL_AUTO_BASE;
    DolRecompSymbolMap symbols = {0};

    title_id[0] = '\0';
    game_name[0] = '\0';

    if (has_rpx_extension(input_path) && effective_cpu != DOLRECOMP_CPU_ESPRESSO) {
        fprintf(stderr, "error: .rpx input requires --cpu espresso\n");
        return 1;
    }
    if (rel_mode && effective_cpu == DOLRECOMP_CPU_ESPRESSO) {
        fprintf(stderr, "error: .rel input cannot use espresso mode\n");
        return 1;
    }
    if (opts.map_path && (rel_mode || has_rpx_extension(input_path))) {
        fprintf(stderr, "error: --map currently supports DOL input only\n");
        return 1;
    }
    if (!titleless_mode && !database_titles_available()) {
        print_database_missing_notice();
        effective_gamecube_mode = 1;
        titleless_mode = 1;
        if (!output_arg && title_id_arg && !is_title_id_length_valid(title_id_arg))
            output_arg = title_id_arg;
        title_id_arg = NULL;
    }

    if (!titleless_mode) {
        if (!title_id_arg) {
            fprintf(stderr, "title id missing! specify gamecube mode if working with a gamecube dol with --gamecube\n");
            return 1;
        }
        if (!is_title_id_length_valid(title_id_arg)) {
            fprintf(stderr, "title id length is invalid\n");
            return 1;
        }
        if (!is_title_id(title_id_arg)) {
            fprintf(stderr, "error: title id must contain only letters/numbers, like SUKE01\n");
            return 1;
        }
    }

    if (!opts.cpu_explicit)
        effective_cpu = effective_gamecube_mode ? DOLRECOMP_CPU_GEKKO : DOLRECOMP_CPU_BROADWAY;
    espresso_rpx_mode = effective_cpu == DOLRECOMP_CPU_ESPRESSO;

    if (effective_gamecube_mode) {
        snprintf(game_name, sizeof(game_name), rel_mode ? "GameCube REL" : "GameCube DOL");
    } else if (effective_cpu == DOLRECOMP_CPU_ESPRESSO) {
        snprintf(game_name, sizeof(game_name), "Wii U executable");
    } else {
        copy_title_id(title_id, sizeof(title_id), title_id_arg);
        describe_game(game_name, sizeof(game_name), title_id, 0);
    }

    if (espresso_rpx_mode) {
        if (!has_rpx_extension(input_path)) {
            fprintf(stderr, "error: espresso mode expects an .rpx input\n");
            return 1;
        }

        RPXFile rpx;
        if (!rpx_load(&rpx, input_path))
            return 1;

        rpx_print_info(&rpx, game_name);
        printf("cpu: %s\n", cpu_display_name(effective_cpu));

        if (!output_arg) {
            printf("\ngenerating code...\n");
            output_arg = ".";
        }

        if (has_c_extension(output_arg)) {
            output_path = output_arg;
        } else {
            if (!build_gamecube_output_path(output_arg, named_output_path,
                                            sizeof(named_output_path))) {
                rpx_free(&rpx);
                return 1;
            }
            output_path = named_output_path;
            local_chunks_dir = 1;
        }

        printf("\nwriting output to: %s\n", output_path);
        if (!emit_rpx_split(&rpx, output_path, effective_cpu, opts.jobs,
                            local_chunks_dir)) {
            rpx_free(&rpx);
            return 1;
        }

        rpx_free(&rpx);
        return 0;
    }

    if (input_is_directory) {
        if (has_c_extension(output_arg ? output_arg : "")) {
            fprintf(stderr, "error: REL directory output must be a directory\n");
            return 1;
        }
        if (!output_arg) {
            printf("\ngenerating code...\n");
            output_arg = ".";
        }

        printf("REL base start: 0x%08X\n", rel_start_base);
        if (!emit_rel_directory(input_path, output_arg, title_id,
                                titleless_mode, effective_cpu, opts.jobs,
                                rel_start_base)) {
            return 1;
        }
        return 0;
    }

    if (rel_mode) {
        RELFile rel;
        if (!rel_load(&rel, input_path, rel_start_base))
            return 1;

        rel_print_info(&rel, game_name);
        printf("cpu: %s\n", cpu_display_name(effective_cpu));

        if (!output_arg) {
            printf("\ngenerating code...\n");
            output_arg = ".";
        }

        if (has_c_extension(output_arg)) {
            output_path = output_arg;
        } else if (titleless_mode) {
            if (!build_gamecube_output_path(output_arg, named_output_path,
                                            sizeof(named_output_path))) {
                rel_free(&rel);
                return 1;
            }
            output_path = named_output_path;
            local_chunks_dir = 1;
        } else {
            if (!build_named_output_path(output_arg, title_id,
                                         named_output_path, sizeof(named_output_path))) {
                rel_free(&rel);
                return 1;
            }
            output_path = named_output_path;
            local_chunks_dir = 1;
        }

        printf("\nwriting output to: %s\n", output_path);
        if (!emit_rel_split(&rel, output_path, effective_cpu, opts.jobs,
                            local_chunks_dir)) {
            rel_free(&rel);
            return 1;
        }

        rel_free(&rel);
        return 0;
    }

    DOLFile dol;
    if (!dol_load(&dol, input_path))
        return 1;
    dol_print_info(&dol, game_name);
    printf("cpu: %s\n", cpu_display_name(effective_cpu));

    if (!output_arg) {
        printf("\ngenerating code...\n");
        output_arg = ".";
    }

    if (has_c_extension(output_arg)) {
        output_path = output_arg;
    } else if (titleless_mode) {
        if (!build_gamecube_output_path(output_arg, named_output_path,
                                        sizeof(named_output_path))) {
            dol_free(&dol);
            return 1;
        }
        output_path = named_output_path;
        local_chunks_dir = 1;
    } else {
        if (!build_named_output_path(output_arg, title_id,
                                     named_output_path, sizeof(named_output_path))) {
            dol_free(&dol);
            return 1;
        }
        output_path = named_output_path;
        local_chunks_dir = 1;
    }

    printf("\nwriting output to: %s\n", output_path);
    if (opts.map_path && !symbol_map_load(&symbols, opts.map_path)) {
        dol_free(&dol);
        return 1;
    }
    if (!emit_dol_split(&dol, output_path, effective_cpu, opts.jobs, local_chunks_dir,
                        opts.map_path ? &symbols : NULL)) {
        symbol_map_free(&symbols);
        dol_free(&dol);
        return 1;
    }

    symbol_map_free(&symbols);
    dol_free(&dol);
    return 0;
}
