#include "platform/fs.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

int make_dir(const char* path) {
#ifdef _WIN32
    int rc = _mkdir(path);
#else
    int rc = mkdir(path, 0777);
#endif
    if (rc == 0 || errno == EEXIST)
        return 1;

    fprintf(stderr, "error: can't create directory '%s'\n", path);
    return 0;
}

const char* path_basename(const char* path) {
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    const char* base = NULL;
    if (!slash) {
        base = backslash;
    } else if (!backslash) {
        base = slash;
    } else {
        base = slash > backslash ? slash : backslash;
    }
    return base ? base + 1 : path;
}

int path_dirname(const char* path, char* dir, size_t dir_size) {
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    const char* last = NULL;

    if (!slash) {
        last = backslash;
    } else if (!backslash) {
        last = slash;
    } else {
        last = slash > backslash ? slash : backslash;
    }

    if (!last) {
        if (dir_size < 2)
            return 0;
        dir[0] = '.';
        dir[1] = '\0';
        return 1;
    }

    size_t len = (size_t)(last - path);
    if (len == 0)
        len = 1;
    if (len + 1 > dir_size)
        return 0;

    memcpy(dir, path, len);
    dir[len] = '\0';
    return 1;
}

int is_path_sep(char ch) {
    return ch == '/' || ch == '\\';
}

int make_dir_tree(const char* path) {
    char temp[1024];
    size_t len = strlen(path);

    if (len == 0)
        return 1;
    if (len + 1 > sizeof(temp)) {
        fprintf(stderr, "error: path is too long: %s\n", path);
        return 0;
    }

    memcpy(temp, path, len + 1);
    for (size_t i = 0; i < len; i++) {
        if (is_path_sep(temp[i])) {
            char saved = temp[i];
            temp[i] = '\0';
            if (temp[0] != '\0' && !(strlen(temp) == 2 && temp[1] == ':')) {
                if (!make_dir(temp))
                    return 0;
            }
            temp[i] = saved;
        }
    }

    if (temp[0] != '\0' && !(strlen(temp) == 2 && temp[1] == ':'))
        return make_dir(temp);
    return 1;
}

int join_path(char* out, size_t out_size, const char* dir, const char* name) {
    size_t len = strlen(dir);
    const char* sep = (len > 0 && is_path_sep(dir[len - 1])) ? "" :
#ifdef _WIN32
        "\\";
#else
        "/";
#endif
    int written = snprintf(out, out_size, "%s%s%s", dir, sep, name);
    return written > 0 && (size_t)written < out_size;
}

int file_exists(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file)
        return 0;
    fclose(file);
    return 1;
}

int path_is_directory(const char* path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

