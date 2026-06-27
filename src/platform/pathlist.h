#ifndef DOLRECOMP_PLATFORM_PATHLIST_H
#define DOLRECOMP_PLATFORM_PATHLIST_H

#include "common/types.h"

typedef struct {
    char** paths;
    u32 count;
    u32 capacity;
} PathList;

void path_list_free(PathList* list);
int path_list_add(PathList* list, const char* path);
int collect_rel_paths(const char* root, PathList* list);
void path_list_sort(PathList* list);

#endif
