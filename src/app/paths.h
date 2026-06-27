#ifndef DOLRECOMP_APP_PATHS_H
#define DOLRECOMP_APP_PATHS_H

#include <stddef.h>
#include "common/types.h"

int build_named_output_path(const char* output_root, const char* title_id,
                            char* output_path, size_t output_path_size);
int build_gamecube_output_path(const char* output_root, char* output_path,
                               size_t output_path_size);
int build_generated_folder_path(const char* output_root, const char* title_id,
                                int titleless_mode, char* folder_path,
                                size_t folder_path_size);
void safe_module_name(const char* path, char* out, size_t out_size);
int build_rel_output_path(const char* generated_root, const char* rel_path,
                          u32 module_id, char* output_path,
                          size_t output_path_size);
int make_output_stem(const char* output_path, char* stem, size_t stem_size);
int split_include_name(const char* stem, char* include_name, size_t include_size);

#endif
