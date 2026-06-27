#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define PATH_SEP '\\'
#else
#include <unistd.h>
#define PATH_SEP '/'
#endif

#include "common/types.h"

static void put_be32(u8* p, u32 v) {
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static int make_dir_one(const char* path) {
#ifdef _WIN32
    return _mkdir(path) == 0 || errno == EEXIST;
#else
    return mkdir(path, 0777) == 0 || errno == EEXIST;
#endif
}

static int join_path(char* out, size_t out_size, const char* a, const char* b) {
    size_t alen = strlen(a);
    const char* sep = "";
    if (alen > 0 && a[alen - 1] != '/' && a[alen - 1] != '\\')
        sep = "/";
    return snprintf(out, out_size, "%s%s%s", a, sep, b) > 0 &&
           strlen(out) < out_size;
}

static char* copy_string(const char* text) {
    size_t len = strlen(text);
    char* out = (char*)malloc(len + 1);
    if (out)
        memcpy(out, text, len + 1);
    return out;
}

static char* normalize_path_arg(const char* path) {
#ifdef _WIN32
    if (path[0] == '/' && path[1] != '\0') {
        if (((path[1] >= 'a' && path[1] <= 'z') ||
             (path[1] >= 'A' && path[1] <= 'Z')) &&
            path[2] == '/') {
            size_t len = strlen(path);
            char* out = (char*)malloc(len + 2);
            if (!out)
                return NULL;
            out[0] = path[1];
            out[1] = ':';
            for (size_t i = 2; i < len; i++)
                out[i] = path[i] == '/' ? '\\' : path[i];
            out[len] = '\0';
            return out;
        }

        if (strncmp(path, "/mnt/", 5) == 0 &&
            ((path[5] >= 'a' && path[5] <= 'z') ||
             (path[5] >= 'A' && path[5] <= 'Z')) &&
            path[6] == '/') {
            size_t len = strlen(path);
            char* out = (char*)malloc(len);
            if (!out)
                return NULL;
            out[0] = path[5];
            out[1] = ':';
            for (size_t i = 6; i < len; i++)
                out[i - 4] = path[i] == '/' ? '\\' : path[i];
            out[len - 4] = '\0';
            return out;
        }

        if (strncmp(path, "/home/", 6) == 0) {
            const char* userprofile = getenv("USERPROFILE");
            const char* rest = strchr(path + 6, '/');
            if (userprofile && rest) {
                size_t root_len = strlen(userprofile);
                size_t rest_len = strlen(rest);
                char* out = (char*)malloc(root_len + rest_len + 1);
                if (!out)
                    return NULL;
                memcpy(out, userprofile, root_len);
                for (size_t i = 0; i < rest_len; i++)
                    out[root_len + i] = rest[i] == '/' ? '\\' : rest[i];
                out[root_len + rest_len] = '\0';
                return out;
            }
        }
    }
#endif
    return copy_string(path);
}

#ifndef _WIN32
static char* quote_arg(const char* arg) {
    size_t len = strlen(arg);
    char* out = (char*)malloc(len * 4 + 3);
    if (!out)
        return NULL;
    size_t w = 0;
    out[w++] = '\'';
    for (size_t i = 0; i < len; i++) {
        if (arg[i] == '\'') {
            memcpy(out + w, "'\\''", 4);
            w += 4;
        } else {
            out[w++] = arg[i];
        }
    }
    out[w++] = '\'';
    out[w] = '\0';
    return out;
}
#endif

static int write_sample_gcm(const char* path) {
    u8 image[0x6000];
    memset(image, 0, sizeof(image));

    memcpy(image, "GAFE01", 6);
    put_be32(image + 0x1c, 0xC2339F3Du);
    memcpy(image + 0x20, "DolRecomp Test Disc", 19);

    put_be32(image + 0x420, 0x3000);
    put_be32(image + 0x424, 0x4000);
    put_be32(image + 0x428, 0x40);
    put_be32(image + 0x42c, 0x40);

    u8* fst = image + 0x4000;
    put_be32(fst + 0x00, 0x01000000);
    put_be32(fst + 0x04, 0);
    put_be32(fst + 0x08, 3);
    put_be32(fst + 0x0c, 0x01000001);
    put_be32(fst + 0x10, 0);
    put_be32(fst + 0x14, 3);
    put_be32(fst + 0x18, 5);
    put_be32(fst + 0x1c, 0x5000);
    put_be32(fst + 0x20, 5);
    memcpy(fst + 0x24, "\0dir\0hello.txt\0", 16);
    memcpy(image + 0x5000, "hello", 5);

    FILE* f = fopen(path, "wb");
    if (!f)
        return 0;
    int ok = fwrite(image, 1, sizeof(image), f) == sizeof(image);
    return fclose(f) == 0 && ok;
}

static int read_file(const char* path, char* out, size_t out_size) {
    FILE* f = fopen(path, "rb");
    if (!f)
        return 0;
    size_t n = fread(out, 1, out_size - 1, f);
    out[n] = '\0';
    int ok = fclose(f) == 0;
    return ok;
}

static int run_extract_native(const char* extractor, const char* image,
                              const char* out_dir) {
#ifdef _WIN32
    const char* args[] = {
        extractor, "extract", "--native-only", image, out_dir, NULL
    };
    return _spawnv(_P_WAIT, extractor, args) == 0;
#else
    char* qexe = quote_arg(extractor);
    char* qimage = quote_arg(image);
    char* qout = quote_arg(out_dir);
    if (!qexe || !qimage || !qout) {
        free(qexe);
        free(qimage);
        free(qout);
        return 0;
    }

    size_t cmd_size = strlen(qexe) + strlen(qimage) + strlen(qout) + 64;
    char* cmd = (char*)malloc(cmd_size);
    if (!cmd) {
        free(qexe);
        free(qimage);
        free(qout);
        return 0;
    }

    snprintf(cmd, cmd_size, "%s extract --native-only %s %s", qexe, qimage, qout);
    int rc = system(cmd);
    free(cmd);
    free(qexe);
    free(qimage);
    free(qout);
    return rc == 0;
#endif
}

static int run_info(const char* extractor, const char* image) {
#ifdef _WIN32
    const char* args[] = {
        extractor, "extract", "--info", image, NULL
    };
    return _spawnv(_P_WAIT, extractor, args) == 0;
#else
    char* qexe = quote_arg(extractor);
    char* qimage = quote_arg(image);
    if (!qexe || !qimage) {
        free(qexe);
        free(qimage);
        return 0;
    }

    size_t cmd_size = strlen(qexe) + strlen(qimage) + 48;
    char* cmd = (char*)malloc(cmd_size);
    if (!cmd) {
        free(qexe);
        free(qimage);
        return 0;
    }

    snprintf(cmd, cmd_size, "%s extract --info %s", qexe, qimage);
    int rc = system(cmd);
    free(cmd);
    free(qexe);
    free(qimage);
    return rc == 0;
#endif
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: test_disc_extract <extract-exe> <work-dir>\n");
        return 1;
    }

    char* extractor_path = normalize_path_arg(argv[1]);
    char* work_dir_path = normalize_path_arg(argv[2]);
    if (!extractor_path || !work_dir_path) {
        fprintf(stderr, "out of memory\n");
        free(extractor_path);
        free(work_dir_path);
        return 1;
    }

    const char* extractor = extractor_path;
    const char* work_dir = work_dir_path;
    if (!make_dir_one(work_dir)) {
        fprintf(stderr, "failed to create work dir\n");
        free(extractor_path);
        free(work_dir_path);
        return 1;
    }

    char image_path[1024];
    char out_dir[1024];
    char extracted_path[1024];
    if (!join_path(image_path, sizeof(image_path), work_dir, "sample.iso") ||
        !join_path(out_dir, sizeof(out_dir), work_dir, "out") ||
        !join_path(extracted_path, sizeof(extracted_path), out_dir,
                   "files/dir/hello.txt")) {
        fprintf(stderr, "path too long\n");
        free(extractor_path);
        free(work_dir_path);
        return 1;
    }

    if (!write_sample_gcm(image_path)) {
        fprintf(stderr, "failed to write sample GCM\n");
        free(extractor_path);
        free(work_dir_path);
        return 1;
    }

    if (!run_extract_native(extractor, image_path, out_dir)) {
        fprintf(stderr, "extractor failed\n");
        free(extractor_path);
        free(work_dir_path);
        return 1;
    }

    char contents[16];
    if (!read_file(extracted_path, contents, sizeof(contents))) {
        fprintf(stderr, "failed to read extracted file\n");
        free(extractor_path);
        free(work_dir_path);
        return 1;
    }
    if (strcmp(contents, "hello") != 0) {
        fprintf(stderr, "extracted contents mismatch: '%s'\n", contents);
        free(extractor_path);
        free(work_dir_path);
        return 1;
    }

    char gcm_path[1024];
    if (!join_path(gcm_path, sizeof(gcm_path), work_dir, "sample.gcm")) {
        fprintf(stderr, "path too long\n");
        free(extractor_path);
        free(work_dir_path);
        return 1;
    }
    if (!write_sample_gcm(gcm_path)) {
        fprintf(stderr, "failed to write sample GCM\n");
        free(extractor_path);
        free(work_dir_path);
        return 1;
    }

    if (run_info(extractor, gcm_path)) {
        fprintf(stderr, "unsupported .gcm was accepted\n");
        free(extractor_path);
        free(work_dir_path);
        return 1;
    }

    free(extractor_path);
    free(work_dir_path);
    return 0;
}
