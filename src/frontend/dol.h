#ifndef DOLRECOMP_DOL_H
#define DOLRECOMP_DOL_H

#include "../core/types.h"

// gamecube DOL executable format
// 7 text sections, 11 data sections, all BE u32

#define DOL_HEADER_SIZE     0x100
#define DOL_NUM_TEXT        7
#define DOL_NUM_DATA        11

typedef struct {
    u32 text_offsets[DOL_NUM_TEXT];
    u32 data_offsets[DOL_NUM_DATA];
    u32 text_addresses[DOL_NUM_TEXT];
    u32 data_addresses[DOL_NUM_DATA];
    u32 text_sizes[DOL_NUM_TEXT];
    u32 data_sizes[DOL_NUM_DATA];
    u32 bss_address;
    u32 bss_size;
    u32 entry_point;
} DOLHeader;

typedef struct {
    DOLHeader header;
    u8*  file_data;
    u32  file_size;
} DOLFile;

// load/free
bool dol_load(DOLFile* dol, const char* path);
void dol_free(DOLFile* dol);

// dump header info to stdout
void dol_print_info(const DOLFile* dol, const char* game_name);

// get raw section data (NULL if unused)
const u8* dol_get_text_section(const DOLFile* dol, int index);
const u8* dol_get_data_section(const DOLFile* dol, int index);

#endif /* DOLRECOMP_DOL_H */
