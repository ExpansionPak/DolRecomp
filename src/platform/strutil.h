#ifndef DOLRECOMP_PLATFORM_STRUTIL_H
#define DOLRECOMP_PLATFORM_STRUTIL_H

#include <stddef.h>

int ascii_lower(int ch);
int ascii_upper(int ch);
int ascii_case_equal(const char* a, const char* b);
int has_c_extension(const char* path);
int has_rpx_extension(const char* path);
int has_rel_extension(const char* path);
const char* skip_spaces(const char* text);
void copy_trimmed(char* out, size_t out_size, const char* start, const char* end);
char* copy_string_alloc(const char* text);

#endif
