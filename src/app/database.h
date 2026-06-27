#ifndef DOLRECOMP_APP_DATABASE_H
#define DOLRECOMP_APP_DATABASE_H

#include <stddef.h>

#define DATABASE_DIR "database"
#define DATABASE_TITLES_FILE "titles.txt"
#define DATABASE_SETUP_FLAG ".setup_done.flag"

int database_path(char* out, size_t out_size, const char* name);
int database_titles_path(char* out, size_t out_size);
int database_titles_available(void);
void print_database_missing_notice(void);
int download_titles_database(const char* output_path);
int resolve_game_name(const char* title_id, char* out, size_t out_size);
void describe_game(char* out, size_t out_size, const char* title_id,
                   int gamecube_mode);

#endif
