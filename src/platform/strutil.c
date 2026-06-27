#include "platform/strutil.h"
#include "platform/fs.h"
#include <stdlib.h>
#include <string.h>

int ascii_lower(int ch) {
    if (ch >= 'A' && ch <= 'Z')
        return ch + ('a' - 'A');
    return ch;
}

int ascii_upper(int ch) {
    if (ch >= 'a' && ch <= 'z')
        return ch - ('a' - 'A');
    return ch;
}

int ascii_case_equal(const char* a, const char* b) {
    while (*a && *b) {
        if (ascii_lower((unsigned char)*a) != ascii_lower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int has_c_extension(const char* path) {
    const char* base = path_basename(path);
    const char* dot = strrchr(base, '.');
    return dot && ascii_case_equal(dot, ".c");
}

int has_rpx_extension(const char* path) {
    const char* base = path_basename(path);
    const char* dot = strrchr(base, '.');
    return dot && ascii_case_equal(dot, ".rpx");
}

int has_rel_extension(const char* path) {
    const char* base = path_basename(path);
    const char* dot = strrchr(base, '.');
    return dot && ascii_case_equal(dot, ".rel");
}

const char* skip_spaces(const char* text) {
    while (*text == ' ' || *text == '\t')
        text++;
    return text;
}

void copy_trimmed(char* out, size_t out_size, const char* start,
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

char* copy_string_alloc(const char* text) {
    size_t len = strlen(text);
    char* copy = (char*)malloc(len + 1);
    if (!copy)
        return NULL;
    memcpy(copy, text, len + 1);
    return copy;
}

