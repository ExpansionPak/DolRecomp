#ifndef DOLRECOMP_APP_PIPELINE_H
#define DOLRECOMP_APP_PIPELINE_H

#include "common/types.h"
#include "backend/emitter.h"
#include "frontend/container/dol.h"
#include "frontend/container/rel.h"
#include "frontend/container/rpx.h"

#define REL_AUTO_BASE 0x80500000u
#define REL_AUTO_ALIGN 0x10000u

int emit_dol_split(const DOLFile* dol, const char* output_path,
                   DolRecompCPU cpu, u32 jobs, int local_chunks_dir);
int emit_rpx_split(const RPXFile* rpx, const char* output_path,
                   DolRecompCPU cpu, u32 jobs, int local_chunks_dir);
int emit_rel_split(const RELFile* rel, const char* output_path,
                   DolRecompCPU cpu, u32 jobs, int local_chunks_dir);
int emit_rel_directory(const char* input_dir, const char* output_root,
                       const char* title_id, int titleless_mode,
                       DolRecompCPU cpu, u32 jobs, u32 start_base);

#endif
