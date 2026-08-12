/* Prints the whole-title CFG model for a DOL.
 *
 * Synthetic fixtures prove the shapes are handled; this is what shows the model
 * survives a real title, where the interesting numbers are how much code no
 * entry point reaches and how many indirect sites a region plan will have to
 * end at.
 *
 *   cfg_stats <input.dol> [--map <path>]
 */

#include "analysis/cfg.h"
#include "analysis/regions.h"
#include "analysis/embedded_data.h"
#include "analysis/symbol_map.h"
#include "frontend/container/dol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: cfg_stats <input.dol> [--map <path>]\n");
        return 2;
    }

    const char* map_path = NULL;
    const char* report_path = NULL;
    int compare_modes = 0;
    DolRegionMode mode = DOLREGION_MODE_CFG;
    DolRegionLimits limits;
    dolregion_default_limits(&limits);

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
            map_path = argv[++i];
        } else if (strcmp(argv[i], "--emit-region-report") == 0 && i + 1 < argc) {
            report_path = argv[++i];
        } else if (strcmp(argv[i], "--region-mode") == 0 && i + 1 < argc) {
            if (!dolregion_parse_mode(argv[++i], &mode)) {
                fprintf(stderr, "error: unknown region mode '%s'\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--region-max-instructions") == 0 && i + 1 < argc) {
            limits.max_instructions = (u32)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--region-max-ir") == 0 && i + 1 < argc) {
            limits.max_ir_instructions = (u32)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--compare-modes") == 0) {
            compare_modes = 1;
        } else if (strcmp(argv[i], "--no-adjacency") == 0) {
            limits.merge_address_adjacent = 0;
        } else if (strcmp(argv[i], "--adjacency-gap") == 0 && i + 1 < argc) {
            limits.max_adjacency_gap = (u32)strtoul(argv[++i], NULL, 0);
        }
    }

    DOLFile dol;
    if (!dol_load(&dol, argv[1]))
        return 1;

    DolCfgProgram program;
    dolcfg_init(&program);
    program.entry_point = dol.header.entry_point;

    PPCInst** decoded = (PPCInst**)calloc(DOL_NUM_TEXT, sizeof(PPCInst*));
    if (!decoded) {
        dol_free(&dol);
        return 1;
    }

    u32 sections = 0;
    for (u32 s = 0; s < DOL_NUM_TEXT; s++) {
        if (dol.header.text_sizes[s] == 0)
            continue;

        u32 count = dol.header.text_sizes[s] / 4u;
        PPCInst* insts = (PPCInst*)malloc(count * sizeof(PPCInst));
        if (!insts)
            break;

        const u8* data = dol.file_data + dol.header.text_offsets[s];
        u32 base = dol.header.text_addresses[s];
        for (u32 i = 0; i < count; i++) {
            u32 raw = read_be32(data + i * 4u);
            insts[i] = ppc_decode(raw, base + i * 4u);
            /* Must match pipeline.c exactly: a word is data only if it did not
               decode AND looks like data. Testing the data predicate alone
               reclassifies decodable instructions as data and reports a
               different program than the backends actually compile. */
            if (insts[i].op == PPC_OP_UNKNOWN &&
                embedded_data_word(EMBEDDED_DATA_DOL, raw))
                insts[i].embedded_data = true;
        }

        decoded[sections] = insts;
        dolcfg_add_section(&program, insts, count, base, "text");
        sections++;
    }

    if (map_path) {
        DolRecompSymbolMap symbols = {0};
        if (symbol_map_load(&symbols, map_path)) {
            for (u32 i = 0; i < symbols.count; i++) {
                dolcfg_add_known_function(&program, symbols.symbols[i].address,
                                          symbols.symbols[i].name,
                                          DOLCFG_FUNC_PATCHABLE);
            }
            printf("map symbols: %u\n", symbols.count);
        }
        symbol_map_free(&symbols);
    }

    if (!dolcfg_build(&program, stderr)) {
        dolcfg_free(&program);
        dol_free(&dol);
        return 1;
    }

    u32 code_instructions = 0;
    u32 covered = 0;
    u32 unreached = 0;
    u32 loop_blocks = 0;
    u32 smc_blocks = 0;
    u32 term_counts[16];
    memset(term_counts, 0, sizeof(term_counts));

    for (u32 i = 0; i < program.block_count; i++) {
        const DolCfgBlock* block = &program.blocks[i];
        covered += block->instruction_count;
        if (block->flags & DOLCFG_BLOCK_UNREACHED)
            unreached += block->instruction_count;
        if (block->flags & DOLCFG_BLOCK_LOOP_HEADER)
            loop_blocks++;
        if (block->flags & DOLCFG_BLOCK_SMC_SUSPECT)
            smc_blocks++;
        if ((u32)block->terminator < 16u)
            term_counts[block->terminator]++;
    }

    for (u32 s = 0; s < program.section_count; s++) {
        const DolCfgSection* section = &program.sections[s];
        for (u32 i = 0; i < section->count; i++) {
            if (!section->insts[i].embedded_data)
                code_instructions++;
        }
    }

    /* Why is code unreached? Either nothing in the model branches to it -- so
       it is only enterable through an indirect transfer, which is a Phase 4
       problem -- or something does reach it and ownership stopped early, which
       would be a defect here. Separating the two is the difference between a
       finding and a bug. */
    u32* in_degree = (u32*)calloc(program.block_count ? program.block_count : 1u,
                                  sizeof(u32));
    u32 unreached_blocks = 0;
    u32 unreached_no_in_edge = 0;
    u32 unreached_with_in_edge = 0;
    if (in_degree) {
        for (u32 i = 0; i < program.block_count; i++) {
            const DolCfgBlock* block = &program.blocks[i];
            for (u32 s = 0; s < block->successor_count; s++) {
                if (block->successors[s] != DOLCFG_NO_BLOCK)
                    in_degree[block->successors[s]]++;
            }
        }
        for (u32 i = 0; i < program.block_count; i++) {
            if (!(program.blocks[i].flags & DOLCFG_BLOCK_UNREACHED))
                continue;
            unreached_blocks++;
            if (in_degree[i] == 0)
                unreached_no_in_edge++;
            else
                unreached_with_in_edge++;
        }
    }

    printf("sections            %u\n", program.section_count);
    printf("blocks              %u\n", program.block_count);
    printf("functions           %u\n", program.function_count);
    printf("SCCs                %u\n", program.scc_count);
    printf("loop headers        %u\n", program.loop_count);
    printf("indirect sites      %u\n", program.indirect_site_count);
    printf("code instructions   %u\n", code_instructions);
    printf("covered by blocks   %u (%.2f%%)\n", covered,
           code_instructions ? 100.0 * covered / code_instructions : 0.0);
    printf("unreached code      %u (%.2f%%)\n", unreached,
           code_instructions ? 100.0 * unreached / code_instructions : 0.0);
    printf("loop-header blocks  %u\n", loop_blocks);
    printf("SMC-suspect blocks  %u\n", smc_blocks);
    printf("unreached blocks    %u\n", unreached_blocks);
    printf("  no in-edge        %u (indirect-only entry)\n", unreached_no_in_edge);
    printf("  has in-edge       %u (reached but unowned)\n", unreached_with_in_edge);
    free(in_degree);
    printf("\nterminators\n");
    for (u32 i = 0; i < 16; i++) {
        if (term_counts[i])
            printf("  %-14s %u\n", dolcfg_terminator_name((DolCfgTerminator)i),
                   term_counts[i]);
    }

    /* Region planning. The comparison is the point: the same title through
       every mode, so the cost of an arbitrary boundary is visible next to the
       cost of a chosen one. */
    const DolRegionMode all_modes[] = {
        DOLREGION_MODE_FIXED, DOLREGION_MODE_FUNCTION, DOLREGION_MODE_CFG,
    };
    u32 mode_count = compare_modes ? 3u : 1u;

    printf("\nregion plans (max %u instructions)\n", limits.max_instructions);
    printf("  %-10s %9s %9s %11s %11s %9s\n", "mode", "regions", "instr/rgn",
           "crossings", "internal", "split fns");

    for (u32 m = 0; m < mode_count; m++) {
        DolRegionMode selected = compare_modes ? all_modes[m] : mode;

        DolRegionPlan plan;
        dolregion_plan_init(&plan);
        if (!dolregion_plan_build(&plan, &program, selected, &limits, stderr)) {
            dolregion_plan_free(&plan);
            continue;
        }

        u64 internal = 0;
        for (u32 r = 0; r < plan.region_count; r++)
            internal += plan.regions[r].internal_edges;

        printf("  %-10s %9u %9.1f %11u %11llu %9u\n",
               dolregion_mode_name(selected), plan.region_count,
               plan.region_count
                   ? (double)plan.total_instructions / (double)plan.region_count
                   : 0.0,
               plan.cross_region_edges, (unsigned long long)internal,
               plan.split_functions);

        if (report_path && (!compare_modes || selected == mode)) {
            if (dolregion_write_report(&plan, &program, report_path, stderr))
                printf("  region report: %s\n", report_path);
        }
        dolregion_plan_free(&plan);
    }

    dolcfg_free(&program);
    for (u32 s = 0; s < sections; s++)
        free(decoded[s]);
    free(decoded);
    dol_free(&dol);
    return 0;
}
