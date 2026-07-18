#include "app/pipeline.h"
#include "app/paths.h"
#include "platform/fs.h"
#include "platform/pathlist.h"
#include "platform/strutil.h"
#include "frontend/decoder.h"
#include "frontend/container/dol.h"
#include "frontend/container/rel.h"
#include "frontend/container/rpx.h"
#include "backend/emitter.h"
#include "backend/dispatch.h"
#include "backend/codegen.h"
#include "backend/symbols.h"
#include "analysis/code_section.h"
#include "analysis/embedded_data.h"
#include "analysis/smc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int emit_code_sections_split(const LoadedCodeSection* sections,
                                    u32 section_count,
                                    const char* output_path,
                                    DolRecompCPU cpu, u32 entry_point, u32 jobs,
                                    int local_chunks_dir,
                                    const DolRecompSymbolMap* symbols) {
    char stem[1024];
    char header_path[1100];
    char symbol_header_path[1100];
    char chunks_dir[1100];
    char chunks_label[512];
    char include_name[512];

    if (!make_output_stem(output_path, stem, sizeof(stem)))
        return 0;
    if (!split_include_name(stem, include_name, sizeof(include_name))) {
        fprintf(stderr, "error: output include name is too long\n");
        return 0;
    }

    if (snprintf(header_path, sizeof(header_path), "%s.h", stem) >= (int)sizeof(header_path)) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }
    if (snprintf(symbol_header_path, sizeof(symbol_header_path), "%s_symbols.h", stem) >=
        (int)sizeof(symbol_header_path)) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }

    if (local_chunks_dir) {
        char output_dir[1024];
        if (!path_dirname(output_path, output_dir, sizeof(output_dir)) ||
            !join_path(chunks_dir, sizeof(chunks_dir), output_dir, "chunks")) {
            fprintf(stderr, "error: output path is too long\n");
            return 0;
        }
        snprintf(chunks_label, sizeof(chunks_label), "chunks");
    } else {
        if (snprintf(chunks_dir, sizeof(chunks_dir), "%s_chunks", stem) >= (int)sizeof(chunks_dir)) {
            fprintf(stderr, "error: output path is too long\n");
            return 0;
        }
        snprintf(chunks_label, sizeof(chunks_label), "%s", path_basename(chunks_dir));
    }

    if (!make_dir_tree(chunks_dir))
        return 0;

    FILE* manifest = fopen(output_path, "w");
    if (!manifest) {
        fprintf(stderr, "error: can't open output '%s'\n", output_path);
        return 0;
    }

    FILE* header = fopen(header_path, "w");
    if (!header) {
        fprintf(stderr, "error: can't open output '%s'\n", header_path);
        fclose(manifest);
        return 0;
    }

    fprintf(manifest, "// DolRecomp split output\n");
    fprintf(manifest, "#include \"%s\"\n\n", include_name);
    fprintf(manifest, "// Build these C files too:\n");

    emit_header_for_cpu(header, cpu);
    if (symbols) {
        u32 symbol_count = count_code_symbols(symbols, sections, section_count);
        if (symbol_count == 0) {
            fprintf(stderr, "error: symbol map has no entries in executable sections\n");
            fclose(header);
            fclose(manifest);
            return 0;
        }
        FILE* symbol_header = fopen(symbol_header_path, "w");
        if (!symbol_header) {
            fprintf(stderr, "error: can't open output '%s'\n", symbol_header_path);
            fclose(header);
            fclose(manifest);
            return 0;
        }
        if (!emit_symbol_definitions(symbol_header, symbols, sections, section_count)) {
            fprintf(stderr, "error: failed to emit symbol map\n");
            fclose(symbol_header);
            fclose(header);
            fclose(manifest);
            return 0;
        }
        fclose(symbol_header);
        printf("loaded %u executable symbols\n", symbol_count);
    } else {
        remove(symbol_header_path);
    }
    fprintf(header, "\n// Function entry points\n");

    u32 file_count = 0;
    FunctionList funcs = {0};
    SMCAnalysis smc = {0};

    for (u32 s = 0; s < section_count; s++) {
        const LoadedCodeSection* section = &sections[s];
        if (section->size == 0 || !section->data) continue;

        const u8* section_data = section->data;
        u32 base_addr = section->address;
        u32 section_sz = section->size;
        u32 num_insts = section_sz / 4;

        if (section->name && section->name[0] != '\0') {
            printf("decoding %s[%u] %s: %u instructions at 0x%08X\n",
                   section->label, section->index, section->name, num_insts,
                   base_addr);
        } else {
            printf("decoding %s[%u]: %u instructions at 0x%08X\n",
                   section->label, section->index, num_insts, base_addr);
        }

        PPCInst* insts = (PPCInst*)malloc(num_insts * sizeof(PPCInst));
        if (!insts) {
            fprintf(stderr, "error: out of memory\n");
            smc_analysis_free(&smc);
            function_list_free(&funcs);
            fclose(header);
            fclose(manifest);
            return 0;
        }

        u32 decoded = 0, embedded = 0, unknown = 0;
        for (u32 i = 0; i < num_insts; i++) {
            u32 raw = read_be32(section_data + i * 4);
            u32 addr = base_addr + i * 4;
            insts[i] = ppc_decode(raw, addr);
            if (insts[i].op == PPC_OP_UNKNOWN &&
                embedded_data_word(section->embedded_data_mode, raw)) {
                insts[i].embedded_data = true;
            }
            decoded++;
            if (insts[i].embedded_data) {
                embedded++;
            } else if (insts[i].op == PPC_OP_UNKNOWN) {
                unknown++;
            }
        }

        if (embedded != 0) {
            printf("  %u decoded, %u known, %u embedded data, %u unknown\n",
                   decoded, decoded - embedded - unknown, embedded, unknown);
        } else {
            printf("  %u decoded, %u known, %u unknown\n",
                   decoded, decoded - unknown, unknown);
        }

        if (section->embedded_data_mode == EMBEDDED_DATA_DOL) {
            analyze_smc_section(sections, section_count, insts, num_insts, &smc);
            if (smc.allocation_failed) {
                fprintf(stderr, "error: out of memory\n");
                smc_analysis_free(&smc);
                function_list_free(&funcs);
                free(insts);
                fclose(header);
                fclose(manifest);
                return 0;
            }
        }

        u32 section_job_count =
            (num_insts + EMIT_CHUNK_INSTRUCTIONS - 1u) / EMIT_CHUNK_INSTRUCTIONS;
        ChunkJob* chunk_jobs = (ChunkJob*)calloc(section_job_count, sizeof(ChunkJob));
        if (!chunk_jobs) {
            fprintf(stderr, "error: out of memory\n");
            smc_analysis_free(&smc);
            function_list_free(&funcs);
            free(insts);
            fclose(header);
            fclose(manifest);
            return 0;
        }

        for (u32 start = 0; start < num_insts; start += EMIT_CHUNK_INSTRUCTIONS) {
            u32 chunk_count = num_insts - start;
            u32 func_addr = base_addr + start * 4u;
            char chunk_name[128];
            u32 job_index = start / EMIT_CHUNK_INSTRUCTIONS;

            if (chunk_count > EMIT_CHUNK_INSTRUCTIONS)
                chunk_count = EMIT_CHUNK_INSTRUCTIONS;

            if (snprintf(chunk_name, sizeof(chunk_name),
                         "chunk_%04u_%s%u_%08X.c", file_count,
                         section->label, section->index, func_addr) >=
                (int)sizeof(chunk_name)) {
                fprintf(stderr, "error: chunk name is too long\n");
                smc_analysis_free(&smc);
                function_list_free(&funcs);
                free(chunk_jobs);
                free(insts);
                fclose(header);
                fclose(manifest);
                return 0;
            }

            ChunkJob* job = &chunk_jobs[job_index];
            job->insts = insts + start;
            job->count = chunk_count;
            job->func_addr = func_addr;

            if (!join_path(job->path, sizeof(job->path), chunks_dir, chunk_name)) {
                fprintf(stderr, "error: chunk path is too long\n");
                smc_analysis_free(&smc);
                function_list_free(&funcs);
                free(chunk_jobs);
                free(insts);
                fclose(header);
                fclose(manifest);
                return 0;
            }

            if (snprintf(job->include_name, sizeof(job->include_name), "%s",
                         include_name) >= (int)sizeof(job->include_name)) {
                fprintf(stderr, "error: output include name is too long\n");
                smc_analysis_free(&smc);
                function_list_free(&funcs);
                free(chunk_jobs);
                free(insts);
                fclose(header);
                fclose(manifest);
                return 0;
            }

            emit_chunk_prototype(header, func_addr);
            if (!function_list_add(&funcs, func_addr, func_addr + chunk_count * 4u)) {
                smc_analysis_free(&smc);
                function_list_free(&funcs);
                free(chunk_jobs);
                free(insts);
                fclose(header);
                fclose(manifest);
                return 0;
            }
            fprintf(manifest, "// %s/%s\n", chunks_label, chunk_name);
            file_count++;
        }

        u32 active_jobs = effective_chunk_jobs(section_job_count, jobs);
        printf("  writing %u chunks with %u job%s\n",
               section_job_count, active_jobs, active_jobs == 1 ? "" : "s");
        if (!run_chunk_jobs(chunk_jobs, section_job_count, jobs)) {
            smc_analysis_free(&smc);
            function_list_free(&funcs);
            free(chunk_jobs);
            free(insts);
            fclose(header);
            fclose(manifest);
            return 0;
        }

        free(chunk_jobs);
        free(insts);
    }

    if (smc.possible) {
        u32 display_count = smc.range_count;
        char smc_report_path[1100];
        if (display_count > SMC_DISPLAY_RANGE_LIMIT)
            display_count = SMC_DISPLAY_RANGE_LIMIT;

        printf("warning: this DOL may patch executable memory at runtime. generated code many need additional patches\n");
        printf("  possible patching instructions:\n");
        for (u32 i = 0; i < display_count; i++) {
            printf("    0x%08X-0x%08X\n", smc.ranges[i].start, smc.ranges[i].end);
        }

        if (smc.range_count > SMC_DISPLAY_RANGE_LIMIT) {
            if (snprintf(smc_report_path, sizeof(smc_report_path), "%s_smc.txt", stem) >=
                (int)sizeof(smc_report_path)) {
                fprintf(stderr, "error: SMC report path is too long\n");
                smc_analysis_free(&smc);
                function_list_free(&funcs);
                fclose(header);
                fclose(manifest);
                return 0;
            }
            if (!write_smc_report(&smc, smc_report_path)) {
                smc_analysis_free(&smc);
                function_list_free(&funcs);
                fclose(header);
                fclose(manifest);
                return 0;
            }
            printf("    ...\n");
            printf("  full list: %s\n", smc_report_path);
        }
    }

    emit_dispatch_helpers(header, &funcs, entry_point);
    emit_footer(header);
    smc_analysis_free(&smc);
    function_list_free(&funcs);
    fprintf(manifest, "\n// %u C files\n", file_count);

    fclose(header);
    fclose(manifest);

    printf("done!\n");
    printf("  header: %s\n", header_path);
    if (symbols)
        printf("  symbols: %s\n", symbol_header_path);
    printf("  chunks: %s (%u files)\n", chunks_dir, file_count);
    return 1;
}

int emit_dol_split(const DOLFile* dol, const char* output_path,
                          DolRecompCPU cpu, u32 jobs, int local_chunks_dir,
                          const DolRecompSymbolMap* symbols) {
    LoadedCodeSection sections[DOL_NUM_TEXT];
    u32 section_count = 0;

    for (u32 i = 0; i < DOL_NUM_TEXT; i++) {
        if (dol->header.text_sizes[i] == 0)
            continue;

        const u8* data = dol_get_text_section(dol, (int)i);
        if (!data)
            continue;

        LoadedCodeSection* section = &sections[section_count++];
        section->label = "text";
        section->name = NULL;
        section->data = data;
        section->index = i;
        section->file_offset = dol->header.text_offsets[i];
        section->address = dol->header.text_addresses[i];
        section->size = dol->header.text_sizes[i];
        section->embedded_data_mode = EMBEDDED_DATA_DOL;
    }

    return emit_code_sections_split(sections, section_count, output_path, cpu,
                                    dol->header.entry_point, jobs,
                                    local_chunks_dir, symbols);
}

int emit_rpx_split(const RPXFile* rpx, const char* output_path,
                          DolRecompCPU cpu, u32 jobs, int local_chunks_dir) {
    LoadedCodeSection sections[RPX_MAX_CODE_SECTIONS];

    for (u32 i = 0; i < rpx->code_section_count; i++) {
        const RPXCodeSection* code = &rpx->code_sections[i];
        LoadedCodeSection* section = &sections[i];
        section->label = "rpx";
        section->name = code->name;
        section->data = code->data;
        section->index = i;
        section->file_offset = code->offset;
        section->address = code->address;
        section->size = code->size;
        section->embedded_data_mode = EMBEDDED_DATA_RPX;
    }

    return emit_code_sections_split(sections, rpx->code_section_count,
                                    output_path, cpu, 0, jobs,
                                    local_chunks_dir, NULL);
}

int emit_rel_split(const RELFile* rel, const char* output_path,
                          DolRecompCPU cpu, u32 jobs, int local_chunks_dir) {
    LoadedCodeSection* sections =
        (LoadedCodeSection*)calloc(rel->section_count, sizeof(LoadedCodeSection));
    if (!sections) {
        fprintf(stderr, "error: out of memory\n");
        return 0;
    }

    u32 section_count = 0;
    for (u32 i = 0; i < rel->section_count; i++) {
        const RELSection* rel_section = &rel->sections[i];
        if (!rel_section->executable || rel_section->size == 0 || !rel_section->data)
            continue;

        LoadedCodeSection* section = &sections[section_count++];
        section->label = "rel";
        section->name = NULL;
        section->data = rel_section->data;
        section->index = rel_section->index;
        section->file_offset = rel_section->offset;
        section->address = rel_section->address;
        section->size = rel_section->size;
        section->embedded_data_mode = EMBEDDED_DATA_DOL;
    }

    int ok = emit_code_sections_split(sections, section_count, output_path, cpu,
                                      rel->entry_point, jobs, local_chunks_dir, NULL);
    free(sections);
    return ok;
}

typedef struct {
    RELFile rel;
} RELBatchItem;

void rel_batch_free(RELBatchItem* items, u32 count) {
    if (!items)
        return;
    for (u32 i = 0; i < count; i++)
        rel_free(&items[i].rel);
    free(items);
}

u32 align_up_cli(u32 value, u32 alignment, int* ok) {
    u64 result = ((u64)value + alignment - 1u) / alignment * alignment;
    if (result > 0xFFFFFFFFu) {
        *ok = 0;
        return 0;
    }
    return (u32)result;
}

int next_rel_base(const RELFile* rel, u32* cursor) {
    u32 end;
    int ok = 1;
    if (rel->file_size > 0xFFFFFFFFu - rel->base_address ||
        rel->bss_size > 0xFFFFFFFFu - rel->base_address - rel->file_size) {
        fprintf(stderr, "error: REL auto address range overflow\n");
        return 0;
    }

    end = rel->base_address + rel->file_size + rel->bss_size;
    *cursor = align_up_cli(end, REL_AUTO_ALIGN, &ok);
    if (!ok) {
        fprintf(stderr, "error: REL auto address range overflow\n");
        return 0;
    }
    return 1;
}

int check_duplicate_rel_module(const RELBatchItem* items, u32 count,
                                      u32 module_id) {
    for (u32 i = 0; i < count; i++) {
        if (items[i].rel.module_id == module_id) {
            fprintf(stderr, "error: duplicate REL module id %u\n", module_id);
            return 0;
        }
    }
    return 1;
}

int emit_rel_directory(const char* input_dir, const char* output_root,
                              const char* title_id, int titleless_mode,
                              DolRecompCPU cpu, u32 jobs, u32 start_base) {
    PathList paths = {0};
    RELBatchItem* items = NULL;
    RELModuleMapEntry* map_entries = NULL;
    char generated_root[1200];
    int ok = 0;
    u32 cursor = start_base;

    if (!collect_rel_paths(input_dir, &paths))
        goto done;
    path_list_sort(&paths);

    if (paths.count == 0) {
        fprintf(stderr, "error: no .rel files found in '%s'\n", input_dir);
        goto done;
    }

    items = (RELBatchItem*)calloc(paths.count, sizeof(*items));
    map_entries = (RELModuleMapEntry*)calloc(paths.count, sizeof(*map_entries));
    if (!items || !map_entries) {
        fprintf(stderr, "error: out of memory\n");
        goto done;
    }

    printf("found %u REL module%s\n", paths.count, paths.count == 1 ? "" : "s");
    for (u32 i = 0; i < paths.count; i++) {
        if (!rel_load_image(&items[i].rel, paths.paths[i], cursor))
            goto done;
        if (!check_duplicate_rel_module(items, i, items[i].rel.module_id))
            goto done;

        map_entries[i].module_id = items[i].rel.module_id;
        map_entries[i].rel = &items[i].rel;

        printf("  module %u: %s -> base 0x%08X\n",
               items[i].rel.module_id, paths.paths[i], items[i].rel.base_address);
        if (!next_rel_base(&items[i].rel, &cursor))
            goto done;
    }

    RELModuleMap map = { map_entries, paths.count };
    for (u32 i = 0; i < paths.count; i++) {
        if (!rel_apply_relocations(&items[i].rel, &map))
            goto done;
    }

    if (!build_generated_folder_path(output_root, title_id, titleless_mode,
                                     generated_root, sizeof(generated_root))) {
        goto done;
    }

    for (u32 i = 0; i < paths.count; i++) {
        char rel_output_path[1200];
        printf("\nREL %u/%u: %s\n", i + 1, paths.count, paths.paths[i]);
        rel_print_info(&items[i].rel, NULL);
        if (!build_rel_output_path(generated_root, paths.paths[i],
                                   items[i].rel.module_id,
                                   rel_output_path, sizeof(rel_output_path))) {
            goto done;
        }
        printf("\nwriting output to: %s\n", rel_output_path);
        if (!emit_rel_split(&items[i].rel, rel_output_path, cpu, jobs, 1))
            goto done;
    }

    ok = 1;

done:
    free(map_entries);
    rel_batch_free(items, paths.count);
    path_list_free(&paths);
    return ok;
}
