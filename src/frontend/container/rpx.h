#ifndef DOLRECOMP_RPX_H
#define DOLRECOMP_RPX_H

#include "../../common/types.h"

#define RPX_MAX_CODE_SECTIONS 64

typedef struct {
    char name[64];
    u32 offset;
    u32 address;
    u32 size;
    u32 flags;
    bool compressed;
    const u8* data;
    u8* owned_data;
} RPXCodeSection;

typedef struct {
    u8* file_data;
    u32 file_size;
    u32 entry_point;
    u16 section_count;
    u32 code_section_count;
    RPXCodeSection code_sections[RPX_MAX_CODE_SECTIONS];
} RPXFile;

bool rpx_load(RPXFile* rpx, const char* path);
void rpx_free(RPXFile* rpx);
void rpx_print_info(const RPXFile* rpx, const char* game_name);

#endif /* DOLRECOMP_RPX_H */
