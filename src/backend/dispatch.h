#ifndef DOLRECOMP_BACKEND_DISPATCH_H
#define DOLRECOMP_BACKEND_DISPATCH_H

#include "common/types.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/* uses_fast_memory: the generated bodies were emitted with the fast guest
   memory lowering, so the module needs the runtime guard that checks that
   mode's assumptions. Only the LLVM backends lower memory that way; the C
   backend reads its bounds from CPUState unconditionally and must not carry a
   guard, or lockstep verification against it would refuse to run natively for
   assumptions its code never made. */
void emit_dispatch_helpers(FILE* out, const FunctionList* funcs, u32 entry_point,
                           int uses_fast_memory);

#ifdef __cplusplus
}
#endif

#endif
