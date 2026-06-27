#include "platform/pathlist.h"
#include "platform/fs.h"
#include "platform/strutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#endif

void path_list_free(PathList* list) {
    for (u32 i = 0; i < list->count; i++)
        free(list->paths[i]);
    free(list->paths);
    list->paths = NULL;
    list->count = 0;
    list->capacity = 0;
}

int path_list_add(PathList* list, const char* path) {
    if (list->count == list->capacity) {
        u32 new_capacity = list->capacity ? list->capacity * 2u : 32u;
        char** new_paths =
            (char**)realloc(list->paths, new_capacity * sizeof(*new_paths));
        if (!new_paths) {
            fprintf(stderr, "error: out of memory\n");
            return 0;
        }
        list->paths = new_paths;
        list->capacity = new_capacity;
    }

    list->paths[list->count] = copy_string_alloc(path);
    if (!list->paths[list->count]) {
        fprintf(stderr, "error: out of memory\n");
        return 0;
    }
    list->count++;
    return 1;
}

int collect_rel_paths(const char* root, PathList* list) {
#ifdef _WIN32
    char pattern[1200];
    if (!join_path(pattern, sizeof(pattern), root, "*")) {
        fprintf(stderr, "error: path is too long\n");
        return 0;
    }

    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "error: can't read directory '%s'\n", root);
        return 0;
    }

    do {
        if (strcmp(data.cFileName, ".") == 0 ||
            strcmp(data.cFileName, "..") == 0) {
            continue;
        }

        char child[1200];
        if (!join_path(child, sizeof(child), root, data.cFileName)) {
            FindClose(find);
            fprintf(stderr, "error: path is too long\n");
            return 0;
        }

        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (!collect_rel_paths(child, list)) {
                FindClose(find);
                return 0;
            }
        } else if (has_rel_extension(child)) {
            if (!path_list_add(list, child)) {
                FindClose(find);
                return 0;
            }
        }
    } while (FindNextFileA(find, &data));

    FindClose(find);
    return 1;
#else
    DIR* dir = opendir(root);
    if (!dir) {
        fprintf(stderr, "error: can't read directory '%s'\n", root);
        return 0;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char child[1200];
        if (!join_path(child, sizeof(child), root, entry->d_name)) {
            closedir(dir);
            fprintf(stderr, "error: path is too long\n");
            return 0;
        }

        if (path_is_directory(child)) {
            if (!collect_rel_paths(child, list)) {
                closedir(dir);
                return 0;
            }
        } else if (has_rel_extension(child)) {
            if (!path_list_add(list, child)) {
                closedir(dir);
                return 0;
            }
        }
    }

    closedir(dir);
    return 1;
#endif
}

int compare_paths_for_sort(const void* a, const void* b) {
    const char* const* pa = (const char* const*)a;
    const char* const* pb = (const char* const*)b;
    return strcmp(*pa, *pb);
}

void path_list_sort(PathList* list) {
    if (list->count > 1)
        qsort(list->paths, list->count, sizeof(list->paths[0]),
              compare_paths_for_sort);
}

