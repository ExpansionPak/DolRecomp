#include "app/database.h"
#include "platform/fs.h"
#include "platform/strutil.h"
#include "platform/process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

#define GAMETDB_TITLES_URL "https://www.gametdb.com/titles.txt?LANG=EN"

int parse_gametdb_title_line(const char* line, const char* title_id,
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

int resolve_game_name_from_stream(FILE* stream, const char* title_id,
                                         char* out, size_t out_size) {
    char line[1024];

    while (fgets(line, sizeof(line), stream)) {
        if (parse_gametdb_title_line(line, title_id, out, out_size))
            return 1;
    }

    return 0;
}

int resolve_game_name_from_file(const char* path, const char* title_id,
                                       char* out, size_t out_size) {
    FILE* file = fopen(path, "r");
    if (!file)
        return 0;

    int found = resolve_game_name_from_stream(file, title_id, out, out_size);
    fclose(file);
    return found;
}

int resolve_game_name(const char* title_id, char* out, size_t out_size) {
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

void describe_game(char* out, size_t out_size, const char* title_id,
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

int database_path(char* out, size_t out_size, const char* name) {
    return join_path(out, out_size, DATABASE_DIR, name);
}

int database_titles_path(char* out, size_t out_size) {
    return database_path(out, out_size, DATABASE_TITLES_FILE);
}

int database_titles_available(void) {
    char path[1024];
    return database_titles_path(path, sizeof(path)) && file_exists(path);
}

void print_database_missing_notice(void) {
    fprintf(stderr,
            "database/titles.txt is missing; using GameCube mode. Run dolrecomp --setup to download it.\n");
}

int download_titles_database(const char* output_path) {
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

