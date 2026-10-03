#ifndef DOLRECOMP_NATIVE_MODULE_H
#define DOLRECOMP_NATIVE_MODULE_H

#include "backend/dispatch.h"
#include "backend/llvm/llvm_backend.h"

int emit_native_module(FILE* out, const FunctionList* functions,
                       const DolLLVMFunctionRange* ranges, u32 range_count,
                       const char* game_id, const u32* entries,
                       u32 entry_count, const DolLLVMPatch* patches,
                       u32 patch_count);

#endif
