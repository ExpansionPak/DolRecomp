#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #x); return 1; } } while (0)

enum {
    MG_NATIVE_ABI_VERSION = 9,
    MG_NATIVE_EXIT_BUDGET = 0,
    MG_NATIVE_EXIT_EXCEPTION = 1,
    MG_NATIVE_EXIT_FALLBACK = 2,
    MG_NATIVE_EXIT_INTERCEPT = 3,
    MG_NATIVE_EXIT_INVALIDATED_CODE = 4,
    MG_NATIVE_EXIT_STOP = 5,
    MG_NATIVE_SERVICE_CONTINUE = 0,
    MG_NATIVE_SERVICE_EXCEPTION = 1,
    MG_NATIVE_SERVICE_YIELD = 2,
    MG_NATIVE_SERVICE_FALLBACK = 3,
    MG_NATIVE_SERVICE_STOP = 4,
};

typedef struct MGNativeExit {
    uint32_t reason;
    uint32_t pc;
    uint32_t next_pc;
    uint32_t cycles;
    uint32_t instructions;
    uint32_t load_store_instructions;
    uint32_t fp_instructions;
} MGNativeExit;

typedef struct MGNativeState {
    uint32_t abi_version;
    uint32_t struct_size;
    void* context;
    uint32_t (*read_gpr)(void*, uint32_t);
    void (*write_gpr)(void*, uint32_t, uint32_t);
    uint64_t (*read_fpr)(void*, uint32_t, uint32_t);
    void (*write_fpr)(void*, uint32_t, uint32_t, uint64_t);
    uint32_t (*read_cr)(void*);
    void (*write_cr)(void*, uint32_t);
    uint32_t (*read_xer)(void*);
    void (*write_xer)(void*, uint32_t);
    uint32_t (*read_spr)(void*, uint32_t);
    void (*write_spr)(void*, uint32_t, uint32_t);
    uint32_t (*read_sr)(void*, uint32_t);
    void (*write_sr)(void*, uint32_t, uint32_t);
    uint32_t (*read_fpscr)(void*);
    void (*write_fpscr)(void*, uint32_t);
    uint32_t (*read_msr)(void*);
    void (*write_msr)(void*, uint32_t);
    uint32_t (*read_exceptions)(void*);
    void (*write_exceptions)(void*, uint32_t);
    uint32_t (*read_reserve_address)(void*);
    void (*write_reserve_address)(void*, uint32_t);
    uint32_t (*read_reserve_valid)(void*);
    void (*write_reserve_valid)(void*, uint32_t);
    uint32_t (*try_write_registers)(const struct MGNativeState*, const uint64_t*,
                                    const uint64_t*, const uint64_t*, uint32_t,
                                    uint32_t, uint32_t);
    uint32_t (*try_read_registers)(const struct MGNativeState*, uint64_t*,
                                   uint64_t*, uint64_t*, uint32_t, uint32_t,
                                   uint32_t);
} MGNativeState;

typedef struct MGNativeMemoryResult {
    uint64_t value;
    uint32_t status;
    uint32_t remaining_cycles;
} MGNativeMemoryResult;

typedef struct MGNativeServices {
    uint32_t abi_version;
    uint32_t struct_size;
    void* context;
    MGNativeMemoryResult (*read_memory)(void*, uint32_t, uint32_t, uint32_t,
                                        uint64_t);
    MGNativeMemoryResult (*write_memory)(void*, uint32_t, uint32_t, uint64_t,
                                         uint32_t, uint64_t);
    uint64_t (*read_timebase)(void*, uint64_t);
    uint32_t (*cache_control)(void*, uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t (*execute_instruction)(void*, uint32_t);
    uint32_t (*try_write_fifo)(void*, uint64_t, uint32_t);
    uint32_t (*continue_native)(void*, uint64_t, uint32_t);
    uint32_t (*try_write_msr)(void*, uint32_t, uint32_t, uint64_t);
    uint32_t (*execute_paired)(void*, uint32_t, uint32_t);
    uint32_t (*execute_paired_partial)(void*, uint32_t, uint32_t);
    MGNativeMemoryResult (*write_fifo_partial)(void*, uint32_t, uint32_t,
                                               uint64_t, uint32_t, uint64_t);
    uint32_t (*execute_scalar_partial)(void*, uint32_t, uint32_t);
} MGNativeServices;

typedef struct MGNativeRuntime {
    uint32_t abi_version;
    uint32_t struct_size;
    void* context;
    uint8_t* mem1;
    uint32_t mem1_size;
    uint32_t mem1_mask;
    uint8_t* mem2;
    uint32_t mem2_size;
    uint32_t mem2_mask;
    uint8_t* locked_cache;
    uint32_t locked_cache_size;
    const MGNativeServices* services;
    uint32_t (*should_intercept)(void*, uint32_t);
} MGNativeRuntime;

typedef struct TestContext {
    uint32_t gpr[32];
    uint64_t ps[32][2];
    uint32_t lr;
    uint32_t gpr_read_calls;
    uint32_t gpr3_read_calls;
    uint32_t gpr_write_calls;
    uint32_t fpr_read_calls;
    uint32_t fpr_write_calls;
    uint32_t bulk_read_calls;
    uint32_t bulk_write_calls;
    uint32_t bulk_accept;
    uint32_t bulk_read_gpr_mask;
    uint32_t bulk_read_ps0_mask;
    uint32_t bulk_read_ps1_mask;
    uint32_t bulk_write_gpr_mask;
    uint32_t bulk_write_ps0_mask;
    uint32_t bulk_write_ps1_mask;
    uint32_t read_calls;
    uint32_t write_calls;
    uint32_t fifo_try_calls;
    uint32_t fifo_partial_calls;
    uint32_t instruction_calls;
    uint32_t instruction_status;
    uint32_t msr;
    uint32_t service_status;
    uint32_t write_status;
    uint32_t fifo_partial_status;
    uint32_t read_remaining_cycles;
    uint32_t remaining_cycles;
    uint32_t fifo_try_accept;
    uint32_t try_observed_gpr3;
    uint32_t partial_observed_gpr3;
    uint32_t last_pc;
    uint32_t last_address;
    uint32_t last_size;
    uint64_t last_elapsed;
    uint64_t last_value;
    uint64_t partial_elapsed;
    uint64_t full_elapsed;
    uint32_t intercept_pc;
    uint32_t intercept_calls;
} TestContext;

static uint32_t should_intercept(void* opaque, uint32_t guest_pc)
{
    TestContext* context = (TestContext*)opaque;
    context->intercept_calls++;
    return guest_pc == context->intercept_pc;
}

MGNativeExit raw_func_80003120(const MGNativeRuntime*, const MGNativeState*,
                           uint64_t*, uint64_t*, uint64_t*,
                           uint32_t, uint32_t, uint32_t) __asm__("func_80003120");
MGNativeExit raw_func_80003140(const MGNativeRuntime*, const MGNativeState*,
                           uint64_t*, uint64_t*, uint64_t*,
                           uint32_t, uint32_t, uint32_t) __asm__("func_80003140");
MGNativeExit raw_func_80003500(const MGNativeRuntime*, const MGNativeState*,
                           uint64_t*, uint64_t*, uint64_t*,
                           uint32_t, uint32_t, uint32_t) __asm__("func_80003500");
MGNativeExit raw_func_80003D40(const MGNativeRuntime*, const MGNativeState*,
                           uint64_t*, uint64_t*, uint64_t*,
                           uint32_t, uint32_t, uint32_t) __asm__("func_80003D40");
MGNativeExit raw_func_80003DA0(const MGNativeRuntime*, const MGNativeState*,
                           uint64_t*, uint64_t*, uint64_t*,
                           uint32_t, uint32_t, uint32_t) __asm__("func_80003DA0");
MGNativeExit raw_func_80004A00(const MGNativeRuntime*, const MGNativeState*,
                           uint64_t*, uint64_t*, uint64_t*,
                           uint32_t, uint32_t, uint32_t) __asm__("func_80004A00");
MGNativeExit raw_func_80004E00(const MGNativeRuntime*, const MGNativeState*,
                           uint64_t*, uint64_t*, uint64_t*,
                           uint32_t, uint32_t, uint32_t) __asm__("func_80004E00");
MGNativeExit raw_func_80005000(const MGNativeRuntime*, const MGNativeState*,
                           uint64_t*, uint64_t*, uint64_t*,
                           uint32_t, uint32_t, uint32_t) __asm__("func_80005000");
MGNativeExit raw_func_80004D00(const MGNativeRuntime*, const MGNativeState*,
                           uint64_t*, uint64_t*, uint64_t*,
                           uint32_t, uint32_t, uint32_t) __asm__("func_80004D00");
MGNativeExit raw_func_80004D20(const MGNativeRuntime*, const MGNativeState*,
                           uint64_t*, uint64_t*, uint64_t*,
                           uint32_t, uint32_t, uint32_t) __asm__("func_80004D20");
MGNativeExit raw_func_80004D40(const MGNativeRuntime*, const MGNativeState*,
                           uint64_t*, uint64_t*, uint64_t*,
                           uint32_t, uint32_t, uint32_t) __asm__("func_80004D40");

static uint64_t staged_values[256];
static uint64_t staged_dirty[4];
static uint64_t staged_valid[4];

void moderngekko_commit_state(const MGNativeState* state, const uint64_t* values,
                              const uint64_t* mask);
void moderngekko_reload_state(const MGNativeState* state, uint64_t* values,
                              const uint64_t* mask);

static void reset_staged_state(void) {
    memset(staged_values, 0, sizeof(staged_values));
    memset(staged_dirty, 0, sizeof(staged_dirty));
    memset(staged_valid, 0, sizeof(staged_valid));
}

static void prepare_staged_gprs(const MGNativeState* state, uint32_t gpr_mask) {
    uint64_t mask[4] = {gpr_mask, 0u, 0u, 0u};
    moderngekko_reload_state(state, staged_values, mask);
    staged_valid[0] |= gpr_mask;
}

typedef MGNativeExit (*TestNativeEntry)(const MGNativeRuntime*,
                                       const MGNativeState*, uint64_t*,
                                       uint64_t*, uint64_t*, uint32_t,
                                       uint32_t, uint32_t);

static MGNativeExit run_native_once(TestNativeEntry fn,
                                    const MGNativeRuntime* runtime,
                                    const MGNativeState* state, uint32_t pc,
                                    uint32_t budget, uint32_t base) {
    reset_staged_state();
    uint32_t gpr_mask = 0u;
    if (fn == raw_func_80003120 || fn == raw_func_80003DA0)
        gpr_mask = 1u << 4;
    else if (fn == raw_func_80003140)
        gpr_mask = 1u << 3;
    else if (fn == raw_func_80003500 || fn == raw_func_80004A00 ||
             fn == raw_func_80005000)
        gpr_mask = (1u << 3) | (1u << 4);
    else if (fn == raw_func_80003D40)
        gpr_mask = 0x1f8u;
    else if (fn == raw_func_80004D20 || fn == raw_func_80004D40)
        gpr_mask = 1u << 5;
    if (gpr_mask)
        prepare_staged_gprs(state, gpr_mask);
    MGNativeExit exit = fn(runtime, state, staged_values, staged_dirty,
                           staged_valid, pc, budget, base);
    moderngekko_commit_state(state, staged_values, staged_dirty);
    return exit;
}

#define RUN_NATIVE(fn, runtime, state, pc, budget, base) \
    run_native_once(fn, runtime, state, pc, budget, base)

#define func_80003120(runtime, state, pc, budget, base) \
    RUN_NATIVE(raw_func_80003120, runtime, state, pc, budget, base)
#define func_80003140(runtime, state, pc, budget, base) \
    RUN_NATIVE(raw_func_80003140, runtime, state, pc, budget, base)
#define func_80003500(runtime, state, pc, budget, base) \
    RUN_NATIVE(raw_func_80003500, runtime, state, pc, budget, base)
#define func_80003D40(runtime, state, pc, budget, base) \
    RUN_NATIVE(raw_func_80003D40, runtime, state, pc, budget, base)
#define func_80003DA0(runtime, state, pc, budget, base) \
    RUN_NATIVE(raw_func_80003DA0, runtime, state, pc, budget, base)
#define func_80004A00(runtime, state, pc, budget, base) \
    RUN_NATIVE(raw_func_80004A00, runtime, state, pc, budget, base)
#define func_80004E00(runtime, state, pc, budget, base) \
    RUN_NATIVE(raw_func_80004E00, runtime, state, pc, budget, base)
#define func_80005000(runtime, state, pc, budget, base) \
    RUN_NATIVE(raw_func_80005000, runtime, state, pc, budget, base)
#define func_80004D00(runtime, state, pc, budget, base) \
    RUN_NATIVE(raw_func_80004D00, runtime, state, pc, budget, base)
#define func_80004D20(runtime, state, pc, budget, base) \
    RUN_NATIVE(raw_func_80004D20, runtime, state, pc, budget, base)
#define func_80004D40(runtime, state, pc, budget, base) \
    RUN_NATIVE(raw_func_80004D40, runtime, state, pc, budget, base)

static uint32_t read_gpr(void* opaque, uint32_t index) {
    TestContext* context = (TestContext*)opaque;
    context->gpr_read_calls++;
    if (index == 3u)
        context->gpr3_read_calls++;
    return context->gpr[index];
}

static void write_gpr(void* opaque, uint32_t index, uint32_t value) {
    TestContext* context = (TestContext*)opaque;
    context->gpr_write_calls++;
    context->gpr[index] = value;
}

static uint64_t read_fpr(void* opaque, uint32_t index, uint32_t lane) {
    TestContext* context = (TestContext*)opaque;
    context->fpr_read_calls++;
    return context->ps[index][lane];
}

static void write_fpr(void* opaque, uint32_t index, uint32_t lane, uint64_t value) {
    TestContext* context = (TestContext*)opaque;
    context->fpr_write_calls++;
    context->ps[index][lane] = value;
}

static uint32_t try_write_registers(const MGNativeState* state,
                                    const uint64_t* gpr, const uint64_t* ps0,
                                    const uint64_t* ps1, uint32_t gpr_mask,
                                    uint32_t ps0_mask, uint32_t ps1_mask) {
    TestContext* context = (TestContext*)state->context;
    context->bulk_write_calls++;
    context->bulk_write_gpr_mask = gpr_mask;
    context->bulk_write_ps0_mask = ps0_mask;
    context->bulk_write_ps1_mask = ps1_mask;
    if (!context->bulk_accept)
        return 0u;
    for (uint32_t reg = 0; reg < 32u; reg++) {
        uint32_t bit = 1u << reg;
        if (gpr_mask & bit)
            context->gpr[reg] = (uint32_t)gpr[reg];
        if (ps0_mask & bit)
            context->ps[reg][0] = ps0[reg];
        if (ps1_mask & bit)
            context->ps[reg][1] = ps1[reg];
    }
    return 1u;
}

static uint32_t try_read_registers(const MGNativeState* state, uint64_t* gpr,
                                   uint64_t* ps0, uint64_t* ps1,
                                   uint32_t gpr_mask, uint32_t ps0_mask,
                                   uint32_t ps1_mask) {
    TestContext* context = (TestContext*)state->context;
    context->bulk_read_calls++;
    context->bulk_read_gpr_mask = gpr_mask;
    context->bulk_read_ps0_mask = ps0_mask;
    context->bulk_read_ps1_mask = ps1_mask;
    if (!context->bulk_accept)
        return 0u;
    for (uint32_t reg = 0; reg < 32u; reg++) {
        uint32_t bit = 1u << reg;
        if (gpr_mask & bit)
            gpr[reg] = context->gpr[reg];
        if (ps0_mask & bit)
            ps0[reg] = context->ps[reg][0];
        if (ps1_mask & bit)
            ps1[reg] = context->ps[reg][1];
    }
    return 1u;
}

static uint32_t read_zero(void* opaque) {
    (void)opaque;
    return 0;
}

static void write_ignore(void* opaque, uint32_t value) {
    (void)opaque;
    (void)value;
}

static uint32_t read_msr(void* opaque) {
    return ((TestContext*)opaque)->msr;
}

static void write_msr(void* opaque, uint32_t value) {
    ((TestContext*)opaque)->msr = value;
}

static uint32_t read_spr(void* opaque, uint32_t index) {
    TestContext* context = (TestContext*)opaque;
    return index == 8u ? context->lr : 0u;
}

static void write_spr(void* opaque, uint32_t index, uint32_t value) {
    if (index == 8u)
        ((TestContext*)opaque)->lr = value;
}

static uint32_t read_indexed_zero(void* opaque, uint32_t index) {
    (void)opaque;
    (void)index;
    return 0;
}

static void write_indexed_ignore(void* opaque, uint32_t index, uint32_t value) {
    (void)opaque;
    (void)index;
    (void)value;
}

static uint32_t execute_instruction(void* opaque, uint32_t pc) {
    TestContext* context = (TestContext*)opaque;
    context->instruction_calls++;
    context->last_pc = pc;
    if (context->instruction_status != MG_NATIVE_SERVICE_FALLBACK)
        context->gpr[4] += 5u;
    return context->instruction_status;
}

static MGNativeMemoryResult read_memory(void* opaque, uint32_t pc,
                                        uint32_t address, uint32_t size,
                                        uint64_t elapsed) {
    TestContext* context = (TestContext*)opaque;
    context->read_calls++;
    context->last_pc = pc;
    context->last_address = address;
    context->last_size = size;
    context->last_elapsed = elapsed;
    MGNativeMemoryResult result = {
        0xA1B2C3D4u,
        context->service_status,
        context->service_status == MG_NATIVE_SERVICE_CONTINUE
            ? (context->read_remaining_cycles ? context->read_remaining_cycles : 1u)
            : 0u,
    };
    return result;
}

static MGNativeMemoryResult write_memory(void* opaque, uint32_t pc,
                                         uint32_t address, uint64_t value,
                                         uint32_t size, uint64_t elapsed) {
    TestContext* context = (TestContext*)opaque;
    context->write_calls++;
    context->last_pc = pc;
    context->last_address = address;
    context->last_size = size;
    context->last_value = value;
    context->last_elapsed = elapsed;
    context->full_elapsed = elapsed;
    MGNativeMemoryResult result = {
        0,
        context->write_status,
        context->remaining_cycles,
    };
    return result;
}

static uint32_t try_write_fifo(void* opaque, uint64_t value, uint32_t size) {
    TestContext* context = (TestContext*)opaque;
    context->fifo_try_calls++;
    context->try_observed_gpr3 = context->gpr[3];
    context->last_value = value;
    context->last_size = size;
    return context->fifo_try_accept;
}

static MGNativeMemoryResult write_fifo_partial(void* opaque, uint32_t pc,
                                               uint32_t address, uint64_t value,
                                               uint32_t size, uint64_t elapsed) {
    TestContext* context = (TestContext*)opaque;
    context->fifo_partial_calls++;
    context->last_pc = pc;
    context->last_address = address;
    context->last_size = size;
    context->last_value = value;
    context->last_elapsed = elapsed;
    context->partial_elapsed = elapsed;
    context->partial_observed_gpr3 = context->gpr[3];
    MGNativeMemoryResult result = {
        0,
        context->fifo_partial_status,
        context->remaining_cycles,
    };
    return result;
}

static bool g_region_available = true;
static uint32_t g_unavailable_region_start;

bool moderngekko_native_region_available(const MGNativeRuntime* runtime,
                                         uint32_t start, uint32_t end) {
    (void)runtime;
    (void)end;
    return g_region_available && start != g_unavailable_region_start;
}

static bool state_dirty(const uint64_t* mask, uint32_t slot) {
    return ((mask[slot / 64u] >> (slot % 64u)) & 1u) != 0;
}

enum {
    MG_STATE_GPR0 = 0,
    MG_STATE_FPR0 = 32,
    MG_STATE_PS1_0 = 64,
    MG_STATE_LR = 97,
    MG_STATE_COUNT = 150,
};

void moderngekko_commit_state(const MGNativeState* state, const uint64_t* values,
                              const uint64_t* mask) {
    uint32_t gpr_mask = 0u;
    uint32_t ps0_mask = 0u;
    uint32_t ps1_mask = 0u;
    for (uint32_t reg = 0; reg < 32u; reg++) {
        uint32_t bit = 1u << reg;
        if (state_dirty(mask, MG_STATE_GPR0 + reg))
            gpr_mask |= bit;
        if (state_dirty(mask, MG_STATE_FPR0 + reg))
            ps0_mask |= bit;
        if (state_dirty(mask, MG_STATE_PS1_0 + reg))
            ps1_mask |= bit;
    }
    uint32_t transferred = 0u;
    if ((gpr_mask | ps0_mask | ps1_mask) != 0u &&
        state->struct_size >= offsetof(MGNativeState, try_write_registers) +
                                  sizeof(state->try_write_registers) &&
        state->try_write_registers) {
        transferred = state->try_write_registers(
            state, values + MG_STATE_GPR0, values + MG_STATE_FPR0,
            values + MG_STATE_PS1_0, gpr_mask, ps0_mask, ps1_mask);
    }
    if (!transferred) {
        for (uint32_t reg = 0; reg < 32u; reg++) {
            uint32_t bit = 1u << reg;
            if (gpr_mask & bit)
                state->write_gpr(state->context, reg,
                                 (uint32_t)values[MG_STATE_GPR0 + reg]);
            if (ps0_mask & bit)
                state->write_fpr(state->context, reg, 0u,
                                 values[MG_STATE_FPR0 + reg]);
            if (ps1_mask & bit)
                state->write_fpr(state->context, reg, 1u,
                                 values[MG_STATE_PS1_0 + reg]);
        }
    }
    if (state_dirty(mask, MG_STATE_LR))
        state->write_spr(state->context, 8u, (uint32_t)values[MG_STATE_LR]);
}

void moderngekko_reload_state(const MGNativeState* state, uint64_t* values,
                              const uint64_t* mask) {
    uint32_t gpr_mask = 0u;
    uint32_t ps0_mask = 0u;
    uint32_t ps1_mask = 0u;
    for (uint32_t reg = 0; reg < 32u; reg++) {
        uint32_t bit = 1u << reg;
        if (state_dirty(mask, MG_STATE_GPR0 + reg))
            gpr_mask |= bit;
        if (state_dirty(mask, MG_STATE_FPR0 + reg))
            ps0_mask |= bit;
        if (state_dirty(mask, MG_STATE_PS1_0 + reg))
            ps1_mask |= bit;
    }
    uint32_t transferred = 0u;
    if ((gpr_mask | ps0_mask | ps1_mask) != 0u &&
        state->struct_size >= offsetof(MGNativeState, try_read_registers) +
                                  sizeof(state->try_read_registers) &&
        state->try_read_registers) {
        transferred = state->try_read_registers(
            state, values + MG_STATE_GPR0, values + MG_STATE_FPR0,
            values + MG_STATE_PS1_0, gpr_mask, ps0_mask, ps1_mask);
    }
    if (!transferred) {
        for (uint32_t reg = 0; reg < 32u; reg++) {
            uint32_t bit = 1u << reg;
            if (gpr_mask & bit)
                values[MG_STATE_GPR0 + reg] = state->read_gpr(state->context, reg);
            if (ps0_mask & bit)
                values[MG_STATE_FPR0 + reg] = state->read_fpr(state->context, reg, 0u);
            if (ps1_mask & bit)
                values[MG_STATE_PS1_0 + reg] = state->read_fpr(state->context, reg, 1u);
        }
    }
    if (state_dirty(mask, MG_STATE_LR))
        values[MG_STATE_LR] = state->read_spr(state->context, 8u);
}

uint32_t moderngekko_try_write_msr(const MGNativeRuntime* runtime,
                                   uint32_t old_msr, uint32_t new_msr,
                                   uint64_t elapsed) {
    (void)runtime;
    (void)old_msr;
    (void)new_msr;
    (void)elapsed;
    return 0;
}

uint32_t moderngekko_read_memory(const MGNativeRuntime* runtime, uint32_t pc,
                                 uint32_t address, uint32_t size,
                                 uint64_t elapsed, uint64_t* value,
                                 uint32_t* remaining) {
    MGNativeMemoryResult result = runtime->services->read_memory(
        runtime->services->context, pc, address, size, elapsed);
    *value = result.value;
    *remaining = result.remaining_cycles;
    return result.status;
}

uint32_t moderngekko_write_memory(const MGNativeRuntime* runtime, uint32_t pc,
                                  uint32_t address, uint64_t value,
                                  uint32_t size, uint64_t elapsed,
                                  uint32_t* remaining) {
    MGNativeMemoryResult result = runtime->services->write_memory(
        runtime->services->context, pc, address, value, size, elapsed);
    *remaining = result.remaining_cycles;
    return result.status;
}

static MGNativeState make_state(TestContext* context) {
    MGNativeState state;
    memset(&state, 0, sizeof(state));
    state.abi_version = MG_NATIVE_ABI_VERSION;
    state.struct_size = sizeof(state);
    state.context = context;
    state.read_gpr = read_gpr;
    state.write_gpr = write_gpr;
    state.read_fpr = read_fpr;
    state.write_fpr = write_fpr;
    state.read_cr = read_zero;
    state.write_cr = write_ignore;
    state.read_xer = read_zero;
    state.write_xer = write_ignore;
    state.read_spr = read_spr;
    state.write_spr = write_spr;
    state.read_sr = read_indexed_zero;
    state.write_sr = write_indexed_ignore;
    state.read_fpscr = read_zero;
    state.write_fpscr = write_ignore;
    state.read_msr = read_msr;
    state.write_msr = write_msr;
    state.read_exceptions = read_zero;
    state.write_exceptions = write_ignore;
    state.read_reserve_address = read_zero;
    state.write_reserve_address = write_ignore;
    state.read_reserve_valid = read_zero;
    state.write_reserve_valid = write_ignore;
    state.try_write_registers = try_write_registers;
    state.try_read_registers = try_read_registers;
    return state;
}

int main(void) {
    TestContext context;
    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x11111111u;
    context.gpr[4] = 0x70000000u;
    context.service_status = MG_NATIVE_SERVICE_CONTINUE;

    MGNativeServices services;
    memset(&services, 0, sizeof(services));
    services.abi_version = MG_NATIVE_ABI_VERSION;
    services.struct_size = sizeof(services);
    services.context = &context;
    services.read_memory = read_memory;
    services.write_memory = write_memory;
    services.execute_instruction = execute_instruction;
    services.try_write_fifo = try_write_fifo;
    services.write_fifo_partial = write_fifo_partial;
    MGNativeRuntime runtime = {
        MG_NATIVE_ABI_VERSION, sizeof(runtime), NULL,
        NULL, 0x01800000u, 0x017fffffu,
        NULL, 0, 0,
        NULL, 0,
        &services,
        NULL,
    };
    MGNativeState state = make_state(&context);

    MGNativeExit exit = func_80003120(&runtime, &state, 0x80003120u, 100u, 0u);
    CHECK(context.read_calls == 1u);
    CHECK(context.last_pc == 0x80003120u);
    CHECK(context.last_address == 0x70000004u);
    CHECK(context.last_size == 4u);
    CHECK(context.last_elapsed == 0u);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);
    CHECK(exit.pc == 0x80003124u && exit.next_pc == 0x80003128u);
    CHECK(exit.cycles == 1u);

    context.intercept_pc = 0x80003120u;
    context.intercept_calls = 0u;
    runtime.context = &context;
    runtime.should_intercept = should_intercept;
    exit = func_80003120(&runtime, &state, 0x80003120u, 100u, 0u);
    CHECK(context.intercept_calls == 1u);
    CHECK(exit.reason == MG_NATIVE_EXIT_INTERCEPT);
    CHECK(exit.pc == 0x80003120u);
    CHECK(exit.cycles == 0u);
    runtime.should_intercept = NULL;
    context.intercept_pc = 0u;
    CHECK(context.gpr[3] == 0xA1B2C3D4u);
    CHECK(context.gpr[4] == 0x70000004u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x22222222u;
    context.gpr[4] = 0x70000000u;
    context.service_status = MG_NATIVE_SERVICE_FALLBACK;
    state = make_state(&context);

    exit = func_80003120(&runtime, &state, 0x80003120u, 100u, 0u);
    CHECK(context.read_calls == 1u);
    CHECK(context.last_pc == 0x80003120u);
    CHECK(context.last_address == 0x70000004u);
    CHECK(context.last_size == 4u);
    CHECK(context.last_elapsed == 0u);
    CHECK(exit.reason == MG_NATIVE_EXIT_FALLBACK);
    CHECK(exit.pc == 0x80003120u && exit.next_pc == 0x80003124u);
    CHECK(exit.cycles == 0u);
    CHECK(context.gpr[3] == 0x22222222u);
    CHECK(context.gpr[4] == 0x70000000u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x33333333u;
    context.gpr[4] = 0x70000000u;
    context.service_status = MG_NATIVE_SERVICE_YIELD;
    state = make_state(&context);

    exit = func_80003120(&runtime, &state, 0x80003120u, 100u, 0u);
    CHECK(context.read_calls == 1u);
    CHECK(context.last_elapsed == 0u);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);
    CHECK(exit.pc == 0x80003124u && exit.next_pc == 0x80003128u);
    CHECK(exit.cycles == 1u);
    CHECK(context.gpr[3] == 0xA1B2C3D4u);
    CHECK(context.gpr[4] == 0x70000004u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x44444444u;
    context.gpr[4] = 0x70000000u;
    context.service_status = MG_NATIVE_SERVICE_EXCEPTION;
    state = make_state(&context);

    exit = func_80003120(&runtime, &state, 0x80003120u, 100u, 0u);
    CHECK(context.read_calls == 1u);
    CHECK(context.last_elapsed == 0u);
    CHECK(exit.reason == MG_NATIVE_EXIT_EXCEPTION);
    CHECK(exit.pc == 0x80003120u && exit.next_pc == 0x80003124u);
    CHECK(exit.cycles == 0u);
    CHECK(context.gpr[3] == 0x44444444u);
    CHECK(context.gpr[4] == 0x70000000u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x55555555u;
    context.gpr[4] = 0x70000000u;
    context.service_status = MG_NATIVE_SERVICE_STOP;
    state = make_state(&context);

    exit = func_80003120(&runtime, &state, 0x80003120u, 100u, 0u);
    CHECK(context.read_calls == 1u);
    CHECK(context.last_elapsed == 0u);
    CHECK(exit.reason == MG_NATIVE_EXIT_STOP);
    CHECK(exit.pc == 0x80003120u && exit.next_pc == 0x80003124u);
    CHECK(exit.cycles == 0u);
    CHECK(context.gpr[3] == 0x55555555u);
    CHECK(context.gpr[4] == 0x70000000u);

    // Buffered FIFO success must not publish state or enter a timed service.
    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x12345678u;
    context.fifo_try_accept = 1u;
    context.write_status = MG_NATIVE_SERVICE_CONTINUE;
    context.fifo_partial_status = MG_NATIVE_SERVICE_CONTINUE;
    context.remaining_cycles = 90u;
    services.struct_size = sizeof(services);
    services.context = &context;
    state = make_state(&context);
    exit = func_80003140(&runtime, &state, 0x80003140u, 100u, 0u);
    CHECK(context.fifo_try_calls == 1u);
    CHECK(context.fifo_partial_calls == 0u);
    CHECK(context.write_calls == 0u);
    CHECK(context.try_observed_gpr3 == 0x12345678u);
    CHECK(context.last_value == 0x12345679u && context.last_size == 4u);
    CHECK(context.gpr[3] == 0x12345679u);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);

    // Two exported wrappers in one module.run burst must reuse staged GPR state
    // without publishing/reloading it between ranges.
    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x12345678u;
    context.fifo_try_accept = 1u;
    services.struct_size = sizeof(services);
    services.context = &context;
    state = make_state(&context);
    reset_staged_state();
    prepare_staged_gprs(&state, 1u << 3);
    exit = raw_func_80003140(&runtime, &state, staged_values, staged_dirty,
                             staged_valid, 0x80003140u, 100u, 0u);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);
    CHECK(context.gpr[3] == 0x12345678u);
    CHECK(context.last_value == 0x12345679u);
    const uint32_t gpr3_reads_after_first = context.gpr3_read_calls;
    const uint32_t bulk_reads_after_first = context.bulk_read_calls;
    exit = raw_func_80003140(&runtime, &state, staged_values, staged_dirty,
                             staged_valid, 0x80003140u, 100u, exit.cycles);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);
    CHECK(context.gpr[3] == 0x12345678u);
    CHECK(context.last_value == 0x1234567Au);
    CHECK(context.gpr3_read_calls == gpr3_reads_after_first);
    CHECK(context.bulk_read_calls == bulk_reads_after_first);
    moderngekko_commit_state(&state, staged_values, staged_dirty);
    CHECK(context.gpr[3] == 0x1234567Au);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x22334455u;
    context.fifo_partial_status = MG_NATIVE_SERVICE_CONTINUE;
    context.write_status = MG_NATIVE_SERVICE_CONTINUE;
    context.remaining_cycles = 90u;
    services.struct_size = sizeof(services);
    services.context = &context;
    state = make_state(&context);
    exit = func_80003140(&runtime, &state, 0x80003140u, 100u, 0u);
    CHECK(context.fifo_try_calls == 1u);
    CHECK(context.fifo_partial_calls == 1u);
    CHECK(context.write_calls == 0u);
    CHECK(context.try_observed_gpr3 == 0x22334455u);
    CHECK(context.partial_observed_gpr3 == 0x22334455u);
    CHECK(context.last_value == 0x22334456u);
    CHECK(context.partial_elapsed == 3u);
    CHECK(context.gpr[3] == 0x22334456u);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x33445566u;
    context.fifo_partial_status = MG_NATIVE_SERVICE_FALLBACK;
    context.write_status = MG_NATIVE_SERVICE_CONTINUE;
    context.remaining_cycles = 90u;
    services.struct_size = sizeof(services);
    services.context = &context;
    state = make_state(&context);
    exit = func_80003140(&runtime, &state, 0x80003140u, 100u, 0u);
    CHECK(context.fifo_try_calls == 1u);
    CHECK(context.fifo_partial_calls == 1u);
    CHECK(context.write_calls == 1u);
    CHECK(context.partial_elapsed == 3u && context.full_elapsed == 3u);
    CHECK(context.last_pc == 0x8000314Cu);
    CHECK(context.last_address == 0xCC008000u);
    CHECK(context.last_value == 0x33445567u && context.last_size == 4u);
    CHECK(context.gpr[3] == 0x33445567u);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);

    // Older service structs must not have optional FIFO fields touched.
    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x44556677u;
    context.write_status = MG_NATIVE_SERVICE_CONTINUE;
    context.remaining_cycles = 90u;
    services.struct_size = offsetof(MGNativeServices, try_write_fifo);
    services.context = &context;
    state = make_state(&context);
    exit = func_80003140(&runtime, &state, 0x80003140u, 100u, 0u);
    CHECK(context.fifo_try_calls == 0u);
    CHECK(context.fifo_partial_calls == 0u);
    CHECK(context.write_calls == 1u);
    CHECK(context.full_elapsed == 3u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x55667788u;
    context.write_status = MG_NATIVE_SERVICE_CONTINUE;
    context.remaining_cycles = 90u;
    services.struct_size = offsetof(MGNativeServices, write_fifo_partial);
    services.context = &context;
    state = make_state(&context);
    exit = func_80003140(&runtime, &state, 0x80003140u, 100u, 0u);
    CHECK(context.fifo_try_calls == 1u);
    CHECK(context.fifo_partial_calls == 0u);
    CHECK(context.write_calls == 1u);
    CHECK(context.full_elapsed == 3u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x66778899u;
    context.fifo_partial_status = MG_NATIVE_SERVICE_EXCEPTION;
    context.write_status = MG_NATIVE_SERVICE_CONTINUE;
    services.struct_size = sizeof(services);
    services.context = &context;
    state = make_state(&context);
    exit = func_80003140(&runtime, &state, 0x80003140u, 100u, 40u);
    CHECK(context.fifo_try_calls == 1u);
    CHECK(context.fifo_partial_calls == 1u);
    CHECK(context.write_calls == 0u);
    CHECK(context.partial_elapsed == 43u);
    CHECK(context.gpr[3] == 0x6677889Au);
    CHECK(exit.reason == MG_NATIVE_EXIT_EXCEPTION);
    CHECK(exit.pc == 0x8000314Cu && exit.next_pc == 0x80003150u);
    CHECK(exit.cycles == 3u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.bulk_accept = 1u;
    for (uint32_t reg = 3u; reg <= 8u; reg++)
        context.gpr[reg] = 0x1000u + reg;
    services.context = &context;
    state = make_state(&context);
    exit = func_80003D40(&runtime, &state, 0x80003D40u, 100u, 0u);
    CHECK(context.bulk_read_calls == 1u);
    CHECK(context.bulk_write_calls == 1u);
    CHECK(context.bulk_read_gpr_mask == 0x1F8u);
    CHECK(context.bulk_write_gpr_mask == 0x1F8u);
    CHECK(context.bulk_read_ps0_mask == 0u && context.bulk_read_ps1_mask == 0u);
    CHECK(context.bulk_write_ps0_mask == 0u && context.bulk_write_ps1_mask == 0u);
    CHECK(context.gpr_read_calls == 0u && context.gpr_write_calls == 0u);
    for (uint32_t reg = 3u; reg <= 8u; reg++)
        CHECK(context.gpr[reg] == 0x1001u + reg);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.bulk_accept = 1u;
    for (uint32_t reg = 3u; reg <= 8u; reg++)
        context.gpr[reg] = 0x1800u + reg;
    services.context = &context;
    state = make_state(&context);
    reset_staged_state();
    prepare_staged_gprs(&state, 0x1f8u);
    exit = raw_func_80003D40(&runtime, &state, staged_values, staged_dirty,
                             staged_valid, 0x80003D40u, 100u, 0u);
    CHECK(context.bulk_read_calls == 1u);
    CHECK(context.bulk_write_calls == 0u);
    for (uint32_t reg = 3u; reg <= 8u; reg++)
        CHECK(context.gpr[reg] == 0x1800u + reg);
    exit = raw_func_80003D40(&runtime, &state, staged_values, staged_dirty,
                             staged_valid, 0x80003D40u, 100u, exit.cycles);
    CHECK(context.bulk_read_calls == 1u);
    CHECK(context.bulk_write_calls == 0u);
    moderngekko_commit_state(&state, staged_values, staged_dirty);
    CHECK(context.bulk_write_calls == 1u);
    for (uint32_t reg = 3u; reg <= 8u; reg++)
        CHECK(context.gpr[reg] == 0x1802u + reg);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    for (uint32_t reg = 3u; reg <= 8u; reg++)
        context.gpr[reg] = 0x2000u + reg;
    services.context = &context;
    state = make_state(&context);
    exit = func_80003D40(&runtime, &state, 0x80003D40u, 100u, 0u);
    CHECK(context.bulk_read_calls == 1u && context.bulk_write_calls == 1u);
    CHECK(context.gpr_read_calls == 6u && context.gpr_write_calls == 6u);
    for (uint32_t reg = 3u; reg <= 8u; reg++)
        CHECK(context.gpr[reg] == 0x2001u + reg);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.bulk_accept = 1u;
    for (uint32_t reg = 3u; reg <= 8u; reg++)
        context.gpr[reg] = 0x3000u + reg;
    services.context = &context;
    state = make_state(&context);
    state.struct_size = offsetof(MGNativeState, try_read_registers);
    exit = func_80003D40(&runtime, &state, 0x80003D40u, 100u, 0u);
    CHECK(context.bulk_read_calls == 0u && context.bulk_write_calls == 1u);
    CHECK(context.gpr_read_calls == 6u && context.gpr_write_calls == 0u);
    for (uint32_t reg = 3u; reg <= 8u; reg++)
        CHECK(context.gpr[reg] == 0x3001u + reg);

    memset(&context, 0, sizeof(context));
    context.bulk_accept = 1u;
    state = make_state(&context);
    uint64_t values[MG_STATE_COUNT];
    uint64_t mask[3];
    memset(values, 0, sizeof(values));
    memset(mask, 0, sizeof(mask));
    values[MG_STATE_GPR0 + 3u] = 0xAABBCCDDu;
    values[MG_STATE_FPR0 + 1u] = UINT64_C(0x1122334455667788);
    values[MG_STATE_PS1_0 + 2u] = UINT64_C(0x8877665544332211);
    mask[(MG_STATE_GPR0 + 3u) / 64u] |= UINT64_C(1) << ((MG_STATE_GPR0 + 3u) & 63u);
    mask[(MG_STATE_FPR0 + 1u) / 64u] |= UINT64_C(1) << ((MG_STATE_FPR0 + 1u) & 63u);
    mask[(MG_STATE_PS1_0 + 2u) / 64u] |= UINT64_C(1) << ((MG_STATE_PS1_0 + 2u) & 63u);
    moderngekko_commit_state(&state, values, mask);
    CHECK(context.bulk_write_calls == 1u);
    CHECK(context.bulk_write_gpr_mask == (1u << 3u));
    CHECK(context.bulk_write_ps0_mask == (1u << 1u));
    CHECK(context.bulk_write_ps1_mask == (1u << 2u));
    CHECK(context.gpr_write_calls == 0u && context.fpr_write_calls == 0u);
    CHECK(context.gpr[3] == 0xAABBCCDDu);
    CHECK(context.ps[1][0] == UINT64_C(0x1122334455667788));
    CHECK(context.ps[2][1] == UINT64_C(0x8877665544332211));

    context.gpr[3] = 0x10203040u;
    context.ps[1][0] = UINT64_C(0x0102030405060708);
    context.ps[2][1] = UINT64_C(0x8070605040302010);
    context.bulk_read_calls = 0u;
    memset(values, 0, sizeof(values));
    moderngekko_reload_state(&state, values, mask);
    CHECK(context.bulk_read_calls == 1u);
    CHECK(context.bulk_read_gpr_mask == (1u << 3u));
    CHECK(context.bulk_read_ps0_mask == (1u << 1u));
    CHECK(context.bulk_read_ps1_mask == (1u << 2u));
    CHECK(context.gpr_read_calls == 0u && context.fpr_read_calls == 0u);
    CHECK(values[MG_STATE_GPR0 + 3u] == 0x10203040u);
    CHECK(values[MG_STATE_FPR0 + 1u] == UINT64_C(0x0102030405060708));
    CHECK(values[MG_STATE_PS1_0 + 2u] == UINT64_C(0x8070605040302010));

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x10203040u;
    context.gpr[4] = 0x50607080u;
    services.struct_size = sizeof(services);
    services.context = &context;
    services.continue_native = NULL;
    state = make_state(&context);
    exit = func_80003500(&runtime, &state, 0x80003500u, 0u, 0u);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);
    CHECK(exit.pc == 0x80003600u && exit.next_pc == 0x80003604u);
    CHECK(exit.cycles == 2u);
    CHECK(context.gpr[0] == 0x81234564u);
    CHECK(context.lr == 0x80003508u);
    CHECK(context.gpr[3] == 0x10203040u);
    CHECK(context.gpr[4] == 0x50607080u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x11110000u;
    context.gpr[4] = 0x22220000u;
    services.context = &context;
    state = make_state(&context);
    exit = func_80004A00(&runtime, &state, 0x80004A00u, 2u, 0u);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);
    CHECK(exit.pc == 0x80004C00u && exit.next_pc == 0x80004C04u);
    CHECK(exit.cycles == 3u);
    CHECK(context.gpr[3] == 0x11110001u);
    CHECK(context.gpr[4] == 0x22220000u);
    CHECK(context.lr == 0x80004B08u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x33330000u;
    context.gpr[4] = 0x44440000u;
    services.context = &context;
    state = make_state(&context);
    g_unavailable_region_start = 0x80004C00u;
    exit = func_80004A00(&runtime, &state, 0x80004A00u, 100u, 0u);
    g_unavailable_region_start = 0u;
    CHECK(exit.reason == MG_NATIVE_EXIT_INVALIDATED_CODE);
    CHECK(exit.pc == 0x80004C00u && exit.next_pc == 0x80004C04u);
    CHECK(exit.cycles == 3u);
    CHECK(context.gpr[3] == 0x33330001u);
    CHECK(context.gpr[4] == 0x44440000u);
    CHECK(context.lr == 0x80004B08u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    services.context = &context;
    state = make_state(&context);
    exit = func_80003D40(&runtime, &state, 0x80003D41u, 100u, 0u);
    CHECK(exit.reason == MG_NATIVE_EXIT_FALLBACK);
    CHECK(exit.pc == 0x80003D41u && exit.next_pc == 0x80003D45u);
    CHECK(exit.cycles == 0u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    services.context = &context;
    state = make_state(&context);
    g_region_available = false;
    exit = func_80003D40(&runtime, &state, 0x80003D40u, 100u, 0u);
    g_region_available = true;
    CHECK(exit.reason == MG_NATIVE_EXIT_INVALIDATED_CODE);
    CHECK(exit.pc == 0x80003D40u && exit.next_pc == 0x80003D44u);
    CHECK(exit.cycles == 0u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    services.context = &context;
    state = make_state(&context);
    exit = func_80004E00(&runtime, &state, 0x80004E00u, 100u, 0u);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);
    CHECK(exit.pc == 0x81234000u && exit.next_pc == 0x81234004u);
    CHECK(exit.cycles == 7u);
    CHECK(context.lr == 0x81234000u);
    CHECK(context.gpr[0] == 0x81234564u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[3] = 0x100u;
    context.gpr[4] = 0x200u;
    context.msr = 1u << 13;
    context.instruction_status = MG_NATIVE_SERVICE_FALLBACK;
    services.context = &context;
    services.struct_size = sizeof(services);
    state = make_state(&context);
    exit = func_80005000(&runtime, &state, 0x80005000u, 100u, 0u);
    CHECK(context.instruction_calls == 1u);
    CHECK(exit.reason == MG_NATIVE_EXIT_FALLBACK);
    CHECK(exit.pc == 0x80005100u && exit.next_pc == 0x80005104u);
    CHECK(exit.cycles == 3u);
    CHECK(context.gpr[3] == 0x101u);
    CHECK(context.gpr[4] == 0x200u);
    CHECK(context.lr == 0x8000500Cu);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[4] = 10u;
    context.msr = 1u << 13;
    context.instruction_status = MG_NATIVE_SERVICE_CONTINUE;
    services.context = &context;
    services.struct_size = sizeof(services);
    state = make_state(&context);
    exit = func_80003DA0(&runtime, &state, 0x80003DA0u, 100u, 0u);
    CHECK(context.instruction_calls == 1u);
    CHECK(context.last_pc == 0x80003DA0u);
    CHECK(context.gpr[4] == 22u);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);
    CHECK(exit.pc == 0x81234564u && exit.next_pc == 0x81234568u);
    CHECK(exit.cycles == 3u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[4] = 20u;
    context.msr = 1u << 13;
    context.instruction_status = MG_NATIVE_SERVICE_YIELD;
    services.context = &context;
    state = make_state(&context);
    exit = func_80003DA0(&runtime, &state, 0x80003DA0u, 100u, 0u);
    CHECK(context.instruction_calls == 1u);
    CHECK(context.gpr[4] == 25u);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);
    CHECK(exit.pc == 0x80003DA4u && exit.next_pc == 0x80003DA8u);
    CHECK(exit.cycles == 1u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[4] = 30u;
    context.msr = 1u << 13;
    context.instruction_status = MG_NATIVE_SERVICE_FALLBACK;
    services.context = &context;
    state = make_state(&context);
    exit = func_80003DA0(&runtime, &state, 0x80003DA0u, 100u, 0u);
    CHECK(context.instruction_calls == 1u);
    CHECK(context.gpr[4] == 30u);
    CHECK(exit.reason == MG_NATIVE_EXIT_FALLBACK);
    CHECK(exit.pc == 0x80003DA0u && exit.next_pc == 0x80003DA4u);
    CHECK(exit.cycles == 0u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[4] = 40u;
    context.msr = 1u << 13;
    context.instruction_status = MG_NATIVE_SERVICE_EXCEPTION;
    services.context = &context;
    state = make_state(&context);
    exit = func_80003DA0(&runtime, &state, 0x80003DA0u, 100u, 0u);
    CHECK(context.instruction_calls == 1u);
    CHECK(context.gpr[4] == 45u);
    CHECK(exit.reason == MG_NATIVE_EXIT_EXCEPTION);
    CHECK(exit.pc == 0x80003DA0u && exit.next_pc == 0x80003DA4u);
    CHECK(exit.cycles == 1u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[4] = 50u;
    context.msr = 1u << 13;
    context.instruction_status = MG_NATIVE_SERVICE_STOP;
    services.context = &context;
    state = make_state(&context);
    exit = func_80003DA0(&runtime, &state, 0x80003DA0u, 100u, 0u);
    CHECK(context.instruction_calls == 1u);
    CHECK(context.gpr[4] == 55u);
    CHECK(exit.reason == MG_NATIVE_EXIT_STOP);
    CHECK(exit.pc == 0x80003DA0u && exit.next_pc == 0x80003DA4u);
    CHECK(exit.cycles == 1u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.gpr[4] = 60u;
    context.msr = 0u;
    context.instruction_status = MG_NATIVE_SERVICE_CONTINUE;
    services.context = &context;
    state = make_state(&context);
    exit = func_80003DA0(&runtime, &state, 0x80003DA0u, 100u, 0u);
    CHECK(context.instruction_calls == 0u);
    CHECK(context.gpr[4] == 60u);
    CHECK(exit.reason == MG_NATIVE_EXIT_FALLBACK);
    CHECK(exit.pc == 0x80003DA0u && exit.next_pc == 0x80003DA4u);
    CHECK(exit.cycles == 0u);

    uint8_t mem1[0x1000];
    uint8_t mem2[0x1000];
    memset(mem1, 0, sizeof(mem1));
    memset(mem2, 0, sizeof(mem2));
    runtime.mem1 = mem1;
    runtime.mem1_size = 0x01800000u;
    runtime.mem1_mask = 0x017fffffu;
    runtime.mem2 = mem2;
    runtime.mem2_size = 0x04000000u;
    runtime.mem2_mask = 0x03ffffffu;

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    mem2[0x600] = 0x11u;
    mem2[0x601] = 0x22u;
    mem2[0x602] = 0x33u;
    mem2[0x603] = 0x44u;
    services.context = &context;
    state = make_state(&context);
    exit = func_80004D00(&runtime, &state, 0x80004D00u, 100u, 0u);
    CHECK(context.read_calls == 0u && context.write_calls == 0u);
    CHECK(context.gpr[3] == 0x11223344u);
    CHECK(memcmp(mem2 + 0x600, mem2 + 0x604, 4u) == 0);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);
    CHECK(exit.pc == 0x81234564u && exit.next_pc == 0x81234568u);

    memset(&context, 0, sizeof(context));
    context.lr = 0x81234564u;
    context.service_status = MG_NATIVE_SERVICE_CONTINUE;
    context.write_status = MG_NATIVE_SERVICE_CONTINUE;
    context.read_remaining_cycles = 100u;
    context.remaining_cycles = 100u;
    services.context = &context;
    runtime.mem2 = NULL;
    state = make_state(&context);
    exit = func_80004D00(&runtime, &state, 0x80004D00u, 100u, 0u);
    CHECK(context.read_calls == 1u && context.write_calls == 1u);
    CHECK(context.last_address == 0x90000604u);
    CHECK(context.last_value == 0xA1B2C3D4u);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);
    runtime.mem2 = mem2;

    memset(&context, 0, sizeof(context));
    memset(mem1, 0, sizeof(mem1));
    context.lr = 0x81234564u;
    context.gpr[5] = 0x20u;
    mem1[0x20] = 0xA1u;
    mem1[0x21] = 0xB2u;
    mem1[0x22] = 0xC3u;
    mem1[0x23] = 0xD4u;
    services.context = &context;
    state = make_state(&context);
    exit = func_80004D20(&runtime, &state, 0x80004D20u, 100u, 0u);
    CHECK(context.read_calls == 0u && context.write_calls == 0u);
    CHECK(context.gpr[3] == 0xA1B2C3D4u);
    CHECK(memcmp(mem1 + 0x20, mem1 + 0x24, 4u) == 0);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);

    memset(&context, 0, sizeof(context));
    memset(mem2, 0, sizeof(mem2));
    context.lr = 0x81234564u;
    context.gpr[5] = 0x30u;
    mem2[0x30] = 0xDEu;
    mem2[0x31] = 0xADu;
    mem2[0x32] = 0xBEu;
    mem2[0x33] = 0xEFu;
    services.context = &context;
    state = make_state(&context);
    exit = func_80004D40(&runtime, &state, 0x80004D40u, 100u, 0u);
    CHECK(context.read_calls == 0u && context.write_calls == 0u);
    CHECK(context.gpr[3] == 0xDEADBEEFu);
    CHECK(memcmp(mem2 + 0x30, mem2 + 0x34, 4u) == 0);
    CHECK(exit.reason == MG_NATIVE_EXIT_BUDGET);
    return 0;
}
