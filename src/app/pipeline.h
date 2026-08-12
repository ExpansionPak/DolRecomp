#ifndef DOLRECOMP_APP_PIPELINE_H
#define DOLRECOMP_APP_PIPELINE_H

#include "common/types.h"
#include "backend/emitter.h"
#include "frontend/container/dol.h"
#include "frontend/container/rel.h"
#include "frontend/container/rpx.h"
#include "analysis/symbol_map.h"
#include "app/cli.h"

#define REL_AUTO_BASE 0x80500000u
#define REL_AUTO_ALIGN 0x10000u

/* Region settings reach the emitters through a setter rather than five more
   parameters on four already-long signatures. Set once from main() before any
   emit_* call; zeroed values mean "planner default". */
typedef struct {
    int enabled;              /* llvm-aot selected */
    const char* mode_name;    /* NULL -> cfg */
    u32 max_instructions;     /* 0 -> planner default */
    u32 max_ir_instructions;  /* 0 -> planner default */
    const char* report_path;  /* NULL -> no report */
    const char* profile_path; /* NULL -> no weights; pgo mode degrades */
} DolRecompRegionOptions;

void pipeline_set_region_options(const DolRecompRegionOptions* options);
const DolRecompRegionOptions* pipeline_region_options(void);

int emit_dol_split(const DOLFile* dol, const char* output_path,
                   DolRecompCPU cpu, u32 jobs, int local_chunks_dir,
                   const DolRecompSymbolMap* symbols, DolRecompBackend backend);
int emit_rpx_split(const RPXFile* rpx, const char* output_path,
                   DolRecompCPU cpu, u32 jobs, int local_chunks_dir,
                   DolRecompBackend backend);
int emit_rel_split(const RELFile* rel, const char* output_path,
                   DolRecompCPU cpu, u32 jobs, int local_chunks_dir,
                   DolRecompBackend backend);
int emit_rel_directory(const char* input_dir, const char* output_root,
                       const char* title_id, int titleless_mode,
                       DolRecompCPU cpu, u32 jobs, u32 start_base,
                       DolRecompBackend backend);

#endif
