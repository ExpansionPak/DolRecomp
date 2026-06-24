#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

#include "core/types.h"
#include "frontend/dol.h"
#include "frontend/rel.h"
#include "frontend/rpx.h"
#include "frontend/disc_extract.h"
#include "frontend/decoder.h"
#include "backend/emitter.h"

#define EMIT_CHUNK_INSTRUCTIONS 4096u
#define REL_AUTO_BASE 0x80500000u
#define REL_AUTO_ALIGN 0x10000u
#define DATABASE_DIR "database"
#define DATABASE_TITLES_FILE "titles.txt"
#define DATABASE_SETUP_FLAG ".setup_done.flag"
#define GAMETDB_TITLES_URL "https://www.gametdb.com/titles.txt?LANG=EN"
#define EXTERN_DIR "extern"
#define WIT_DIR "extern/wit"
#define WIT_BIN_DIR "extern/wit/bin"

#ifdef _WIN32
#define WIT_DOWNLOAD_URL "https://wit.wiimm.de/download/wit-v3.05a-r8638-cygwin64.zip"
#define WIT_ARCHIVE_NAME "wit-v3.05a-r8638-cygwin64.zip"
#define WIT_EXE_NAME "wit.exe"
#elif defined(__APPLE__)
#define WIT_DOWNLOAD_URL "https://wit.wiimm.de/download/wit-v3.05a-r8638-mac.tar.gz"
#define WIT_ARCHIVE_NAME "wit-v3.05a-r8638-mac.tar.gz"
#define WIT_EXE_NAME "wit"
#else
#define WIT_DOWNLOAD_URL "https://wit.wiimm.de/download/wit-v3.05a-r8638-x86_64.tar.gz"
#define WIT_ARCHIVE_NAME "wit-v3.05a-r8638-x86_64.tar.gz"
#define WIT_EXE_NAME "wit"
#endif

static void print_usage(const char* argv0) {
    (void)argv0;

    fprintf(stderr, "usage: dolrecomp.exe [-jN] [--cpu gekko|broadway|espresso] [--gamecube] <input> [wii-title-id] [output.c | output-dir]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "extract:  dolrecomp.exe extract game.iso output_folder\n");
    fprintf(stderr, "extract:  dolrecomp.exe extract game.wbfs output_folder\n");
    fprintf(stderr, "wii:      dolrecomp.exe <input.dol> SUKE01 build\n");
    fprintf(stderr, "gamecube: dolrecomp.exe --gamecube <input.dol> build\n");
    fprintf(stderr, "rel:      dolrecomp.exe <input.rel | rel_folder> SUKE01 build\n");
    fprintf(stderr, "wii u cpu: dolrecomp.exe --cpu espresso <input.rpx> build\n");
    fprintf(stderr, "with output.c: writes that split C set\n");
    fprintf(stderr, "with output-dir: writes output-dir/<wii-title-id>_generated/<wii-title-id>.c\n");
    fprintf(stderr, "with --gamecube or --cpu espresso output-dir: writes output-dir/generated/generated.c\n");
    fprintf(stderr, "-jN writes split C files with N jobs, like -j14\n");
    fprintf(stderr, "--rel-base optionally sets the first virtual load address used for REL codegen\n");
    fprintf(stderr, "--setup downloads database/titles.txt and can install wit tools\n");
    fprintf(stderr, "without output: writes generated code under the current directory\n");
}

static int make_dir(const char* path) {
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

static const char* path_basename(const char* path) {
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

static int path_dirname(const char* path, char* dir, size_t dir_size) {
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

static int ascii_lower(int ch) {
    if (ch >= 'A' && ch <= 'Z')
        return ch + ('a' - 'A');
    return ch;
}

static int ascii_upper(int ch) {
    if (ch >= 'a' && ch <= 'z')
        return ch - ('a' - 'A');
    return ch;
}

static int ascii_case_equal(const char* a, const char* b) {
    while (*a && *b) {
        if (ascii_lower((unsigned char)*a) != ascii_lower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int is_path_sep(char ch) {
    return ch == '/' || ch == '\\';
}

static int has_c_extension(const char* path) {
    const char* base = path_basename(path);
    const char* dot = strrchr(base, '.');
    return dot && ascii_case_equal(dot, ".c");
}

static int has_rpx_extension(const char* path) {
    const char* base = path_basename(path);
    const char* dot = strrchr(base, '.');
    return dot && ascii_case_equal(dot, ".rpx");
}

static int has_rel_extension(const char* path) {
    const char* base = path_basename(path);
    const char* dot = strrchr(base, '.');
    return dot && ascii_case_equal(dot, ".rel");
}

static int is_title_id(const char* text) {
    size_t len = strlen(text);
    if (len != 6)
        return 0;

    for (size_t i = 0; i < len; i++) {
        char ch = text[i];
        if (!((ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9'))) {
            return 0;
        }
    }

    return 1;
}

static int is_title_id_length_valid(const char* text) {
    return strlen(text) == 6;
}

static int parse_cpu_name(const char* text, DolRecompCPU* cpu) {
    if (ascii_case_equal(text, "gekko") || ascii_case_equal(text, "gamecube")) {
        *cpu = DOLRECOMP_CPU_GEKKO;
        return 1;
    }

    if (ascii_case_equal(text, "broadway") || ascii_case_equal(text, "wii")) {
        *cpu = DOLRECOMP_CPU_BROADWAY;
        return 1;
    }

    if (ascii_case_equal(text, "espresso") || ascii_case_equal(text, "wiiu") ||
        ascii_case_equal(text, "wii-u")) {
        *cpu = DOLRECOMP_CPU_ESPRESSO;
        return 1;
    }

    return 0;
}

static const char* cpu_display_name(DolRecompCPU cpu) {
    switch (cpu) {
    case DOLRECOMP_CPU_BROADWAY:
        return "Broadway (Wii)";
    case DOLRECOMP_CPU_ESPRESSO:
        return "Espresso (Wii U)";
    case DOLRECOMP_CPU_GEKKO:
    default:
        return "Gekko (GameCube)";
    }
}

static void copy_title_id(char* out, size_t out_size, const char* title_id) {
    size_t len = strlen(title_id);
    if (len >= out_size)
        len = out_size - 1;

    for (size_t i = 0; i < len; i++)
        out[i] = (char)ascii_upper((unsigned char)title_id[i]);
    out[len] = '\0';
}

static void sleep_ms(u32 milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep((useconds_t)milliseconds * 1000u);
#endif
}

static const char* skip_spaces(const char* text) {
    while (*text == ' ' || *text == '\t')
        text++;
    return text;
}

static void copy_trimmed(char* out, size_t out_size, const char* start,
                         const char* end) {
    while (end > start &&
           (end[-1] == '\r' || end[-1] == '\n' ||
            end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }

    size_t len = (size_t)(end - start);
    if (len >= out_size)
        len = out_size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
}

static int database_titles_path(char* out, size_t out_size);

static int parse_gametdb_title_line(const char* line, const char* title_id,
                                    char* out, size_t out_size) {
    const char* eq = strchr(line, '=');
    if (!eq)
        return 0;

    const char* id_start = skip_spaces(line);
    const char* id_end = eq;
    while (id_end > id_start && (id_end[-1] == ' ' || id_end[-1] == '\t'))
        id_end--;

    if ((size_t)(id_end - id_start) != 6)
        return 0;

    for (size_t i = 0; i < 6; i++) {
        if (ascii_upper((unsigned char)id_start[i]) !=
            ascii_upper((unsigned char)title_id[i])) {
            return 0;
        }
    }

    const char* title_start = skip_spaces(eq + 1);
    const char* title_end = title_start + strlen(title_start);
    copy_trimmed(out, out_size, title_start, title_end);
    return out[0] != '\0';
}

static int resolve_game_name_from_stream(FILE* stream, const char* title_id,
                                         char* out, size_t out_size) {
    char line[1024];

    while (fgets(line, sizeof(line), stream)) {
        if (parse_gametdb_title_line(line, title_id, out, out_size))
            return 1;
    }

    return 0;
}

static int resolve_game_name_from_file(const char* path, const char* title_id,
                                       char* out, size_t out_size) {
    FILE* file = fopen(path, "r");
    if (!file)
        return 0;

    int found = resolve_game_name_from_stream(file, title_id, out, out_size);
    fclose(file);
    return found;
}

static int resolve_game_name(const char* title_id, char* out, size_t out_size) {
    const char* env_path = getenv("DOLRECOMP_GAMETDB_TITLES");
    if (env_path && env_path[0] != '\0' &&
        resolve_game_name_from_file(env_path, title_id, out, out_size)) {
        return 1;
    }

    char path[1024];
    if (!database_titles_path(path, sizeof(path)))
        return 0;

    return resolve_game_name_from_file(path, title_id, out, out_size);
}

static void describe_game(char* out, size_t out_size, const char* title_id,
                          int gamecube_mode) {
    char game_name[220];

    if (gamecube_mode) {
        snprintf(out, out_size, "GameCube DOL");
        return;
    }

    printf("Searching for game info...\n");
    sleep_ms(2000);

    if (resolve_game_name(title_id, game_name, sizeof(game_name))) {
        printf("Found!\n");
        printf("%s (%s)\n", game_name, title_id);
        snprintf(out, out_size, "%s (%s)", game_name, title_id);
    } else {
        printf("Not found, using title id.\n");
        printf("%s\n", title_id);
        snprintf(out, out_size, "%s", title_id);
    }
}

static int make_dir_tree(const char* path) {
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

static int join_path(char* out, size_t out_size, const char* dir, const char* name) {
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

static int database_path(char* out, size_t out_size, const char* name) {
    return join_path(out, out_size, DATABASE_DIR, name);
}

static int file_exists(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file)
        return 0;
    fclose(file);
    return 1;
}

static int path_is_directory(const char* path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

typedef struct {
    char** paths;
    u32 count;
    u32 capacity;
} PathList;

static void path_list_free(PathList* list) {
    for (u32 i = 0; i < list->count; i++)
        free(list->paths[i]);
    free(list->paths);
    list->paths = NULL;
    list->count = 0;
    list->capacity = 0;
}

static char* copy_string_alloc(const char* text) {
    size_t len = strlen(text);
    char* copy = (char*)malloc(len + 1);
    if (!copy)
        return NULL;
    memcpy(copy, text, len + 1);
    return copy;
}

static int path_list_add(PathList* list, const char* path) {
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

static int collect_rel_paths(const char* root, PathList* list) {
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

static int compare_paths_for_sort(const void* a, const void* b) {
    const char* const* pa = (const char* const*)a;
    const char* const* pb = (const char* const*)b;
    return strcmp(*pa, *pb);
}

static void path_list_sort(PathList* list) {
    if (list->count > 1)
        qsort(list->paths, list->count, sizeof(list->paths[0]),
              compare_paths_for_sort);
}

static char* shell_quote_arg(const char* arg) {
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

static int run_shell_command(const char* command) {
    return system(command) == 0;
}

static int database_titles_path(char* out, size_t out_size) {
    return database_path(out, out_size, DATABASE_TITLES_FILE);
}

static int database_titles_available(void) {
    char path[1024];
    return database_titles_path(path, sizeof(path)) && file_exists(path);
}

static void print_database_missing_notice(void) {
    fprintf(stderr,
            "database/titles.txt is missing; using GameCube mode. Run dolrecomp --setup to download it.\n");
}

static int write_setup_flag(const char* path) {
#ifdef _WIN32
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
#endif

    FILE* file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "error: can't write setup flag '%s'\n", path);
        return 0;
    }

    fprintf(file, "done\n");
    if (fclose(file) != 0) {
        fprintf(stderr, "error: can't finish setup flag '%s'\n", path);
        return 0;
    }

#ifdef _WIN32
    SetFileAttributesA(path, FILE_ATTRIBUTE_HIDDEN);
#endif
    return 1;
}

static int download_titles_database(const char* output_path) {
    char temp_path[1100];
    if (snprintf(temp_path, sizeof(temp_path), "%s.tmp", output_path) >=
        (int)sizeof(temp_path)) {
        fprintf(stderr, "error: database path is too long\n");
        return 0;
    }

#ifdef _WIN32
    const char* command =
        "curl.exe -fsSL --max-time 60 \"" GAMETDB_TITLES_URL "\"";
#else
    const char* command =
        "curl -fsSL --max-time 60 '" GAMETDB_TITLES_URL "'";
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        fprintf(stderr, "error: can't start curl\n");
        return 0;
    }

    FILE* out = fopen(temp_path, "wb");
    if (!out) {
        pclose(pipe);
        fprintf(stderr, "error: can't write '%s'\n", temp_path);
        return 0;
    }

    unsigned char buffer[8192];
    size_t total = 0;
    for (;;) {
        size_t got = fread(buffer, 1, sizeof(buffer), pipe);
        if (got > 0) {
            if (fwrite(buffer, 1, got, out) != got) {
                fclose(out);
                pclose(pipe);
                remove(temp_path);
                fprintf(stderr, "error: failed writing '%s'\n", temp_path);
                return 0;
            }
            total += got;
        }
        if (got < sizeof(buffer)) {
            if (feof(pipe))
                break;
            if (ferror(pipe)) {
                fclose(out);
                pclose(pipe);
                remove(temp_path);
                fprintf(stderr, "error: failed reading titles database\n");
                return 0;
            }
        }
    }

    if (fclose(out) != 0) {
        pclose(pipe);
        remove(temp_path);
        fprintf(stderr, "error: failed writing '%s'\n", temp_path);
        return 0;
    }

    int rc = pclose(pipe);
    if (rc != 0 || total == 0) {
        remove(temp_path);
        fprintf(stderr, "error: failed downloading GameTDB titles\n");
        return 0;
    }

    remove(output_path);
    if (rename(temp_path, output_path) != 0) {
        remove(temp_path);
        fprintf(stderr, "error: can't install '%s'\n", output_path);
        return 0;
    }

    return 1;
}

static int local_wit_path(char* out, size_t out_size) {
    char path[1024];

    if (join_path(path, sizeof(path), WIT_BIN_DIR, WIT_EXE_NAME) &&
        file_exists(path)) {
        return snprintf(out, out_size, "%s", path) > 0 &&
               strlen(out) < out_size;
    }

    if (join_path(path, sizeof(path), WIT_DIR, WIT_EXE_NAME) &&
        file_exists(path)) {
        return snprintf(out, out_size, "%s", path) > 0 &&
               strlen(out) < out_size;
    }

    return 0;
}

static int command_exists_quiet(const char* command_name) {
    char* quoted = shell_quote_arg(command_name);
    if (!quoted)
        return 0;

    char command[1200];
#ifdef _WIN32
    int written = snprintf(command, sizeof(command), "%s --version >NUL 2>NUL",
                           quoted);
#else
    int written = snprintf(command, sizeof(command), "%s --version >/dev/null 2>/dev/null",
                           quoted);
#endif
    free(quoted);
    return written > 0 && (size_t)written < sizeof(command) &&
           run_shell_command(command);
}

static int wit_tools_available(void) {
    char path[1024];
    if (local_wit_path(path, sizeof(path)))
        return 1;
    return command_exists_quiet("wit");
}

static int prompt_yes_no(const char* prompt) {
    char line[32];
    printf("%s", prompt);
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin))
        return 0;
    return line[0] == 'y' || line[0] == 'Y';
}

static int download_file(const char* url, const char* output_path) {
    char temp_path[1200];
    if (snprintf(temp_path, sizeof(temp_path), "%s.tmp", output_path) >=
        (int)sizeof(temp_path)) {
        fprintf(stderr, "error: download path is too long\n");
        return 0;
    }

    char* qurl = shell_quote_arg(url);
    char* qout = shell_quote_arg(temp_path);
    if (!qurl || !qout) {
        free(qurl);
        free(qout);
        fprintf(stderr, "error: out of memory\n");
        return 0;
    }

    char command[2600];
#ifdef _WIN32
    int written = snprintf(command, sizeof(command),
                           "curl.exe -fL --max-time 300 -o %s %s",
                           qout, qurl);
#else
    int written = snprintf(command, sizeof(command),
                           "curl -fL --max-time 300 -o %s %s",
                           qout, qurl);
#endif
    free(qurl);
    free(qout);

    if (written <= 0 || (size_t)written >= sizeof(command)) {
        fprintf(stderr, "error: download command is too long\n");
        return 0;
    }

    remove(temp_path);
    if (!run_shell_command(command)) {
        remove(temp_path);
        fprintf(stderr, "error: download failed\n");
        return 0;
    }

    remove(output_path);
    if (rename(temp_path, output_path) != 0) {
        remove(temp_path);
        fprintf(stderr, "error: can't install '%s'\n", output_path);
        return 0;
    }

    return 1;
}

#ifdef _WIN32
static void write_ps_quoted(FILE* file, const char* text) {
    fputc('\'', file);
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\'')
            fputs("''", file);
        else
            fputc(text[i], file);
    }
    fputc('\'', file);
}

static int write_wit_install_script(const char* script_path,
                                    const char* archive_path) {
    FILE* file = fopen(script_path, "w");
    if (!file) {
        fprintf(stderr, "error: can't write '%s'\n", script_path);
        return 0;
    }

    fprintf(file, "$ErrorActionPreference = 'Stop'\n");
    fprintf(file, "$archive = ");
    write_ps_quoted(file, archive_path);
    fprintf(file, "\n$root = ");
    write_ps_quoted(file, WIT_DIR);
    fprintf(file, "\n$unpack = Join-Path $root 'unpack'\n");
    fprintf(file, "$bin = ");
    write_ps_quoted(file, WIT_BIN_DIR);
    fprintf(file, "\nRemove-Item -LiteralPath $unpack -Recurse -Force -ErrorAction SilentlyContinue\n");
    fprintf(file, "Remove-Item -LiteralPath $bin -Recurse -Force -ErrorAction SilentlyContinue\n");
    fprintf(file, "New-Item -ItemType Directory -Force -Path $unpack | Out-Null\n");
    fprintf(file, "New-Item -ItemType Directory -Force -Path $bin | Out-Null\n");
    fprintf(file, "Expand-Archive -LiteralPath $archive -DestinationPath $unpack -Force\n");
    fprintf(file, "$wit = Get-ChildItem -LiteralPath $unpack -Recurse -Filter 'wit.exe' | Select-Object -First 1\n");
    fprintf(file, "if (-not $wit) { exit 2 }\n");
    fprintf(file, "Copy-Item -Path (Join-Path $wit.Directory.FullName '*') -Destination $bin -Recurse -Force\n");
    fprintf(file, "Remove-Item -LiteralPath $unpack -Recurse -Force -ErrorAction SilentlyContinue\n");

    if (fclose(file) != 0) {
        fprintf(stderr, "error: failed writing '%s'\n", script_path);
        return 0;
    }
    return 1;
}

static int extract_wit_archive(const char* archive_path) {
    char script_path[1200];
    if (join_path(script_path, sizeof(script_path), WIT_DIR, "install_wit.ps1") == 0) {
        fprintf(stderr, "error: setup path is too long\n");
        return 0;
    }

    if (!write_wit_install_script(script_path, archive_path))
        return 0;

    char* qscript = shell_quote_arg(script_path);
    if (!qscript) {
        fprintf(stderr, "error: out of memory\n");
        return 0;
    }

    char command[1800];
    int written = snprintf(command, sizeof(command),
                           "powershell.exe -NoProfile -ExecutionPolicy Bypass -File %s",
                           qscript);
    free(qscript);

    if (written <= 0 || (size_t)written >= sizeof(command)) {
        fprintf(stderr, "error: extract command is too long\n");
        return 0;
    }

    if (!run_shell_command(command)) {
        fprintf(stderr, "error: failed extracting wit tools\n");
        return 0;
    }

    remove(script_path);
    return 1;
}
#else
static int extract_wit_archive(const char* archive_path) {
    char* qarchive = shell_quote_arg(archive_path);
    char* qroot = shell_quote_arg(WIT_DIR);
    char* qbin = shell_quote_arg(WIT_BIN_DIR);
    if (!qarchive || !qroot || !qbin) {
        free(qarchive);
        free(qroot);
        free(qbin);
        fprintf(stderr, "error: out of memory\n");
        return 0;
    }

    char command[3000];
    int written = snprintf(command, sizeof(command),
        "rm -rf %s/unpack %s && mkdir -p %s/unpack %s && "
        "tar -xzf %s -C %s/unpack && "
        "found=$(find %s/unpack -type f -name wit | head -n 1) && "
        "test -n \"$found\" && cp -R \"$(dirname \"$found\")\"/. %s && "
        "chmod +x %s/wit && rm -rf %s/unpack",
        qroot, qbin, qroot, qbin, qarchive, qroot, qroot, qbin, qbin, qroot);

    free(qarchive);
    free(qroot);
    free(qbin);

    if (written <= 0 || (size_t)written >= sizeof(command)) {
        fprintf(stderr, "error: extract command is too long\n");
        return 0;
    }

    if (!run_shell_command(command)) {
        fprintf(stderr, "error: failed extracting wit tools\n");
        return 0;
    }
    return 1;
}
#endif

static int install_wit_tools(void) {
    char archive_path[1200];

    if (!make_dir_tree(EXTERN_DIR) || !make_dir_tree(WIT_DIR))
        return 0;

    if (!join_path(archive_path, sizeof(archive_path), WIT_DIR,
                   WIT_ARCHIVE_NAME)) {
        fprintf(stderr, "error: wit path is too long\n");
        return 0;
    }

    printf("downloading Wiimms ISO Tools...\n");
    if (!download_file(WIT_DOWNLOAD_URL, archive_path))
        return 0;

    printf("installing Wiimms ISO Tools...\n");
    if (!extract_wit_archive(archive_path))
        return 0;

    char wit_path[1024];
    if (!local_wit_path(wit_path, sizeof(wit_path))) {
        fprintf(stderr, "error: wit install did not produce %s\n", WIT_EXE_NAME);
        return 0;
    }

    printf("  wit:    %s\n", wit_path);
    return 1;
}

static int setup_wit_tools(void) {
    if (wit_tools_available()) {
        printf("wit tools found.\n");
        return 1;
    }

    if (!prompt_yes_no("wit tools missing, download? [Y/N] ")) {
        printf("skipping wit tools.\n");
        return 1;
    }

    return install_wit_tools();
}

static int run_setup(void) {
    char titles_path[1024];
    char flag_path[1024];

    if (!make_dir_tree(DATABASE_DIR))
        return 0;
    if (!database_titles_path(titles_path, sizeof(titles_path)) ||
        !database_path(flag_path, sizeof(flag_path), DATABASE_SETUP_FLAG)) {
        fprintf(stderr, "error: database path is too long\n");
        return 0;
    }

    printf("setting up database...\n");
    printf("downloading GameTDB titles.txt...\n");
    if (!download_titles_database(titles_path))
        return 0;

    if (!write_setup_flag(flag_path))
        return 0;

    if (!setup_wit_tools())
        return 0;

    printf("done!\n");
    printf("  titles: %s\n", titles_path);
    printf("  flag:   %s\n", flag_path);
    return 1;
}

static int build_named_output_path(const char* output_root, const char* title_id,
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

static int build_gamecube_output_path(const char* output_root, char* output_path,
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

static int build_generated_folder_path(const char* output_root,
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

static void safe_module_name(const char* path, char* out, size_t out_size) {
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

static int build_rel_output_path(const char* generated_root,
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

static int make_output_stem(const char* output_path, char* stem, size_t stem_size) {
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

static int split_include_name(const char* stem, char* include_name, size_t include_size) {
    const char* base = path_basename(stem);
    int written = snprintf(include_name, include_size, "%s.h", base);
    return written > 0 && (size_t)written < include_size;
}

static void emit_chunk_prototype(FILE* out, u32 func_addr) {
    fprintf(out, "void func_%08X(CPUState* ctx);\n", func_addr);
}

typedef struct {
    u32 start;
    u32 end;
} FunctionRange;

typedef struct {
    FunctionRange* ranges;
    u32 count;
    u32 capacity;
} FunctionList;

static void function_list_free(FunctionList* list) {
    free(list->ranges);
    list->ranges = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int function_list_add(FunctionList* list, u32 start, u32 end) {
    if (list->count == list->capacity) {
        u32 new_capacity = list->capacity ? list->capacity * 2u : 64u;
        FunctionRange* new_ranges =
            (FunctionRange*)realloc(list->ranges, new_capacity * sizeof(*new_ranges));
        if (!new_ranges) {
            fprintf(stderr, "error: out of memory\n");
            return 0;
        }
        list->ranges = new_ranges;
        list->capacity = new_capacity;
    }

    list->ranges[list->count].start = start;
    list->ranges[list->count].end = end;
    list->count++;
    return 1;
}

static void emit_dispatch_helpers(FILE* out, const FunctionList* funcs, u32 entry_point) {
    fprintf(out, "\n#define DOLRECOMP_ENTRY_POINT 0x%08Xu\n", entry_point);
    fprintf(out, "\ntypedef void (*DolRecompFunction)(CPUState* ctx);\n");
    fprintf(out, "\nstatic inline int dolrecomp_call(CPUState* ctx, u32 address) {\n");
    fprintf(out, "    if (ppc_host_call(ctx, address)) return 1;\n");
    for (u32 i = 0; i < funcs->count; i++) {
        fprintf(out,
                "    if (address >= 0x%08Xu && address < 0x%08Xu && "
                "((address - 0x%08Xu) & 3u) == 0u) { func_%08X(ctx); return 1; }\n",
                funcs->ranges[i].start, funcs->ranges[i].end,
                funcs->ranges[i].start, funcs->ranges[i].start);
    }
    fprintf(out, "    return 0;\n");
    fprintf(out, "}\n");
    fprintf(out, "\nstatic inline int dolrecomp_run_blocks(CPUState* ctx, u32 max_blocks) {\n");
    fprintf(out, "    u32 blocks = 0;\n");
    fprintf(out, "    while (max_blocks == 0u || blocks < max_blocks) {\n");
    fprintf(out, "        if (!dolrecomp_call(ctx, ctx->pc)) return 0;\n");
    fprintf(out, "        if (ctx->exception) return 0;\n");
    fprintf(out, "        blocks++;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return 1;\n");
    fprintf(out, "}\n");
}

typedef enum {
    EMBEDDED_DATA_NONE = 0,
    EMBEDDED_DATA_DOL,
    EMBEDDED_DATA_RPX
} EmbeddedDataMode;

typedef struct {
    const char* label;
    const char* name;
    const u8* data;
    u32 index;
    u32 file_offset;
    u32 address;
    u32 size;
    EmbeddedDataMode embedded_data_mode;
} LoadedCodeSection;

typedef struct {
    int known;
    u32 value;
} KnownReg;

#define SMC_DISPLAY_RANGE_LIMIT 8

typedef struct {
    u32 start;
    u32 end;
} SMCRange;

typedef struct {
    int possible;
    int allocation_failed;
    u32 range_count;
    u32 range_capacity;
    SMCRange* ranges;
} SMCAnalysis;

static int rpx_embedded_data_word(u32 raw) {
    return raw == 0x00400000u || raw == 0x00600000u;
}

static int word_bytes_are_text(u32 raw) {
    u8 bytes[4] = {
        (u8)(raw >> 24),
        (u8)(raw >> 16),
        (u8)(raw >> 8),
        (u8)raw,
    };

    int printable = 0;
    for (u32 i = 0; i < 4; i++) {
        if (bytes[i] >= 0x20 && bytes[i] <= 0x7Eu) {
            printable++;
        } else if (bytes[i] != 0) {
            return 0;
        }
    }

    return printable >= 3;
}

static int dol_embedded_data_word(u32 raw) {
    if (raw == 0)
        return 1;
    if ((raw >> 26) == 0)
        return 1;
    if (word_bytes_are_text(raw))
        return 1;
    return 0;
}

static int embedded_data_word(EmbeddedDataMode mode, u32 raw) {
    switch (mode) {
    case EMBEDDED_DATA_DOL:
        return dol_embedded_data_word(raw);
    case EMBEDDED_DATA_RPX:
        return rpx_embedded_data_word(raw);
    default:
        return 0;
    }
}

static void smc_analysis_free(SMCAnalysis* smc) {
    free(smc->ranges);
    smc->ranges = NULL;
    smc->range_count = 0;
    smc->range_capacity = 0;
}

static void smc_note(SMCAnalysis* smc, u32 addr) {
    smc->possible = 1;

    if (smc->range_count > 0) {
        SMCRange* last = &smc->ranges[smc->range_count - 1u];
        if (addr >= last->start && addr <= last->end + 4u) {
            if (addr > last->end)
                last->end = addr;
            return;
        }
    }

    if (smc->range_count >= smc->range_capacity) {
        u32 new_capacity = smc->range_capacity ? smc->range_capacity * 2u : 16u;
        SMCRange* new_ranges =
            (SMCRange*)realloc(smc->ranges, new_capacity * sizeof(SMCRange));
        if (!new_ranges) {
            smc->allocation_failed = 1;
            return;
        }
        smc->ranges = new_ranges;
        smc->range_capacity = new_capacity;
    }

    smc->ranges[smc->range_count].start = addr;
    smc->ranges[smc->range_count].end = addr;
    smc->range_count++;
}

static int write_smc_report(const SMCAnalysis* smc, const char* path) {
    FILE* report = fopen(path, "w");
    if (!report) {
        fprintf(stderr, "error: can't open output '%s'\n", path);
        return 0;
    }

    fprintf(report, "possible patching instructions:\n");
    for (u32 i = 0; i < smc->range_count; i++) {
        fprintf(report, "0x%08X-0x%08X\n",
                smc->ranges[i].start, smc->ranges[i].end);
    }

    if (fclose(report) != 0) {
        fprintf(stderr, "error: failed writing '%s'\n", path);
        return 0;
    }

    return 1;
}

static int code_range_overlaps(const LoadedCodeSection* sections,
                               u32 section_count, u32 address, u32 size) {
    u64 begin = address;
    u64 end = begin + (size ? size : 1u);

    for (u32 i = 0; i < section_count; i++) {
        const LoadedCodeSection* section = &sections[i];
        if (section->embedded_data_mode != EMBEDDED_DATA_DOL)
            continue;
        if (section->size == 0)
            continue;

        u64 section_begin = section->address;
        u64 section_end = section_begin + section->size;
        if (begin < section_end && end > section_begin)
            return 1;
    }

    return 0;
}

static int known_dform_ea(const KnownReg regs[32], const PPCInst* inst, u32* ea) {
    if (inst->rA == 0) {
        *ea = (u32)(s32)inst->simm;
        return 1;
    }
    if (!regs[inst->rA].known)
        return 0;
    *ea = regs[inst->rA].value + (u32)(s32)inst->simm;
    return 1;
}

static int known_indexed_ea(const KnownReg regs[32], const PPCInst* inst, u32* ea) {
    u32 base = 0;
    if (inst->rA != 0) {
        if (!regs[inst->rA].known)
            return 0;
        base = regs[inst->rA].value;
    }
    if (!regs[inst->rB].known)
        return 0;

    *ea = base + regs[inst->rB].value;
    return 1;
}

static int store_size_for_inst(const PPCInst* inst, u32* size) {
    switch (inst->op) {
    case PPC_OP_STB:
    case PPC_OP_STBU:
    case PPC_OP_STBX:
    case PPC_OP_STBUX:
        *size = 1;
        return 1;

    case PPC_OP_STH:
    case PPC_OP_STHU:
    case PPC_OP_STHX:
    case PPC_OP_STHUX:
    case PPC_OP_STHBRX:
        *size = 2;
        return 1;

    case PPC_OP_STW:
    case PPC_OP_STWU:
    case PPC_OP_STWX:
    case PPC_OP_STWUX:
    case PPC_OP_STWBRX:
    case PPC_OP_STWCX:
    case PPC_OP_STFIWX:
    case PPC_OP_STFS:
    case PPC_OP_STFSU:
    case PPC_OP_STFSX:
    case PPC_OP_STFSUX:
    case PPC_OP_PSQ_ST:
    case PPC_OP_PSQ_STU:
    case PPC_OP_PSQ_STX:
    case PPC_OP_PSQ_STUX:
        *size = 4;
        return 1;

    case PPC_OP_STFD:
    case PPC_OP_STFDU:
    case PPC_OP_STFDX:
    case PPC_OP_STFDUX:
        *size = 8;
        return 1;

    case PPC_OP_STMW:
        *size = (32u - inst->rS) * 4u;
        return 1;

    case PPC_OP_STSWI:
        *size = inst->nb ? inst->nb : 32u;
        return 1;

    case PPC_OP_STSWX:
        *size = 1;
        return 1;

    case PPC_OP_DCBZ:
    case PPC_OP_DCBZ_L:
        *size = 32;
        return 1;

    default:
        return 0;
    }
}

static int inst_has_dform_ea(PPCOpcode op) {
    switch (op) {
    case PPC_OP_STW:
    case PPC_OP_STWU:
    case PPC_OP_STB:
    case PPC_OP_STBU:
    case PPC_OP_STH:
    case PPC_OP_STHU:
    case PPC_OP_STMW:
    case PPC_OP_STSWI:
    case PPC_OP_STFS:
    case PPC_OP_STFSU:
    case PPC_OP_STFD:
    case PPC_OP_STFDU:
    case PPC_OP_PSQ_ST:
    case PPC_OP_PSQ_STU:
        return 1;
    default:
        return 0;
    }
}

static int inst_has_indexed_ea(PPCOpcode op) {
    switch (op) {
    case PPC_OP_STWX:
    case PPC_OP_STWUX:
    case PPC_OP_STBX:
    case PPC_OP_STBUX:
    case PPC_OP_STHX:
    case PPC_OP_STHUX:
    case PPC_OP_STWBRX:
    case PPC_OP_STHBRX:
    case PPC_OP_STSWX:
    case PPC_OP_STWCX:
    case PPC_OP_STFIWX:
    case PPC_OP_STFSX:
    case PPC_OP_STFSUX:
    case PPC_OP_STFDX:
    case PPC_OP_STFDUX:
    case PPC_OP_PSQ_STX:
    case PPC_OP_PSQ_STUX:
    case PPC_OP_DCBZ:
    case PPC_OP_DCBZ_L:
        return 1;
    default:
        return 0;
    }
}

static int smc_inst_targets_code(const LoadedCodeSection* sections,
                                 u32 section_count, const PPCInst* inst,
                                 const KnownReg regs[32]) {
    u32 ea = 0;
    u32 size = 0;

    if (!store_size_for_inst(inst, &size))
        return 0;

    if (inst_has_dform_ea(inst->op)) {
        if (!known_dform_ea(regs, inst, &ea))
            return 0;
    } else if (inst_has_indexed_ea(inst->op)) {
        if (!known_indexed_ea(regs, inst, &ea))
            return 0;
    } else {
        return 0;
    }

    return code_range_overlaps(sections, section_count, ea, size);
}

static void clear_reg(KnownReg regs[32], u8 reg) {
    regs[reg].known = 0;
    regs[reg].value = 0;
}

static void set_reg(KnownReg regs[32], u8 reg, u32 value) {
    regs[reg].known = 1;
    regs[reg].value = value;
}

static void update_known_regs(KnownReg regs[32], const PPCInst* inst) {
    u32 ea = 0;

    switch (inst->op) {
    case PPC_OP_ADDI:
        if (inst->rA == 0) {
            set_reg(regs, inst->rD, (u32)(s32)inst->simm);
        } else if (regs[inst->rA].known) {
            set_reg(regs, inst->rD, regs[inst->rA].value + (u32)(s32)inst->simm);
        } else {
            clear_reg(regs, inst->rD);
        }
        break;

    case PPC_OP_ADDIS:
        if (inst->rA == 0) {
            set_reg(regs, inst->rD, ((u32)inst->simm) << 16);
        } else if (regs[inst->rA].known) {
            set_reg(regs, inst->rD, regs[inst->rA].value + (((u32)inst->simm) << 16));
        } else {
            clear_reg(regs, inst->rD);
        }
        break;

    case PPC_OP_ORI:
        if (regs[inst->rS].known)
            set_reg(regs, inst->rA, regs[inst->rS].value | inst->uimm);
        else
            clear_reg(regs, inst->rA);
        break;

    case PPC_OP_ORIS:
        if (regs[inst->rS].known)
            set_reg(regs, inst->rA, regs[inst->rS].value | ((u32)inst->uimm << 16));
        else
            clear_reg(regs, inst->rA);
        break;

    case PPC_OP_XORI:
        if (regs[inst->rS].known)
            set_reg(regs, inst->rA, regs[inst->rS].value ^ inst->uimm);
        else
            clear_reg(regs, inst->rA);
        break;

    case PPC_OP_XORIS:
        if (regs[inst->rS].known)
            set_reg(regs, inst->rA, regs[inst->rS].value ^ ((u32)inst->uimm << 16));
        else
            clear_reg(regs, inst->rA);
        break;

    case PPC_OP_LWZ:
    case PPC_OP_LBZ:
    case PPC_OP_LHZ:
    case PPC_OP_LHA:
    case PPC_OP_LWZX:
    case PPC_OP_LBZX:
    case PPC_OP_LHZX:
    case PPC_OP_LHAX:
    case PPC_OP_LWBRX:
    case PPC_OP_LHBRX:
    case PPC_OP_LWARX:
        clear_reg(regs, inst->rD);
        break;

    case PPC_OP_LWZU:
    case PPC_OP_LBZU:
    case PPC_OP_LHZU:
    case PPC_OP_LHAU:
        if (known_dform_ea(regs, inst, &ea))
            set_reg(regs, inst->rA, ea);
        else
            clear_reg(regs, inst->rA);
        clear_reg(regs, inst->rD);
        break;

    case PPC_OP_LWZUX:
    case PPC_OP_LBZUX:
    case PPC_OP_LHZUX:
    case PPC_OP_LHAUX:
        if (known_indexed_ea(regs, inst, &ea))
            set_reg(regs, inst->rA, ea);
        else
            clear_reg(regs, inst->rA);
        clear_reg(regs, inst->rD);
        break;

    case PPC_OP_STWU:
    case PPC_OP_STBU:
    case PPC_OP_STHU:
        if (known_dform_ea(regs, inst, &ea))
            set_reg(regs, inst->rA, ea);
        else
            clear_reg(regs, inst->rA);
        break;

    case PPC_OP_STWUX:
    case PPC_OP_STBUX:
    case PPC_OP_STHUX:
    case PPC_OP_STFSUX:
    case PPC_OP_STFDUX:
    case PPC_OP_PSQ_STUX:
        if (known_indexed_ea(regs, inst, &ea))
            set_reg(regs, inst->rA, ea);
        else
            clear_reg(regs, inst->rA);
        break;

    case PPC_OP_MFCR:
    case PPC_OP_MFSPR:
    case PPC_OP_MFTB:
    case PPC_OP_MFMSR:
    case PPC_OP_MFSR:
    case PPC_OP_MFSRIN:
    case PPC_OP_MULLI:
    case PPC_OP_SUBFIC:
    case PPC_OP_ADD:
    case PPC_OP_ADDO:
    case PPC_OP_ADDC:
    case PPC_OP_ADDCO:
    case PPC_OP_ADDE:
    case PPC_OP_ADDEO:
    case PPC_OP_ADDME:
    case PPC_OP_ADDMEO:
    case PPC_OP_ADDZE:
    case PPC_OP_ADDZEO:
    case PPC_OP_SUBF:
    case PPC_OP_SUBFO:
    case PPC_OP_SUBFC:
    case PPC_OP_SUBFCO:
    case PPC_OP_SUBFE:
    case PPC_OP_SUBFEO:
    case PPC_OP_SUBFME:
    case PPC_OP_SUBFMEO:
    case PPC_OP_SUBFZE:
    case PPC_OP_SUBFZEO:
    case PPC_OP_NEG:
    case PPC_OP_NEGO:
    case PPC_OP_MULLW:
    case PPC_OP_MULLWO:
    case PPC_OP_MULHW:
    case PPC_OP_MULHWU:
    case PPC_OP_DIVW:
    case PPC_OP_DIVWO:
    case PPC_OP_DIVWU:
    case PPC_OP_DIVWUO:
        clear_reg(regs, inst->rD);
        break;

    case PPC_OP_AND:
    case PPC_OP_ANDC:
    case PPC_OP_OR:
    case PPC_OP_ORC:
    case PPC_OP_XOR:
    case PPC_OP_NAND:
    case PPC_OP_NOR:
    case PPC_OP_EQV:
    case PPC_OP_CNTLZW:
    case PPC_OP_EXTSB:
    case PPC_OP_EXTSH:
    case PPC_OP_SLW:
    case PPC_OP_SRW:
    case PPC_OP_SRAW:
    case PPC_OP_SRAWI:
    case PPC_OP_RLWINM:
    case PPC_OP_RLWNM:
    case PPC_OP_RLWIMI:
        clear_reg(regs, inst->rA);
        break;

    case PPC_OP_LMW:
        for (u32 r = inst->rD; r < 32; r++)
            clear_reg(regs, (u8)r);
        break;

    default:
        break;
    }
}

static void analyze_smc_section(const LoadedCodeSection* sections,
                                u32 section_count, const PPCInst* insts,
                                u32 count, SMCAnalysis* smc) {
    KnownReg regs[32];
    memset(regs, 0, sizeof(regs));

    for (u32 i = 0; i < count; i++) {
        const PPCInst* inst = &insts[i];
        if (inst->embedded_data || inst->op == PPC_OP_UNKNOWN)
            continue;

        if (inst->op == PPC_OP_ICBI ||
            smc_inst_targets_code(sections, section_count, inst, regs)) {
            smc_note(smc, inst->address);
        }

        update_known_regs(regs, inst);
    }
}

typedef struct {
    const char* input_path;
    const char* title_id_arg;
    const char* output_arg;
    DolRecompCPU cpu;
    u32 jobs;
    u32 rel_base;
    int gamecube_mode;
    int cpu_explicit;
    int rel_base_set;
    int setup_mode;
    int show_help;
} CliOptions;

static int parse_job_count(const char* text, u32* jobs) {
    char* end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value == 0 || value > 256) {
        fprintf(stderr, "error: job count must be 1..256\n");
        return 0;
    }

    *jobs = (u32)value;
    return 1;
}

static int parse_u32_arg(const char* text, const char* name, u32* value_out) {
    char* end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 0);
    if (errno != 0 || !end || *end != '\0' || value > 0xFFFFFFFFul) {
        fprintf(stderr, "error: %s must be a 32-bit address\n", name);
        return 0;
    }

    *value_out = (u32)value;
    return 1;
}

static int parse_cli(int argc, char** argv, CliOptions* opts) {
    const char* positional[3];
    int positional_count = 0;

    memset(opts, 0, sizeof(*opts));
    opts->cpu = DOLRECOMP_CPU_GEKKO;
    opts->jobs = 1;

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(argv[0]);
            opts->show_help = 1;
            return 1;
        }

        if (strcmp(arg, "--setup") == 0) {
            opts->setup_mode = 1;
            continue;
        }

        if (strcmp(arg, "--gamecube") == 0 || strcmp(arg, "-gc") == 0) {
            opts->gamecube_mode = 1;
            continue;
        }

        if (strcmp(arg, "--cpu") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --cpu needs gekko, broadway, or espresso\n");
                return 0;
            }
            if (!parse_cpu_name(argv[++i], &opts->cpu)) {
                fprintf(stderr, "error: unknown cpu '%s'\n", argv[i]);
                return 0;
            }
            opts->cpu_explicit = 1;
            continue;
        }

        if (strncmp(arg, "--cpu=", 6) == 0) {
            if (!parse_cpu_name(arg + 6, &opts->cpu)) {
                fprintf(stderr, "error: unknown cpu '%s'\n", arg + 6);
                return 0;
            }
            opts->cpu_explicit = 1;
            continue;
        }

        if (strcmp(arg, "--gekko") == 0) {
            opts->cpu = DOLRECOMP_CPU_GEKKO;
            opts->cpu_explicit = 1;
            continue;
        }

        if (strcmp(arg, "--broadway") == 0) {
            opts->cpu = DOLRECOMP_CPU_BROADWAY;
            opts->cpu_explicit = 1;
            continue;
        }

        if (strcmp(arg, "--espresso") == 0 || strcmp(arg, "--wiiu-cpu") == 0) {
            opts->cpu = DOLRECOMP_CPU_ESPRESSO;
            opts->cpu_explicit = 1;
            continue;
        }

        if (strcmp(arg, "-j") == 0 || strcmp(arg, "--jobs") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: -j needs a number\n");
                return 0;
            }
            if (!parse_job_count(argv[++i], &opts->jobs))
                return 0;
            continue;
        }

        if (strncmp(arg, "-j", 2) == 0 && arg[2] != '\0') {
            if (!parse_job_count(arg + 2, &opts->jobs))
                return 0;
            continue;
        }

        if (strncmp(arg, "--jobs=", 7) == 0) {
            if (!parse_job_count(arg + 7, &opts->jobs))
                return 0;
            continue;
        }

        if (strcmp(arg, "--rel-base") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --rel-base needs an address\n");
                return 0;
            }
            if (!parse_u32_arg(argv[++i], "--rel-base", &opts->rel_base))
                return 0;
            opts->rel_base_set = 1;
            continue;
        }

        if (strncmp(arg, "--rel-base=", 11) == 0) {
            if (!parse_u32_arg(arg + 11, "--rel-base", &opts->rel_base))
                return 0;
            opts->rel_base_set = 1;
            continue;
        }

        if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "error: unknown option '%s'\n", arg);
            return 0;
        }

        if (positional_count >= 3) {
            print_usage(argv[0]);
            return 0;
        }
        positional[positional_count++] = arg;
    }

    if (positional_count == 0) {
        if (opts->setup_mode)
            return 1;
        print_usage(argv[0]);
        return 0;
    }

    if (opts->setup_mode) {
        print_usage(argv[0]);
        return 0;
    }

    if (opts->gamecube_mode && opts->cpu == DOLRECOMP_CPU_ESPRESSO) {
        fprintf(stderr, "error: --gamecube cannot be used with espresso\n");
        return 0;
    }

    opts->input_path = positional[0];

    if (opts->gamecube_mode || opts->cpu == DOLRECOMP_CPU_ESPRESSO) {
        opts->title_id_arg = "generated";
        opts->output_arg = positional_count > 1 ? positional[1] : NULL;
        if (positional_count > 2) {
            print_usage(argv[0]);
            return 0;
        }
    } else {
        opts->title_id_arg = positional_count > 1 ? positional[1] : NULL;
        opts->output_arg = positional_count > 2 ? positional[2] : NULL;
    }

    return 1;
}

typedef struct {
    const PPCInst* insts;
    u32 count;
    u32 func_addr;
    char path[1200];
    char include_name[512];
} ChunkJob;

typedef struct {
    const ChunkJob* jobs;
    u32 job_count;
    u32 next_job;
    int failed;
#ifdef _WIN32
    CRITICAL_SECTION lock;
#else
    pthread_mutex_t lock;
#endif
} WorkerQueue;

static int emit_chunk_file(const ChunkJob* job) {
    FILE* chunk = fopen(job->path, "w");
    if (!chunk) {
        fprintf(stderr, "error: can't open output '%s'\n", job->path);
        return 0;
    }

    fprintf(chunk, "// DolRecomp output\n");
    fprintf(chunk, "#include \"../%s\"\n\n", job->include_name);
    emit_function(chunk, job->insts, job->count, job->func_addr);

    if (fclose(chunk) != 0) {
        fprintf(stderr, "error: failed writing '%s'\n", job->path);
        return 0;
    }

    return 1;
}

static int queue_init(WorkerQueue* queue, const ChunkJob* jobs, u32 job_count) {
    queue->jobs = jobs;
    queue->job_count = job_count;
    queue->next_job = 0;
    queue->failed = 0;
#ifdef _WIN32
    InitializeCriticalSection(&queue->lock);
    return 1;
#else
    return pthread_mutex_init(&queue->lock, NULL) == 0;
#endif
}

static void queue_destroy(WorkerQueue* queue) {
#ifdef _WIN32
    DeleteCriticalSection(&queue->lock);
#else
    pthread_mutex_destroy(&queue->lock);
#endif
}

static void queue_lock(WorkerQueue* queue) {
#ifdef _WIN32
    EnterCriticalSection(&queue->lock);
#else
    pthread_mutex_lock(&queue->lock);
#endif
}

static void queue_unlock(WorkerQueue* queue) {
#ifdef _WIN32
    LeaveCriticalSection(&queue->lock);
#else
    pthread_mutex_unlock(&queue->lock);
#endif
}

static int queue_take_job(WorkerQueue* queue, const ChunkJob** job) {
    int ok = 0;

    queue_lock(queue);
    if (!queue->failed && queue->next_job < queue->job_count) {
        *job = &queue->jobs[queue->next_job++];
        ok = 1;
    }
    queue_unlock(queue);

    return ok;
}

static void queue_mark_failed(WorkerQueue* queue) {
    queue_lock(queue);
    queue->failed = 1;
    queue_unlock(queue);
}

#ifdef _WIN32
static DWORD WINAPI chunk_worker_main(LPVOID arg) {
#else
static void* chunk_worker_main(void* arg) {
#endif
    WorkerQueue* queue = (WorkerQueue*)arg;
    const ChunkJob* job = NULL;

    while (queue_take_job(queue, &job)) {
        if (!emit_chunk_file(job)) {
            queue_mark_failed(queue);
            break;
        }
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static int run_chunk_jobs(const ChunkJob* jobs, u32 job_count, u32 requested_jobs) {
    if (job_count == 0)
        return 1;

    if (requested_jobs == 0)
        requested_jobs = 1;
    if (requested_jobs > job_count)
        requested_jobs = job_count;
#ifdef _WIN32
    if (requested_jobs > 64)
        requested_jobs = 64;
#endif

    if (requested_jobs == 1) {
        for (u32 i = 0; i < job_count; i++) {
            if (!emit_chunk_file(&jobs[i]))
                return 0;
        }
        return 1;
    }

    WorkerQueue queue;
    if (!queue_init(&queue, jobs, job_count)) {
        fprintf(stderr, "error: can't start worker queue\n");
        return 0;
    }

#ifdef _WIN32
    HANDLE* handles = (HANDLE*)calloc(requested_jobs, sizeof(HANDLE));
    if (!handles) {
        queue_destroy(&queue);
        fprintf(stderr, "error: out of memory\n");
        return 0;
    }

    u32 created = 0;
    for (; created < requested_jobs; created++) {
        handles[created] = CreateThread(NULL, 0, chunk_worker_main, &queue, 0, NULL);
        if (!handles[created]) {
            queue_mark_failed(&queue);
            fprintf(stderr, "error: can't start worker thread\n");
            break;
        }
    }

    if (created > 0)
        WaitForMultipleObjects(created, handles, TRUE, INFINITE);

    for (u32 i = 0; i < created; i++)
        CloseHandle(handles[i]);
    free(handles);
#else
    pthread_t* threads = (pthread_t*)calloc(requested_jobs, sizeof(pthread_t));
    if (!threads) {
        queue_destroy(&queue);
        fprintf(stderr, "error: out of memory\n");
        return 0;
    }

    u32 created = 0;
    for (; created < requested_jobs; created++) {
        if (pthread_create(&threads[created], NULL, chunk_worker_main, &queue) != 0) {
            queue_mark_failed(&queue);
            fprintf(stderr, "error: can't start worker thread\n");
            break;
        }
    }

    for (u32 i = 0; i < created; i++)
        pthread_join(threads[i], NULL);
    free(threads);
#endif

    int ok = !queue.failed;
    queue_destroy(&queue);
    return ok;
}

static u32 effective_chunk_jobs(u32 job_count, u32 requested_jobs) {
    if (job_count == 0)
        return 0;
    if (requested_jobs == 0)
        requested_jobs = 1;
    if (requested_jobs > job_count)
        requested_jobs = job_count;
#ifdef _WIN32
    if (requested_jobs > 64)
        requested_jobs = 64;
#endif
    return requested_jobs;
}

static int emit_code_sections_split(const LoadedCodeSection* sections,
                                    u32 section_count,
                                    const char* output_path,
                                    DolRecompCPU cpu, u32 entry_point, u32 jobs,
                                    int local_chunks_dir) {
    char stem[1024];
    char header_path[1100];
    char chunks_dir[1100];
    char chunks_label[512];
    char include_name[512];

    if (!make_output_stem(output_path, stem, sizeof(stem)))
        return 0;
    if (!split_include_name(stem, include_name, sizeof(include_name))) {
        fprintf(stderr, "error: output include name is too long\n");
        return 0;
    }

    if (snprintf(header_path, sizeof(header_path), "%s.h", stem) >= (int)sizeof(header_path)) {
        fprintf(stderr, "error: output path is too long\n");
        return 0;
    }

    if (local_chunks_dir) {
        char output_dir[1024];
        if (!path_dirname(output_path, output_dir, sizeof(output_dir)) ||
            !join_path(chunks_dir, sizeof(chunks_dir), output_dir, "chunks")) {
            fprintf(stderr, "error: output path is too long\n");
            return 0;
        }
        snprintf(chunks_label, sizeof(chunks_label), "chunks");
    } else {
        if (snprintf(chunks_dir, sizeof(chunks_dir), "%s_chunks", stem) >= (int)sizeof(chunks_dir)) {
            fprintf(stderr, "error: output path is too long\n");
            return 0;
        }
        snprintf(chunks_label, sizeof(chunks_label), "%s", path_basename(chunks_dir));
    }

    if (!make_dir_tree(chunks_dir))
        return 0;

    FILE* manifest = fopen(output_path, "w");
    if (!manifest) {
        fprintf(stderr, "error: can't open output '%s'\n", output_path);
        return 0;
    }

    FILE* header = fopen(header_path, "w");
    if (!header) {
        fprintf(stderr, "error: can't open output '%s'\n", header_path);
        fclose(manifest);
        return 0;
    }

    fprintf(manifest, "// DolRecomp split output\n");
    fprintf(manifest, "#include \"%s\"\n\n", include_name);
    fprintf(manifest, "// Build these C files too:\n");

    emit_header_for_cpu(header, cpu);
    fprintf(header, "\n// Function entry points\n");

    u32 file_count = 0;
    FunctionList funcs = {0};
    SMCAnalysis smc = {0};

    for (u32 s = 0; s < section_count; s++) {
        const LoadedCodeSection* section = &sections[s];
        if (section->size == 0 || !section->data) continue;

        const u8* section_data = section->data;
        u32 base_addr = section->address;
        u32 section_sz = section->size;
        u32 num_insts = section_sz / 4;

        if (section->name && section->name[0] != '\0') {
            printf("decoding %s[%u] %s: %u instructions at 0x%08X\n",
                   section->label, section->index, section->name, num_insts,
                   base_addr);
        } else {
            printf("decoding %s[%u]: %u instructions at 0x%08X\n",
                   section->label, section->index, num_insts, base_addr);
        }

        PPCInst* insts = (PPCInst*)malloc(num_insts * sizeof(PPCInst));
        if (!insts) {
            fprintf(stderr, "error: out of memory\n");
            smc_analysis_free(&smc);
            function_list_free(&funcs);
            fclose(header);
            fclose(manifest);
            return 0;
        }

        u32 decoded = 0, embedded = 0, unknown = 0;
        for (u32 i = 0; i < num_insts; i++) {
            u32 raw = read_be32(section_data + i * 4);
            u32 addr = base_addr + i * 4;
            insts[i] = ppc_decode(raw, addr);
            if (insts[i].op == PPC_OP_UNKNOWN &&
                embedded_data_word(section->embedded_data_mode, raw)) {
                insts[i].embedded_data = true;
            }
            decoded++;
            if (insts[i].embedded_data) {
                embedded++;
            } else if (insts[i].op == PPC_OP_UNKNOWN) {
                unknown++;
            }
        }

        if (embedded != 0) {
            printf("  %u decoded, %u known, %u embedded data, %u unknown\n",
                   decoded, decoded - embedded - unknown, embedded, unknown);
        } else {
            printf("  %u decoded, %u known, %u unknown\n",
                   decoded, decoded - unknown, unknown);
        }

        if (section->embedded_data_mode == EMBEDDED_DATA_DOL) {
            analyze_smc_section(sections, section_count, insts, num_insts, &smc);
            if (smc.allocation_failed) {
                fprintf(stderr, "error: out of memory\n");
                smc_analysis_free(&smc);
                function_list_free(&funcs);
                free(insts);
                fclose(header);
                fclose(manifest);
                return 0;
            }
        }

        u32 section_job_count =
            (num_insts + EMIT_CHUNK_INSTRUCTIONS - 1u) / EMIT_CHUNK_INSTRUCTIONS;
        ChunkJob* chunk_jobs = (ChunkJob*)calloc(section_job_count, sizeof(ChunkJob));
        if (!chunk_jobs) {
            fprintf(stderr, "error: out of memory\n");
            smc_analysis_free(&smc);
            function_list_free(&funcs);
            free(insts);
            fclose(header);
            fclose(manifest);
            return 0;
        }

        for (u32 start = 0; start < num_insts; start += EMIT_CHUNK_INSTRUCTIONS) {
            u32 chunk_count = num_insts - start;
            u32 func_addr = base_addr + start * 4u;
            char chunk_name[128];
            u32 job_index = start / EMIT_CHUNK_INSTRUCTIONS;

            if (chunk_count > EMIT_CHUNK_INSTRUCTIONS)
                chunk_count = EMIT_CHUNK_INSTRUCTIONS;

            if (snprintf(chunk_name, sizeof(chunk_name),
                         "chunk_%04u_%s%u_%08X.c", file_count,
                         section->label, section->index, func_addr) >=
                (int)sizeof(chunk_name)) {
                fprintf(stderr, "error: chunk name is too long\n");
                smc_analysis_free(&smc);
                function_list_free(&funcs);
                free(chunk_jobs);
                free(insts);
                fclose(header);
                fclose(manifest);
                return 0;
            }

            ChunkJob* job = &chunk_jobs[job_index];
            job->insts = insts + start;
            job->count = chunk_count;
            job->func_addr = func_addr;

            if (!join_path(job->path, sizeof(job->path), chunks_dir, chunk_name)) {
                fprintf(stderr, "error: chunk path is too long\n");
                smc_analysis_free(&smc);
                function_list_free(&funcs);
                free(chunk_jobs);
                free(insts);
                fclose(header);
                fclose(manifest);
                return 0;
            }

            if (snprintf(job->include_name, sizeof(job->include_name), "%s",
                         include_name) >= (int)sizeof(job->include_name)) {
                fprintf(stderr, "error: output include name is too long\n");
                smc_analysis_free(&smc);
                function_list_free(&funcs);
                free(chunk_jobs);
                free(insts);
                fclose(header);
                fclose(manifest);
                return 0;
            }

            emit_chunk_prototype(header, func_addr);
            if (!function_list_add(&funcs, func_addr, func_addr + chunk_count * 4u)) {
                smc_analysis_free(&smc);
                function_list_free(&funcs);
                free(chunk_jobs);
                free(insts);
                fclose(header);
                fclose(manifest);
                return 0;
            }
            fprintf(manifest, "// %s/%s\n", chunks_label, chunk_name);
            file_count++;
        }

        u32 active_jobs = effective_chunk_jobs(section_job_count, jobs);
        printf("  writing %u chunks with %u job%s\n",
               section_job_count, active_jobs, active_jobs == 1 ? "" : "s");
        if (!run_chunk_jobs(chunk_jobs, section_job_count, jobs)) {
            smc_analysis_free(&smc);
            function_list_free(&funcs);
            free(chunk_jobs);
            free(insts);
            fclose(header);
            fclose(manifest);
            return 0;
        }

        free(chunk_jobs);
        free(insts);
    }

    if (smc.possible) {
        u32 display_count = smc.range_count;
        char smc_report_path[1100];
        if (display_count > SMC_DISPLAY_RANGE_LIMIT)
            display_count = SMC_DISPLAY_RANGE_LIMIT;

        printf("warning: this DOL may patch executable memory at runtime. generated code many need additional patches\n");
        printf("  possible patching instructions:\n");
        for (u32 i = 0; i < display_count; i++) {
            printf("    0x%08X-0x%08X\n", smc.ranges[i].start, smc.ranges[i].end);
        }

        if (smc.range_count > SMC_DISPLAY_RANGE_LIMIT) {
            if (snprintf(smc_report_path, sizeof(smc_report_path), "%s_smc.txt", stem) >=
                (int)sizeof(smc_report_path)) {
                fprintf(stderr, "error: SMC report path is too long\n");
                smc_analysis_free(&smc);
                function_list_free(&funcs);
                fclose(header);
                fclose(manifest);
                return 0;
            }
            if (!write_smc_report(&smc, smc_report_path)) {
                smc_analysis_free(&smc);
                function_list_free(&funcs);
                fclose(header);
                fclose(manifest);
                return 0;
            }
            printf("    ...\n");
            printf("  full list: %s\n", smc_report_path);
        }
    }

    emit_dispatch_helpers(header, &funcs, entry_point);
    emit_footer(header);
    smc_analysis_free(&smc);
    function_list_free(&funcs);
    fprintf(manifest, "\n// %u C files\n", file_count);

    fclose(header);
    fclose(manifest);

    printf("done!\n");
    printf("  header: %s\n", header_path);
    printf("  chunks: %s (%u files)\n", chunks_dir, file_count);
    return 1;
}

static int emit_dol_split(const DOLFile* dol, const char* output_path,
                          DolRecompCPU cpu, u32 jobs, int local_chunks_dir) {
    LoadedCodeSection sections[DOL_NUM_TEXT];
    u32 section_count = 0;

    for (u32 i = 0; i < DOL_NUM_TEXT; i++) {
        if (dol->header.text_sizes[i] == 0)
            continue;

        const u8* data = dol_get_text_section(dol, (int)i);
        if (!data)
            continue;

        LoadedCodeSection* section = &sections[section_count++];
        section->label = "text";
        section->name = NULL;
        section->data = data;
        section->index = i;
        section->file_offset = dol->header.text_offsets[i];
        section->address = dol->header.text_addresses[i];
        section->size = dol->header.text_sizes[i];
        section->embedded_data_mode = EMBEDDED_DATA_DOL;
    }

    return emit_code_sections_split(sections, section_count, output_path, cpu,
                                    dol->header.entry_point, jobs,
                                    local_chunks_dir);
}

static int emit_rpx_split(const RPXFile* rpx, const char* output_path,
                          DolRecompCPU cpu, u32 jobs, int local_chunks_dir) {
    LoadedCodeSection sections[RPX_MAX_CODE_SECTIONS];

    for (u32 i = 0; i < rpx->code_section_count; i++) {
        const RPXCodeSection* code = &rpx->code_sections[i];
        LoadedCodeSection* section = &sections[i];
        section->label = "rpx";
        section->name = code->name;
        section->data = code->data;
        section->index = i;
        section->file_offset = code->offset;
        section->address = code->address;
        section->size = code->size;
        section->embedded_data_mode = EMBEDDED_DATA_RPX;
    }

    return emit_code_sections_split(sections, rpx->code_section_count,
                                    output_path, cpu, 0, jobs,
                                    local_chunks_dir);
}

static int emit_rel_split(const RELFile* rel, const char* output_path,
                          DolRecompCPU cpu, u32 jobs, int local_chunks_dir) {
    LoadedCodeSection* sections =
        (LoadedCodeSection*)calloc(rel->section_count, sizeof(LoadedCodeSection));
    if (!sections) {
        fprintf(stderr, "error: out of memory\n");
        return 0;
    }

    u32 section_count = 0;
    for (u32 i = 0; i < rel->section_count; i++) {
        const RELSection* rel_section = &rel->sections[i];
        if (!rel_section->executable || rel_section->size == 0 || !rel_section->data)
            continue;

        LoadedCodeSection* section = &sections[section_count++];
        section->label = "rel";
        section->name = NULL;
        section->data = rel_section->data;
        section->index = rel_section->index;
        section->file_offset = rel_section->offset;
        section->address = rel_section->address;
        section->size = rel_section->size;
        section->embedded_data_mode = EMBEDDED_DATA_DOL;
    }

    int ok = emit_code_sections_split(sections, section_count, output_path, cpu,
                                      rel->entry_point, jobs, local_chunks_dir);
    free(sections);
    return ok;
}

typedef struct {
    RELFile rel;
} RELBatchItem;

static void rel_batch_free(RELBatchItem* items, u32 count) {
    if (!items)
        return;
    for (u32 i = 0; i < count; i++)
        rel_free(&items[i].rel);
    free(items);
}

static u32 align_up_cli(u32 value, u32 alignment, int* ok) {
    u64 result = ((u64)value + alignment - 1u) / alignment * alignment;
    if (result > 0xFFFFFFFFu) {
        *ok = 0;
        return 0;
    }
    return (u32)result;
}

static int next_rel_base(const RELFile* rel, u32* cursor) {
    u32 end;
    int ok = 1;
    if (rel->file_size > 0xFFFFFFFFu - rel->base_address ||
        rel->bss_size > 0xFFFFFFFFu - rel->base_address - rel->file_size) {
        fprintf(stderr, "error: REL auto address range overflow\n");
        return 0;
    }

    end = rel->base_address + rel->file_size + rel->bss_size;
    *cursor = align_up_cli(end, REL_AUTO_ALIGN, &ok);
    if (!ok) {
        fprintf(stderr, "error: REL auto address range overflow\n");
        return 0;
    }
    return 1;
}

static int check_duplicate_rel_module(const RELBatchItem* items, u32 count,
                                      u32 module_id) {
    for (u32 i = 0; i < count; i++) {
        if (items[i].rel.module_id == module_id) {
            fprintf(stderr, "error: duplicate REL module id %u\n", module_id);
            return 0;
        }
    }
    return 1;
}

static int emit_rel_directory(const char* input_dir, const char* output_root,
                              const char* title_id, int titleless_mode,
                              DolRecompCPU cpu, u32 jobs, u32 start_base) {
    PathList paths = {0};
    RELBatchItem* items = NULL;
    RELModuleMapEntry* map_entries = NULL;
    char generated_root[1200];
    int ok = 0;
    u32 cursor = start_base;

    if (!collect_rel_paths(input_dir, &paths))
        goto done;
    path_list_sort(&paths);

    if (paths.count == 0) {
        fprintf(stderr, "error: no .rel files found in '%s'\n", input_dir);
        goto done;
    }

    items = (RELBatchItem*)calloc(paths.count, sizeof(*items));
    map_entries = (RELModuleMapEntry*)calloc(paths.count, sizeof(*map_entries));
    if (!items || !map_entries) {
        fprintf(stderr, "error: out of memory\n");
        goto done;
    }

    printf("found %u REL module%s\n", paths.count, paths.count == 1 ? "" : "s");
    for (u32 i = 0; i < paths.count; i++) {
        if (!rel_load_image(&items[i].rel, paths.paths[i], cursor))
            goto done;
        if (!check_duplicate_rel_module(items, i, items[i].rel.module_id))
            goto done;

        map_entries[i].module_id = items[i].rel.module_id;
        map_entries[i].rel = &items[i].rel;

        printf("  module %u: %s -> base 0x%08X\n",
               items[i].rel.module_id, paths.paths[i], items[i].rel.base_address);
        if (!next_rel_base(&items[i].rel, &cursor))
            goto done;
    }

    RELModuleMap map = { map_entries, paths.count };
    for (u32 i = 0; i < paths.count; i++) {
        if (!rel_apply_relocations(&items[i].rel, &map))
            goto done;
    }

    if (!build_generated_folder_path(output_root, title_id, titleless_mode,
                                     generated_root, sizeof(generated_root))) {
        goto done;
    }

    for (u32 i = 0; i < paths.count; i++) {
        char rel_output_path[1200];
        printf("\nREL %u/%u: %s\n", i + 1, paths.count, paths.paths[i]);
        rel_print_info(&items[i].rel, NULL);
        if (!build_rel_output_path(generated_root, paths.paths[i],
                                   items[i].rel.module_id,
                                   rel_output_path, sizeof(rel_output_path))) {
            goto done;
        }
        printf("\nwriting output to: %s\n", rel_output_path);
        if (!emit_rel_split(&items[i].rel, rel_output_path, cpu, jobs, 1))
            goto done;
    }

    ok = 1;

done:
    free(map_entries);
    rel_batch_free(items, paths.count);
    path_list_free(&paths);
    return ok;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "extract") == 0)
        return disc_extract_main(argc - 1, argv + 1);

    CliOptions opts;
    if (!parse_cli(argc, argv, &opts))
        return 1;
    if (opts.show_help)
        return 0;
    if (opts.setup_mode)
        return run_setup() ? 0 : 1;

    const char* input_path  = opts.input_path;
    const char* title_id_arg = opts.title_id_arg;
    const char* output_arg = opts.output_arg;
    char title_id[64];
    char game_name[256];
    char named_output_path[1200];
    const char* output_path = NULL;
    int local_chunks_dir = 0;
    int effective_gamecube_mode = opts.gamecube_mode;
    DolRecompCPU effective_cpu = opts.cpu;
    int titleless_mode = effective_gamecube_mode || effective_cpu == DOLRECOMP_CPU_ESPRESSO;
    int espresso_rpx_mode = effective_cpu == DOLRECOMP_CPU_ESPRESSO;
    int input_is_directory = path_is_directory(input_path);
    int rel_mode = has_rel_extension(input_path) || input_is_directory;
    u32 rel_start_base = opts.rel_base_set ? opts.rel_base : REL_AUTO_BASE;

    title_id[0] = '\0';
    game_name[0] = '\0';

    if (has_rpx_extension(input_path) && effective_cpu != DOLRECOMP_CPU_ESPRESSO) {
        fprintf(stderr, "error: .rpx input requires --cpu espresso\n");
        return 1;
    }
    if (rel_mode && effective_cpu == DOLRECOMP_CPU_ESPRESSO) {
        fprintf(stderr, "error: .rel input cannot use espresso mode\n");
        return 1;
    }
    if (!titleless_mode && !database_titles_available()) {
        print_database_missing_notice();
        effective_gamecube_mode = 1;
        titleless_mode = 1;
        if (!output_arg && title_id_arg && !is_title_id_length_valid(title_id_arg))
            output_arg = title_id_arg;
        title_id_arg = NULL;
    }

    if (!titleless_mode) {
        if (!title_id_arg) {
            fprintf(stderr, "title id missing! specify gamecube mode if working with a gamecube dol with --gamecube\n");
            return 1;
        }
        if (!is_title_id_length_valid(title_id_arg)) {
            fprintf(stderr, "title id length is invalid\n");
            return 1;
        }
        if (!is_title_id(title_id_arg)) {
            fprintf(stderr, "error: title id must contain only letters/numbers, like SUKE01\n");
            return 1;
        }
    }

    if (!opts.cpu_explicit)
        effective_cpu = effective_gamecube_mode ? DOLRECOMP_CPU_GEKKO : DOLRECOMP_CPU_BROADWAY;
    espresso_rpx_mode = effective_cpu == DOLRECOMP_CPU_ESPRESSO;

    if (effective_gamecube_mode) {
        snprintf(game_name, sizeof(game_name), rel_mode ? "GameCube REL" : "GameCube DOL");
    } else if (effective_cpu == DOLRECOMP_CPU_ESPRESSO) {
        snprintf(game_name, sizeof(game_name), "Wii U executable");
    } else {
        copy_title_id(title_id, sizeof(title_id), title_id_arg);
        describe_game(game_name, sizeof(game_name), title_id, 0);
    }

    if (espresso_rpx_mode) {
        if (!has_rpx_extension(input_path)) {
            fprintf(stderr, "error: espresso mode expects an .rpx input\n");
            return 1;
        }

        RPXFile rpx;
        if (!rpx_load(&rpx, input_path))
            return 1;

        rpx_print_info(&rpx, game_name);
        printf("cpu: %s\n", cpu_display_name(effective_cpu));

        if (!output_arg) {
            printf("\ngenerating code...\n");
            output_arg = ".";
        }

        if (has_c_extension(output_arg)) {
            output_path = output_arg;
        } else {
            if (!build_gamecube_output_path(output_arg, named_output_path,
                                            sizeof(named_output_path))) {
                rpx_free(&rpx);
                return 1;
            }
            output_path = named_output_path;
            local_chunks_dir = 1;
        }

        printf("\nwriting output to: %s\n", output_path);
        if (!emit_rpx_split(&rpx, output_path, effective_cpu, opts.jobs,
                            local_chunks_dir)) {
            rpx_free(&rpx);
            return 1;
        }

        rpx_free(&rpx);
        return 0;
    }

    if (input_is_directory) {
        if (has_c_extension(output_arg ? output_arg : "")) {
            fprintf(stderr, "error: REL directory output must be a directory\n");
            return 1;
        }
        if (!output_arg) {
            printf("\ngenerating code...\n");
            output_arg = ".";
        }

        printf("REL base start: 0x%08X\n", rel_start_base);
        if (!emit_rel_directory(input_path, output_arg, title_id,
                                titleless_mode, effective_cpu, opts.jobs,
                                rel_start_base)) {
            return 1;
        }
        return 0;
    }

    if (rel_mode) {
        RELFile rel;
        if (!rel_load(&rel, input_path, rel_start_base))
            return 1;

        rel_print_info(&rel, game_name);
        printf("cpu: %s\n", cpu_display_name(effective_cpu));

        if (!output_arg) {
            printf("\ngenerating code...\n");
            output_arg = ".";
        }

        if (has_c_extension(output_arg)) {
            output_path = output_arg;
        } else if (titleless_mode) {
            if (!build_gamecube_output_path(output_arg, named_output_path,
                                            sizeof(named_output_path))) {
                rel_free(&rel);
                return 1;
            }
            output_path = named_output_path;
            local_chunks_dir = 1;
        } else {
            if (!build_named_output_path(output_arg, title_id,
                                         named_output_path, sizeof(named_output_path))) {
                rel_free(&rel);
                return 1;
            }
            output_path = named_output_path;
            local_chunks_dir = 1;
        }

        printf("\nwriting output to: %s\n", output_path);
        if (!emit_rel_split(&rel, output_path, effective_cpu, opts.jobs,
                            local_chunks_dir)) {
            rel_free(&rel);
            return 1;
        }

        rel_free(&rel);
        return 0;
    }

    DOLFile dol;
    if (!dol_load(&dol, input_path))
        return 1;
    dol_print_info(&dol, game_name);
    printf("cpu: %s\n", cpu_display_name(effective_cpu));

    if (!output_arg) {
        printf("\ngenerating code...\n");
        output_arg = ".";
    }

    if (has_c_extension(output_arg)) {
        output_path = output_arg;
    } else if (titleless_mode) {
        if (!build_gamecube_output_path(output_arg, named_output_path,
                                        sizeof(named_output_path))) {
            dol_free(&dol);
            return 1;
        }
        output_path = named_output_path;
        local_chunks_dir = 1;
    } else {
        if (!build_named_output_path(output_arg, title_id,
                                     named_output_path, sizeof(named_output_path))) {
            dol_free(&dol);
            return 1;
        }
        output_path = named_output_path;
        local_chunks_dir = 1;
    }

    printf("\nwriting output to: %s\n", output_path);
    if (!emit_dol_split(&dol, output_path, effective_cpu, opts.jobs, local_chunks_dir)) {
        dol_free(&dol);
        return 1;
    }

    dol_free(&dol);
    return 0;
}
