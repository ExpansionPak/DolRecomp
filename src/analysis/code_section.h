#ifndef DOLRECOMP_ANALYSIS_CODE_SECTION_H
#define DOLRECOMP_ANALYSIS_CODE_SECTION_H

#include "common/types.h"
#include "analysis/embedded_data.h"

typedef struct {
    const char* label;
    const char* name;
    const u8* data;
    u32 index;
    u32 file_offset;
    u32 address;
    u32 size;
    EmbeddedDataMode embedded_data_mode;
} LoadedCodeSection;

#endif
