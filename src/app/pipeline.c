#include "app/pipeline.h"
#include "analysis/code_section.h"
#include "analysis/embedded_data.h"
#include "analysis/smc.h"
#include "app/patch_config.h"
#include "app/paths.h"
#include "backend/codegen.h"
#include "backend/dispatch.h"
#include "backend/emitter.h"
#include "backend/native_module.h"
#include "backend/symbols.h"
#include "backend/variant_output.h"
#include "frontend/container/dol.h"
#include "frontend/container/rel.h"
#include "frontend/container/rpx.h"
#include "frontend/decoder.h"
#include "platform/fs.h"
#include "platform/pathlist.h"
#include "platform/strutil.h"
#ifdef DOLRECOMP_ENABLE_LLVM
#include "backend/llvm/llvm_backend.h"
#include "cpu/cpu.h"
#include "ir/dolir_builder.h"
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#else
#include <process.h>
#endif

#define DOLC_DEFAULT_CHUNK_INSTRUCTIONS 4096u

static u32 c_chunk_instructions(void) {
    const char* configured = getenv("DOLRECOMP_C_CHUNK_INSTRUCTIONS");
    if (!configured || !configured[0])
        return DOLC_DEFAULT_CHUNK_INSTRUCTIONS;

    char* end = NULL;
    errno = 0;
    unsigned long value = strtoul(configured, &end, 10);
    if (errno || !end || *end || value < 128u || value > 4096u) {
        fprintf(stderr,
                "warning: DOLRECOMP_C_CHUNK_INSTRUCTIONS must be 128..4096; "
                "using %u\n",
                DOLC_DEFAULT_CHUNK_INSTRUCTIONS);
        return DOLC_DEFAULT_CHUNK_INSTRUCTIONS;
    }
    return (u32)value;
}

#ifdef DOLRECOMP_ENABLE_LLVM
#define DOLLLVM_DEFAULT_CHUNK_INSTRUCTIONS 128u
#define DOLLLVM_DEFAULT_WORKER_BATCH 1u
#define DOLLLVM_DEFAULT_RANGES_PER_OBJECT 1u
// SSA regions, ABI v4 variants and ThinLTO summaries.
#define DOLLLVM_CACHE_VERSION "dolllvm-v29"

typedef struct {
    const PPCInst* insts;
    u32 count;
    u32 function_address;
    u32 first_range_index;
    u32 object_range_count;
    u32 index;
    u32 total;
    const DolLLVMFunctionRange* ranges;
    u32 range_count;
    const u32* entry_points;
    u32 entry_point_count;
    const DolLLVMPatch* patches;
    u32 patch_count;
    DolLLVMTargetProfile target_profile;
    DolLLVMSemantics semantics;
    DolLLVMInstrumentation instrumentation;
    DolLLVMNativeABIPolicy native_abi_policy;
    DolLLVMRuntime runtime;
    const char* profile_generate_path;
    const char* profile_use_path;
    u64 partition_seed;
    int state_in_memory;
    u32 ram_size;
    u32 mem2_size;
    u32 optimization_level;
    int fast_passes;
    int emit_thinlto;
    char symbol_suffix[32];
    char thinlto_path[1400];
    u64 hash;
    char name[128];
    char path[1400];
    char cache_path[1400];
    char cache_bitcode_path[1400];
} LLVMChunkJob;

typedef struct {
    u32 pc;
    u32 range_index;
} LLVMNativeEntryCandidate;

static int llvm_fast_iteration(void) {
    const char* configured = getenv("DOLRECOMP_LLVM_FAST_ITERATION");
    return configured && configured[0] && strcmp(configured, "0") != 0;
}

static u32 llvm_optimization_level(void) {
    const u32 fallback = 3u;
    const char* configured = getenv("DOLRECOMP_LLVM_OPT_LEVEL");
    if (!configured || !configured[0])
        return fallback;
    char* end = NULL;
    errno = 0;
    unsigned long value = strtoul(configured, &end, 10);
    if (errno || !end || *end || value > 3u) {
        fprintf(stderr,
                "warning: DOLRECOMP_LLVM_OPT_LEVEL must be 0..3; using %u\n",
                fallback);
        return fallback;
    }
    return (u32)value;
}

static int llvm_fast_passes(void) {
    const char* configured = getenv("DOLRECOMP_LLVM_OPT_LEVEL");
    return llvm_fast_iteration() && (!configured || !configured[0]);
}

static int llvm_emit_thinlto(void) {
    const char* configured = getenv("DOLRECOMP_LLVM_EMIT_THINLTO");
    if (!configured || !configured[0])
        return !llvm_fast_iteration();
    return strcmp(configured, "0") != 0;
}

static u32 parse_llvm_target_set(const char* text,
                                 DolLLVMTargetProfile profiles[5]) {
    char copy[128];
    if (!text || strlen(text) >= sizeof(copy))
        return 0;
    snprintf(copy, sizeof(copy), "%s", text);
    u32 count = 0;
    char* cursor = copy;
    while (cursor && *cursor && count < 5u) {
        char* comma = strchr(cursor, ',');
        if (comma)
            *comma = '\0';
        if (!dolllvm_parse_target_profile(cursor, &profiles[count]))
            return 0;
        for (u32 i = 0; i < count; i++)
            if (profiles[i] == profiles[count])
                return 0;
        count++;
        cursor = comma ? comma + 1 : NULL;
    }
    return count;
}

static u32 llvm_chunk_instructions(void) {
    const char* configured = getenv("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS");
    if (!configured || !configured[0])
        return DOLLLVM_DEFAULT_CHUNK_INSTRUCTIONS;
    char* end = NULL;
    errno = 0;
    unsigned long value = strtoul(configured, &end, 10);
    if (errno || !end || *end || value < 64u || value > 4096u) {
        fprintf(stderr,
                "warning: DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS must be 64..4096; "
                "using %u\n",
                DOLLLVM_DEFAULT_CHUNK_INSTRUCTIONS);
        return DOLLLVM_DEFAULT_CHUNK_INSTRUCTIONS;
    }
    return (u32)value;
}

static u32 llvm_worker_batch_size(void) {
    const char* configured = getenv("DOLRECOMP_LLVM_WORKER_BATCH");
    if (!configured || !configured[0])
        return DOLLLVM_DEFAULT_WORKER_BATCH;
    char* end = NULL;
    errno = 0;
    unsigned long value = strtoul(configured, &end, 10);
    if (errno || !end || *end || value < 1u || value > 64u) {
        fprintf(stderr,
                "warning: DOLRECOMP_LLVM_WORKER_BATCH must be 1..64; "
                "using %u\n",
                DOLLLVM_DEFAULT_WORKER_BATCH);
        return DOLLLVM_DEFAULT_WORKER_BATCH;
    }
    return (u32)value;
}

static u32 llvm_ranges_per_object(void) {
    const char* configured = getenv("DOLRECOMP_LLVM_RANGES_PER_OBJECT");
    if (!configured || !configured[0])
        return DOLLLVM_DEFAULT_RANGES_PER_OBJECT;
    char* end = NULL;
    errno = 0;
    unsigned long value = strtoul(configured, &end, 10);
    if (errno || !end || *end || value < 1u || value > 256u) {
        fprintf(stderr,
                "warning: DOLRECOMP_LLVM_RANGES_PER_OBJECT must be 1..256; "
                "using %u\n",
                DOLLLVM_DEFAULT_RANGES_PER_OBJECT);
        return DOLLLVM_DEFAULT_RANGES_PER_OBJECT;
    }
    return (u32)value;
}

// Validate the object format selected by the target triple.
static int valid_object_file(const LLVMChunkJob* job, const char* path) {
    DolLLVMOptions options = {0};
    options.target_triple = getenv("DOLRECOMP_LLVM_TARGET");
    options.target_profile = job->target_profile;
    return dolllvm_object_matches_options(path, &options) ? 1 : 0;
}

static int llvm_job_stamp_path(const LLVMChunkJob* job, char* path,
                               size_t size) {
    int written = snprintf(path, size, "%s.hash", job->path);
    return written > 0 && written < (int)size;
}

static int valid_llvm_job_stamp(const LLVMChunkJob* job) {
    char path[1440];
    if (!llvm_job_stamp_path(job, path, sizeof(path)))
        return 0;
    FILE* file = fopen(path, "r");
    if (!file)
        return 0;
    unsigned long long hash = 0;
    int valid = fscanf(file, "%llx", &hash) == 1 && hash == job->hash;
    fclose(file);
    return valid;
}

static void write_llvm_job_stamp(const LLVMChunkJob* job) {
    char path[1440];
    char temp[1480];
    if (!llvm_job_stamp_path(job, path, sizeof(path)))
        return;
#ifdef _WIN32
    int process_id = _getpid();
#else
    int process_id = (int)getpid();
#endif
    if (snprintf(temp, sizeof(temp), "%s.tmp.%d", path, process_id) >=
        (int)sizeof(temp))
        return;
    FILE* file = fopen(temp, "w");
    if (!file)
        return;
    int ok = fprintf(file, "%016llx\n", (unsigned long long)job->hash) > 0;
    if (fclose(file) != 0)
        ok = 0;
    if (!ok) {
        remove(temp);
        return;
    }
    remove(path);
    if (rename(temp, path) != 0)
        remove(temp);
}

static int copy_file(const char* source, const char* destination) {
    FILE* in = fopen(source, "rb");
    if (!in)
        return 0;
    FILE* out = fopen(destination, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    unsigned char buffer[64 * 1024];
    int ok = 1;
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), in)) != 0) {
        if (fwrite(buffer, 1, count, out) != count) {
            ok = 0;
            break;
        }
    }
    if (ferror(in))
        ok = 0;
    if (fclose(out) != 0)
        ok = 0;
    fclose(in);
    if (!ok)
        remove(destination);
    return ok;
}

static u64 hash_bytes(u64 hash, const void* data, size_t size) {
    const unsigned char* bytes = (const unsigned char*)data;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static u64 hash_file_contents(u64 hash, const char* path) {
    if (!path || !path[0])
        return hash;
    FILE* file = fopen(path, "rb");
    if (!file)
        return hash_bytes(hash, path, strlen(path));
    unsigned char bytes[4096];
    size_t count;
    while ((count = fread(bytes, 1, sizeof(bytes), file)) != 0)
        hash = hash_bytes(hash, bytes, count);
    fclose(file);
    return hash;
}

static u64 llvm_job_hash(const LLVMChunkJob* job) {
    u64 hash = 1469598103934665603ull;
    hash =
        hash_bytes(hash, DOLLLVM_CACHE_VERSION, strlen(DOLLLVM_CACHE_VERSION));
#ifdef DOLRECOMP_CODEGEN_SOURCE_HASH
    /* A hand-edited version string cannot invalidate the cache when the code
       that generates the objects changes -- it only invalidates when somebody
       remembers to bump it. CMake hashes the emitter sources and passes the
       digest in, so editing any of them changes every job key automatically. */
    hash = hash_bytes(hash, DOLRECOMP_CODEGEN_SOURCE_HASH,
                      strlen(DOLRECOMP_CODEGEN_SOURCE_HASH));
#endif
    hash =
        hash_bytes(hash, &job->function_address, sizeof(job->function_address));
    hash = hash_bytes(hash, &job->count, sizeof(job->count));
    hash = hash_bytes(hash, &job->first_range_index,
                      sizeof(job->first_range_index));
    hash = hash_bytes(hash, &job->object_range_count,
                      sizeof(job->object_range_count));
    u32 state_size = (u32)sizeof(CPUState);
    hash = hash_bytes(hash, &state_size, sizeof(state_size));
    hash = hash_bytes(hash, &job->target_profile, sizeof(job->target_profile));
    hash = hash_bytes(hash, job->symbol_suffix, strlen(job->symbol_suffix));
    hash = hash_bytes(hash, &job->semantics, sizeof(job->semantics));
    hash =
        hash_bytes(hash, &job->instrumentation, sizeof(job->instrumentation));
    hash = hash_bytes(hash, &job->native_abi_policy,
                      sizeof(job->native_abi_policy));
    hash = hash_bytes(hash, &job->runtime, sizeof(job->runtime));
    hash = hash_bytes(hash, &job->partition_seed, sizeof(job->partition_seed));
    hash =
        hash_bytes(hash, &job->state_in_memory, sizeof(job->state_in_memory));
    hash = hash_bytes(hash, &job->ram_size, sizeof(job->ram_size));
    hash = hash_bytes(hash, &job->mem2_size, sizeof(job->mem2_size));
    hash = hash_bytes(hash, &job->optimization_level,
                      sizeof(job->optimization_level));
    hash = hash_bytes(hash, &job->fast_passes, sizeof(job->fast_passes));
    hash = hash_bytes(hash, &job->emit_thinlto, sizeof(job->emit_thinlto));
    hash = hash_file_contents(hash, job->profile_use_path);
    if (job->profile_generate_path)
        hash = hash_bytes(hash, job->profile_generate_path,
                          strlen(job->profile_generate_path));
    const char* codegen_level = getenv("DOLRECOMP_LLVM_CODEGEN_LEVEL");
    if (!codegen_level || !codegen_level[0])
        codegen_level = "2";
    hash = hash_bytes(hash, codegen_level, strlen(codegen_level));
    const char* write_journal = getenv("DOLRECOMP_LLVM_WRITE_JOURNAL");
    if (!write_journal)
        write_journal = "0";
    hash = hash_bytes(hash, write_journal, strlen(write_journal));
    // Host triples must distinguish caches when no target was requested.
    char triple[256];
    DolLLVMOptions target_options = {0};
    target_options.target_triple = getenv("DOLRECOMP_LLVM_TARGET");
    target_options.target_profile = job->target_profile;
    target_options.native_abi_policy = job->native_abi_policy;
    target_options.runtime = job->runtime;
    if (dolllvm_effective_triple(&target_options, triple, sizeof(triple)))
        hash = hash_bytes(hash, triple, strlen(triple));
    char codegen[1024];
    if (dolllvm_codegen_fingerprint(&target_options, codegen, sizeof(codegen)))
        hash = hash_bytes(hash, codegen, strlen(codegen));
    for (u32 i = 0; i < job->count; i++) {
        hash = hash_bytes(hash, &job->insts[i].address,
                          sizeof(job->insts[i].address));
        hash = hash_bytes(hash, &job->insts[i].raw, sizeof(job->insts[i].raw));
        hash = hash_bytes(hash, &job->insts[i].embedded_data,
                          sizeof(job->insts[i].embedded_data));
    }
    for (u32 i = 0; i < job->range_count; i++)
        hash = hash_bytes(hash, &job->ranges[i], sizeof(job->ranges[i]));
    for (u32 i = 0; i < job->entry_point_count; i++) {
        u32 address = job->entry_points[i];
        if (address >= job->function_address &&
            address < job->function_address + job->count * 4u)
            hash = hash_bytes(hash, &address, sizeof(address));
    }
    for (u32 i = 0; i < job->patch_count; i++) {
        const DolLLVMPatch* patch = &job->patches[i];
        hash = hash_bytes(hash, &patch->start, sizeof(patch->start));
        hash = hash_bytes(hash, &patch->end, sizeof(patch->end));
        hash = hash_bytes(hash, patch->symbol, strlen(patch->symbol));
    }
    return hash;
}

static int compare_u32(const void* left, const void* right) {
    u32 a = *(const u32*)left;
    u32 b = *(const u32*)right;
    return a < b ? -1 : a > b;
}

static int llvm_code_address(const LoadedCodeSection* sections,
                             u32 section_count, u32 address) {
    for (u32 i = 0; i < section_count; i++) {
        const LoadedCodeSection* section = &sections[i];
        if (section->data && address >= section->address &&
            address < section->address + section->size &&
            ((address - section->address) & 3u) == 0)
            return 1;
    }
    return 0;
}

static int llvm_patch_range_valid(const LoadedCodeSection* sections,
                                  u32 section_count, u32 start, u32 end) {
    if (end <= start || ((start | end) & 3u))
        return 0;
    for (u32 i = 0; i < section_count; i++) {
        const LoadedCodeSection* section = &sections[i];
        u32 section_end = section->address + section->size;
        if (section->data && start >= section->address && end <= section_end)
            return 1;
    }
    return 0;
}

static int llvm_patch_range_hash(const LoadedCodeSection* sections,
                                 u32 section_count, u32 start, u32 end,
                                 u64* hash_out) {
    if (!llvm_patch_range_valid(sections, section_count, start, end))
        return 0;
    for (u32 i = 0; i < section_count; i++) {
        const LoadedCodeSection* section = &sections[i];
        u32 section_end = section->address + section->size;
        if (!section->data || start < section->address || end > section_end)
            continue;
        *hash_out = hash_bytes(UINT64_C(0xCBF29CE484222325),
                               section->data + (start - section->address),
                               end - start);
        return 1;
    }
    return 0;
}

static int compare_patch(const void* left, const void* right) {
    const DolLLVMPatch* a = (const DolLLVMPatch*)left;
    const DolLLVMPatch* b = (const DolLLVMPatch*)right;
    return a->start < b->start ? -1 : a->start > b->start;
}

static void free_patches(DolLLVMPatch* patches, u32 count) {
    for (u32 i = 0; i < count; i++)
        free((void*)patches[i].symbol);
    free(patches);
}

static int load_patches(const LoadedCodeSection* sections,
                        u32 section_count, const CliOptions* options,
                        DolLLVMPatch** patches_out, u32* count_out) {
    *patches_out = NULL;
    *count_out = 0;
    if (!options->config_path)
        return 1;
    DolRecompPatchConfig config;
    char error[256];
    if (!dolrecomp_patch_config_load(options->config_path, &config,
                                     error, sizeof(error))) {
        fprintf(stderr, "error: cannot load patch config '%s': %s\n",
                options->config_path, error);
        return 0;
    }
    DolLLVMPatch* patches = config.count
        ? (DolLLVMPatch*)calloc(config.count, sizeof(*patches)) : NULL;
    if (config.count && !patches) {
        dolrecomp_patch_config_free(&config);
        return 0;
    }
    for (u32 i = 0; i < config.count; i++) {
        DolRecompPatchConfigEntry* entry = &config.entries[i];
        if (!llvm_patch_range_valid(sections, section_count,
                                    entry->start, entry->end)) {
            fprintf(stderr, "error: patch %08X-%08X is outside executable code\n",
                    entry->start, entry->end);
            free_patches(patches, i);
            dolrecomp_patch_config_free(&config);
            return 0;
        }
        if (entry->has_expected_fnv64) {
            u64 actual = 0;
            if (!llvm_patch_range_hash(sections, section_count, entry->start,
                                       entry->end, &actual) ||
                actual != entry->expected_fnv64) {
                fprintf(stderr,
                        "error: patch hash mismatch for %08X-%08X: "
                        "expected %016llX, found %016llX\n",
                        entry->start, entry->end,
                        (unsigned long long)entry->expected_fnv64,
                        (unsigned long long)actual);
                free_patches(patches, i);
                dolrecomp_patch_config_free(&config);
                return 0;
            }
        }
        size_t symbol_size = strlen(entry->symbol) + 1u;
        char* symbol = (char*)malloc(symbol_size);
        if (!symbol) {
            free_patches(patches, i);
            dolrecomp_patch_config_free(&config);
            return 0;
        }
        memcpy(symbol, entry->symbol, symbol_size);
        patches[i].start = entry->start;
        patches[i].end = entry->end;
        patches[i].symbol = symbol;
    }
    u32 count = config.count;
    dolrecomp_patch_config_free(&config);
    qsort(patches, count, sizeof(*patches), compare_patch);
    for (u32 i = 1; i < count; i++) {
        if (patches[i - 1u].end > patches[i].start) {
            fprintf(stderr, "error: overlapping patches %08X-%08X and %08X-%08X\n",
                    patches[i - 1u].start, patches[i - 1u].end,
                    patches[i].start, patches[i].end);
            free_patches(patches, count);
            return 0;
        }
    }
    *patches_out = patches;
    *count_out = count;
    return 1;
}

static const DolLLVMPatch* llvm_patch_for_start(
    const DolLLVMPatch* patches, u32 patch_count, u32 start) {
    for (u32 i = 0; i < patch_count; i++)
        if (patches[i].start == start)
            return &patches[i];
    return NULL;
}

static int append_patch_points(const DolLLVMPatch* patches, u32 patch_count,
                               int include_end, u32** points, u32* count) {
    u32 add = patch_count * (include_end ? 2u : 1u);
    if (!add) return 1;
    u32* resized = (u32*)realloc(*points, (size_t)(*count + add) * sizeof(**points));
    if (!resized) return 0;
    *points = resized;
    for (u32 i = 0; i < patch_count; i++) {
        (*points)[(*count)++] = patches[i].start;
        if (include_end) (*points)[(*count)++] = patches[i].end;
    }
    qsort(*points, *count, sizeof(**points), compare_u32);
    u32 unique = 0;
    for (u32 i = 0; i < *count; i++)
        if (!unique || (*points)[i] != (*points)[unique - 1u])
            (*points)[unique++] = (*points)[i];
    *count = unique;
    return 1;
}

static int load_forced_llvm_points(const LoadedCodeSection* sections,
                                   u32 section_count, const CliOptions* options,
                                   u32** points_out, u32* count_out) {
    *points_out = NULL;
    *count_out = 0;
    if (!options->native_entry_points_path)
        return 1;

    FILE* file = fopen(options->native_entry_points_path, "r");
    if (!file) {
        fprintf(stderr, "error: cannot read native entry points '%s'\n",
                options->native_entry_points_path);
        return 0;
    }

    u32 capacity = 32u;
    u32 count = 0;
    u32* points = (u32*)malloc((size_t)capacity * sizeof(*points));
    if (!points) {
        fclose(file);
        return 0;
    }

    char line[256];
    u32 line_number = 0;
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        char* start = line;
        while (*start == ' ' || *start == '\t')
            start++;
        char* comment = strchr(start, '#');
        if (comment)
            *comment = '\0';
        char* end_text = start + strlen(start);
        while (end_text > start &&
               (end_text[-1] == ' ' || end_text[-1] == '\t' ||
                end_text[-1] == '\r' || end_text[-1] == '\n'))
            *--end_text = '\0';
        if (!*start)
            continue;

        char* end = NULL;
        errno = 0;
        unsigned long value = strtoul(start, &end, 16);
        if (errno || !end || *end || value > UINT32_MAX ||
            !llvm_code_address(sections, section_count, (u32)value)) {
            fprintf(stderr,
                    "error: invalid native entry point '%s' at %s:%u\n",
                    start, options->native_entry_points_path, line_number);
            free(points);
            fclose(file);
            return 0;
        }
        if (count == capacity) {
            capacity *= 2u;
            u32* resized =
                (u32*)realloc(points, (size_t)capacity * sizeof(*points));
            if (!resized) {
                free(points);
                fclose(file);
                return 0;
            }
            points = resized;
        }
        points[count++] = (u32)value;
    }
    fclose(file);

    qsort(points, count, sizeof(*points), compare_u32);
    u32 unique = 0;
    for (u32 i = 0; i < count; i++) {
        if (!unique || points[i] != points[unique - 1u])
            points[unique++] = points[i];
    }
    *points_out = points;
    *count_out = unique;
    return 1;
}

static int append_forced_llvm_points(
    const LoadedCodeSection* sections, u32 section_count,
    const CliOptions* options, u32** points, u32* count) {
    u32* forced = NULL;
    u32 forced_count = 0;
    if (!load_forced_llvm_points(sections, section_count, options,
                                 &forced, &forced_count))
        return 0;
    if (!forced_count) {
        free(forced);
        return 1;
    }

    u32* resized =
        (u32*)realloc(*points, (size_t)(*count + forced_count) * sizeof(**points));
    if (!resized) {
        free(forced);
        return 0;
    }
    *points = resized;
    memcpy(*points + *count, forced, (size_t)forced_count * sizeof(*forced));
    *count += forced_count;
    free(forced);

    qsort(*points, *count, sizeof(**points), compare_u32);
    u32 unique = 0;
    for (u32 i = 0; i < *count; i++) {
        if (!unique || (*points)[i] != (*points)[unique - 1u])
            (*points)[unique++] = (*points)[i];
    }
    *count = unique;
    return 1;
}

static u32* collect_llvm_partition_points(const LoadedCodeSection* sections,
                                          u32 section_count, u32 program_entry,
                                          const DolRecompSymbolMap* symbols,
                                          u32* result_count) {
    size_t capacity = 1u + (symbols ? symbols->count : 0u);
    for (u32 i = 0; i < section_count; i++)
        capacity += sections[i].size / 4u;
    u32* points = (u32*)malloc(capacity * sizeof(*points));
    if (!points)
        return NULL;
    u32 count = 0;
    if (llvm_code_address(sections, section_count, program_entry))
        points[count++] = program_entry;
    if (symbols) {
        for (u32 i = 0; i < symbols->count; i++) {
            u32 address = symbols->symbols[i].address;
            if (llvm_code_address(sections, section_count, address))
                points[count++] = address;
        }
    }
    for (u32 section_index = 0; section_index < section_count;
         section_index++) {
        const LoadedCodeSection* section = &sections[section_index];
        for (u32 offset = 0; section->data && offset + 4u <= section->size;
             offset += 4u) {
            u32 address = section->address + offset;
            PPCInst inst =
                ppc_decode(read_be32(section->data + offset), address);
            if ((inst.op == PPC_OP_B || inst.op == PPC_OP_BC) && inst.lk &&
                llvm_code_address(sections, section_count, inst.branch_target))
                points[count++] = inst.branch_target;
            if (inst.op == PPC_OP_BCLR && !inst.lk && inst.bo == 20u &&
                llvm_code_address(sections, section_count, address + 4u))
                points[count++] = address + 4u;
        }
    }
    qsort(points, count, sizeof(*points), compare_u32);
    u32 unique = 0;
    for (u32 i = 0; i < count; i++) {
        if (!unique || points[i] != points[unique - 1u])
            points[unique++] = points[i];
    }
    *result_count = unique;
    return points;
}

static u32 next_llvm_partition_point(const u32* points, u32 count, u32 start,
                                     u32 end) {
    u32 low = 0;
    u32 high = count;
    while (low < high) {
        u32 middle = low + (high - low) / 2u;
        if (points[middle] <= start)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < count && points[low] < end ? points[low] : end;
}

static DolLLVMFunctionRange*
build_llvm_ranges(const LoadedCodeSection* sections, u32 section_count,
                  u32 chunk_instructions, const u32* partition_points,
                  u32 point_count, const DolLLVMPatch* patches,
                  u32 patch_count, u32* result_count) {
    u32 capacity = point_count;
    for (u32 i = 0; i < section_count; i++)
        capacity += ((sections[i].size / 4u) + chunk_instructions - 1u) /
                    chunk_instructions;
    DolLLVMFunctionRange* ranges =
        (DolLLVMFunctionRange*)calloc(capacity, sizeof(*ranges));
    if (!ranges)
        return NULL;
    u32 count = 0;
    for (u32 i = 0; i < section_count; i++) {
        u32 start = sections[i].address;
        u32 section_end = start + (sections[i].size & ~3u);
        while (start < section_end) {
            const DolLLVMPatch* patch =
                llvm_patch_for_start(patches, patch_count, start);
            u32 end;
            if (patch) {
                end = patch->end;
            } else {
                u32 remaining = section_end - start;
                u32 span = chunk_instructions * 4u;
                end = remaining < span ? section_end : start + span;
                end = next_llvm_partition_point(partition_points, point_count,
                                                start, end);
            }
            ranges[count].start = start;
            ranges[count].end = end;
            count++;
            start = end;
        }
    }
    *result_count = count;
    return ranges;
}

static const DolLLVMFunctionRange*
llvm_range_for(const DolLLVMFunctionRange* ranges, u32 count, u32 address) {
    u32 first = 0;
    u32 last = count;
    while (first < last) {
        u32 middle = first + (last - first) / 2u;
        if (address < ranges[middle].start)
            last = middle;
        else if (address >= ranges[middle].end)
            first = middle + 1u;
        else
            return &ranges[middle];
    }
    return NULL;
}

static u32 llvm_call_target_count(const DolIRTerminator* term) {
    return term->kind == DOLIR_TERM_COND_BRANCH ? 2u
           : term->kind == DOLIR_TERM_FALLTHROUGH ||
                     term->kind == DOLIR_TERM_BRANCH
               ? 1u
           : term->kind == DOLIR_TERM_INDIRECT ? 2u
                                                : 0u;
}

static int append_native_entry_candidate(LLVMNativeEntryCandidate** entries,
                                         u32* count, u32* capacity, u32 pc,
                                         u32 range_index) {
    if (*count == *capacity) {
        u32 next = *capacity ? *capacity * 2u : 4096u;
        LLVMNativeEntryCandidate* resized = (LLVMNativeEntryCandidate*)realloc(
            *entries, (size_t)next * sizeof(**entries));
        if (!resized)
            return 0;
        *entries = resized;
        *capacity = next;
    }
    (*entries)[(*count)++] = (LLVMNativeEntryCandidate){pc, range_index};
    return 1;
}

static int llvm_decode_at(const LoadedCodeSection* sections, u32 section_count,
                          u32 address, PPCInst* result) {
    for (u32 s = 0; s < section_count; s++) {
        const LoadedCodeSection* section = &sections[s];
        u32 end = section->address + section->size;
        if (!section->data || address < section->address || address >= end ||
            ((address - section->address) & 3u))
            continue;
        u32 offset = address - section->address;
        u32 raw = read_be32(section->data + offset);
        *result = ppc_decode(raw, address);
        if (result->op == PPC_OP_UNKNOWN &&
            embedded_data_word(section->embedded_data_mode, raw))
            result->embedded_data = true;
        return 1;
    }
    return 0;
}

typedef struct {
    u32 range_index;
    u64 samples;
} LLVMHotRange;

static int compare_hot_range(const void* a, const void* b) {
    const LLVMHotRange* left = (const LLVMHotRange*)a;
    const LLVMHotRange* right = (const LLVMHotRange*)b;
    if (left->samples != right->samples)
        return left->samples < right->samples ? 1 : -1;
    return left->range_index > right->range_index ? 1
           : left->range_index < right->range_index ? -1
                                                    : 0;
}

static u32 llvm_range_index_for(const DolLLVMFunctionRange* ranges, u32 count,
                                u32 address) {
    const DolLLVMFunctionRange* range = llvm_range_for(ranges, count, address);
    return range ? (u32)(range - ranges) : UINT32_MAX;
}

static const LoadedCodeSection*
llvm_section_for_range(const LoadedCodeSection* sections, u32 section_count,
                       const DolLLVMFunctionRange* range) {
    for (u32 i = 0; i < section_count; i++) {
        u32 end = sections[i].address + sections[i].size;
        if (sections[i].data && range->start >= sections[i].address &&
            range->end <= end)
            return &sections[i];
    }
    return NULL;
}

static int llvm_profile_select_address(const DolLLVMFunctionRange* ranges,
                                       u32 full_count, u32 address,
                                       unsigned char* selected, u32* queue,
                                       u32* queue_count) {
    u32 target = llvm_range_index_for(ranges, full_count, address);
    if (target == UINT32_MAX || selected[target])
        return 0;
    selected[target] = 1;
    queue[(*queue_count)++] = target;
    return 1;
}

static int llvm_profile_instruction_has_fallthrough(const PPCInst* inst) {
    switch (inst->op) {
    case PPC_OP_B:
        return 0;
    case PPC_OP_BC:
    case PPC_OP_BCLR:
    case PPC_OP_BCCTR:
        return (inst->bo & 0x14u) != 0x14u;
    case PPC_OP_SC:
    case PPC_OP_RFI:
        return 0;
    default:
        return 1;
    }
}

static int select_profiled_llvm_ranges(
    const LoadedCodeSection* sections, u32 section_count,
    DolLLVMFunctionRange* ranges, u32* range_count, const CliOptions* options,
    const DolLLVMPatch* patches, u32 patch_count) {
    if (!options->range_profile_path)
        return 1;

    const u32 full_count = *range_count;
    u64* weights = (u64*)calloc(full_count ? full_count : 1u, sizeof(*weights));
    u64* entry_weights =
        (u64*)calloc(full_count ? full_count : 1u, sizeof(*entry_weights));
    LLVMHotRange* hot =
        (LLVMHotRange*)malloc((size_t)(full_count ? full_count : 1u) *
                              sizeof(*hot));
    unsigned char* selected =
        (unsigned char*)calloc(full_count ? full_count : 1u, 1);
    u32* queue =
        (u32*)malloc((size_t)(full_count ? full_count : 1u) * sizeof(*queue));
    FILE* profile = fopen(options->range_profile_path, "r");
    if (!weights || !entry_weights || !hot || !selected || !queue || !profile) {
        fprintf(stderr, "error: cannot read range profile '%s'\n",
                options->range_profile_path);
        free(weights);
        free(entry_weights);
        free(hot);
        free(selected);
        free(queue);
        if (profile)
            fclose(profile);
        return 0;
    }

    char line[256];
    u64 total_samples = 0;
    while (fgets(line, sizeof(line), profile)) {
        char kind[32];
        char pc_text[32];
        unsigned long long samples = 0;
        if (sscanf(line, "%31[^,],%31[^,],%llu", kind, pc_text, &samples) != 3 ||
            !samples)
            continue;
        char* end = NULL;
        errno = 0;
        unsigned long pc = strtoul(pc_text, &end, 16);
        if (errno || !end || (*end && *end != '\r' && *end != '\n') ||
            pc > UINT32_MAX)
            continue;
        u32 index = llvm_range_index_for(ranges, full_count, (u32)pc);
        if (index == UINT32_MAX)
            continue;
        u64 sample_count = (u64)samples;
        if (strcmp(kind, "module_miss") == 0) {
            if (sample_count < options->range_profile_miss_min_samples)
                continue;
            if (UINT64_MAX - entry_weights[index] < sample_count)
                entry_weights[index] = UINT64_MAX;
            else
                entry_weights[index] += sample_count;
            continue;
        }
        if (strcmp(kind, "native_cycles") != 0)
            continue;
        if (UINT64_MAX - weights[index] < sample_count)
            weights[index] = UINT64_MAX;
        else
            weights[index] += sample_count;
        if (UINT64_MAX - total_samples < sample_count)
            total_samples = UINT64_MAX;
        else
            total_samples += sample_count;
    }
    fclose(profile);

    u32 hot_count = 0;
    u64 eligible_samples = 0;
    for (u32 i = 0; i < full_count; i++) {
        if (weights[i] < options->range_profile_min_samples || !weights[i])
            continue;
        hot[hot_count++] = (LLVMHotRange){i, weights[i]};
        eligible_samples += weights[i];
    }
    if (!hot_count || !eligible_samples) {
        fprintf(stderr,
                "error: range profile has no eligible native_cycles samples\n");
        free(weights);
        free(entry_weights);
        free(hot);
        free(selected);
        free(queue);
        return 0;
    }
    qsort(hot, hot_count, sizeof(*hot), compare_hot_range);

    u64 covered = 0;
    u32 seed_count = 0;
    while (seed_count < hot_count &&
           ((long double)covered * 100.0L <
                (long double)eligible_samples *
                    (long double)options->range_profile_coverage ||
            !seed_count)) {
        u32 index = hot[seed_count++].range_index;
        selected[index] = 1;
        covered += weights[index];
    }

    u32 queue_count = 0;
    for (u32 i = 0; i < seed_count; i++) {
        u32 index = hot[i].range_index;
        queue[queue_count++] = index;
        u32 left = index;
        u32 right = index;
        for (u32 step = 0; step < options->range_profile_neighbors; step++) {
            if (left && ranges[left - 1u].end == ranges[left].start) {
                left--;
                if (!selected[left]) {
                    selected[left] = 1;
                    queue[queue_count++] = left;
                }
            }
            if (right + 1u < full_count &&
                ranges[right].end == ranges[right + 1u].start) {
                right++;
                if (!selected[right]) {
                    selected[right] = 1;
                    queue[queue_count++] = right;
                }
            }
        }
    }
    const u32 neighbor_count = queue_count - seed_count;
    u32 profile_entry_ranges = 0;
    u32 profile_entry_added = 0;
    for (u32 i = 0; i < full_count; i++) {
        if (entry_weights[i] < options->range_profile_miss_min_samples ||
            !entry_weights[i])
            continue;
        profile_entry_ranges++;
        if (!selected[i]) {
            selected[i] = 1;
            queue[queue_count++] = i;
            profile_entry_added++;
        }
    }
    u32 forced_entry_ranges = 0;
    u32 forced_entry_added = 0;
    u32* forced_points = NULL;
    u32 forced_point_count = 0;
    if (!load_forced_llvm_points(sections, section_count, options,
                                 &forced_points, &forced_point_count)) {
        free(weights);
        free(entry_weights);
        free(hot);
        free(selected);
        free(queue);
        return 0;
    }
    for (u32 i = 0; i < forced_point_count; i++) {
        u32 index = llvm_range_index_for(ranges, full_count, forced_points[i]);
        if (index == UINT32_MAX)
            continue;
        forced_entry_ranges++;
        if (!selected[index]) {
            selected[index] = 1;
            queue[queue_count++] = index;
            forced_entry_added++;
        }
    }
    free(forced_points);

    for (u32 i = 0; i < patch_count; i++) {
        u32 index = llvm_range_index_for(ranges, full_count, patches[i].start);
        if (index != UINT32_MAX && !selected[index]) {
            selected[index] = 1;
            queue[queue_count++] = index;
        }
    }

    const u32 pre_call_closure_count = queue_count;
    u32 closure_begin = 0;
    u32 closure_end = queue_count;
    for (u32 depth = 0;
         depth < options->range_profile_call_closure_depth &&
         closure_begin < closure_end;
         depth++) {
        for (u32 cursor = closure_begin; cursor < closure_end; cursor++) {
            u32 index = queue[cursor];
            const DolLLVMFunctionRange* range = &ranges[index];
            if (llvm_patch_for_start(patches, patch_count, range->start))
                continue;
            const LoadedCodeSection* section =
                llvm_section_for_range(sections, section_count, range);
            if (!section)
                continue;
            for (u32 address = range->start; address < range->end; address += 4u) {
                u32 offset = address - section->address;
                PPCInst inst = ppc_decode(read_be32(section->data + offset), address);
                if ((inst.op != PPC_OP_B && inst.op != PPC_OP_BC) || !inst.lk)
                    continue;
                llvm_profile_select_address(ranges, full_count,
                                            inst.branch_target, selected,
                                            queue, &queue_count);
            }
        }
        closure_begin = closure_end;
        closure_end = queue_count;
    }
    const u32 call_closure_count = queue_count - pre_call_closure_count;

    const u32 pre_successor_closure_count = queue_count;
    closure_begin = 0;
    closure_end = queue_count;
    for (u32 depth = 0;
         depth < options->range_profile_successor_closure_depth &&
         closure_begin < closure_end;
         depth++) {
        for (u32 cursor = closure_begin; cursor < closure_end; cursor++) {
            u32 index = queue[cursor];
            const DolLLVMFunctionRange* range = &ranges[index];
            if (llvm_patch_for_start(patches, patch_count, range->start))
                continue;
            const LoadedCodeSection* section =
                llvm_section_for_range(sections, section_count, range);
            if (!section)
                continue;

            for (u32 address = range->start; address < range->end; address += 4u) {
                u32 offset = address - section->address;
                PPCInst inst =
                    ppc_decode(read_be32(section->data + offset), address);
                if ((inst.op == PPC_OP_B || inst.op == PPC_OP_BC) && !inst.lk)
                    llvm_profile_select_address(ranges, full_count,
                                                inst.branch_target, selected,
                                                queue, &queue_count);
                if ((inst.op == PPC_OP_BC || inst.op == PPC_OP_BCLR ||
                     inst.op == PPC_OP_BCCTR) &&
                    llvm_profile_instruction_has_fallthrough(&inst))
                    llvm_profile_select_address(ranges, full_count, address + 4u,
                                                selected, queue, &queue_count);
            }

            u32 section_end = section->address + (section->size & ~3u);
            if (range->end > range->start && range->end < section_end) {
                PPCInst last;
                if (llvm_decode_at(sections, section_count, range->end - 4u,
                                   &last) &&
                    llvm_profile_instruction_has_fallthrough(&last))
                    llvm_profile_select_address(ranges, full_count, range->end,
                                                selected, queue, &queue_count);
            }
        }
        closure_begin = closure_end;
        closure_end = queue_count;
    }
    const u32 successor_closure_count =
        queue_count - pre_successor_closure_count;

    u32 compacted = 0;
    for (u32 i = 0; i < full_count; i++)
        if (selected[i])
            ranges[compacted++] = ranges[i];
    *range_count = compacted;
    printf("range profile: %u/%u ranges selected; %u hot seeds, %u neighbors, "
           "%u profile-entry ranges (%u added), %u forced-entry ranges (%u added), "
           "%u call-closure, "
           "%u successor-closure; "
           "%.3Lf%% eligible native cycles covered"
           " (%llu/%llu total samples)\n",
           compacted, full_count, seed_count, neighbor_count,
           profile_entry_ranges, profile_entry_added,
           forced_entry_ranges, forced_entry_added,
           call_closure_count, successor_closure_count,
           eligible_samples
               ? (long double)covered * 100.0L / (long double)eligible_samples
               : 0.0L,
           (unsigned long long)eligible_samples,
           (unsigned long long)total_samples);
    free(weights);
    free(entry_weights);
    free(hot);
    free(selected);
    free(queue);
    return compacted != 0;
}

static int build_llvm_range_module(const LoadedCodeSection* sections,
                                   u32 section_count,
                                   const DolLLVMFunctionRange* range,
                                   DolIRModule* module) {
    const LoadedCodeSection* section = NULL;
    for (u32 s = 0; s < section_count; s++) {
        u32 section_end = sections[s].address + sections[s].size;
        if (range->start >= sections[s].address && range->end <= section_end) {
            section = &sections[s];
            break;
        }
    }
    if (!section || !section->data)
        return 0;
    u32 start = (range->start - section->address) / 4u;
    u32 count = (range->end - range->start) / 4u;
    PPCInst* instructions =
        (PPCInst*)malloc((size_t)count * sizeof(*instructions));
    if (!instructions)
        return 0;
    for (u32 i = 0; i < count; i++) {
        u32 raw = read_be32(section->data + (start + i) * 4u);
        instructions[i] =
            ppc_decode(raw, section->address + (start + i) * 4u);
        if (instructions[i].op == PPC_OP_UNKNOWN &&
            embedded_data_word(section->embedded_data_mode, raw))
            instructions[i].embedded_data = true;
    }
    dolir_module_init(module);
    int ok = dolir_build_chunk(module, instructions, count,
                               section->address + start * 4u);
    free(instructions);
    if (!ok)
        dolir_module_free(module);
    return ok;
}

static int append_llvm_abi_edge(DolLLVMCallEdge** edges, u32* count,
                                u32* capacity, u32 caller, u32 callsite_pc,
                                u32 callee, const u64* live_after,
                                const u64* defined_before,
                                const u64* may_dirty_before) {
    if (*count == *capacity) {
        u32 next = *capacity ? *capacity * 2u : 256u;
        DolLLVMCallEdge* resized =
            (DolLLVMCallEdge*)realloc(*edges, (size_t)next * sizeof(**edges));
        if (!resized)
            return 0;
        *edges = resized;
        *capacity = next;
    }
    (*edges)[*count].caller_start = caller;
    (*edges)[*count].callsite_pc = callsite_pc;
    (*edges)[*count].callee_address = callee;
    memcpy((*edges)[*count].live_after, live_after,
           sizeof((*edges)[*count].live_after));
    memcpy((*edges)[*count].defined_before, defined_before,
           sizeof((*edges)[*count].defined_before));
    memcpy((*edges)[*count].may_dirty_before, may_dirty_before,
           sizeof((*edges)[*count].may_dirty_before));
    (*count)++;
    return 1;
}

static int prepare_llvm_function_abis(const LoadedCodeSection* sections,
                                      u32 section_count,
                                      DolLLVMFunctionRange* ranges,
                                      u32 range_count, DolLLVMRuntime runtime,
                                      const u32* entry_points,
                                      u32 entry_point_count,
                                      FILE* fallback_report,
                                      LLVMNativeEntryCandidate** candidates,
                                      u32* candidate_count,
                                      u32* candidate_capacity) {
    DolLLVMCallEdge* edges = NULL;
    u32 edge_count = 0;
    u32 edge_capacity = 0;

    for (u32 range_index = 0; range_index < range_count; range_index++) {
        DolIRModule module;
        if (!build_llvm_range_module(sections, section_count,
                                     &ranges[range_index], &module))
            goto fail;
        if (!dolllvm_analyze_function_abi(&module.functions[0],
                                          &ranges[range_index])) {
            dolir_module_free(&module);
            goto fail;
        }
        const DolIRFunction* function = &module.functions[0];
        size_t state_word_count =
            (size_t)function->block_count * DOLIR_STATE_MASK_WORDS;
        u64* live_after =
            (u64*)malloc((state_word_count ? state_word_count : 1u) * sizeof(u64));
        u64* defined_before =
            (u64*)malloc((state_word_count ? state_word_count : 1u) * sizeof(u64));
        u64* may_dirty_before =
            (u64*)malloc((state_word_count ? state_word_count : 1u) * sizeof(u64));
        if (!live_after || !defined_before || !may_dirty_before ||
            !dolllvm_analyze_callsite_states(
                function, ranges[range_index].may_def_state, NULL, live_after,
                defined_before, may_dirty_before)) {
            free(live_after);
            free(defined_before);
            free(may_dirty_before);
            dolir_module_free(&module);
            goto fail;
        }
        for (u32 block = 0; block < function->block_count; block++) {
            const DolIRTerminator* term = &function->blocks[block].terminator;
            size_t state_offset = (size_t)block * DOLIR_STATE_MASK_WORDS;
            u32 targets = llvm_call_target_count(term);
            for (u32 slot = 0; slot < targets; slot++) {
                const DolLLVMFunctionRange* target = llvm_range_for(
                    ranges, range_count, term->target_addresses[slot]);
                if (target && target->start != ranges[range_index].start &&
                    !append_llvm_abi_edge(
                        &edges, &edge_count, &edge_capacity,
                        ranges[range_index].start, term->guest_pc,
                        term->target_addresses[slot], live_after + state_offset,
                        defined_before + state_offset,
                        may_dirty_before + state_offset)) {
                    free(live_after);
                    free(defined_before);
                    free(may_dirty_before);
                    dolir_module_free(&module);
                    goto fail;
                }
            }
        }
        free(live_after);
        free(defined_before);
        free(may_dirty_before);
        dolir_module_free(&module);
    }

    if (runtime == DOLLLVM_RUNTIME_MODERNGEKKO)
        dolllvm_enable_native_services(ranges, range_count);
    if (!dolllvm_propagate_function_abis(ranges, range_count, edges,
                                         edge_count))
        goto fail;

    u32 edge_index = 0;
    for (u32 range_index = 0; range_index < range_count; range_index++) {
        DolIRModule module;
        if (!build_llvm_range_module(sections, section_count,
                                     &ranges[range_index], &module))
            goto fail;
        const DolIRFunction* function = &module.functions[0];
        size_t post_word_count =
            (size_t)function->block_count * DOLIR_STATE_MASK_WORDS;
        u64* post_call_defs =
            (u64*)calloc(post_word_count ? post_word_count : 1u, sizeof(u64));
        if (!post_call_defs) {
            dolir_module_free(&module);
            goto fail;
        }
        for (u32 block = 0; block < function->block_count; block++) {
            const DolIRTerminator* term = &function->blocks[block].terminator;
            if (!term->linked)
                continue;
            u32 targets = llvm_call_target_count(term);
            for (u32 slot = 0; slot < targets; slot++) {
                const DolLLVMFunctionRange* target = llvm_range_for(
                    ranges, range_count, term->target_addresses[slot]);
                if (!target || target->start == ranges[range_index].start ||
                    !(target->abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE))
                    continue;
                for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
                    post_call_defs[(size_t)block * DOLIR_STATE_MASK_WORDS +
                                   word] |= target->output_state[word];
            }
        }
        u64* all_may_dirty_before =
            (u64*)malloc((post_word_count ? post_word_count : 1u) * sizeof(u64));
        if (!all_may_dirty_before ||
            !dolllvm_analyze_callsite_dirty(function, post_call_defs,
                                            all_may_dirty_before)) {
            free(all_may_dirty_before);
            free(post_call_defs);
            dolir_module_free(&module);
            goto fail;
        }
        for (u32 block = 0; block < function->block_count; block++) {
            const DolIRTerminator* term = &function->blocks[block].terminator;
            const u64* may_dirty_before =
                all_may_dirty_before + (size_t)block * DOLIR_STATE_MASK_WORDS;
            u32 targets = llvm_call_target_count(term);
            for (u32 slot = 0; slot < targets; slot++) {
                const DolLLVMFunctionRange* target = llvm_range_for(
                    ranges, range_count, term->target_addresses[slot]);
                if (!target || target->start == ranges[range_index].start)
                    continue;
                if (edge_index >= edge_count ||
                    edges[edge_index].caller_start != ranges[range_index].start ||
                    edges[edge_index].callsite_pc != term->guest_pc ||
                    edges[edge_index].callee_address !=
                        term->target_addresses[slot]) {
                    free(all_may_dirty_before);
                    free(post_call_defs);
                    dolir_module_free(&module);
                    goto fail;
                }
                memcpy(edges[edge_index].may_dirty_before, may_dirty_before,
                       sizeof(edges[edge_index].may_dirty_before));
                edge_index++;
            }

            if (fallback_report && term->kind == DOLIR_TERM_FALLBACK) {
                PPCInst fallback;
                if (!llvm_decode_at(sections, section_count, term->guest_pc,
                                    &fallback)) {
                    free(all_may_dirty_before);
                    free(post_call_defs);
                    dolir_module_free(&module);
                    goto fail;
                }
                const char* reason =
                    fallback.embedded_data          ? "embedded-data"
                    : fallback.op == PPC_OP_UNKNOWN ? "unknown"
                    : (fallback.op == PPC_OP_SC || fallback.op == PPC_OP_RFI)
                        ? "exception-boundary"
                        : "unsupported";
                char detail[32] = "";
                switch (fallback.op) {
                case PPC_OP_MFSPR:
                case PPC_OP_MTSPR:
                case PPC_OP_MFTB:
                    snprintf(detail, sizeof(detail), "spr=%u", fallback.spr);
                    break;
                case PPC_OP_MFSR:
                case PPC_OP_MTSR:
                    snprintf(detail, sizeof(detail), "sr=%u", fallback.sr);
                    break;
                case PPC_OP_TW:
                case PPC_OP_TWI:
                    snprintf(detail, sizeof(detail), "to=%u", fallback.to);
                    break;
                default:
                    break;
                }
                fprintf(fallback_report, "%08X,%08X,%s,%s,%s\n",
                        fallback.address, fallback.raw,
                        ppc_op_name(fallback.op), reason, detail);
            }
        }

        if (runtime == DOLLLVM_RUNTIME_MODERNGEKKO && candidates &&
            candidate_count && candidate_capacity) {
            DolLLVMFunctionRange entry_range = ranges[range_index];
            entry_range.abi_flags |= DOLLLVM_FUNCTION_ABI_NATIVE;
            DolLLVMOptions entry_options = {0};
            entry_options.runtime = DOLLLVM_RUNTIME_MODERNGEKKO;
            entry_options.function_ranges = &entry_range;
            entry_options.function_range_count = 1u;
            entry_options.entry_points = entry_points;
            entry_options.entry_point_count = entry_point_count;
            u32 max_entries = function->block_count;
            u32* local_entries = (u32*)malloc(
                (size_t)(max_entries ? max_entries : 1u) * sizeof(*local_entries));
            if (!local_entries) {
                free(all_may_dirty_before);
                free(post_call_defs);
                dolir_module_free(&module);
                goto fail;
            }
            u32 added = dolllvm_collect_native_entries(
                function, &entry_options, local_entries, max_entries);
            if (added > max_entries) {
                free(local_entries);
                free(all_may_dirty_before);
                free(post_call_defs);
                dolir_module_free(&module);
                goto fail;
            }
            for (u32 i = 0; i < added; i++) {
                if (!append_native_entry_candidate(
                        candidates, candidate_count, candidate_capacity,
                        local_entries[i], range_index)) {
                    free(local_entries);
                    free(all_may_dirty_before);
                    free(post_call_defs);
                    dolir_module_free(&module);
                    goto fail;
                }
            }
            free(local_entries);
        }
        free(all_may_dirty_before);
        free(post_call_defs);
        dolir_module_free(&module);
    }
    if (edge_index != edge_count ||
        !dolllvm_propagate_function_abis(ranges, range_count, edges,
                                         edge_count))
        goto fail;
    if (runtime == DOLLLVM_RUNTIME_MODERNGEKKO)
        for (u32 range_index = 0; range_index < range_count; range_index++)
            memset(ranges[range_index].escape_state, 0,
                   sizeof(ranges[range_index].escape_state));

    free(edges);
    return 1;
fail:
    free(edges);
    return 0;
}

static u32* collect_llvm_entry_points(const LoadedCodeSection* sections,
                                      u32 section_count, u32 program_entry,
                                      const DolRecompSymbolMap* symbols,
                                      u32* result_count) {
    size_t capacity = 1u + (symbols ? symbols->count : 0u);
    for (u32 i = 0; i < section_count; i++)
        capacity += sections[i].size / 4u;
    u32* points = (u32*)malloc(capacity * sizeof(*points));
    if (!points)
        return NULL;
    u32 count = 0;
    if (llvm_code_address(sections, section_count, program_entry))
        points[count++] = program_entry;
    if (symbols) {
        for (u32 i = 0; i < symbols->count; i++) {
            u32 address = symbols->symbols[i].address;
            if (llvm_code_address(sections, section_count, address))
                points[count++] = address;
        }
    }
    for (u32 section_index = 0; section_index < section_count;
         section_index++) {
        const LoadedCodeSection* section = &sections[section_index];
        for (u32 offset = 0; section->data && offset + 4u <= section->size;
             offset += 4u) {
            u32 address = section->address + offset;
            PPCInst inst =
                ppc_decode(read_be32(section->data + offset), address);
            if ((inst.op == PPC_OP_B || inst.op == PPC_OP_BC) &&
                llvm_code_address(sections, section_count, inst.branch_target))
                points[count++] = inst.branch_target;
        }
    }
    qsort(points, count, sizeof(*points), compare_u32);
    u32 unique = 0;
    for (u32 i = 0; i < count; i++) {
        if (!unique || points[i] != points[unique - 1u])
            points[unique++] = points[i];
    }
    *result_count = unique;
    return points;
}

static int append_profiled_llvm_points(
    const LoadedCodeSection* sections, u32 section_count,
    const CliOptions* options, const char* label, u32** points, u32* count) {
    if (!options->range_profile_path)
        return 1;

    FILE* profile = fopen(options->range_profile_path, "r");
    if (!profile)
        return 0;
    u32 capacity = *count + 64u;
    u32* resized = (u32*)realloc(*points, (size_t)capacity * sizeof(**points));
    if (!resized) {
        fclose(profile);
        return 0;
    }
    *points = resized;

    char line[256];
    u32 added = 0;
    while (fgets(line, sizeof(line), profile)) {
        char kind[32];
        char pc_text[32];
        unsigned long long samples = 0;
        if (sscanf(line, "%31[^,],%31[^,],%llu", kind, pc_text, &samples) != 3 ||
            strcmp(kind, "module_miss") != 0 ||
            samples < options->range_profile_miss_min_samples)
            continue;
        char* end = NULL;
        errno = 0;
        unsigned long pc = strtoul(pc_text, &end, 16);
        if (errno || !end || (*end && *end != '\r' && *end != '\n') ||
            pc > UINT32_MAX ||
            !llvm_code_address(sections, section_count, (u32)pc))
            continue;
        if (*count == capacity) {
            capacity *= 2u;
            resized =
                (u32*)realloc(*points, (size_t)capacity * sizeof(**points));
            if (!resized) {
                fclose(profile);
                return 0;
            }
            *points = resized;
        }
        (*points)[(*count)++] = (u32)pc;
        added++;
    }
    fclose(profile);

    qsort(*points, *count, sizeof(**points), compare_u32);
    u32 unique = 0;
    for (u32 i = 0; i < *count; i++) {
        if (!unique || (*points)[i] != (*points)[unique - 1u])
            (*points)[unique++] = (*points)[i];
    }
    *count = unique;
    if (added)
        printf("range profile: %u observed module-miss %s added\n", added,
               label);
    return 1;
}

static int llvm_cache_dir(char* path, size_t size) {
    const char* configured = getenv("DOLRECOMP_LLVM_CACHE");
    if (configured && (!configured[0] || strcmp(configured, "off") == 0))
        return 0;
    if (!configured && llvm_fast_iteration())
        return 0;
    if (configured) {
        if (snprintf(path, size, "%s", configured) >= (int)size)
            return 0;
    } else {
#ifdef _WIN32
        const char* root = getenv("LOCALAPPDATA");
        if (!root ||
            snprintf(path, size, "%s\\DolRecomp\\llvm", root) >= (int)size)
            return 0;
#else
        const char* root = getenv("XDG_CACHE_HOME");
        if (root) {
            if (snprintf(path, size, "%s/dolrecomp/llvm", root) >= (int)size)
                return 0;
        } else {
            root = getenv("HOME");
            if (!root || snprintf(path, size, "%s/.cache/dolrecomp/llvm",
                                  root) >= (int)size)
                return 0;
        }
#endif
    }
    return make_dir_tree(path);
}

static int reuse_llvm_object(const LLVMChunkJob* job) {
    if (getenv("DOLRECOMP_LLVM_RESUME") && valid_object_file(job, job->path) &&
        (!job->emit_thinlto || file_exists(job->thinlto_path)) &&
        valid_llvm_job_stamp(job))
        return 1;
    if (!job->cache_path[0] || !valid_object_file(job, job->cache_path) ||
        (job->emit_thinlto && !file_exists(job->cache_bitcode_path)) ||
        !copy_file(job->cache_path, job->path))
        return 0;
    if (job->emit_thinlto &&
        !copy_file(job->cache_bitcode_path, job->thinlto_path))
        return 0;
    write_llvm_job_stamp(job);
    return 1;
}

static void cache_llvm_object(const LLVMChunkJob* job) {
    if (!job->cache_path[0] || (valid_object_file(job, job->cache_path) &&
         (!job->emit_thinlto || file_exists(job->cache_bitcode_path))))
        return;
    char temp[1440];
#ifdef _WIN32
    int process_id = _getpid();
#else
    int process_id = (int)getpid();
#endif
    if (snprintf(temp, sizeof(temp), "%s.tmp.%d", job->cache_path,
                 process_id) >= (int)sizeof(temp))
        return;
    remove(temp);
    if (!copy_file(job->path, temp))
        return;
    if (rename(temp, job->cache_path) != 0)
        remove(temp);
    if (!job->emit_thinlto)
        return;
    if (snprintf(temp, sizeof(temp), "%s.tmp.%d", job->cache_bitcode_path,
                 process_id) >= (int)sizeof(temp))
        return;
    remove(temp);
    if (!copy_file(job->thinlto_path, temp))
        return;
    if (rename(temp, job->cache_bitcode_path) != 0)
        remove(temp);
}

static int emit_llvm_chunk_job(const void* data, void* user) {
    const LLVMChunkJob* job = (const LLVMChunkJob*)data;
    (void)user;
    if (reuse_llvm_object(job))
        return 1;
    char temp_path[1440];
#ifdef _WIN32
    int process_id = _getpid();
#else
    int process_id = (int)getpid();
#endif
    if (snprintf(temp_path, sizeof(temp_path), "%s.tmp.%d", job->path,
                 process_id) >= (int)sizeof(temp_path))
        return 0;
    remove(temp_path);
    DolIRModule module;
    dolir_module_init(&module);
    int built = 1;
    for (u32 i = 0; i < job->object_range_count; i++) {
        const DolLLVMFunctionRange* range =
            &job->ranges[job->first_range_index + i];
        u32 offset = (range->start - job->function_address) / 4u;
        u32 count = (range->end - range->start) / 4u;
        if (!dolir_build_chunk(&module, job->insts + offset, count,
                               range->start)) {
            built = 0;
            break;
        }
    }
    if (!built || !dolir_verify(&module, stderr)) {
        dolir_module_free(&module);
        return 0;
    }
    DolLLVMOptions options = {0};
    options.target_triple = getenv("DOLRECOMP_LLVM_TARGET");
    options.target_profile = job->target_profile;
    options.semantics = job->semantics;
    options.instrumentation = job->instrumentation;
    options.native_abi_policy = job->native_abi_policy;
    options.runtime = job->runtime;
    options.symbol_suffix = job->symbol_suffix;
    options.profile_generate_path = job->profile_generate_path;
    options.profile_use_path = job->profile_use_path;
    options.partition_seed = job->partition_seed;
    options.state_in_memory = job->state_in_memory;
    options.emit_thinlto = job->emit_thinlto;
    options.thinlto_path = job->emit_thinlto ? job->thinlto_path : NULL;
    options.fixed_memory_layout = 1;
    options.ram_size = job->ram_size;
    options.mem2_size = job->mem2_size;
    options.function_ranges_prepared = 1;
    options.optimization_level = (int)job->optimization_level;
    options.fast_iteration = job->fast_passes;
    options.verify = !llvm_fast_iteration();
    options.function_ranges = job->ranges;
    options.function_range_count = job->range_count;
    options.entry_points = job->entry_points;
    options.entry_point_count = job->entry_point_count;
    options.patches = job->patches;
    options.patch_count = job->patch_count;
    const char* write_journal = getenv("DOLRECOMP_LLVM_WRITE_JOURNAL");
    if (write_journal && !strcmp(write_journal, "1"))
        options.instrumentation = DOLLLVM_INSTRUMENTATION_LOCKSTEP;
    char ir_path[1440];
    const char* dump_ir = getenv("DOLRECOMP_LLVM_DUMP_IR");
    if (dump_ir && (!strcmp(dump_ir, "1") || strstr(job->name, dump_ir))) {
        if (snprintf(ir_path, sizeof(ir_path), "%s.ll", job->path) >=
            (int)sizeof(ir_path)) {
            dolir_module_free(&module);
            return 0;
        }
        options.emit_ir = 1;
        options.ir_path = ir_path;
    }
    if (!job->emit_thinlto)
        remove(job->thinlto_path);
    int ok = dolllvm_emit_object(&module, temp_path, &options, stderr);
    dolir_module_free(&module);
    if (ok) {
        remove(job->path);
        if (rename(temp_path, job->path) != 0) {
            fprintf(stderr, "error: cannot publish LLVM object %s: %s\n",
                    job->path, strerror(errno));
            ok = 0;
        }
    }
    if (ok) {
        write_llvm_job_stamp(job);
        cache_llvm_object(job);
    }
    if (!ok)
        remove(temp_path);
    return ok;
}

static void report_llvm_progress(const LLVMChunkJob* jobs,
                                 const unsigned char* states, u32 count,
                                 u32* next_report) {
    while (*next_report < count && states[*next_report]) {
        const LLVMChunkJob* job = &jobs[*next_report];
        printf("[%u/%u] %s LLVM object %s\n", job->index, job->total,
               states[*next_report] == 2 ? "Reusing cached" : "Emitting",
               job->name);
        (*next_report)++;
    }
    fflush(stdout);
}

static int run_llvm_chunk_jobs(const LLVMChunkJob* jobs, u32 count,
                               u32 requested_jobs) {
    u32 workers = effective_chunk_jobs(count, requested_jobs);
#ifdef _WIN32
    for (u32 i = 0; i < count; i++) {
        printf("[%u/%u] Emitting LLVM object %s\n", jobs[i].index,
               jobs[i].total, jobs[i].name);
        fflush(stdout);
    }
    return run_parallel_jobs(jobs, sizeof(*jobs), count, workers,
                             emit_llvm_chunk_job, NULL);
#else
    typedef struct {
        pid_t pid;
        u32 batch_start;
        u32 batch_count;
    } LLVMWorker;
    LLVMWorker* active_workers =
        (LLVMWorker*)calloc(workers, sizeof(*active_workers));
    u32* pending = (u32*)calloc(count, sizeof(*pending));
    u32* retry = (u32*)calloc(count, sizeof(*retry));
    unsigned char* states = (unsigned char*)calloc(count, sizeof(*states));
    if (!active_workers || !pending || !retry || !states) {
        free(active_workers);
        free(pending);
        free(retry);
        free(states);
        return 0;
    }

    u32 completed = 0;
    u32 pending_count = 0;
    u32 next_report = 0;
    for (u32 i = 0; i < count; i++) {
        if (reuse_llvm_object(&jobs[i])) {
            completed++;
            states[i] = 2;
        } else {
            pending[pending_count++] = i;
        }
    }
    report_llvm_progress(jobs, states, count, &next_report);

    int failed = 0;
    u32 next = 0;
    u32 active = 0;
    u32 retry_count = 0;
    const u32 batch_size = llvm_worker_batch_size();
    while (next < pending_count || active != 0) {
        while (next < pending_count && active < workers) {
            const u32 batch_start = next;
            u32 batch_count = pending_count - next;
            if (batch_count > batch_size)
                batch_count = batch_size;
            next += batch_count;
            fflush(NULL);
            const pid_t pid = fork();
            if (pid == 0) {
                int ok = 1;
                for (u32 i = 0; i < batch_count; i++) {
                    const u32 job = pending[batch_start + i];
                    if (!emit_llvm_chunk_job(&jobs[job], NULL))
                        ok = 0;
                }
                _exit(ok ? 0 : 1);
            }
            if (pid < 0) {
                fprintf(stderr, "error: can't start LLVM worker process\n");
                for (u32 i = 0; i < batch_count; i++)
                    retry[retry_count++] = pending[batch_start + i];
                continue;
            }
            active_workers[active++] =
                (LLVMWorker){pid, batch_start, batch_count};
        }
        if (!active)
            break;

        int status = 0;
        pid_t finished;
        do {
            finished = waitpid(-1, &status, 0);
        } while (finished < 0 && errno == EINTR);
        if (finished < 0) {
            failed = 1;
            break;
        }

        u32 slot = 0;
        while (slot < active && active_workers[slot].pid != finished)
            slot++;
        if (slot == active) {
            failed = 1;
            continue;
        }
        const u32 batch_start = active_workers[slot].batch_start;
        const u32 batch_count = active_workers[slot].batch_count;
        active_workers[slot] = active_workers[--active];
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            for (u32 i = 0; i < batch_count; i++) {
                const u32 job = pending[batch_start + i];
                completed++;
                states[job] = 1;
            }
            report_llvm_progress(jobs, states, count, &next_report);
        } else {
            for (u32 i = 0; i < batch_count; i++)
                retry[retry_count++] = pending[batch_start + i];
        }
    }

    while (next < pending_count)
        retry[retry_count++] = pending[next++];

    for (u32 i = 0; i < retry_count; i++) {
        const u32 job = retry[i];
        fprintf(stderr, "retrying LLVM object %s after worker failure\n",
                jobs[job].name);
        fflush(NULL);
        const pid_t pid = fork();
        int status = 0;
        if (pid == 0)
            _exit(emit_llvm_chunk_job(&jobs[job], NULL) ? 0 : 1);
        if (pid < 0 || waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
            failed = 1;
            continue;
        }
        completed++;
        states[job] = 1;
        report_llvm_progress(jobs, states, count, &next_report);
    }
    free(active_workers);
    free(pending);
    free(retry);
    free(states);
    return !failed && completed == count;
#endif
}

static int emit_code_sections_llvm(const LoadedCodeSection* sections,
                                   u32 section_count, const char* output_path,
                                   DolRecompCPU cpu, u32 entry_point,
                                   u32 requested_jobs, int local_chunks_dir,
                                   const DolRecompSymbolMap* symbols,
                                   const CliOptions* options) {
    DolLLVMTargetProfile profiles[5];
    u32 profile_count = parse_llvm_target_set(options->llvm_targets, profiles);
    if (!profile_count) {
        fprintf(stderr, "error: invalid or duplicate LLVM target set\n");
        return 0;
    }
    const int modern_runtime =
        options->llvm_runtime == DOLLLVM_RUNTIME_MODERNGEKKO;
    if (modern_runtime && profile_count != 1u) {
        fprintf(
            stderr,
            "error: ModernGekko modules currently require one LLVM target\n");
        return 0;
    }
    if (modern_runtime && !options->game_id) {
        fprintf(stderr, "error: ModernGekko modules require a game ID\n");
        return 0;
    }
    char stem[1024];
    char header_path[1100];
    char symbol_header_path[1100];
    char fallback_path[1100];
    char chunks_dir[1100];
    char include_name[512];
    if (!make_output_stem(output_path, stem, sizeof(stem)) ||
        !split_include_name(stem, include_name, sizeof(include_name)))
        return 0;
    if (snprintf(header_path, sizeof(header_path), "%s.h", stem) >=
            (int)sizeof(header_path) ||
        snprintf(symbol_header_path, sizeof(symbol_header_path), "%s_symbols.h",
                 stem) >= (int)sizeof(symbol_header_path) ||
        snprintf(fallback_path, sizeof(fallback_path), "%s_fallbacks.csv",
                 stem) >= (int)sizeof(fallback_path)) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }
    if (local_chunks_dir) {
        char output_dir[1024];
        if (!path_dirname(output_path, output_dir, sizeof(output_dir)) ||
            !join_path(chunks_dir, sizeof(chunks_dir), output_dir, "chunks"))
            return 0;
    } else if (snprintf(chunks_dir, sizeof(chunks_dir), "%s_chunks", stem) >=
               (int)sizeof(chunks_dir)) {
        return 0;
    }
    if (!make_dir_tree(chunks_dir))
        return 0;

    FILE* manifest = fopen(output_path, "w");
    FILE* header = fopen(header_path, "w");
    FILE* fallback_report = fopen(fallback_path, "w");
    if (!manifest || !header || !fallback_report) {
        fprintf(stderr, "error: cannot create LLVM output files\n");
        if (manifest)
            fclose(manifest);
        if (header)
            fclose(header);
        if (fallback_report)
            fclose(fallback_report);
        return 0;
    }
    fprintf(fallback_report, "address,raw,opcode,reason,detail\n");
    fprintf(manifest, "#include \"%s\"\n", include_name);
    if (modern_runtime) {
        fprintf(header, "#ifndef RECOMP_GENERATED_H\n"
                        "#define RECOMP_GENERATED_H\n\n"
                        "#include <stdbool.h>\n"
                        "#include <stddef.h>\n"
                        "#include <stdatomic.h>\n"
                        "#include <stdint.h>\n"
                        "#include \"Core/PowerPC/Native/NativeModuleABI.h\"\n\n"
                        "#define DOLRECOMP_BACKEND_LLVM 1\n\n");
    } else {
    emit_header_for_cpu(header, cpu);
    fprintf(header, "#define DOLRECOMP_BACKEND_LLVM 1\n\n");
    }
    if (symbols) {
        u32 symbol_count = count_code_symbols(symbols, sections, section_count);
        if (!symbol_count) {
            fprintf(stderr, "error: symbol map has no executable entries\n");
            fclose(header);
            fclose(manifest);
            fclose(fallback_report);
            return 0;
        }
        FILE* symbol_header = fopen(symbol_header_path, "w");
        if (!symbol_header ||
            !emit_symbol_definitions(symbol_header, symbols, sections,
                                     section_count)) {
            fprintf(stderr, "error: failed to emit symbol map\n");
            if (symbol_header)
                fclose(symbol_header);
            fclose(header);
            fclose(manifest);
            fclose(fallback_report);
            return 0;
        }
        fclose(symbol_header);
    } else {
        remove(symbol_header_path);
    }
    fprintf(header, "\n// Function entry points\n");

    FunctionList funcs = {0};
    SMCAnalysis smc = {0};
    DolLLVMFunctionRange* ranges = NULL;
    u32* entry_points = NULL;
    u32* partition_points = NULL;
    u32* native_entries = NULL;
    DolLLVMPatch* patches = NULL;
    LLVMNativeEntryCandidate* native_entry_candidates = NULL;
    u32 entry_point_count = 0;
    u32 patch_count = 0;
    u32 native_entry_count = 0;
    u32 native_entry_candidate_count = 0;
    u32 native_entry_candidate_capacity = 0;
    if (!load_patches(sections, section_count, options,
                             &patches, &patch_count))
        goto fail;
    entry_points = collect_llvm_entry_points(
        sections, section_count, entry_point, symbols, &entry_point_count);
    if (!entry_points)
        goto fail;
    if (!append_profiled_llvm_points(sections, section_count, options,
                                     "entry PCs", &entry_points,
                                     &entry_point_count))
        goto fail;
    if (!append_forced_llvm_points(sections, section_count, options,
                                   &entry_points, &entry_point_count))
        goto fail;
    if (!append_patch_points(patches, patch_count, 0,
                             &entry_points, &entry_point_count))
        goto fail;
    u32 partition_point_count = 0;
    partition_points = collect_llvm_partition_points(
        sections, section_count, entry_point, symbols, &partition_point_count);
    if (!partition_points)
        goto fail;
    if (!append_profiled_llvm_points(sections, section_count, options,
                                     "partition PCs", &partition_points,
                                     &partition_point_count))
        goto fail;
    if (!append_forced_llvm_points(sections, section_count, options,
                                   &partition_points, &partition_point_count))
        goto fail;
    if (!append_patch_points(patches, patch_count, 1,
                             &partition_points, &partition_point_count))
        goto fail;
    u32 file_count = 0;
    u32 range_count = 0;
    const u32 chunk_instructions = options->partition_instructions
                                       ? options->partition_instructions
                                       : llvm_chunk_instructions();
    ranges = build_llvm_ranges(sections, section_count, chunk_instructions,
                               partition_points, partition_point_count,
                               patches, patch_count,
                               &range_count);
    if (!ranges)
        goto fail;
    if (!select_profiled_llvm_ranges(sections, section_count, ranges,
                                     &range_count, options, patches,
                                     patch_count))
        goto fail;
    char cache_dir[1100] = "";
    if (!llvm_cache_dir(cache_dir, sizeof(cache_dir)))
        cache_dir[0] = '\0';
    if (!prepare_llvm_function_abis(
            sections, section_count, ranges, range_count,
            (DolLLVMRuntime)options->llvm_runtime, entry_points,
            entry_point_count, fallback_report, &native_entry_candidates,
            &native_entry_candidate_count, &native_entry_candidate_capacity))
        goto fail;
#if defined(__GLIBC__)
    if (!llvm_fast_iteration())
        malloc_trim(0);
#endif
    dolllvm_apply_native_abi_policy(
        ranges, range_count, (DolLLVMNativeABIPolicy)options->llvm_native_abi);
    for (u32 i = 0; i < patch_count; i++) {
        const DolLLVMFunctionRange* range =
            llvm_range_for(ranges, range_count, patches[i].start);
        if (!range || range->start != patches[i].start ||
            range->end != patches[i].end) {
            fprintf(stderr,
                    "error: patch %08X-%08X did not form an exact LLVM range\n",
                    patches[i].start, patches[i].end);
            goto fail;
        }
    }
    if (modern_runtime) {
        native_entries = (u32*)malloc(
            (size_t)(native_entry_candidate_count + patch_count
                         ? native_entry_candidate_count + patch_count
                         : 1u) *
            sizeof(*native_entries));
        if (!native_entries)
            goto fail;
        for (u32 i = 0; i < native_entry_candidate_count; i++) {
            LLVMNativeEntryCandidate candidate = native_entry_candidates[i];
            const DolLLVMPatch* patch =
                candidate.range_index < range_count
                    ? llvm_patch_for_start(patches, patch_count,
                                           ranges[candidate.range_index].start)
                    : NULL;
            if (patch && candidate.pc != patch->start)
                continue;
            if (candidate.range_index < range_count &&
                (ranges[candidate.range_index].abi_flags &
                 DOLLLVM_FUNCTION_ABI_NATIVE))
                native_entries[native_entry_count++] = candidate.pc;
        }
        for (u32 i = 0; i < patch_count; i++)
            native_entries[native_entry_count++] = patches[i].start;
    }
    if (getenv("DOLRECOMP_LLVM_ABI_STATS")) {
        for (u32 variant = 0; variant < profile_count; variant++) {
            DolLLVMOptions stats_options = {0};
            char triple[128];
            stats_options.target_profile = profiles[variant];
            if (dolllvm_effective_triple(&stats_options, triple,
                                         sizeof(triple)))
                dolllvm_report_abi_stats(ranges, range_count, triple, stdout);
        }
    }
    const u32 ranges_per_object = llvm_ranges_per_object();
    const u32 optimization_level = llvm_optimization_level();
    const int fast_passes = llvm_fast_passes();
    const int emit_thinlto = llvm_emit_thinlto();
    u32 total_job_count = 0;
    u32 counted_ranges = 0;
    for (u32 s = 0; s < section_count; s++) {
        const LoadedCodeSection* section = &sections[s];
        if (!section->data || !section->size)
            continue;
        u32 first = counted_ranges;
        u32 section_end = section->address + section->size;
        while (counted_ranges < range_count &&
               ranges[counted_ranges].start >= section->address &&
               ranges[counted_ranges].end <= section_end)
            counted_ranges++;
        u32 count = counted_ranges - first;
        total_job_count +=
            ((count + ranges_per_object - 1u) / ranges_per_object) *
            profile_count;
    }
    if (counted_ranges != range_count)
        goto fail;

    u32 emitted_range_count = 0;
    for (u32 s = 0; s < section_count; s++) {
        const LoadedCodeSection* section = &sections[s];
        if (!section->data || !section->size)
            continue;
        u32 num_insts = section->size / 4u;
        PPCInst* insts = (PPCInst*)malloc((size_t)num_insts * sizeof(*insts));
        if (!insts) {
            fprintf(stderr, "error: out of memory\n");
            goto fail;
        }
        u32 embedded = 0;
        u32 unknown = 0;
        for (u32 i = 0; i < num_insts; i++) {
            u32 raw = read_be32(section->data + i * 4u);
            insts[i] = ppc_decode(raw, section->address + i * 4u);
            if (insts[i].op == PPC_OP_UNKNOWN &&
                embedded_data_word(section->embedded_data_mode, raw))
                insts[i].embedded_data = true;
            embedded += insts[i].embedded_data;
            unknown += insts[i].op == PPC_OP_UNKNOWN && !insts[i].embedded_data;
        }
        printf("decoding %s[%u]: %u instructions at 0x%08X\n", section->label,
               section->index, num_insts, section->address);
        printf("  %u known, %u embedded data, %u unknown\n",
               num_insts - embedded - unknown, embedded, unknown);
        if (section->embedded_data_mode == EMBEDDED_DATA_DOL) {
            analyze_smc_section(sections, section_count, insts, num_insts,
                                &smc);
            if (smc.allocation_failed) {
                free(insts);
                goto fail;
            }
        }

        u32 first_range = emitted_range_count;
        u32 section_end = section->address + section->size;
        while (emitted_range_count < range_count &&
               ranges[emitted_range_count].start >= section->address &&
               ranges[emitted_range_count].end <= section_end)
            emitted_range_count++;
        u32 chunk_total = emitted_range_count - first_range;
        u32 object_total =
            (chunk_total + ranges_per_object - 1u) / ranges_per_object;
        u32 job_total = object_total * profile_count;
        LLVMChunkJob* chunk_jobs = job_total
                                       ? (LLVMChunkJob*)calloc(
                                             job_total, sizeof(*chunk_jobs))
                                       : NULL;
        if (job_total && !chunk_jobs) {
            free(insts);
            goto fail;
        }

        for (u32 object_index = 0; object_index < object_total;
             object_index++) {
            u32 local_first = object_index * ranges_per_object;
            u32 object_range_count = chunk_total - local_first;
            if (object_range_count > ranges_per_object)
                object_range_count = ranges_per_object;
            u32 output_index = first_range + local_first;
            const DolLLVMFunctionRange* first = &ranges[output_index];
            const DolLLVMFunctionRange* last =
                &ranges[output_index + object_range_count - 1u];
            u32 function_address = first->start;
            u32 start = (function_address - section->address) / 4u;
            u32 object_instruction_count =
                (last->end - function_address) / 4u;
            LLVMChunkJob* job = &chunk_jobs[object_index * profile_count];
            for (u32 variant = 0; variant < profile_count; variant++) {
                LLVMChunkJob* target_job = &job[variant];
                const char* target_suffix =
                    dolllvm_target_profile_suffix(profiles[variant]);
                if (variant)
                    snprintf(target_job->symbol_suffix,
                             sizeof(target_job->symbol_suffix), "__%s",
                             target_suffix);
                int name_length;
                if (ranges_per_object == 1u) {
                    name_length =
                        variant
                            ? snprintf(target_job->name,
                                       sizeof(target_job->name),
                                       "chunk_%04u_%s%u_%08X_%s.o",
                                       output_index, section->label,
                                       section->index, function_address,
                                       target_suffix)
                            : snprintf(target_job->name,
                                       sizeof(target_job->name),
                                       "chunk_%04u_%s%u_%08X.o", output_index,
                                       section->label, section->index,
                                       function_address);
                } else {
                    name_length =
                        variant
                            ? snprintf(target_job->name,
                                       sizeof(target_job->name),
                                       "chunk_%04u_%s%u_%08X_r%u_%s.o",
                                       output_index, section->label,
                                       section->index, function_address,
                                       object_range_count, target_suffix)
                            : snprintf(target_job->name,
                                       sizeof(target_job->name),
                                       "chunk_%04u_%s%u_%08X_r%u.o",
                                       output_index, section->label,
                                       section->index, function_address,
                                       object_range_count);
                }
                if (name_length >= (int)sizeof(target_job->name) ||
                    !join_path(target_job->path, sizeof(target_job->path),
                               chunks_dir, target_job->name)) {
                    free(chunk_jobs);
                    free(insts);
                    goto fail;
                }
                if (snprintf(target_job->thinlto_path,
                             sizeof(target_job->thinlto_path), "%s.bc",
                             target_job->path) >=
                    (int)sizeof(target_job->thinlto_path)) {
                    free(chunk_jobs);
                    free(insts);
                    goto fail;
                }
                target_job->insts = insts + start;
                target_job->count = object_instruction_count;
                target_job->function_address = function_address;
                target_job->first_range_index = output_index;
                target_job->object_range_count = object_range_count;
                target_job->index = file_count +
                                    object_index * profile_count + variant + 1u;
                target_job->total = total_job_count;
                target_job->ranges = ranges;
                target_job->range_count = range_count;
                target_job->entry_points = entry_points;
                target_job->entry_point_count = entry_point_count;
                target_job->patches = patches;
                target_job->patch_count = patch_count;
                target_job->target_profile = profiles[variant];
                target_job->semantics = options->fast_semantics
                                            ? DOLLLVM_SEMANTICS_FAST
                                            : DOLLLVM_SEMANTICS_EXACT;
                target_job->instrumentation =
                    options->lockstep_instrumentation
                        ? DOLLLVM_INSTRUMENTATION_LOCKSTEP
                        : DOLLLVM_INSTRUMENTATION_NONE;
                target_job->native_abi_policy =
                    (DolLLVMNativeABIPolicy)options->llvm_native_abi;
                target_job->runtime = (DolLLVMRuntime)options->llvm_runtime;
                target_job->profile_generate_path =
                    options->profile_generate_path;
                target_job->profile_use_path = options->profile_use_path;
                target_job->partition_seed = options->partition_seed;
                target_job->state_in_memory = options->state_in_memory;
                target_job->ram_size = GC_MAIN_RAM_SIZE;
                target_job->mem2_size =
                    cpu == DOLRECOMP_CPU_GEKKO ? 0u : WII_MEM2_SIZE;
                target_job->optimization_level = optimization_level;
                target_job->fast_passes = fast_passes;
                target_job->emit_thinlto = emit_thinlto;
                target_job->hash = llvm_job_hash(target_job);
                if (cache_dir[0]) {
                    char cache_name[64];
                    snprintf(cache_name, sizeof(cache_name), "%016llx.o",
                             (unsigned long long)target_job->hash);
                    if (!join_path(target_job->cache_path,
                                   sizeof(target_job->cache_path), cache_dir,
                                   cache_name))
                        target_job->cache_path[0] = '\0';
                    if (target_job->cache_path[0] && emit_thinlto &&
                        snprintf(target_job->cache_bitcode_path,
                                 sizeof(target_job->cache_bitcode_path),
                                 "%s.bc", target_job->cache_path) >=
                            (int)sizeof(target_job->cache_bitcode_path))
                        target_job->cache_path[0] = '\0';
                }
            }
        }

        // Logical range metadata is independent of physical object batching.
        for (u32 chunk_index = 0; chunk_index < chunk_total; chunk_index++) {
            u32 output_index = first_range + chunk_index;
            const DolLLVMFunctionRange* range = &ranges[output_index];
            u32 function_address = range->start;
            const DolLLVMPatch* patch =
                llvm_patch_for_start(patches, patch_count,
                                     function_address);
            u32 start = (function_address - section->address) / 4u;
            u32 chunk_count = (range->end - range->start) / 4u;
            if (modern_runtime) {
                if ((range->abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE) || patch) {
                    fprintf(header,
                            "MGNativeExit func_%08X(const MGNativeRuntime*, "
                            "const MGNativeState*, uint64_t*, uint64_t*, "
                            "uint64_t*, uint32_t, uint32_t, uint32_t);\n",
                            function_address);
                    u64 native_hash = hash_bytes(UINT64_C(0xCBF29CE484222325),
                                                 section->data + start * 4u,
                                                 (size_t)chunk_count * 4u);
                    if (!function_list_add_hashed(&funcs, function_address,
                                                  range->end, native_hash)) {
                        free(chunk_jobs);
                        free(insts);
                        goto fail;
                    }
                }
            } else {
                emit_chunk_prototype(header, function_address);
                for (u32 variant = 1; variant < profile_count; variant++)
                    fprintf(header, "void func_%08X__%s(CPUState* ctx);\n",
                            function_address,
                            dolllvm_target_profile_suffix(profiles[variant]));
                if (!function_list_add(&funcs, function_address, range->end)) {
                    free(chunk_jobs);
                    free(insts);
                    goto fail;
                }
            }
        }

        for (u32 object_index = 0; object_index < object_total;
             object_index++) {
            LLVMChunkJob* job = &chunk_jobs[object_index * profile_count];
            for (u32 variant = 0; variant < profile_count; variant++) {
                fprintf(manifest, "// object: chunks/%s\n", job[variant].name);
                fprintf(manifest, "// object[%s]: chunks/%s\n",
                        dolllvm_target_profile_name(profiles[variant]),
                        job[variant].name);
            }
            if (emit_thinlto)
                fprintf(manifest, "// ThinLTO summaries: chunks/*.o.bc\n");
            file_count += profile_count;
        }
        u32 active_jobs = effective_chunk_jobs(job_total, requested_jobs);
        printf("  writing %u LLVM objects with %u job%s\n", job_total,
               active_jobs, active_jobs == 1 ? "" : "s");
        if (!run_llvm_chunk_jobs(chunk_jobs, job_total, requested_jobs)) {
            free(chunk_jobs);
            free(insts);
            goto fail;
        }
        free(chunk_jobs);
        free(insts);
    }
    if (emitted_range_count != range_count)
        goto fail;

    if (native_entry_count) {
        qsort(native_entries, native_entry_count, sizeof(*native_entries),
              compare_u32);
        u32 unique = 0;
        for (u32 i = 0; i < native_entry_count; i++) {
            if (!unique || native_entries[i] != native_entries[unique - 1u])
                native_entries[unique++] = native_entries[i];
        }
        native_entry_count = unique;
    }

    {
        char report[1100];
        if (snprintf(report, sizeof(report), "%s_smc.txt", stem) >=
                (int)sizeof(report) ||
            !write_smc_report(&smc, report))
            goto fail;
        if (smc.possible)
            printf("warning: executable memory writes detected; report: %s\n",
                   report);
    }
    if (modern_runtime) {
        if (!emit_native_module(header, &funcs, ranges, range_count,
                                options->game_id,
                                native_entries, native_entry_count,
                                patches, patch_count))
            goto fail;
    } else {
    emit_dispatch_helpers(header, &funcs, entry_point);
    emit_llvm_variant_table(header, &funcs, profiles, profile_count,
                            options->fast_semantics);
    }
    emit_footer(header);
    fprintf(manifest, "\n// %u native objects\n", file_count);
    fclose(header);
    fclose(manifest);
    fclose(fallback_report);
    smc_analysis_free(&smc);
    function_list_free(&funcs);
    free(ranges);
    free(entry_points);
    free(partition_points);
    free(native_entries);
    free_patches(patches, patch_count);
    free(native_entry_candidates);
    printf("done!\n  header: %s\n  objects: %s (%u files)\n", header_path,
           chunks_dir, file_count);
    return 1;

fail:
    smc_analysis_free(&smc);
    function_list_free(&funcs);
    free(ranges);
    free(entry_points);
    free(partition_points);
    free(native_entries);
    free_patches(patches, patch_count);
    free(native_entry_candidates);
    fclose(header);
    fclose(manifest);
    fclose(fallback_report);
    return 0;
}
#endif

int emit_code_sections_split(const LoadedCodeSection* sections,
                             u32 section_count, const char* output_path,
                                    DolRecompCPU cpu, u32 entry_point, u32 jobs,
                                    int local_chunks_dir,
                                    const DolRecompSymbolMap* symbols,
                                    const CliOptions* options) {
#ifdef DOLRECOMP_ENABLE_LLVM
    if (options->backend == DOLRECOMP_BACKEND_LLVM)
        return emit_code_sections_llvm(sections, section_count, output_path,
                                       cpu, entry_point, jobs, local_chunks_dir,
                                       symbols, options);
#else
    if (options->backend == DOLRECOMP_BACKEND_LLVM) {
        fprintf(stderr, "error: LLVM backend is unavailable in this build\n");
        return 0;
    }
#endif
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

    if (snprintf(header_path, sizeof(header_path), "%s.h", stem) >=
        (int)sizeof(header_path)) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }
    if (snprintf(symbol_header_path, sizeof(symbol_header_path), "%s_symbols.h",
                 stem) >= (int)sizeof(symbol_header_path)) {
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
        if (snprintf(chunks_dir, sizeof(chunks_dir), "%s_chunks", stem) >=
            (int)sizeof(chunks_dir)) {
            fprintf(stderr, "error: output path is too long\n");
            return 0;
        }
        snprintf(chunks_label, sizeof(chunks_label), "%s",
                 path_basename(chunks_dir));
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
            fprintf(
                stderr,
                "error: symbol map has no entries in executable sections\n");
            fclose(header);
            fclose(manifest);
            return 0;
        }
        FILE* symbol_header = fopen(symbol_header_path, "w");
        if (!symbol_header) {
            fprintf(stderr, "error: can't open output '%s'\n",
                    symbol_header_path);
            fclose(header);
            fclose(manifest);
            return 0;
        }
        if (!emit_symbol_definitions(symbol_header, symbols, sections,
                                     section_count)) {
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
    const u32 chunk_instructions = c_chunk_instructions();
    FunctionList funcs = {0};
    SMCAnalysis smc = {0};

    for (u32 s = 0; s < section_count; s++) {
        const LoadedCodeSection* section = &sections[s];
        if (section->size == 0 || !section->data)
            continue;

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
            printf("  %u decoded, %u known, %u unknown\n", decoded,
                   decoded - unknown, unknown);
        }

        if (section->embedded_data_mode == EMBEDDED_DATA_DOL) {
            analyze_smc_section(sections, section_count, insts, num_insts,
                                &smc);
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
            (num_insts + chunk_instructions - 1u) / chunk_instructions;
        ChunkJob* chunk_jobs =
            (ChunkJob*)calloc(section_job_count, sizeof(ChunkJob));
        if (!chunk_jobs) {
            fprintf(stderr, "error: out of memory\n");
            smc_analysis_free(&smc);
            function_list_free(&funcs);
            free(insts);
            fclose(header);
            fclose(manifest);
            return 0;
        }

        for (u32 start = 0; start < num_insts; start += chunk_instructions) {
            u32 chunk_count = num_insts - start;
            u32 func_addr = base_addr + start * 4u;
            char chunk_name[128];
            u32 job_index = start / chunk_instructions;

            if (chunk_count > chunk_instructions)
                chunk_count = chunk_instructions;

            if (snprintf(chunk_name, sizeof(chunk_name),
                         "chunk_%04u_%s%u_%08X.c", file_count, section->label,
                         section->index,
                         func_addr) >= (int)sizeof(chunk_name)) {
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

            if (!join_path(job->path, sizeof(job->path), chunks_dir,
                           chunk_name)) {
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
            if (!function_list_add(&funcs, func_addr,
                                   func_addr + chunk_count * 4u)) {
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
        printf("  writing %u chunks with %u job%s\n", section_job_count,
               active_jobs, active_jobs == 1 ? "" : "s");
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

    {
        char smc_report_path[1100];
        if (snprintf(smc_report_path, sizeof(smc_report_path), "%s_smc.txt",
                     stem) >= (int)sizeof(smc_report_path) ||
            !write_smc_report(&smc, smc_report_path)) {
            smc_analysis_free(&smc);
            function_list_free(&funcs);
            fclose(header);
            fclose(manifest);
            return 0;
        }
    }

    if (smc.possible) {
        u32 display_count = smc.range_count;
        if (display_count > SMC_DISPLAY_RANGE_LIMIT)
            display_count = SMC_DISPLAY_RANGE_LIMIT;

        printf("warning: this DOL may patch executable memory at runtime. "
               "generated code many need additional patches\n");
        printf("  possible patching instructions:\n");
        for (u32 i = 0; i < display_count; i++) {
            printf("    0x%08X-0x%08X\n", smc.ranges[i].start,
                   smc.ranges[i].end);
        }

        if (smc.range_count > SMC_DISPLAY_RANGE_LIMIT) {
            printf("    ...\n");
            printf("  full list: %s_smc.txt\n", stem);
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
                          const DolRecompSymbolMap* symbols,
                          const CliOptions* options) {
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
                                    local_chunks_dir, symbols, options);
}

int emit_rpx_split(const RPXFile* rpx, const char* output_path,
                          DolRecompCPU cpu, u32 jobs, int local_chunks_dir,
                          const CliOptions* options) {
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
                                    output_path, cpu, 0, jobs, local_chunks_dir,
                                    NULL, options);
}

int emit_rel_split(const RELFile* rel, const char* output_path,
                          DolRecompCPU cpu, u32 jobs, int local_chunks_dir,
                          const CliOptions* options) {
    LoadedCodeSection* sections = (LoadedCodeSection*)calloc(
        rel->section_count, sizeof(LoadedCodeSection));
    if (!sections) {
        fprintf(stderr, "error: out of memory\n");
        return 0;
    }

    u32 section_count = 0;
    for (u32 i = 0; i < rel->section_count; i++) {
        const RELSection* rel_section = &rel->sections[i];
        if (!rel_section->executable || rel_section->size == 0 ||
            !rel_section->data)
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
                                      rel->entry_point, jobs, local_chunks_dir,
                                      NULL, options);
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
                              DolRecompCPU cpu, u32 jobs, u32 start_base,
                              const CliOptions* options) {
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

        printf("  module %u: %s -> base 0x%08X\n", items[i].rel.module_id,
               paths.paths[i], items[i].rel.base_address);
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
                                   items[i].rel.module_id, rel_output_path,
                                   sizeof(rel_output_path))) {
            goto done;
        }
        printf("\nwriting output to: %s\n", rel_output_path);
        if (!emit_rel_split(&items[i].rel, rel_output_path, cpu, jobs, 1,
                            options))
            goto done;
    }

    ok = 1;

done:
    free(map_entries);
    rel_batch_free(items, paths.count);
    path_list_free(&paths);
    return ok;
}
