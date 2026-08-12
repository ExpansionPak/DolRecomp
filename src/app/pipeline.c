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
#include "analysis/cfg.h"
#include "analysis/regions.h"
#include "common/perf.h"
#include <stdint.h>
#ifdef DOLRECOMP_ENABLE_LLVM
#include "ir/dolir_builder.h"
#include "backend/llvm/llvm_backend.h"
#include "cpu/cpu.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

static DolRecompRegionOptions g_region_options;

void pipeline_set_region_options(const DolRecompRegionOptions* options) {
    if (options)
        g_region_options = *options;
    else
        memset(&g_region_options, 0, sizeof(g_region_options));
}

const DolRecompRegionOptions* pipeline_region_options(void) {
    return &g_region_options;
}

/* Emitted code size per region. Reported, never load-bearing: a size that
   cannot be read is recorded as zero rather than failing the build. */
static u32 perf_file_size(const char* path) {
    FILE* in = fopen(path, "rb");
    if (!in)
        return 0;
    u32 size = 0;
    if (fseek(in, 0, SEEK_END) == 0) {
        long value = ftell(in);
        if (value > 0)
            size = (u32)value;
    }
    fclose(in);
    return size;
}

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
// 128, measured. A chunk is one LLVM function, so this is the block count the
// register allocator keeps the whole guest register file live across. Against
// the previous 1024 default this is +57.9% throughput and -66% .text on Mario
// Kart; 64 gains a further 1.4% but its range overlaps 128's, so it is not a
// proven gain. See docs/LLVM-EXPERIMENTS.md E002-E004.
#define DOLLLVM_DEFAULT_CHUNK_INSTRUCTIONS 128u
#define DOLLLVM_DEFAULT_WORKER_BATCH 4u
// v6 carries the execution budget across generated function calls.
// Any change that alters generated code must bump this, because
// llvm_job_hash() omits the pass pipeline, opt level and LLVM version.
// v7: ps1 preservation fix in dolir_builder (lfd and fmr/fneg/fabs/fnabs/fsel
// no longer splat into the high paired-single slot). Default codegen changed,
// so every cached object from v6 is stale.
#define DOLLLVM_CACHE_VERSION "dolllvm-v7"
// The LLVM optimisation level used for generated objects. Named so it can be
// folded into the cache key; changing it must not reuse cached objects.
#define DOLLLVM_OPT_LEVEL 2

/* One contiguous stretch of guest code. A fixed chunk is exactly one of these;
   a planned region is one or more, because accreting a caller with a callee
   that does not sit next to it in memory produces a region with a hole. */
typedef struct {
    const PPCInst* insts;
    u32 count;
    u32 address;
} LLVMRun;

typedef struct {
    /* The first run, kept as named fields so the fixed path is untouched.
       runs == NULL means "this job is exactly the single run described here". */
    const PPCInst* insts;
    u32 count;
    u32 function_address;

    const LLVMRun* runs;
    u32 run_count;

    u32 index;
    u32 total;
    const DolLLVMFunctionRange* ranges;
    u32 range_count;
    u64 hash;
    char name[128];
    char path[1400];
    char cache_path[1400];
} LLVMChunkJob;

/* Uniform access to a job's runs whether or not it carries an explicit list. */
static u32 llvm_job_run_count(const LLVMChunkJob* job) {
    return job->runs ? job->run_count : 1u;
}

static LLVMRun llvm_job_run(const LLVMChunkJob* job, u32 index) {
    if (job->runs)
        return job->runs[index];
    LLVMRun run;
    run.insts = job->insts;
    run.count = job->count;
    run.address = job->function_address;
    return run;
}

// The floor is 32, not the 128 the C path uses.
//
// A chunk becomes exactly one LLVM function, so this value is the number of
// basic blocks the register allocator has to keep the whole promoted guest
// register file live across -- and that scope is what drives the generated
// code size. Measured on Mario Kart (LLVM-EXPERIMENTS E002/E003), against the
// 1024 default:
//
//     1024   .text 1,012,522,870   speed 0.3288
//      256   .text   450,227,766   speed 0.4404   +33.9%
//      128   .text   345,215,974   speed 0.5192   +57.9%
//
// monotonic, with disjoint confidence ranges at every step, so 128 was the
// binding constraint rather than the optimum. Smaller chunks do eventually
// cost -- a call that leaves the chunk returns through the dispatcher instead
// of branching -- so this is a curve with a minimum, not a free win. Sweep it
// per title rather than assuming this one's answer.
#define DOLLLVM_MIN_CHUNK_INSTRUCTIONS 32u

static u32 llvm_chunk_instructions(void) {
    const char* configured = getenv("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS");
    if (!configured || !configured[0])
        return DOLLLVM_DEFAULT_CHUNK_INSTRUCTIONS;
    char* end = NULL;
    errno = 0;
    unsigned long value = strtoul(configured, &end, 10);
    if (errno || !end || *end || value < DOLLLVM_MIN_CHUNK_INSTRUCTIONS ||
        value > 4096u) {
        fprintf(stderr,
                "warning: DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS must be %u..4096; "
                "using %u\n",
                DOLLLVM_MIN_CHUNK_INSTRUCTIONS,
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

// Validate the object format selected by the target triple.
static int valid_object_file(const char* path) {
    return dolllvm_object_matches_triple(path, getenv("DOLRECOMP_LLVM_TARGET"))
               ? 1
               : 0;
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

static u64 llvm_job_hash(const LLVMChunkJob* job) {
    u64 hash = 1469598103934665603ull;
    hash = hash_bytes(hash, DOLLLVM_CACHE_VERSION, strlen(DOLLLVM_CACHE_VERSION));
    hash = hash_bytes(hash, &job->function_address, sizeof(job->function_address));
    hash = hash_bytes(hash, &job->count, sizeof(job->count));
    u32 state_size = (u32)sizeof(CPUState);
    hash = hash_bytes(hash, &state_size, sizeof(state_size));
    // Host triples must distinguish caches when no target was requested.
    char triple[256];
    if (dolllvm_effective_triple(getenv("DOLRECOMP_LLVM_TARGET"), triple,
                                 sizeof(triple)))
        hash = hash_bytes(hash, triple, strlen(triple));
    // LLVM version, target CPU and features, and the pass pipeline. Without
    // these a codegen experiment reuses objects built with the old settings
    // and reports them as its result.
    char codegen[1024];
    if (dolllvm_codegen_fingerprint(codegen, sizeof(codegen)))
        hash = hash_bytes(hash, codegen, strlen(codegen));
    u32 opt_level = (u32)DOLLLVM_OPT_LEVEL;
    hash = hash_bytes(hash, &opt_level, sizeof(opt_level));
    /* Every run, and the run partition itself: two regions covering the same
       instructions in a different grouping generate different code, so they
       must not collide in the cache. */
    u32 run_count = llvm_job_run_count(job);
    hash = hash_bytes(hash, &run_count, sizeof(run_count));
    for (u32 r = 0; r < run_count; r++) {
        LLVMRun run = llvm_job_run(job, r);
        hash = hash_bytes(hash, &run.address, sizeof(run.address));
        hash = hash_bytes(hash, &run.count, sizeof(run.count));
        for (u32 i = 0; i < run.count; i++) {
            hash = hash_bytes(hash, &run.insts[i].address,
                              sizeof(run.insts[i].address));
            hash = hash_bytes(hash, &run.insts[i].raw, sizeof(run.insts[i].raw));
            hash = hash_bytes(hash, &run.insts[i].embedded_data,
                              sizeof(run.insts[i].embedded_data));
        }
    }
    for (u32 i = 0; i < job->range_count; i++)
        hash = hash_bytes(hash, &job->ranges[i], sizeof(job->ranges[i]));
    return hash;
}

static int llvm_cache_dir(char* path, size_t size) {
    const char* configured = getenv("DOLRECOMP_LLVM_CACHE");
    if (configured && (!configured[0] || strcmp(configured, "off") == 0))
        return 0;
    if (configured) {
        if (snprintf(path, size, "%s", configured) >= (int)size)
            return 0;
    } else {
#ifdef _WIN32
        const char* root = getenv("LOCALAPPDATA");
        if (!root || snprintf(path, size, "%s\\DolRecomp\\llvm", root) >= (int)size)
            return 0;
#else
        const char* root = getenv("XDG_CACHE_HOME");
        if (root) {
            if (snprintf(path, size, "%s/dolrecomp/llvm", root) >= (int)size)
                return 0;
        } else {
            root = getenv("HOME");
            if (!root || snprintf(path, size, "%s/.cache/dolrecomp/llvm", root) >=
                             (int)size)
                return 0;
        }
#endif
    }
    return make_dir_tree(path);
}

static int reuse_llvm_object(const LLVMChunkJob* job) {
    if (getenv("DOLRECOMP_LLVM_RESUME") && valid_object_file(job->path) &&
        valid_llvm_job_stamp(job))
        return 1;
    if (!job->cache_path[0] || !valid_object_file(job->cache_path) ||
        !copy_file(job->cache_path, job->path))
        return 0;
    write_llvm_job_stamp(job);
    return 1;
}

static void cache_llvm_object(const LLVMChunkJob* job) {
    if (!job->cache_path[0] || valid_object_file(job->cache_path))
        return;
    char temp[1440];
#ifdef _WIN32
    int process_id = _getpid();
#else
    int process_id = (int)getpid();
#endif
    if (snprintf(temp, sizeof(temp), "%s.tmp.%d", job->cache_path, process_id) >=
        (int)sizeof(temp))
        return;
    remove(temp);
    if (!copy_file(job->path, temp))
        return;
    if (rename(temp, job->cache_path) != 0)
        remove(temp);
}

static int emit_llvm_chunk_job(const void* data, void* user) {
    const LLVMChunkJob* job = (const LLVMChunkJob*)data;
    (void)user;
    if (reuse_llvm_object(job)) {
#ifdef _WIN32
        // Say so. A silent reuse is indistinguishable from a regeneration in
        // the log, and "the cache was hit" is exactly the thing that must not
        // be assumed when checking whether a codegen change was really tested.
        printf("[%u/%u] Reusing cached LLVM object %s\n", job->index,
               job->total, job->name);
        fflush(stdout);
#endif
        return 1;
    }
#ifdef _WIN32
    // See run_llvm_chunk_jobs: on Windows this is the only live progress.
    printf("[%u/%u] Emitting LLVM object %s\n", job->index, job->total,
           job->name);
    fflush(stdout);
    time_t started = time(NULL);
#endif
    char temp_path[1440];
#ifdef _WIN32
    int process_id = _getpid();
#else
    int process_id = (int)getpid();
#endif
    if (snprintf(temp_path, sizeof(temp_path), "%s.tmp.%d", job->path,
                 process_id) >=
        (int)sizeof(temp_path))
        return 0;
    remove(temp_path);
    DolIRModule module;
    dolir_module_init(&module);
    /* One DolIRFunction per run. A region's runs land in one module, which is
       what lets LLVM see a caller and its callee together. */
    u32 run_count = llvm_job_run_count(job);
    for (u32 r = 0; r < run_count; r++) {
        LLVMRun run = llvm_job_run(job, r);
        if (!dolir_build_chunk(&module, run.insts, run.count, run.address)) {
            dolir_module_free(&module);
            return 0;
        }
    }
    if (!dolir_verify(&module, stderr)) {
        dolir_module_free(&module);
        return 0;
    }
    DolLLVMOptions options = {0};
    options.target_triple = getenv("DOLRECOMP_LLVM_TARGET");
    options.optimization_level = DOLLLVM_OPT_LEVEL;
    options.verify = 1;
    options.function_ranges = job->ranges;
    options.function_range_count = job->range_count;
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
#ifdef _WIN32
    printf("[%u/%u] %s LLVM object %s (%llds)\n", job->index, job->total,
           ok ? "Finished" : "FAILED", job->name,
           (long long)(time(NULL) - started));
    fflush(stdout);
#endif
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
    // Progress is reported from inside the job, not dumped up front. The
    // Windows path used to print every line before starting any work, so a
    // chunk that hung produced a complete-looking log and no indication of
    // which chunk was stuck -- one such hang ran 49 minutes with nothing to
    // point at. A start line, and a done line carrying elapsed seconds, means
    // the stuck chunk is the one with no matching completion.
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

static int compare_function_range(const void* a, const void* b) {
    u32 left = ((const DolLLVMFunctionRange*)a)->start;
    u32 right = ((const DolLLVMFunctionRange*)b)->start;
    return (left > right) - (left < right);
}

/* Region-planned LLVM emission.
 *
 * Deliberately a separate path from the fixed-chunk emitter rather than a flag
 * threaded through it. The brief requires the fixed path stay available until
 * this one reaches parity, and the surest way to keep it available is to not
 * touch it.
 *
 * The shape differs in one structural way: the fixed path can precompute every
 * chunk boundary from a formula before decoding anything, whereas region
 * boundaries are a result of analysis. So this decodes every section first,
 * builds one CFG across all of them, plans, and only then emits.
 */
static int emit_llvm_regions(const LoadedCodeSection* sections, u32 section_count,
                             DolRecompCPU cpu, u32 entry_point, u32 requested_jobs,
                             const char* chunks_dir, const char* stem,
                             const char* header_path, FILE* header,
                             FILE* manifest, FILE* fallback_report) {
    (void)cpu;
    const DolRecompRegionOptions* options = pipeline_region_options();

    DolCfgProgram cfg;
    dolcfg_init(&cfg);
    DolRegionPlan plan;
    dolregion_plan_init(&plan);
    FunctionList funcs = {0};
    SMCAnalysis smc = {0};

    PPCInst** decoded = (PPCInst**)calloc(section_count ? section_count : 1u,
                                          sizeof(PPCInst*));
    LLVMRun* runs = NULL;
    LLVMChunkJob* jobs = NULL;
    DolLLVMFunctionRange* ranges = NULL;
    unsigned char* cached_before_run = NULL;
    u32 file_count = 0;
    int status = 0;

    if (!decoded)
        goto done;

    cfg.entry_point = entry_point;

    for (u32 s = 0; s < section_count; s++) {
        const LoadedCodeSection* section = &sections[s];
        if (!section->data || !section->size)
            continue;

        u32 num_insts = section->size / 4u;
        PPCInst* insts = (PPCInst*)malloc((size_t)num_insts * sizeof(*insts));
        if (!insts) {
            fprintf(stderr, "error: out of memory\n");
            goto done;
        }
        decoded[s] = insts;

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
        printf("decoding %s[%u]: %u instructions at 0x%08X\n",
               section->label, section->index, num_insts, section->address);
        printf("  %u known, %u embedded data, %u unknown\n",
               num_insts - embedded - unknown, embedded, unknown);

        if (section->embedded_data_mode == EMBEDDED_DATA_DOL) {
            analyze_smc_section(sections, section_count, insts, num_insts, &smc);
            if (smc.allocation_failed)
                goto done;
        }

        if (!dolcfg_add_section(&cfg, insts, num_insts, section->address,
                                section->label))
            goto done;
    }

    /* SMC-suspect code must be able to end a region: a boundary there is what
       keeps a conservative path available for it. */
    for (u32 i = 0; i < smc.range_count; i++) {
        if (!dolcfg_add_smc_range(&cfg, smc.ranges[i].start, smc.ranges[i].end))
            goto done;
    }

    if (!dolcfg_build(&cfg, stderr))
        goto done;

    DolRegionMode mode = DOLREGION_MODE_CFG;
    if (options->mode_name && !dolregion_parse_mode(options->mode_name, &mode)) {
        fprintf(stderr, "error: unknown region mode '%s'\n", options->mode_name);
        goto done;
    }

    DolRegionLimits limits;
    dolregion_default_limits(&limits);
    if (options->max_instructions)
        limits.max_instructions = options->max_instructions;
    if (options->max_ir_instructions)
        limits.max_ir_instructions = options->max_ir_instructions;

    if (!dolregion_plan_build(&plan, &cfg, mode, &limits, stderr))
        goto done;

    printf("planned %u regions (%s, max %u instructions): "
           "%u blocks, %u functions, %u crossings\n",
           plan.region_count, dolregion_mode_name(mode), limits.max_instructions,
           cfg.block_count, cfg.function_count, plan.cross_region_edges);

    if (options->report_path &&
        !dolregion_write_report(&plan, &cfg, options->report_path, stderr))
        goto done;

    /* Flatten every region into contiguous runs. A region's blocks are sorted
       by address, so adjacent blocks whose addresses touch form one run. */
    u32 run_capacity = cfg.block_count ? cfg.block_count : 1u;
    runs = (LLVMRun*)malloc(run_capacity * sizeof(*runs));
    jobs = (LLVMChunkJob*)calloc(plan.region_count ? plan.region_count : 1u,
                                 sizeof(*jobs));
    if (!runs || !jobs)
        goto done;

    u32 run_total = 0;
    for (u32 r = 0; r < plan.region_count; r++) {
        DolRegion* region = &plan.regions[r];
        jobs[r].runs = NULL; /* set after the array stops moving */
        jobs[r].run_count = 0;
        u32 first_run = run_total;

        /* Blocks arrive grouped by function, so sort by address before
           coalescing or two adjacent blocks from different functions would not
           be recognised as touching. */
        u32* ordered = (u32*)malloc((region->block_count ? region->block_count : 1u) *
                                    sizeof(u32));
        if (!ordered)
            goto done;
        memcpy(ordered, region->blocks, region->block_count * sizeof(u32));
        for (u32 i = 1; i < region->block_count; i++) {
            u32 key = ordered[i];
            u32 j = i;
            while (j > 0 && cfg.blocks[ordered[j - 1u]].start > cfg.blocks[key].start) {
                ordered[j] = ordered[j - 1u];
                j--;
            }
            ordered[j] = key;
        }

        for (u32 i = 0; i < region->block_count; i++) {
            const DolCfgBlock* block = &cfg.blocks[ordered[i]];
            if (run_total > first_run &&
                runs[run_total - 1u].address +
                        runs[run_total - 1u].count * 4u == block->start) {
                runs[run_total - 1u].count += block->instruction_count;
                continue;
            }
            const DolCfgSection* section = NULL;
            for (u32 s = 0; s < cfg.section_count; s++) {
                u32 end = cfg.sections[s].base_address + cfg.sections[s].count * 4u;
                if (block->start >= cfg.sections[s].base_address && block->start < end) {
                    section = &cfg.sections[s];
                    break;
                }
            }
            if (!section) {
                free(ordered);
                goto done;
            }
            if (run_total == run_capacity) {
                run_capacity *= 2u;
                LLVMRun* grown = (LLVMRun*)realloc(runs, run_capacity * sizeof(*runs));
                if (!grown) {
                    free(ordered);
                    goto done;
                }
                runs = grown;
            }
            runs[run_total].address = block->start;
            runs[run_total].count = block->instruction_count;
            runs[run_total].insts =
                section->insts + (block->start - section->base_address) / 4u;
            run_total++;
        }
        free(ordered);

        jobs[r].run_count = run_total - first_run;
        /* Stored as an offset for now; rebased once `runs` stops reallocating. */
        jobs[r].runs = (const LLVMRun*)(uintptr_t)first_run;
    }

    /* Every run is a separately generated entry point, so the emitter needs all
       of them to tell an intra-module target from a cross-module one. */
    ranges = (DolLLVMFunctionRange*)calloc(run_total ? run_total : 1u,
                                           sizeof(*ranges));
    if (!ranges)
        goto done;
    for (u32 i = 0; i < run_total; i++) {
        ranges[i].start = runs[i].address;
        ranges[i].end = runs[i].address + runs[i].count * 4u;
    }
    /* rangeFor() binary-searches these. Runs are emitted in region order, which
       is not address order, so the sort is required for correctness here --
       unlike the fixed path, where chunks are already ascending. */
    qsort(ranges, run_total, sizeof(*ranges), compare_function_range);

    char cache_dir[1100] = "";
    if (!llvm_cache_dir(cache_dir, sizeof(cache_dir)))
        cache_dir[0] = '\0';

    for (u32 r = 0; r < plan.region_count; r++) {
        LLVMChunkJob* job = &jobs[r];
        u32 first_run = (u32)(uintptr_t)job->runs;
        job->runs = runs + first_run;
        if (job->run_count == 0)
            continue;

        job->insts = job->runs[0].insts;
        job->count = job->runs[0].count;
        job->function_address = job->runs[0].address;
        job->index = r + 1u;
        job->total = plan.region_count;
        job->ranges = ranges;
        job->range_count = run_total;

        if (snprintf(job->name, sizeof(job->name), "region_%06u_%08X.o", r,
                     job->function_address) >= (int)sizeof(job->name) ||
            !join_path(job->path, sizeof(job->path), chunks_dir, job->name))
            goto done;

        job->hash = llvm_job_hash(job);
        if (cache_dir[0]) {
            char cache_name[64];
            snprintf(cache_name, sizeof(cache_name), "%016llx.o",
                     (unsigned long long)job->hash);
            if (!join_path(job->cache_path, sizeof(job->cache_path), cache_dir,
                           cache_name))
                job->cache_path[0] = '\0';
        }

        /* Each run keeps its own public entry point, so dispatch and every
           ModernGekko replacement address still resolve exactly as before. */
        for (u32 i = 0; i < job->run_count; i++) {
            emit_chunk_prototype(header, job->runs[i].address);
            if (!function_list_add(&funcs, job->runs[i].address,
                                   job->runs[i].address + job->runs[i].count * 4u))
                goto done;
        }
        /* Exactly the fixed path's format. The module template parses this
           manifest and takes everything after "// object: " as the object
           path, so an appended "(N runs)" became part of the filename and the
           configure failed looking for "region_000000_80003100.o (16 runs)".
           Run counts belong in the region report, which already carries them. */
        fprintf(manifest, "// object: chunks/%s\n", job->name);
        file_count++;
    }

    u32 active_jobs = effective_chunk_jobs(plan.region_count, requested_jobs);
    printf("  writing %u LLVM region objects with %u job%s\n", plan.region_count,
           active_jobs, active_jobs == 1 ? "" : "s");

    cached_before_run = (unsigned char*)calloc(
        plan.region_count ? plan.region_count : 1u, 1u);
    if (cached_before_run) {
        for (u32 r = 0; r < plan.region_count; r++)
            cached_before_run[r] = reuse_llvm_object(&jobs[r]) ? 1u : 0u;
    }

    if (!run_llvm_chunk_jobs(jobs, plan.region_count, requested_jobs))
        goto done;

    for (u32 r = 0; r < plan.region_count; r++) {
        const DolRegion* region = &plan.regions[r];
        DolPerfRegion record;
        memset(&record, 0, sizeof(record));
        record.region_id = r;
        record.guest_start = region->guest_start;
        record.guest_end = region->guest_end;
        record.guest_instructions = region->instruction_count;
        record.ir_instructions = region->estimated_ir_instructions;
        record.blocks = region->block_count;
        record.loops = region->loop_count;
        record.code_bytes = perf_file_size(jobs[r].path);
        record.cache_hit = cached_before_run ? cached_before_run[r] : 0;
        dolperf_add_region(dolperf_report(), &record);
    }
    snprintf(dolperf_report()->region_mode, sizeof(dolperf_report()->region_mode),
             "%s", dolregion_mode_name(mode));

    {
        char report[1100];
        if (snprintf(report, sizeof(report), "%s_smc.txt", stem) >= (int)sizeof(report) ||
            !write_smc_report(&smc, report))
            goto done;
        if (smc.possible)
            printf("warning: executable memory writes detected; report: %s\n", report);
    }

    emit_dispatch_helpers(header, &funcs, entry_point);
    emit_footer(header);
    fprintf(manifest, "\n// %u native objects\n", file_count);
    printf("done!\n  header: %s\n  objects: %s (%u files)\n", header_path,
           chunks_dir, file_count);
    status = 1;

done:
    free(cached_before_run);
    free(ranges);
    free(jobs);
    free(runs);
    dolregion_plan_free(&plan);
    dolcfg_free(&cfg);
    function_list_free(&funcs);
    smc_analysis_free(&smc);
    if (decoded) {
        for (u32 s = 0; s < section_count; s++)
            free(decoded[s]);
        free(decoded);
    }
    return status;
}

static int emit_code_sections_llvm(const LoadedCodeSection* sections,
                                   u32 section_count,
                                   const char* output_path,
                                   DolRecompCPU cpu, u32 entry_point,
                                   u32 requested_jobs, int local_chunks_dir,
                                   const DolRecompSymbolMap* symbols) {
    char stem[1024];
    char header_path[1100];
    char symbol_header_path[1100];
    char fallback_path[1100];
    char chunks_dir[1100];
    char include_name[512];
    if (!make_output_stem(output_path, stem, sizeof(stem)) ||
        !split_include_name(stem, include_name, sizeof(include_name)))
        return 0;
    if (snprintf(header_path, sizeof(header_path), "%s.h", stem) >= (int)sizeof(header_path) ||
        snprintf(symbol_header_path, sizeof(symbol_header_path), "%s_symbols.h", stem) >=
            (int)sizeof(symbol_header_path) ||
        snprintf(fallback_path, sizeof(fallback_path), "%s_fallbacks.csv", stem) >=
            (int)sizeof(fallback_path)) {
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
        if (manifest) fclose(manifest);
        if (header) fclose(header);
        if (fallback_report) fclose(fallback_report);
        return 0;
    }
    fprintf(fallback_report, "address,raw,opcode,reason,detail\n");
    fprintf(manifest, "#include \"%s\"\n", include_name);
    emit_header_for_cpu(header, cpu);
    fprintf(header, "#define DOLRECOMP_BACKEND_LLVM 1\n\n");
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
            !emit_symbol_definitions(symbol_header, symbols, sections, section_count)) {
            fprintf(stderr, "error: failed to emit symbol map\n");
            if (symbol_header) fclose(symbol_header);
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

    if (pipeline_region_options()->enabled) {
        int ok = emit_llvm_regions(sections, section_count, cpu, entry_point,
                                   requested_jobs, chunks_dir, stem, header_path,
                                   header, manifest, fallback_report);
        fclose(header);
        fclose(manifest);
        fclose(fallback_report);
        return ok;
    }

    FunctionList funcs = {0};
    SMCAnalysis smc = {0};
    u32 file_count = 0;
    u32 range_count = 0;
    const u32 chunk_instructions = llvm_chunk_instructions();
    for (u32 s = 0; s < section_count; s++)
        range_count +=
            ((sections[s].size / 4u) + chunk_instructions - 1u) /
            chunk_instructions;
    DolLLVMFunctionRange* ranges =
        (DolLLVMFunctionRange*)calloc(range_count, sizeof(*ranges));
    if (!ranges)
        goto fail;
    char cache_dir[1100] = "";
    if (!llvm_cache_dir(cache_dir, sizeof(cache_dir)))
        cache_dir[0] = '\0';
    u32 range_index = 0;
    for (u32 s = 0; s < section_count; s++) {
        u32 instructions = sections[s].size / 4u;
        for (u32 start = 0; start < instructions; start += chunk_instructions) {
            u32 count = instructions - start;
            if (count > chunk_instructions)
                count = chunk_instructions;
            ranges[range_index].start = sections[s].address + start * 4u;
            ranges[range_index].end = ranges[range_index].start + count * 4u;
            range_index++;
        }
    }
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
        printf("decoding %s[%u]: %u instructions at 0x%08X\n",
               section->label, section->index, num_insts, section->address);
        printf("  %u known, %u embedded data, %u unknown\n",
               num_insts - embedded - unknown, embedded, unknown);
        if (section->embedded_data_mode == EMBEDDED_DATA_DOL) {
            analyze_smc_section(sections, section_count, insts, num_insts, &smc);
            if (smc.allocation_failed) {
                free(insts);
                goto fail;
            }
        }

        u32 chunk_total =
            (num_insts + chunk_instructions - 1u) / chunk_instructions;
        LLVMChunkJob* chunk_jobs =
            (LLVMChunkJob*)calloc(chunk_total, sizeof(*chunk_jobs));
        if (!chunk_jobs) {
            free(insts);
            goto fail;
        }
        for (u32 start = 0; start < num_insts; start += chunk_instructions) {
            u32 chunk_count = num_insts - start;
            if (chunk_count > chunk_instructions)
                chunk_count = chunk_instructions;
            u32 function_address = section->address + start * 4u;
            u32 job_index = start / chunk_instructions;
            LLVMChunkJob* job = &chunk_jobs[job_index];
            if (snprintf(job->name, sizeof(job->name), "chunk_%04u_%s%u_%08X.o",
                         file_count, section->label, section->index,
                         function_address) >= (int)sizeof(job->name) ||
                !join_path(job->path, sizeof(job->path), chunks_dir, job->name)) {
                free(chunk_jobs);
                free(insts);
                goto fail;
            }
            job->insts = insts + start;
            job->count = chunk_count;
            job->function_address = function_address;
            job->index = file_count + 1u;
            job->total = range_count;
            job->ranges = ranges;
            job->range_count = range_count;
            job->hash = llvm_job_hash(job);
            if (cache_dir[0]) {
                char cache_name[64];
                snprintf(cache_name, sizeof(cache_name), "%016llx.o",
                         (unsigned long long)job->hash);
                if (!join_path(job->cache_path, sizeof(job->cache_path),
                               cache_dir, cache_name))
                    job->cache_path[0] = '\0';
            }
            DolIRModule audit;
            dolir_module_init(&audit);
            if (!dolir_build_chunk(&audit, job->insts, job->count,
                                   job->function_address)) {
                dolir_module_free(&audit);
                free(chunk_jobs);
                free(insts);
                goto fail;
            }
            const DolIRFunction* audit_function = &audit.functions[0];
            for (u32 i = 0; i < audit_function->block_count; i++) {
                if (audit_function->blocks[i].terminator.kind != DOLIR_TERM_FALLBACK)
                    continue;
                const PPCInst* fallback = &job->insts[i];
                const char* reason = fallback->embedded_data ? "embedded-data" :
                    fallback->op == PPC_OP_UNKNOWN ? "unknown" :
                    (fallback->op == PPC_OP_SC || fallback->op == PPC_OP_RFI) ?
                        "exception-boundary" : "unsupported";
                char detail[32] = "";
                switch (fallback->op) {
                case PPC_OP_MFSPR:
                case PPC_OP_MTSPR:
                case PPC_OP_MFTB:
                    snprintf(detail, sizeof(detail), "spr=%u", fallback->spr);
                    break;
                case PPC_OP_MFSR:
                case PPC_OP_MTSR:
                    snprintf(detail, sizeof(detail), "sr=%u", fallback->sr);
                    break;
                case PPC_OP_TW:
                case PPC_OP_TWI:
                    snprintf(detail, sizeof(detail), "to=%u", fallback->to);
                    break;
                default:
                    break;
                }
                fprintf(fallback_report, "%08X,%08X,%s,%s,%s\n",
                        fallback->address, fallback->raw,
                        ppc_op_name(fallback->op), reason, detail);
            }
            dolir_module_free(&audit);
            emit_chunk_prototype(header, function_address);
            if (!function_list_add(&funcs, function_address,
                                   function_address + chunk_count * 4u)) {
                free(chunk_jobs);
                free(insts);
                goto fail;
            }
            fprintf(manifest, "// object: chunks/%s\n", job->name);
            file_count++;
        }
        u32 active_jobs = effective_chunk_jobs(chunk_total, requested_jobs);
        printf("  writing %u LLVM objects with %u job%s\n",
               chunk_total, active_jobs, active_jobs == 1 ? "" : "s");

        /* Cache state has to be sampled before the run: afterwards every object
           is present and a hit is indistinguishable from a fresh emit. */
        unsigned char* cached_before_run =
            (unsigned char*)calloc(chunk_total ? chunk_total : 1u, 1u);
        if (cached_before_run) {
            for (u32 i = 0; i < chunk_total; i++)
                cached_before_run[i] = reuse_llvm_object(&chunk_jobs[i]) ? 1u : 0u;
        }

        if (!run_llvm_chunk_jobs(chunk_jobs, chunk_total, requested_jobs)) {
            free(cached_before_run);
            free(chunk_jobs);
            free(insts);
            goto fail;
        }

        /* Recorded in the parent, after the workers are done.
         *
         * Per-region optimize/codegen timings deliberately stay zero here: the
         * POSIX path forks a worker per batch, so a counter raised inside
         * emit_llvm_chunk_job() dies with the child. Reporting a partial number
         * that is whole on Windows and empty on Linux would be worse than
         * reporting none. The Phase 1 region backend emits in-process and fills
         * these in for real. */
        for (u32 i = 0; i < chunk_total; i++) {
            const LLVMChunkJob* job = &chunk_jobs[i];
            DolPerfRegion region;
            memset(&region, 0, sizeof(region));
            region.region_id = i;
            region.guest_start = job->function_address;
            region.guest_end = job->function_address + job->count * 4u;
            region.guest_instructions = job->count;
            region.blocks = 1;
            region.code_bytes = perf_file_size(job->path);
            region.cache_hit = cached_before_run ? cached_before_run[i] : 0;
            dolperf_add_region(dolperf_report(), &region);
        }
        free(cached_before_run);
        free(chunk_jobs);
        free(insts);
    }

    {
        char report[1100];
        if (snprintf(report, sizeof(report), "%s_smc.txt", stem) >= (int)sizeof(report) ||
            !write_smc_report(&smc, report))
            goto fail;
        if (smc.possible)
        printf("warning: executable memory writes detected; report: %s\n", report);
    }
    emit_dispatch_helpers(header, &funcs, entry_point);
    emit_footer(header);
    fprintf(manifest, "\n// %u native objects\n", file_count);
    fclose(header);
    fclose(manifest);
    fclose(fallback_report);
    smc_analysis_free(&smc);
    function_list_free(&funcs);
    free(ranges);
    printf("done!\n  header: %s\n  objects: %s (%u files)\n",
           header_path, chunks_dir, file_count);
    return 1;

fail:
    smc_analysis_free(&smc);
    function_list_free(&funcs);
    free(ranges);
    fclose(header);
    fclose(manifest);
    fclose(fallback_report);
    return 0;
}
#endif

int emit_code_sections_split(const LoadedCodeSection* sections,
                                    u32 section_count,
                                    const char* output_path,
                                    DolRecompCPU cpu, u32 entry_point, u32 jobs,
                                    int local_chunks_dir,
                                    const DolRecompSymbolMap* symbols,
                                    DolRecompBackend backend) {
#ifdef DOLRECOMP_ENABLE_LLVM
    if (backend == DOLRECOMP_BACKEND_LLVM ||
        backend == DOLRECOMP_BACKEND_LLVM_AOT)
        return emit_code_sections_llvm(sections, section_count, output_path, cpu,
                                       entry_point, jobs, local_chunks_dir, symbols);
#else
    if (backend == DOLRECOMP_BACKEND_LLVM ||
        backend == DOLRECOMP_BACKEND_LLVM_AOT) {
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
    const u32 chunk_instructions = c_chunk_instructions();
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
            (num_insts + chunk_instructions - 1u) / chunk_instructions;
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

        for (u32 start = 0; start < num_insts; start += chunk_instructions) {
            u32 chunk_count = num_insts - start;
            u32 func_addr = base_addr + start * 4u;
            char chunk_name[128];
            u32 job_index = start / chunk_instructions;

            if (chunk_count > chunk_instructions)
                chunk_count = chunk_instructions;

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

        // Direct cross-chunk calls bypass the chassis dispatcher. That is unsafe
        // for runtimes which validate mutable guest code at dispatch boundaries,
        // so only emit them after an explicit opt-in for controlled benchmarks.
        // funcs accumulates across sections; targets in a later section still
        // fall back to the dispatcher because their symbols are not known yet.
        const char* direct = getenv("DOLRECOMP_UNSAFE_DIRECT_CALLS");
        u32* chunk_starts = NULL;
        if (direct && strcmp(direct, "1") == 0) {
            printf("  unsafe cross-chunk direct calls enabled\n");
            chunk_starts = (u32*)malloc((size_t)funcs.count * sizeof(u32));
            if (chunk_starts) {
                for (u32 i = 0; i < funcs.count; ++i)
                    chunk_starts[i] = funcs.ranges[i].start;
                emit_set_chunk_table(chunk_starts, funcs.count);
            }
        }

        u32 active_jobs = effective_chunk_jobs(section_job_count, jobs);
        printf("  writing %u chunks with %u job%s\n",
               section_job_count, active_jobs, active_jobs == 1 ? "" : "s");
        if (!run_chunk_jobs(chunk_jobs, section_job_count, jobs)) {
            emit_set_chunk_table(NULL, 0);
            free(chunk_starts);
            smc_analysis_free(&smc);
            function_list_free(&funcs);
            free(chunk_jobs);
            free(insts);
            fclose(header);
            fclose(manifest);
            return 0;
        }
        emit_set_chunk_table(NULL, 0);
        free(chunk_starts);

        /* Fixed-chunk mode records one region per chunk, so `fixed` and the
           Phase 1 planner modes report through the same structure and stay
           directly comparable. */
        for (u32 i = 0; i < section_job_count; i++) {
            const ChunkJob* job = &chunk_jobs[i];
            DolPerfRegion region;
            memset(&region, 0, sizeof(region));
            region.region_id = i;
            region.guest_start = job->func_addr;
            region.guest_end = job->func_addr + job->count * 4u;
            region.guest_instructions = job->count;
            region.blocks = 1;
            region.code_bytes = perf_file_size(job->path);
            dolperf_add_region(dolperf_report(), &region);
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
                          const DolRecompSymbolMap* symbols,
                          DolRecompBackend backend) {
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
                                    local_chunks_dir, symbols, backend);
}

int emit_rpx_split(const RPXFile* rpx, const char* output_path,
                          DolRecompCPU cpu, u32 jobs, int local_chunks_dir,
                          DolRecompBackend backend) {
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
                                    local_chunks_dir, NULL, backend);
}

int emit_rel_split(const RELFile* rel, const char* output_path,
                          DolRecompCPU cpu, u32 jobs, int local_chunks_dir,
                          DolRecompBackend backend) {
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
                                      rel->entry_point, jobs, local_chunks_dir,
                                      NULL, backend);
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
                              DolRecompBackend backend) {
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
        if (!emit_rel_split(&items[i].rel, rel_output_path, cpu, jobs, 1, backend))
            goto done;
    }

    ok = 1;

done:
    free(map_entries);
    rel_batch_free(items, paths.count);
    path_list_free(&paths);
    return ok;
}
