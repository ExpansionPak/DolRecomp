#ifndef DOLRECOMP_ANALYSIS_SMC_H
#define DOLRECOMP_ANALYSIS_SMC_H

#include "common/types.h"
#include "frontend/decoder.h"
#include "analysis/code_section.h"

#define SMC_DISPLAY_RANGE_LIMIT 8

typedef struct {
    int known;
    u32 value;
} KnownReg;

typedef struct {
    u32 start;
    u32 end;
} SMCRange;

typedef struct {
    int possible;
    int allocation_failed;
    u32 range_count;
    u32 range_capacity;
    SMCRange* ranges;
} SMCAnalysis;

void smc_analysis_free(SMCAnalysis* smc);
void smc_note(SMCAnalysis* smc, u32 addr);
int write_smc_report(const SMCAnalysis* smc, const char* path);
void analyze_smc_section(const LoadedCodeSection* sections, u32 section_count,
                         const PPCInst* insts, u32 count, SMCAnalysis* smc);

#endif
