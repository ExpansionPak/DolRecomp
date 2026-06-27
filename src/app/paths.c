#include "app/paths.h"
#include "platform/fs.h"
#include "platform/strutil.h"
#include <stdio.h>
#include <string.h>

int build_named_output_path(const char* output_root, const char* title_id,
                                   char* output_path, size_t output_path_size) {
    char folder_path[1100];

    if (!make_dir_tree(output_root))
        return 0;

    char folder_name[128];
    if (snprintf(folder_name, sizeof(folder_name), "%s_generated", title_id) >=
        (int)sizeof(folder_name)) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }

    if (!join_path(folder_path, sizeof(folder_path), output_root, folder_name)) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }

    if (!make_dir_tree(folder_path))
        return 0;

    char file_name[128];
    if (snprintf(file_name, sizeof(file_name), "%s.c", title_id) >=
        (int)sizeof(file_name)) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }

    if (!join_path(output_path, output_path_size, folder_path, file_name)) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }

    return 1;
}

int build_gamecube_output_path(const char* output_root, char* output_path,
                                      size_t output_path_size) {
    char folder_path[1100];

    if (!make_dir_tree(output_root))
        return 0;

    if (!join_path(folder_path, sizeof(folder_path), output_root, "generated")) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }

    if (!make_dir_tree(folder_path))
        return 0;

    if (!join_path(output_path, output_path_size, folder_path, "generated.c")) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }

    return 1;
}

int build_generated_folder_path(const char* output_root,
                                       const char* title_id,
                                       int titleless_mode,
                                       char* folder_path,
                                       size_t folder_path_size) {
    if (!make_dir_tree(output_root))
        return 0;

    if (titleless_mode) {
        if (!join_path(folder_path, folder_path_size, output_root, "generated")) {
            fprintf(stderr, "error: output path is too long\n");
            return 0;
        }
    } else {
        char folder_name[128];
        if (snprintf(folder_name, sizeof(folder_name), "%s_generated", title_id) >=
            (int)sizeof(folder_name)) {
            fprintf(stderr, "error: output path is too long\n");
            return 0;
        }
        if (!join_path(folder_path, folder_path_size, output_root, folder_name)) {
            fprintf(stderr, "error: output path is too long\n");
            return 0;
        }
    }

    return make_dir_tree(folder_path);
}

void safe_module_name(const char* path, char* out, size_t out_size) {
    const char* base = path_basename(path);
    size_t len = strlen(base);
    if (len >= 4 && ascii_case_equal(base + len - 4, ".rel"))
        len -= 4;

    size_t w = 0;
    for (size_t i = 0; i < len && w + 1 < out_size; i++) {
        char ch = base[i];
        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-') {
            out[w++] = ch;
        } else {
            out[w++] = '_';
        }
    }

    if (w == 0 && out_size > 1)
        out[w++] = 'm';
    out[w] = '\0';
}

int build_rel_output_path(const char* generated_root,
                                 const char* rel_path,
                                 u32 module_id,
                                 char* output_path,
                                 size_t output_path_size) {
    char rels_dir[1200];
    char module_name[320];
    char module_dir_name[400];
    char module_dir[1200];

    if (!join_path(rels_dir, sizeof(rels_dir), generated_root, "rels")) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }
    if (!make_dir_tree(rels_dir))
        return 0;

    safe_module_name(rel_path, module_name, sizeof(module_name));
    if (snprintf(module_dir_name, sizeof(module_dir_name), "%s_%u",
                 module_name, module_id) >= (int)sizeof(module_dir_name)) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }

    if (!join_path(module_dir, sizeof(module_dir), rels_dir, module_dir_name)) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }
    if (!make_dir_tree(module_dir))
        return 0;

    if (!join_path(output_path, output_path_size, module_dir, "generated.c")) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }

    return 1;
}

int make_output_stem(const char* output_path, char* stem, size_t stem_size) {
    size_t len = strlen(output_path);
    if (len + 1 > stem_size) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }

    memcpy(stem, output_path, len + 1);
    if (len >= 2 && stem[len - 2] == '.' &&
        (stem[len - 1] == 'c' || stem[len - 1] == 'C')) {
        stem[len - 2] = '\0';
    }

    return 1;
}

int split_include_name(const char* stem, char* include_name, size_t include_size) {
    const char* base = path_basename(stem);
    int written = snprintf(include_name, include_size, "%s.h", base);
    return written > 0 && (size_t)written < include_size;
}

