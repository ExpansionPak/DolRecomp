#ifndef DOLRECOMP_PLATFORM_PROCESS_H
#define DOLRECOMP_PLATFORM_PROCESS_H

#include "common/types.h"

void sleep_ms(u32 milliseconds);
char* shell_quote_arg(const char* arg);
int run_shell_command(const char* command);
int command_exists_quiet(const char* command_name);
int prompt_yes_no(const char* prompt);
int download_file(const char* url, const char* output_path);

#endif
