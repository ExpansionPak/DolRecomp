// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 ExpansionPak

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "core/types.h"

#define GC_MAGIC  0xC2339F3Du
#define WII_MAGIC 0x5D1C9EA3u

#define DISC_HEADER_SIZE 0x440u
#define BI2_OFFSET       0x440u
#define BI2_SIZE         0x2000u
#define APPLOADER_OFFSET 0x2440u

#define MAX_PATH_BUF 4096
#define COPY_CHUNK   0x10000u

#ifdef _WIN32
#define WIT_EXE_NAME "wit.exe"
#else
#define WIT_EXE_NAME "wit"
#endif

typedef enum {
    EXTRACT_OK,
    EXTRACT_UNSUPPORTED,
    EXTRACT_FAILED,
} ExtractResult;

typedef struct {
    const char* input_path;
    const char* output_dir;
    const char* wit_path;
    int info_only;
    int native_only;
    int prefer_wit;
} Options;

typedef struct {
    FILE* file;
    u64 size;
} RawReader;

typedef struct {
    bool is_dir;
    u32 name_offset;
    u32 offset;
    u32 size;
    char name[256];
} FstEntry;

static void print_usage(void) {
    printf("usage: dolrecomp.exe extract [options] <image.iso|image.wbfs> <output-dir>\n");
    printf("       dolrecomp.exe extract --info <image.iso|image.wbfs>\n");
    printf("\n");
    printf("options:\n");
    printf("  --info             print basic image info\n");
    printf("  --native-only      only use the built-in extractor\n");
    printf("  --prefer-wit       use Wiimms ISO Tool before native extraction\n");
    printf("  --wit <path>       path to wit or wit.exe\n");
    printf("  --help, -h         show this help\n");
    printf("\n");
    printf("supported inputs: .iso, .wbfs\n");
    printf("native support: GameCube ISO filesystem extraction\n");
    printf("wit bridge: Wii ISO and WBFS extraction\n");
}

static int ascii_ieq(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static const char* path_extension(const char* path) {
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    const char* base = path;
    const char* dot;
    if (slash && slash + 1 > base)
        base = slash + 1;
    if (backslash && backslash + 1 > base)
        base = backslash + 1;
    dot = strrchr(base, '.');
    return dot ? dot : "";
}

static int is_iso_path(const char* path) {
    return ascii_ieq(path_extension(path), ".iso");
}

static int is_wbfs_path(const char* path) {
    return ascii_ieq(path_extension(path), ".wbfs");
}

static int is_supported_image_path(const char* path) {
    return is_iso_path(path) || is_wbfs_path(path);
}

static int seek_file_base(FILE* file, s64 offset, int origin) {
#ifdef _WIN32
    return _fseeki64(file, offset, origin) == 0;
#else
    return fseeko(file, (off_t)offset, origin) == 0;
#endif
}

static int seek_file(FILE* file, u64 offset) {
    return seek_file_base(file, (s64)offset, SEEK_SET);
}

static u64 tell_file(FILE* file) {
#ifdef _WIN32
    return (u64)_ftelli64(file);
#else
    return (u64)ftello(file);
#endif
}

static int get_file_size(FILE* file, u64* size) {
    if (!seek_file(file, 0))
        return 0;
    if (!seek_file_base(file, 0, SEEK_END))
        return 0;
    *size = tell_file(file);
    return seek_file(file, 0);
}

static int read_at(RawReader* reader, u64 offset, void* out, size_t size) {
    if (offset > reader->size || size > reader->size - offset)
        return 0;
    if (!seek_file(reader->file, offset))
        return 0;
    return fread(out, 1, size, reader->file) == size;
}

static int raw_reader_open(RawReader* reader, const char* path) {
    memset(reader, 0, sizeof(*reader));
    reader->file = fopen(path, "rb");
    if (!reader->file) {
        fprintf(stderr, "error: can't open '%s'\n", path);
        return 0;
    }
    if (!get_file_size(reader->file, &reader->size)) {
        fprintf(stderr, "error: can't get size for '%s'\n", path);
        fclose(reader->file);
        reader->file = NULL;
        return 0;
    }
    return 1;
}

static void raw_reader_close(RawReader* reader) {
    if (reader->file)
        fclose(reader->file);
    memset(reader, 0, sizeof(*reader));
}

static int make_dir_one(const char* path) {
#ifdef _WIN32
    int rc = _mkdir(path);
#else
    int rc = mkdir(path, 0777);
#endif
    return rc == 0 || errno == EEXIST;
}

static int file_exists(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file)
        return 0;
    fclose(file);
    return 1;
}

static int make_dir_tree(const char* path) {
    char tmp[MAX_PATH_BUF];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }

    memcpy(tmp, path, len + 1);
    for (size_t i = 0; i < len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char saved = tmp[i];
            if (i == 0)
                continue;
#ifdef _WIN32
            if (i == 2 && tmp[1] == ':')
                continue;
#endif
            tmp[i] = '\0';
            if (tmp[0] != '\0' && !make_dir_one(tmp)) {
                fprintf(stderr, "error: can't create directory '%s'\n", tmp);
                return 0;
            }
            tmp[i] = saved;
        }
    }

    if (!make_dir_one(tmp)) {
        fprintf(stderr, "error: can't create directory '%s'\n", tmp);
        return 0;
    }
    return 1;
}

static int join_path(char* out, size_t out_size, const char* a, const char* b) {
    size_t alen = strlen(a);
    const char* sep = "";
    if (alen > 0 && a[alen - 1] != '/' && a[alen - 1] != '\\')
        sep = "/";
    return snprintf(out, out_size, "%s%s%s", a, sep, b) > 0 &&
           strlen(out) < out_size;
}

static int local_wit_path(char* out, size_t out_size) {
    char bin_dir[MAX_PATH_BUF];

    if (join_path(bin_dir, sizeof(bin_dir), "extern/wit", "bin") &&
        join_path(out, out_size, bin_dir, WIT_EXE_NAME) &&
        file_exists(out)) {
        return 1;
    }

    return join_path(out, out_size, "extern/wit", WIT_EXE_NAME) &&
           file_exists(out);
}

static void sanitize_component(char* out, size_t out_size, const char* in) {
    size_t w = 0;
    if (out_size == 0)
        return;

    if (strcmp(in, ".") == 0 || strcmp(in, "..") == 0 || in[0] == '\0') {
        snprintf(out, out_size, "_");
        return;
    }

    for (size_t i = 0; in[i] != '\0' && w + 1 < out_size; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|') {
            out[w++] = '_';
        } else {
            out[w++] = (char)c;
        }
    }
    out[w] = '\0';
    if (out[0] == '\0')
        snprintf(out, out_size, "_");
}

static int write_range(RawReader* reader, u64 offset, u64 size,
                       const char* output_path) {
    u8 buffer[COPY_CHUNK];
    FILE* out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "error: can't open '%s'\n", output_path);
        return 0;
    }

    u64 remaining = size;
    u64 cursor = offset;
    while (remaining != 0) {
        size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        if (!read_at(reader, cursor, buffer, chunk)) {
            fprintf(stderr, "error: failed reading image at 0x%llX\n",
                    (unsigned long long)cursor);
            fclose(out);
            return 0;
        }
        if (fwrite(buffer, 1, chunk, out) != chunk) {
            fprintf(stderr, "error: failed writing '%s'\n", output_path);
            fclose(out);
            return 0;
        }
        cursor += chunk;
        remaining -= chunk;
    }

    if (fclose(out) != 0) {
        fprintf(stderr, "error: failed closing '%s'\n", output_path);
        return 0;
    }
    return 1;
}

static int write_named_range(RawReader* reader, const char* dir,
                             const char* name, u64 offset, u64 size) {
    char path[MAX_PATH_BUF];
    if (!join_path(path, sizeof(path), dir, name)) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }
    return write_range(reader, offset, size, path);
}

static u32 dol_file_size_from_header(const u8* dol_header) {
    u32 max_end = 0x100u;

    for (u32 i = 0; i < 7; i++) {
        u32 off = read_be32(dol_header + i * 4);
        u32 size = read_be32(dol_header + 0x90 + i * 4);
        if (size != 0 && off + size > max_end)
            max_end = off + size;
    }

    for (u32 i = 0; i < 11; i++) {
        u32 off = read_be32(dol_header + 0x1c + i * 4);
        u32 size = read_be32(dol_header + 0xac + i * 4);
        if (size != 0 && off + size > max_end)
            max_end = off + size;
    }

    return max_end;
}

static int find_string_end(const char* base, size_t size, u32 offset) {
    if (offset >= size)
        return -1;
    for (size_t i = offset; i < size; i++) {
        if (base[i] == '\0')
            return (int)i;
    }
    return -1;
}

static int parse_fst_entries(const u8* fst, u32 fst_size,
                             FstEntry** out_entries, u32* out_count) {
    if (fst_size < 12) {
        fprintf(stderr, "error: FST is too small\n");
        return 0;
    }

    u32 root = read_be32(fst);
    u32 count = read_be32(fst + 8);
    if ((root >> 24) != 1 || count == 0 || count > fst_size / 12) {
        fprintf(stderr, "error: invalid FST root\n");
        return 0;
    }

    const char* names = (const char*)fst + count * 12u;
    size_t names_size = fst_size - count * 12u;
    FstEntry* entries = (FstEntry*)calloc(count, sizeof(FstEntry));
    if (!entries) {
        fprintf(stderr, "error: out of memory\n");
        return 0;
    }

    for (u32 i = 0; i < count; i++) {
        const u8* raw = fst + i * 12u;
        u32 type_name = read_be32(raw);
        entries[i].is_dir = (type_name >> 24) != 0;
        entries[i].name_offset = type_name & 0x00FFFFFFu;
        entries[i].offset = read_be32(raw + 4);
        entries[i].size = read_be32(raw + 8);

        int end = find_string_end(names, names_size, entries[i].name_offset);
        if (end < 0) {
            fprintf(stderr, "error: invalid FST name offset %u\n",
                    entries[i].name_offset);
            free(entries);
            return 0;
        }

        sanitize_component(entries[i].name, sizeof(entries[i].name),
                           names + entries[i].name_offset);
    }

    *out_entries = entries;
    *out_count = count;
    return 1;
}

static int extract_fst_dir(RawReader* reader, const FstEntry* entries,
                           u32 count, u32 dir_index, const char* dir_path,
                           u32* next_index) {
    u32 end = entries[dir_index].size;
    if (end > count || end <= dir_index) {
        fprintf(stderr, "error: invalid FST directory range\n");
        return 0;
    }

    u32 i = dir_index + 1;
    while (i < end) {
        char child_path[MAX_PATH_BUF];
        if (!join_path(child_path, sizeof(child_path), dir_path, entries[i].name)) {
            fprintf(stderr, "error: output path is too long\n");
            return 0;
        }

        if (entries[i].is_dir) {
            if (!make_dir_tree(child_path))
                return 0;
            u32 after_dir = i + 1;
            if (!extract_fst_dir(reader, entries, count, i, child_path, &after_dir))
                return 0;
            i = after_dir;
        } else {
            if (!write_range(reader, entries[i].offset, entries[i].size, child_path))
                return 0;
            i++;
        }
    }

    *next_index = end;
    return 1;
}

static ExtractResult extract_gamecube_iso_native(const Options* opts) {
    RawReader reader;
    u8 header[0x500];
    if (!raw_reader_open(&reader, opts->input_path))
        return EXTRACT_FAILED;

    if (!read_at(&reader, 0, header, sizeof(header))) {
        raw_reader_close(&reader);
        return EXTRACT_UNSUPPORTED;
    }

    u32 wii_magic = read_be32(header + 0x18);
    u32 gc_magic = read_be32(header + 0x1c);
    if (gc_magic != GC_MAGIC) {
        raw_reader_close(&reader);
        if (wii_magic == WII_MAGIC)
            fprintf(stderr, "native extractor: Wii ISO needs the wit bridge\n");
        return EXTRACT_UNSUPPORTED;
    }

    u32 dol_offset = read_be32(header + 0x420);
    u32 fst_offset = read_be32(header + 0x424);
    u32 fst_size = read_be32(header + 0x428);
    if (dol_offset == 0 || fst_offset == 0 || fst_size == 0 ||
        (u64)fst_offset + fst_size > reader.size) {
        fprintf(stderr, "error: invalid GameCube disc header\n");
        raw_reader_close(&reader);
        return EXTRACT_FAILED;
    }

    printf("format: GameCube ISO\n");
    printf("game id: %.6s\n", header);

    if (!make_dir_tree(opts->output_dir)) {
        raw_reader_close(&reader);
        return EXTRACT_FAILED;
    }

    char sys_dir[MAX_PATH_BUF];
    char files_dir[MAX_PATH_BUF];
    if (!join_path(sys_dir, sizeof(sys_dir), opts->output_dir, "sys") ||
        !join_path(files_dir, sizeof(files_dir), opts->output_dir, "files")) {
        fprintf(stderr, "error: output path is too long\n");
        raw_reader_close(&reader);
        return EXTRACT_FAILED;
    }
    if (!make_dir_tree(sys_dir) || !make_dir_tree(files_dir)) {
        raw_reader_close(&reader);
        return EXTRACT_FAILED;
    }

    u8 dol_header[0x100];
    if (!read_at(&reader, dol_offset, dol_header, sizeof(dol_header))) {
        fprintf(stderr, "error: failed reading main.dol header\n");
        raw_reader_close(&reader);
        return EXTRACT_FAILED;
    }
    u32 dol_size = dol_file_size_from_header(dol_header);

    u32 apploader_size = 0x20u;
    u8 app_header[0x20];
    if (read_at(&reader, APPLOADER_OFFSET, app_header, sizeof(app_header))) {
        apploader_size += read_be32(app_header + 0x14);
        apploader_size += read_be32(app_header + 0x18);
    }

    if (!write_named_range(&reader, sys_dir, "boot.bin", 0, DISC_HEADER_SIZE) ||
        !write_named_range(&reader, sys_dir, "bi2.bin", BI2_OFFSET, BI2_SIZE) ||
        !write_named_range(&reader, sys_dir, "apploader.img",
                           APPLOADER_OFFSET, apploader_size) ||
        !write_named_range(&reader, sys_dir, "main.dol", dol_offset, dol_size) ||
        !write_named_range(&reader, sys_dir, "fst.bin", fst_offset, fst_size)) {
        raw_reader_close(&reader);
        return EXTRACT_FAILED;
    }

    u8* fst = (u8*)malloc(fst_size);
    if (!fst) {
        fprintf(stderr, "error: out of memory\n");
        raw_reader_close(&reader);
        return EXTRACT_FAILED;
    }
    if (!read_at(&reader, fst_offset, fst, fst_size)) {
        fprintf(stderr, "error: failed reading FST\n");
        free(fst);
        raw_reader_close(&reader);
        return EXTRACT_FAILED;
    }

    FstEntry* entries = NULL;
    u32 count = 0;
    if (!parse_fst_entries(fst, fst_size, &entries, &count)) {
        free(fst);
        raw_reader_close(&reader);
        return EXTRACT_FAILED;
    }

    u32 next = 0;
    int ok = extract_fst_dir(&reader, entries, count, 0, files_dir, &next);
    free(entries);
    free(fst);
    raw_reader_close(&reader);

    if (!ok)
        return EXTRACT_FAILED;

    printf("extracted to: %s\n", opts->output_dir);
    return EXTRACT_OK;
}

static int print_native_info(const char* path) {
    RawReader reader;
    u8 header[0x500];
    if (!raw_reader_open(&reader, path))
        return 0;
    if (!read_at(&reader, 0, header, sizeof(header))) {
        fprintf(stderr, "error: failed reading image header\n");
        raw_reader_close(&reader);
        return 0;
    }

    if (memcmp(header, "WBFS", 4) == 0) {
        printf("format: WBFS container\n");
        printf("native extraction: use wit bridge\n");
        raw_reader_close(&reader);
        return 1;
    }

    u32 wii_magic = read_be32(header + 0x18);
    u32 gc_magic = read_be32(header + 0x1c);
    if (gc_magic == GC_MAGIC || wii_magic == WII_MAGIC) {
        u32 dol_offset = read_be32(header + 0x420);
        u32 fst_offset = read_be32(header + 0x424);
        u32 fst_size = read_be32(header + 0x428);
        printf("format: %s\n", gc_magic == GC_MAGIC ? "GameCube ISO" : "Wii ISO");
        printf("game id: %.6s\n", header);
        printf("main.dol: 0x%08X\n", dol_offset);
        printf("fst:      0x%08X (0x%X bytes)\n", fst_offset, fst_size);
        if (wii_magic == WII_MAGIC)
            printf("native extraction: use wit bridge for Wii partitions\n");
        raw_reader_close(&reader);
        return 1;
    }

    printf("format: unknown ISO/WBFS contents\n");
    raw_reader_close(&reader);
    return 1;
}

static char* quote_arg(const char* arg) {
    size_t len = strlen(arg);
    size_t cap = len * 4 + 3;
    char* out = (char*)malloc(cap);
    if (!out)
        return NULL;

#ifdef _WIN32
    size_t w = 0;
    out[w++] = '"';
    for (size_t i = 0; i < len; i++) {
        if (arg[i] == '"')
            out[w++] = '\\';
        out[w++] = arg[i];
    }
    out[w++] = '"';
    out[w] = '\0';
#else
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
#endif
    return out;
}

static int command_exists(const char* exe) {
    char* qexe = quote_arg(exe);
    if (!qexe)
        return 0;

    char cmd[MAX_PATH_BUF + 128];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "%s --version >NUL 2>NUL", qexe);
#else
    snprintf(cmd, sizeof(cmd), "%s --version >/dev/null 2>/dev/null", qexe);
#endif
    free(qexe);
    return system(cmd) == 0;
}

static ExtractResult extract_with_wit(const Options* opts) {
    char local_wit[MAX_PATH_BUF];
    const char* wit = opts->wit_path;
    int should_check_command = 1;

    if (!wit && local_wit_path(local_wit, sizeof(local_wit))) {
        wit = local_wit;
        should_check_command = 0;
    }
    if (!wit)
        wit = "wit";

    if (should_check_command && !command_exists(wit)) {
        fprintf(stderr, "wit bridge: '%s' was not found\n", wit);
        fprintf(stderr, "run dolrecomp.exe --setup or pass --wit <path>\n");
        return EXTRACT_UNSUPPORTED;
    }

    if (!make_dir_tree(opts->output_dir))
        return EXTRACT_FAILED;

    char* qwit = quote_arg(wit);
    char* qin = quote_arg(opts->input_path);
    char* qout = quote_arg(opts->output_dir);
    if (!qwit || !qin || !qout) {
        free(qwit);
        free(qin);
        free(qout);
        fprintf(stderr, "error: out of memory\n");
        return EXTRACT_FAILED;
    }

    size_t cmd_size = strlen(qwit) + strlen(qin) + strlen(qout) + 64;
    char* cmd = (char*)malloc(cmd_size);
    if (!cmd) {
        free(qwit);
        free(qin);
        free(qout);
        fprintf(stderr, "error: out of memory\n");
        return EXTRACT_FAILED;
    }

#ifdef _WIN32
    snprintf(cmd, cmd_size, "\"%s EXTRACT -o %s %s\"", qwit, qin, qout);
#else
    snprintf(cmd, cmd_size, "%s EXTRACT -o %s %s", qwit, qin, qout);
#endif
    printf("using wit bridge\n");
    int rc = system(cmd);

    free(cmd);
    free(qwit);
    free(qin);
    free(qout);

    if (rc != 0) {
        fprintf(stderr, "wit extraction failed\n");
        return EXTRACT_FAILED;
    }
    return EXTRACT_OK;
}

static int parse_options(int argc, char** argv, Options* opts) {
    memset(opts, 0, sizeof(*opts));

    const char* positional[2];
    int positional_count = 0;

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(arg, "--info") == 0) {
            opts->info_only = 1;
        } else if (strcmp(arg, "--native-only") == 0 || strcmp(arg, "--no-wit") == 0) {
            opts->native_only = 1;
        } else if (strcmp(arg, "--prefer-wit") == 0) {
            opts->prefer_wit = 1;
        } else if (strcmp(arg, "--wit") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --wit needs a path\n");
                return -1;
            }
            opts->wit_path = argv[++i];
        } else if (strncmp(arg, "--wit=", 6) == 0) {
            opts->wit_path = arg + 6;
        } else if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "error: unknown option '%s'\n", arg);
            return -1;
        } else {
            if (positional_count >= 2) {
                print_usage();
                return -1;
            }
            positional[positional_count++] = arg;
        }
    }

    if (opts->info_only) {
        if (positional_count != 1) {
            print_usage();
            return -1;
        }
        opts->input_path = positional[0];
        return 1;
    }

    if (positional_count != 2) {
        print_usage();
        return -1;
    }

    opts->input_path = positional[0];
    opts->output_dir = positional[1];
    return 1;
}

int disc_extract_main(int argc, char** argv) {
    Options opts;
    int parsed = parse_options(argc, argv, &opts);
    if (parsed <= 0)
        return parsed == 0 ? 0 : 1;

    if (!is_supported_image_path(opts.input_path)) {
        fprintf(stderr, "unsupported format: only .iso and .wbfs are supported\n");
        return 1;
    }

    if (opts.info_only)
        return print_native_info(opts.input_path) ? 0 : 1;

    ExtractResult result = EXTRACT_UNSUPPORTED;
    if (opts.prefer_wit && !opts.native_only)
        result = extract_with_wit(&opts);

    if (result == EXTRACT_UNSUPPORTED)
        result = extract_gamecube_iso_native(&opts);

    if (result == EXTRACT_UNSUPPORTED && !opts.native_only)
        result = extract_with_wit(&opts);

    return result == EXTRACT_OK ? 0 : 1;
}
