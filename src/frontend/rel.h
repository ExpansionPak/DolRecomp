#ifndef DOLRECOMP_REL_H
#define DOLRECOMP_REL_H

#include "../core/types.h"

typedef struct {
    u32 index;
    u32 offset;
    u32 address;
    u32 size;
    bool executable;
    bool bss;
    const u8* data;
    u8* owned_data;
} RELSection;

typedef struct {
    u8* file_data;
    u32 file_size;
    u32 module_id;
    u32 version;
    u32 bss_size;
    u32 base_address;
    u32 entry_point;
    u32 prolog_section;
    u32 epilog_section;
    u32 unresolved_section;
    u32 prolog_offset;
    u32 epilog_offset;
    u32 unresolved_offset;
    u32 import_count;
    u32 relocation_count;
    u32 relocation_offset;
    u32 import_offset;
    u32 import_size;
    u32 section_count;
    RELSection* sections;
} RELFile;

typedef struct {
    u32 module_id;
    const RELFile* rel;
} RELModuleMapEntry;

typedef struct {
    const RELModuleMapEntry* entries;
    u32 count;
} RELModuleMap;

bool rel_load(RELFile* rel, const char* path, u32 base_address);
bool rel_load_image(RELFile* rel, const char* path, u32 base_address);
bool rel_apply_relocations(RELFile* rel, const RELModuleMap* modules);
void rel_free(RELFile* rel);
void rel_print_info(const RELFile* rel, const char* game_name);

#endif /* DOLRECOMP_REL_H */
