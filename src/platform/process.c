#include "platform/process.h"
#include "platform/strutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

void sleep_ms(u32 milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep((useconds_t)milliseconds * 1000u);
#endif
}

char* shell_quote_arg(const char* arg) {
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

int run_shell_command(const char* command) {
    return system(command) == 0;
}

int command_exists_quiet(const char* command_name) {
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

int prompt_yes_no(const char* prompt) {
    char line[32];
    printf("%s", prompt);
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin))
        return 0;
    return line[0] == 'y' || line[0] == 'Y';
}

int download_file(const char* url, const char* output_path) {
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

