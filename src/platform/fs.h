#ifndef DOLRECOMP_PLATFORM_FS_H
#define DOLRECOMP_PLATFORM_FS_H

#include <stddef.h>

int make_dir(const char* path);
int make_dir_tree(const char* path);
const char* path_basename(const char* path);
int path_dirname(const char* path, char* dir, size_t dir_size);
int is_path_sep(char ch);
int join_path(char* out, size_t out_size, const char* dir, const char* name);
int file_exists(const char* path);
int path_is_directory(const char* path);

#endif
