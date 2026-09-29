#ifndef DOLRECOMP_NATIVE_MODULE_H
#define DOLRECOMP_NATIVE_MODULE_H

#include "backend/dispatch.h"

int emit_native_module(FILE* out, const FunctionList* functions,
                       const char* game_id, const u32* entries,
                       u32 entry_count);

#endif
