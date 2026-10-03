#ifndef DOLRECOMP_PATCH_ABI_H
#define DOLRECOMP_PATCH_ABI_H

#include "backend/patch_version.h"
#include "Core/PowerPC/Native/NativeModuleABI.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef MGNativeExit (*DolRecompPatchFn)(
    const MGNativeRuntime* runtime, const MGNativeState* state,
    uint32_t entry_pc, uint32_t cycle_budget, uint32_t cycle_base);

#define DOLRECOMP_PATCH(name) \
    MGNativeExit name(const MGNativeRuntime* runtime, const MGNativeState* state, \
                      uint32_t entry_pc, uint32_t cycle_budget, uint32_t cycle_base)

#ifdef __cplusplus
}
#endif

#endif
