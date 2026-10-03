#include "backend/native_module.h"
#include "backend/native_state.h"
#include "ir/dolir.h"
#include <stdlib.h>

static u64 persistent_state_mask(u32 word) {
    u64 mask = 0;
    for (u32 slot = DOLIR_STATE_GPR0; slot <= DOLIR_STATE_PS1_31; slot++)
        if (slot / 64u == word)
            mask |= UINT64_C(1) << (slot & 63u);
    return mask;
}

static const DolLLVMFunctionRange* find_llvm_range(
    const DolLLVMFunctionRange* ranges, u32 range_count, u32 start) {
    u32 first = 0, last = range_count;
    while (first < last) {
        u32 middle = first + (last - first) / 2u;
        if (ranges[middle].start < start)
            first = middle + 1u;
        else
            last = middle;
    }
    return first < range_count && ranges[first].start == start ? &ranges[first] : NULL;
}

static const DolLLVMPatch* find_patch(const DolLLVMPatch* patches,
                                             u32 patch_count, u32 start) {
    for (u32 i = 0; i < patch_count; i++)
        if (patches[i].start == start)
            return &patches[i];
    return NULL;
}

static void emit_patch_adapters(FILE* out, const DolLLVMPatch* patches,
                                u32 patch_count) {
    if (!patch_count)
        return;
    fprintf(out,
            "typedef MGNativeExit (*DolRecompPatchFn)(\n"
            "    const MGNativeRuntime*, const MGNativeState*, uint32_t, uint32_t, uint32_t);\n\n");
    for (u32 i = 0; i < patch_count; i++) {
        const DolLLVMPatch* patch = &patches[i];
        fprintf(out,
                "extern MGNativeExit %s(const MGNativeRuntime*, const MGNativeState*, "
                "uint32_t, uint32_t, uint32_t);\n"
                "MGNativeExit func_%08X(const MGNativeRuntime* runtime, "
                "const MGNativeState* state, uint64_t* values, uint64_t* dirty, "
                "uint64_t* valid, uint32_t entry_pc, uint32_t cycle_budget, "
                "uint32_t cycle_base) {\n"
                "    if (entry_pc != 0x%08Xu) {\n"
                "        MGNativeExit miss = {MG_NATIVE_EXIT_FALLBACK, entry_pc, "
                "entry_pc + 4u, 0u, 0u, 0u, 0u};\n"
                "        return miss;\n"
                "    }\n"
                "    int staged_dirty = 0;\n"
                "    for (uint32_t word = 0; word < MODERNGEKKO_NATIVE_STATE_MASK_WORDS; word++)\n"
                "        staged_dirty |= dirty[word] != 0u;\n"
                "    if (staged_dirty) moderngekko_commit_state(state, values, dirty);\n"
                "    for (uint32_t word = 0; word < MODERNGEKKO_NATIVE_STATE_MASK_WORDS; word++) {\n"
                "        dirty[word] = 0u; valid[word] = 0u;\n"
                "    }\n"
                "    if (runtime->should_intercept &&\n"
                "        runtime->should_intercept(runtime->context, entry_pc)) {\n"
                "        MGNativeExit intercepted = {MG_NATIVE_EXIT_INTERCEPT, entry_pc,\n"
                "            entry_pc + 4u, 0u, 0u, 0u, 0u};\n"
                "        return intercepted;\n"
                "    }\n"
                "    return %s(runtime, state, entry_pc, cycle_budget, cycle_base);\n"
                "}\n\n",
                patch->symbol, patch->start, patch->start, patch->symbol);
    }
}

static int emit_ranges(FILE* out, const FunctionList* functions,
                       const DolLLVMFunctionRange* ranges, u32 range_count,
                       const DolLLVMPatch* patches, u32 patch_count) {
    fprintf(out,
            "\ntypedef MGNativeExit (*ModernGekkoNativeEntry)(\n"
            "    const MGNativeRuntime*, const MGNativeState*, uint64_t*, uint64_t*,\n"
            "    uint64_t*, uint32_t, uint32_t, uint32_t);\n"
            "typedef struct {\n"
            "    uint32_t start;\n"
            "    uint32_t end;\n"
            "    uint64_t hash;\n"
            "    uint64_t input_mask[MODERNGEKKO_NATIVE_STATE_MASK_WORDS];\n"
            "    ModernGekkoNativeEntry entry;\n"
            "} ModernGekkoNativeRange;\n\n"
            "static const ModernGekkoNativeRange moderngekko_native_ranges[] = {\n");
    for (u32 i = 0; i < functions->count; i++) {
        const FunctionRange* range = &functions->ranges[i];
        const DolLLVMFunctionRange* llvm_range =
            find_llvm_range(ranges, range_count, range->start);
        const DolLLVMPatch* patch =
            find_patch(patches, patch_count, range->start);
        if (!llvm_range)
            return 0;
        fprintf(out, "    {0x%08Xu, 0x%08Xu, UINT64_C(0x%016llX), {",
                range->start, range->end, (unsigned long long)range->hash);
        for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
            u64 input = patch ? 0u :
                (llvm_range->input_state[word] & persistent_state_mask(word));
            fprintf(out, "UINT64_C(0x%016llX)%s", (unsigned long long)input,
                    word + 1u == DOLIR_STATE_MASK_WORDS ? "" : ", ");
        }
        fprintf(out, "}, func_%08X},\n", range->start);
    }
    if (!functions->count)
        fprintf(out, "    {0u, 0u, 0u, {0}, 0},\n");
    fprintf(out,
            "};\n"
            "enum { MODERNGEKKO_NATIVE_DIRTY, MODERNGEKKO_NATIVE_VALID, "
            "MODERNGEKKO_NATIVE_CHANGED };\n"
            "static _Atomic uint32_t moderngekko_native_state[%uu];\n"
            "static _Atomic uint32_t moderngekko_native_unavailable;\n"
            "static _Atomic uint32_t moderngekko_native_needs_validation;\n\n",
            functions->count ? functions->count : 1u);
    return 1;
}

static void emit_staged_state_layout(FILE* out) {
    fprintf(out,
            "enum {\n"
            "    MODERNGEKKO_NATIVE_STATE_COUNT = %uu,\n"
            "    MODERNGEKKO_NATIVE_STATE_MASK_WORDS = %uu\n"
            "};\n\n",
            DOLIR_STATE_COUNT, DOLIR_STATE_MASK_WORDS);
    fprintf(out, "static const uint64_t moderngekko_native_persistent_mask[] = {");
    for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
        fprintf(out, "%sUINT64_C(0x%016llX)", word ? ", " : "",
                (unsigned long long)persistent_state_mask(word));
    fprintf(out, "};\n\n");
}

static void emit_lookup(FILE* out, u32 count) {
    fprintf(out,
            "#define MODERNGEKKO_NATIVE_LOOKUP_SIZE 4096u\n"
            "static _Atomic uint64_t moderngekko_native_lookup[\n"
            "    MODERNGEKKO_NATIVE_LOOKUP_SIZE];\n\n"
            "static uint32_t moderngekko_native_find(uint32_t address) {\n"
            "    uint32_t slot = (address >> 2u) &\n"
            "        (MODERNGEKKO_NATIVE_LOOKUP_SIZE - 1u);\n"
            "    uint64_t cached = atomic_load_explicit(\n"
            "        &moderngekko_native_lookup[slot], memory_order_relaxed);\n"
            "    uint32_t cached_index = (uint32_t)cached;\n"
            "    if ((uint32_t)(cached >> 32u) == address && cached_index != 0u)\n"
            "        return cached_index - 1u;\n"
            "    uint32_t first = 0;\n"
            "    uint32_t count = %uu;\n"
            "    while (first < count) {\n"
            "        uint32_t middle = first + (count - first) / 2u;\n"
            "        const ModernGekkoNativeRange* range = &moderngekko_native_ranges[middle];\n"
            "        if (address < range->start) count = middle;\n"
            "        else if (address >= range->end) first = middle + 1u;\n"
            "        else {\n"
            "            atomic_store_explicit(&moderngekko_native_lookup[slot],\n"
            "                ((uint64_t)address << 32u) | (uint64_t)(middle + 1u),\n"
            "                memory_order_relaxed);\n"
            "            return middle;\n"
            "        }\n"
            "    }\n"
            "    return UINT32_MAX;\n"
            "}\n\n",
            count);
}

static int emit_entry_map(FILE* out, const FunctionList* functions,
                          const u32* entries, u32 entry_count) {
    u64 total_slots = 0;
    for (u32 i = 0; i < functions->count; i++)
        total_slots +=
            (functions->ranges[i].end - functions->ranges[i].start) / 4u;
    if (total_slots > UINT32_MAX)
        return 0;
    u32 slot_count = (u32)total_slots;
    u32 word_count = slot_count / 64u + ((slot_count & 63u) != 0u);
    u64* bits = (u64*)calloc(word_count ? word_count : 1u, sizeof(*bits));
    if (!bits)
        return 0;
    u32 entry_index = 0;
    u32 base = 0;

    fprintf(out, "static const uint32_t moderngekko_native_entry_offsets[] = {\n");
    for (u32 range_index = 0; range_index < functions->count; range_index++) {
        const FunctionRange* range = &functions->ranges[range_index];
        if ((range_index & 7u) == 0)
            fprintf(out, "    ");
        fprintf(out, "%uu%s", base,
                range_index + 1u == functions->count ? "" : ", ");
        if ((range_index & 7u) == 7u || range_index + 1u == functions->count)
            fprintf(out, "\n");
        while (entry_index < entry_count && entries[entry_index] < range->end) {
            u32 address = entries[entry_index++];
            if (address >= range->start && ((address - range->start) & 3u) == 0) {
                u32 bit = base + (address - range->start) / 4u;
                bits[bit / 64u] |= UINT64_C(1) << (bit & 63u);
            }
        }
        base += (range->end - range->start) / 4u;
    }
    if (!functions->count)
        fprintf(out, "    0u,\n");
    fprintf(out, "};\nstatic const uint64_t moderngekko_native_entry_bits[] = {\n");
    if (!word_count) {
        fprintf(out, "    UINT64_C(0),\n");
    } else {
        for (u32 i = 0; i < word_count; i++) {
            if ((i & 3u) == 0)
                fprintf(out, "    ");
            fprintf(out, "UINT64_C(0x%016llX)%s",
                    (unsigned long long)bits[i], i + 1u == word_count ? "" : ", ");
            if ((i & 3u) == 3u || i + 1u == word_count)
                fprintf(out, "\n");
        }
    }
    free(bits);
    fprintf(out,
            "};\n"
            "static int moderngekko_native_supports_entry(uint32_t address) {\n"
            "    uint32_t index = moderngekko_native_find(address);\n"
            "    if (index == UINT32_MAX) return 0;\n"
            "    const ModernGekkoNativeRange* range = &moderngekko_native_ranges[index];\n"
            "    if ((address - range->start) & 3u) return 0;\n"
            "    uint32_t bit = moderngekko_native_entry_offsets[index] +\n"
            "        (address - range->start) / 4u;\n"
            "    return (moderngekko_native_entry_bits[bit / 64u] >>\n"
            "            (bit & 63u)) & 1u;\n"
            "}\n\n");
    return 1;
}

static void emit_validation(FILE* out, u32 count) {
    fprintf(out,
            "static const uint8_t* moderngekko_native_bytes(\n"
            "    const MGNativeRuntime* runtime, const ModernGekkoNativeRange* range) {\n"
            "    uint32_t size = range->end - range->start;\n"
            "    if (range->start >= 0x80000000u &&\n"
            "        range->start - 0x80000000u <= runtime->mem1_size &&\n"
            "        size <= runtime->mem1_size - (range->start - 0x80000000u))\n"
            "        return runtime->mem1 + (range->start - 0x80000000u);\n"
            "    if (range->start >= 0x90000000u &&\n"
            "        range->start - 0x90000000u <= runtime->mem2_size &&\n"
            "        size <= runtime->mem2_size - (range->start - 0x90000000u))\n"
            "        return runtime->mem2 + (range->start - 0x90000000u);\n"
            "    return 0;\n"
            "}\n\n"
            "static uint64_t moderngekko_native_hash(const uint8_t* bytes, uint32_t size) {\n"
            "    uint64_t hash = UINT64_C(0xCBF29CE484222325);\n"
            "    while (size--) { hash ^= *bytes++; hash *= UINT64_C(0x100000001B3); }\n"
            "    return hash;\n"
            "}\n\n"
            "static int moderngekko_native_validate_index(\n"
            "    const MGNativeRuntime* runtime, uint32_t index) {\n"
            "    uint32_t observed = atomic_load_explicit(\n"
            "        &moderngekko_native_state[index], memory_order_acquire);\n"
            "    for (;;) {\n"
            "        uint32_t status = observed & 3u;\n"
            "        if (status == MODERNGEKKO_NATIVE_VALID) return 1;\n"
            "        if (status == MODERNGEKKO_NATIVE_CHANGED) return 0;\n"
            "        const ModernGekkoNativeRange* range = &moderngekko_native_ranges[index];\n"
            "        const uint8_t* bytes = moderngekko_native_bytes(runtime, range);\n"
            "        int matches = bytes && moderngekko_native_hash(\n"
            "            bytes, range->end - range->start) == range->hash;\n"
            "        uint32_t desired = (observed & ~3u) |\n"
            "            (matches ? MODERNGEKKO_NATIVE_VALID : MODERNGEKKO_NATIVE_CHANGED);\n"
            "        if (atomic_compare_exchange_weak_explicit(\n"
            "                &moderngekko_native_state[index], &observed, desired,\n"
            "                memory_order_acq_rel, memory_order_acquire)) {\n"
            "            if (matches) atomic_fetch_sub_explicit(\n"
            "                &moderngekko_native_unavailable, 1u, memory_order_release);\n"
            "            return matches;\n"
            "        }\n"
            "    }\n"
            "}\n\n"
            "bool moderngekko_native_region_available(\n"
            "    const MGNativeRuntime* runtime, uint32_t start, uint32_t end) {\n"
            "    if (!atomic_load_explicit(&moderngekko_native_unavailable, memory_order_acquire))\n"
            "        return true;\n"
            "    uint32_t index = moderngekko_native_find(start);\n"
            "    if (index == UINT32_MAX) return false;\n"
            "    while (index < %uu && moderngekko_native_ranges[index].start < end) {\n"
            "        if (!moderngekko_native_validate_index(runtime, index)) return false;\n"
            "        index++;\n"
            "    }\n"
            "    return true;\n"
            "}\n\n",
            count);
}

static void emit_entry(FILE* out, u32 count) {
    fprintf(out,
            "static void moderngekko_native_validate_all(\n"
            "    const MGNativeRuntime* runtime) {\n"
            "    if (!atomic_exchange_explicit(&moderngekko_native_needs_validation, 0u,\n"
            "                                  memory_order_acq_rel))\n"
            "        return;\n"
            "    for (uint32_t index = 0; index < %uu; index++)\n"
            "        moderngekko_native_validate_index(runtime, index);\n"
            "}\n\n"
            "static int moderngekko_native_contains(uint32_t address) {\n"
            "    return moderngekko_native_find(address) != UINT32_MAX;\n"
            "}\n\n"
            "static int moderngekko_native_validate(\n"
            "    const MGNativeRuntime* runtime, uint32_t address) {\n"
            "    moderngekko_native_validate_all(runtime);\n"
            "    uint32_t index = moderngekko_native_find(address);\n"
            "    return index != UINT32_MAX && moderngekko_native_validate_index(runtime, index);\n"
            "}\n\n"
            "static uint32_t moderngekko_native_add_sat(uint32_t a, uint32_t b) {\n"
            "    return UINT32_MAX - a < b ? UINT32_MAX : a + b;\n"
            "}\n\n"
            "static void moderngekko_native_prepare_state(\n"
            "    const MGNativeState* state, uint64_t* values, uint64_t* dirty,\n"
            "    uint64_t* valid, const uint64_t* required) {\n"
            "    uint64_t missing[MODERNGEKKO_NATIVE_STATE_MASK_WORDS];\n"
            "    int any_missing = 0;\n"
            "    for (uint32_t word = 0; word < MODERNGEKKO_NATIVE_STATE_MASK_WORDS; word++) {\n"
            "        missing[word] = required[word] & ~(dirty[word] | valid[word]);\n"
            "        any_missing |= missing[word] != 0u;\n"
            "    }\n"
            "    if (!any_missing) return;\n"
            "    moderngekko_reload_state(state, values, missing);\n"
            "    for (uint32_t word = 0; word < MODERNGEKKO_NATIVE_STATE_MASK_WORDS; word++)\n"
            "        valid[word] |= missing[word];\n"
            "}\n\n"
            "static void moderngekko_native_retain_persistent_dirty(uint64_t* dirty) {\n"
            "    for (uint32_t word = 0; word < MODERNGEKKO_NATIVE_STATE_MASK_WORDS; word++)\n"
            "        dirty[word] &= moderngekko_native_persistent_mask[word];\n"
            "}\n\n"
            "static MGNativeExit moderngekko_native_run(\n"
            "    const MGNativeRuntime* runtime, const MGNativeState* state,\n"
            "    uint32_t entry_pc, uint32_t cycle_budget) {\n"
            "    moderngekko_native_validate_all(runtime);\n"
            "    uint32_t index = moderngekko_native_find(entry_pc);\n"
            "    if (index == UINT32_MAX || !moderngekko_native_validate_index(runtime, index)) {\n"
            "        MGNativeExit exit = {MG_NATIVE_EXIT_INVALIDATED_CODE, entry_pc,\n"
            "            entry_pc + 4u, 0u, 0u, 0u, 0u};\n"
            "        return exit;\n"
            "    }\n"
            "    uint32_t prior_cycles = 0u;\n"
            "    uint32_t prior_instructions = 0u;\n"
            "    uint32_t prior_load_store = 0u;\n"
            "    uint32_t prior_fp = 0u;\n"
            "    uint32_t remaining = cycle_budget;\n"
            "    uint64_t state_values[MODERNGEKKO_NATIVE_STATE_COUNT] = {0};\n"
            "    uint64_t dirty_mask[MODERNGEKKO_NATIVE_STATE_MASK_WORDS] = {0};\n"
            "    uint64_t valid_mask[MODERNGEKKO_NATIVE_STATE_MASK_WORDS] = {0};\n"
            "    for (;;) {\n"
            "        const ModernGekkoNativeRange* range = &moderngekko_native_ranges[index];\n"
            "        moderngekko_native_prepare_state(\n"
            "            state, state_values, dirty_mask, valid_mask, range->input_mask);\n"
            "        MGNativeExit exit = range->entry(\n"
            "            runtime, state, state_values, dirty_mask, valid_mask,\n"
            "            entry_pc, remaining, prior_cycles);\n"
            "        moderngekko_native_retain_persistent_dirty(dirty_mask);\n"
            "        uint32_t local_cycles = exit.cycles;\n"
            "        int chain = exit.reason == MG_NATIVE_EXIT_BUDGET &&\n"
            "            local_cycles < remaining && (local_cycles || exit.instructions);\n"
            "        uint32_t next_index = chain ? moderngekko_native_find(exit.pc) : UINT32_MAX;\n"
            "        if (next_index == UINT32_MAX ||\n"
            "            !moderngekko_native_supports_entry(exit.pc) ||\n"
            "            !moderngekko_native_validate_index(runtime, next_index))\n"
            "            chain = 0;\n"
            "        if (!chain) {\n"
            "            int staged_dirty = 0;\n"
            "            for (uint32_t word = 0;\n"
            "                 word < MODERNGEKKO_NATIVE_STATE_MASK_WORDS; word++)\n"
            "                staged_dirty |= dirty_mask[word] != 0u;\n"
            "            if (staged_dirty)\n"
            "                moderngekko_commit_state(state, state_values, dirty_mask);\n"
            "            exit.cycles = moderngekko_native_add_sat(prior_cycles, exit.cycles);\n"
            "            exit.instructions = moderngekko_native_add_sat(\n"
            "                prior_instructions, exit.instructions);\n"
            "            exit.load_store_instructions = moderngekko_native_add_sat(\n"
            "                prior_load_store, exit.load_store_instructions);\n"
            "            exit.fp_instructions = moderngekko_native_add_sat(\n"
            "                prior_fp, exit.fp_instructions);\n"
            "            return exit;\n"
            "        }\n"
            "        prior_cycles += local_cycles;\n"
            "        prior_instructions = moderngekko_native_add_sat(\n"
            "            prior_instructions, exit.instructions);\n"
            "        prior_load_store = moderngekko_native_add_sat(\n"
            "            prior_load_store, exit.load_store_instructions);\n"
            "        prior_fp = moderngekko_native_add_sat(prior_fp, exit.fp_instructions);\n"
            "        remaining -= local_cycles;\n"

            "        entry_pc = exit.pc;\n"
            "        index = next_index;\n"
            "    }\n"
            "}\n\n",
            count);
}

static void emit_invalidation(FILE* out, u32 count) {
    fprintf(out,
            "static void moderngekko_native_invalidate(uint32_t address, uint32_t size) {\n"
            "    uint32_t end = UINT32_MAX - address < size ? UINT32_MAX : address + size;\n"
            "    uint32_t first = 0;\n"
            "    uint32_t last = %uu;\n"
            "    while (first < last) {\n"
            "        uint32_t middle = first + (last - first) / 2u;\n"
            "        if (moderngekko_native_ranges[middle].end <= address)\n"
            "            first = middle + 1u;\n"
            "        else\n"
            "            last = middle;\n"
            "    }\n"
            "    for (uint32_t index = first; index < %uu; index++) {\n"
            "        const ModernGekkoNativeRange* range = &moderngekko_native_ranges[index];\n"
            "        if (range->start >= end) break;\n"
            "        uint32_t observed = atomic_load_explicit(\n"
            "            &moderngekko_native_state[index], memory_order_acquire);\n"
            "        uint32_t desired;\n"
            "        do {\n"
            "            desired = ((observed & ~3u) + 4u) | MODERNGEKKO_NATIVE_DIRTY;\n"
            "        } while (!atomic_compare_exchange_weak_explicit(\n"
            "            &moderngekko_native_state[index], &observed, desired,\n"
            "            memory_order_acq_rel, memory_order_acquire));\n"
            "        if ((observed & 3u) == MODERNGEKKO_NATIVE_VALID)\n"
            "            atomic_fetch_add_explicit(\n"
            "                &moderngekko_native_unavailable, 1u, memory_order_release);\n"
            "    }\n"
            "}\n\n"
            "static void moderngekko_native_reset(void) {\n"
            "    for (uint32_t index = 0; index < %uu; index++)\n"
            "        atomic_store_explicit(&moderngekko_native_state[index],\n"
            "                              MODERNGEKKO_NATIVE_DIRTY, memory_order_relaxed);\n"
            "    atomic_store_explicit(&moderngekko_native_unavailable, %uu,\n"
            "                          memory_order_relaxed);\n"
            "    atomic_store_explicit(&moderngekko_native_needs_validation, 1u,\n"
            "                          memory_order_release);\n"
            "}\n\n",
            count, count, count, count);
}

int emit_native_module(FILE* out, const FunctionList* functions,
                       const DolLLVMFunctionRange* ranges, u32 range_count,
                       const char* game_id, const u32* entries,
                       u32 entry_count, const DolLLVMPatch* patches,
                       u32 patch_count) {
    emit_native_state_commit(out);
    emit_staged_state_layout(out);
    emit_patch_adapters(out, patches, patch_count);
    if (!emit_ranges(out, functions, ranges, range_count, patches, patch_count))
        return 0;
    emit_lookup(out, functions->count);
    if (!emit_entry_map(out, functions, entries, entry_count))
        return 0;
    emit_validation(out, functions->count);
    emit_entry(out, functions->count);
    emit_invalidation(out, functions->count);
    fprintf(out,
            "static const MGNativeModule moderngekko_native_module = {\n"
            "    MG_NATIVE_ABI_VERSION, sizeof(MGNativeModule),\n"
            "    \"DolRecomp %s\", \"%s\",\n"
            "    moderngekko_native_contains, moderngekko_native_validate,\n"
            "    moderngekko_native_run, moderngekko_native_invalidate,\n"
            "    moderngekko_native_reset, moderngekko_native_supports_entry,\n"
            "};\n\n"
            "MG_NATIVE_EXPORT const MGNativeModule* moderngekko_get_native_module(\n"
            "    uint32_t runtime_abi_version) {\n"
            "    return runtime_abi_version == MG_NATIVE_ABI_VERSION\n"
            "               ? &moderngekko_native_module : 0;\n"
            "}\n",
            game_id, game_id);
    return 1;
}
