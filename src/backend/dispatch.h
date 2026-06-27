#ifndef DOLRECOMP_BACKEND_DISPATCH_H
#define DOLRECOMP_BACKEND_DISPATCH_H

#include "common/types.h"
#include <stdio.h>

typedef struct {
    u32 start;
    u32 end;
} FunctionRange;

typedef struct {
    FunctionRange* ranges;
    u32 count;
    u32 capacity;
} FunctionList;

void emit_chunk_prototype(FILE* out, u32 func_addr);
void function_list_free(FunctionList* list);
int function_list_add(FunctionList* list, u32 start, u32 end);
void emit_dispatch_helpers(FILE* out, const FunctionList* funcs, u32 entry_point);

#endif
