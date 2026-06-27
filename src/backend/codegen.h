#ifndef DOLRECOMP_BACKEND_CODEGEN_H
#define DOLRECOMP_BACKEND_CODEGEN_H

#include "common/types.h"
#include "frontend/decoder.h"
#include "backend/emitter.h"
#include "analysis/code_section.h"

#define EMIT_CHUNK_INSTRUCTIONS 4096u

typedef struct {
    const PPCInst* insts;
    u32 count;
    u32 func_addr;
    char path[1200];
    char include_name[512];
} ChunkJob;

int run_chunk_jobs(const ChunkJob* jobs, u32 job_count, u32 requested_jobs);
u32 effective_chunk_jobs(u32 job_count, u32 requested_jobs);

#endif
