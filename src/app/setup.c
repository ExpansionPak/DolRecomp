#include "app/setup.h"
#include "app/database.h"
#include "platform/fs.h"
#include "platform/process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

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

int write_setup_flag(const char* path) {
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

int local_wit_path(char* out, size_t out_size) {
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

int wit_tools_available(void) {
    char path[1024];
    if (local_wit_path(path, sizeof(path)))
        return 1;
    return command_exists_quiet("wit");
}

#ifdef _WIN32
void write_ps_quoted(FILE* file, const char* text) {
    fputc('\'', file);
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\'')
            fputs("''", file);
        else
            fputc(text[i], file);
    }
    fputc('\'', file);
}

int write_wit_install_script(const char* script_path,
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

int extract_wit_archive(const char* archive_path) {
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
int extract_wit_archive(const char* archive_path) {
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

int install_wit_tools(void) {
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

int setup_wit_tools(void) {
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

int run_setup(void) {
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

