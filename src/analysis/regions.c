#include "analysis/regions.h"

#include <stdlib.h>
#include <string.h>

void dolregion_default_limits(DolRegionLimits* limits) {
    if (!limits)
        return;
    /* Conservative on purpose.
     *
     * The existing fixed LLVM path uses 128 because a chunk becomes one LLVM
     * function and the register allocator must keep the promoted guest
     * register file live across the whole of it -- 1024 cost 3x the code size
     * for a third less speed (pipeline.c, LLVM-EXPERIMENTS E002/E003).
     *
     * A region is not the same object: its boundaries follow control flow, so
     * the state that has to stay live across a join is what the CFG actually
     * requires rather than everything. That is the premise being tested, not a
     * result, so the default starts modest and the benchmark decides. */
    limits->max_instructions = 1024u;
    limits->max_ir_instructions = 1024u * DOLREGION_IR_PER_GUEST_INSN * 2u;
    limits->max_functions = 64u;
    limits->cold_weight_threshold = 1u;
    limits->merge_address_adjacent = 1;
    limits->max_adjacency_gap = 256u;
}

bool dolregion_parse_mode(const char* text, DolRegionMode* mode) {
    if (!text || !mode)
        return false;
    if (strcmp(text, "fixed") == 0)       { *mode = DOLREGION_MODE_FIXED; return true; }
    if (strcmp(text, "function") == 0)    { *mode = DOLREGION_MODE_FUNCTION; return true; }
    if (strcmp(text, "cfg") == 0)         { *mode = DOLREGION_MODE_CFG; return true; }
    if (strcmp(text, "pgo") == 0)         { *mode = DOLREGION_MODE_PGO; return true; }
    return false;
}

const char* dolregion_mode_name(DolRegionMode mode) {
    switch (mode) {
    case DOLREGION_MODE_FIXED:    return "fixed";
    case DOLREGION_MODE_FUNCTION: return "function";
    case DOLREGION_MODE_CFG:      return "cfg";
    case DOLREGION_MODE_PGO:      return "pgo";
    default:                      return "unknown";
    }
}

const char* dolregion_end_reason_name(DolRegionEndReason reason) {
    switch (reason) {
    case DOLREGION_END_SIZE_LIMIT:       return "size-limit";
    case DOLREGION_END_IR_LIMIT:         return "ir-limit";
    case DOLREGION_END_NO_CANDIDATE:     return "no-connected-candidate";
    case DOLREGION_END_INDIRECT:         return "indirect-transfer";
    case DOLREGION_END_SMC:              return "smc-boundary";
    case DOLREGION_END_PATCHABLE:        return "patchable-boundary";
    case DOLREGION_END_COLD:             return "cold-code";
    case DOLREGION_END_FUNCTION_BOUNDARY:return "function-boundary";
    case DOLREGION_END_FIXED_CHUNK:      return "fixed-chunk";
    case DOLREGION_END_SECTION_END:      return "section-end";
    default:                             return "unknown";
    }
}

void dolregion_plan_init(DolRegionPlan* plan) {
    if (plan)
        memset(plan, 0, sizeof(*plan));
}

void dolregion_plan_free(DolRegionPlan* plan) {
    if (!plan)
        return;
    for (u32 i = 0; i < plan->region_count; i++) {
        free(plan->regions[i].blocks);
        free(plan->regions[i].functions);
    }
    free(plan->regions);
    free(plan->block_region);
    free(plan->function_region);
    memset(plan, 0, sizeof(*plan));
}

/* --- per-function block lists (CSR) -------------------------------------- */

typedef struct {
    u32* offsets;   /* function_count + 1 */
    u32* blocks;    /* block_count, ascending within each function */
} FunctionBlocks;

static int compare_u32_asc(const void* a, const void* b) {
    u32 left = *(const u32*)a;
    u32 right = *(const u32*)b;
    return (left > right) - (left < right);
}

static bool build_function_blocks(const DolCfgProgram* program,
                                  FunctionBlocks* out) {
    out->offsets = (u32*)calloc(program->function_count + 1u, sizeof(u32));
    out->blocks = (u32*)malloc((program->block_count ? program->block_count : 1u) *
                               sizeof(u32));
    if (!out->offsets || !out->blocks)
        return false;

    for (u32 i = 0; i < program->block_count; i++) {
        u32 f = program->blocks[i].function;
        if (f != DOLCFG_NO_BLOCK)
            out->offsets[f + 1u]++;
    }
    for (u32 f = 0; f < program->function_count; f++)
        out->offsets[f + 1u] += out->offsets[f];

    u32* cursor = (u32*)malloc((program->function_count ? program->function_count : 1u) *
                               sizeof(u32));
    if (!cursor)
        return false;
    memcpy(cursor, out->offsets, program->function_count * sizeof(u32));

    for (u32 i = 0; i < program->block_count; i++) {
        u32 f = program->blocks[i].function;
        if (f != DOLCFG_NO_BLOCK)
            out->blocks[cursor[f]++] = i;
    }
    free(cursor);

    /* Blocks are appended in index order, which is section order and therefore
       already ascending by address -- but the plan's determinism must not rest
       on that being true forever. */
    for (u32 f = 0; f < program->function_count; f++) {
        u32 start = out->offsets[f];
        u32 count = out->offsets[f + 1u] - start;
        if (count > 1u)
            qsort(&out->blocks[start], count, sizeof(u32), compare_u32_asc);
    }
    return true;
}

static void free_function_blocks(FunctionBlocks* fb) {
    free(fb->offsets);
    free(fb->blocks);
    fb->offsets = NULL;
    fb->blocks = NULL;
}

/* --- function call graph -------------------------------------------------- */

typedef struct {
    u32* offsets;   /* function_count + 1 */
    u32* targets;
    u64* weights;
    u32 edge_count;
} CallGraph;

typedef struct {
    u32 from;
    u32 to;
    u64 weight;
} RawEdge;

static int compare_raw_edge(const void* a, const void* b) {
    const RawEdge* left = (const RawEdge*)a;
    const RawEdge* right = (const RawEdge*)b;
    if (left->from != right->from)
        return left->from < right->from ? -1 : 1;
    if (left->to != right->to)
        return left->to < right->to ? -1 : 1;
    return 0;
}

/* Undirected adjacency: a caller and callee are equally worth co-locating, and
   the accretion walks outward from a seed in both directions. */
static bool build_call_graph(const DolCfgProgram* program, CallGraph* graph) {
    memset(graph, 0, sizeof(*graph));

    u32 capacity = 1024;
    u32 count = 0;
    RawEdge* raw = (RawEdge*)malloc(capacity * sizeof(*raw));
    if (!raw)
        return false;

    for (u32 i = 0; i < program->block_count; i++) {
        const DolCfgBlock* block = &program->blocks[i];
        if (block->terminator != DOLCFG_TERM_CALL &&
            block->terminator != DOLCFG_TERM_TAIL_CALL)
            continue;
        if (!block->call_target || block->function == DOLCFG_NO_BLOCK)
            continue;

        u32 target_block = dolcfg_block_starting_at(program, block->call_target);
        if (target_block == DOLCFG_NO_BLOCK)
            continue; /* Cross-module: no local function to merge with. */
        u32 callee = program->blocks[target_block].function;
        if (callee == DOLCFG_NO_BLOCK || callee == block->function)
            continue;

        if (count + 2u > capacity) {
            capacity *= 2u;
            RawEdge* grown = (RawEdge*)realloc(raw, capacity * sizeof(*raw));
            if (!grown) {
                free(raw);
                return false;
            }
            raw = grown;
        }
        /* A call site's weight is its profile weight when one exists, else 1 --
           so without a profile this counts call sites, which is still a better
           merge signal than address adjacency. */
        u64 weight = block->weight ? block->weight : 1u;
        raw[count].from = block->function;
        raw[count].to = callee;
        raw[count].weight = weight;
        count++;
        raw[count].from = callee;
        raw[count].to = block->function;
        raw[count].weight = weight;
        count++;
    }

    qsort(raw, count, sizeof(*raw), compare_raw_edge);

    graph->offsets = (u32*)calloc(program->function_count + 1u, sizeof(u32));
    graph->targets = (u32*)malloc((count ? count : 1u) * sizeof(u32));
    graph->weights = (u64*)malloc((count ? count : 1u) * sizeof(u64));
    if (!graph->offsets || !graph->targets || !graph->weights) {
        free(raw);
        return false;
    }

    /* Coalesce duplicate (from,to) pairs, summing weight. */
    u32 out = 0;
    for (u32 i = 0; i < count;) {
        u32 j = i;
        u64 weight = 0;
        while (j < count && raw[j].from == raw[i].from && raw[j].to == raw[i].to) {
            weight += raw[j].weight;
            j++;
        }
        graph->targets[out] = raw[i].to;
        graph->weights[out] = weight;
        graph->offsets[raw[i].from + 1u]++;
        out++;
        i = j;
    }
    for (u32 f = 0; f < program->function_count; f++)
        graph->offsets[f + 1u] += graph->offsets[f];
    graph->edge_count = out;

    free(raw);
    return true;
}

static void free_call_graph(CallGraph* graph) {
    free(graph->offsets);
    free(graph->targets);
    free(graph->weights);
    memset(graph, 0, sizeof(*graph));
}

/* --- region construction -------------------------------------------------- */

static DolRegion* new_region(DolRegionPlan* plan) {
    if (plan->region_count == plan->region_capacity) {
        u32 capacity = plan->region_capacity ? plan->region_capacity * 2u : 256u;
        DolRegion* grown =
            (DolRegion*)realloc(plan->regions, capacity * sizeof(*grown));
        if (!grown)
            return NULL;
        plan->regions = grown;
        plan->region_capacity = capacity;
    }
    DolRegion* region = &plan->regions[plan->region_count];
    memset(region, 0, sizeof(*region));
    region->id = plan->region_count;
    region->guest_start = 0xFFFFFFFFu;
    region->end_reason = DOLREGION_END_NO_CANDIDATE;
    plan->region_count++;
    return region;
}

static bool region_push_block(DolRegion* region, const DolCfgProgram* program,
                              DolRegionPlan* plan, u32 block_index) {
    u32* grown = (u32*)realloc(region->blocks,
                               (region->block_count + 1u) * sizeof(u32));
    if (!grown)
        return false;
    region->blocks = grown;
    region->blocks[region->block_count++] = block_index;

    const DolCfgBlock* block = &program->blocks[block_index];
    if (block->start < region->guest_start)
        region->guest_start = block->start;
    if (block->end > region->guest_end)
        region->guest_end = block->end;
    region->instruction_count += block->instruction_count;
    region->weight += block->weight;
    if (block->flags & DOLCFG_BLOCK_LOOP_HEADER)
        region->loop_count++;
    if (block->flags & DOLCFG_BLOCK_SMC_SUSPECT)
        region->contains_smc = true;
    if (block->terminator == DOLCFG_TERM_INDIRECT)
        region->indirect_sites++;

    plan->block_region[block_index] = region->id;
    return true;
}

static bool region_push_function(DolRegion* region, const DolCfgProgram* program,
                                 DolRegionPlan* plan, const FunctionBlocks* fb,
                                 u32 function_index) {
    u32* grown = (u32*)realloc(region->functions,
                               (region->function_count + 1u) * sizeof(u32));
    if (!grown)
        return false;
    region->functions = grown;
    region->functions[region->function_count++] = function_index;
    plan->function_region[function_index] = region->id;

    if (program->functions[function_index].flags & DOLCFG_FUNC_PATCHABLE)
        region->patchable = true;

    for (u32 i = fb->offsets[function_index];
         i < fb->offsets[function_index + 1u]; i++) {
        if (!region_push_block(region, program, plan, fb->blocks[i]))
            return false;
    }
    region->estimated_ir_instructions =
        region->instruction_count * DOLREGION_IR_PER_GUEST_INSN;
    return true;
}

/* A function too large for one region is cut at block boundaries. SCC members
   are kept together: cutting a loop in half is the single worst place to put a
   boundary, so the cut slides forward to the end of the SCC. A hard ceiling of
   twice the limit stops one enormous SCC from swallowing everything. */
static bool split_large_function(DolRegionPlan* plan, const DolCfgProgram* program,
                                 const FunctionBlocks* fb, u32 function_index,
                                 const DolRegionLimits* limits) {
    u32 first = fb->offsets[function_index];
    u32 last = fb->offsets[function_index + 1u];
    u32 ceiling = limits->max_instructions * 2u;

    DolRegion* region = NULL;
    for (u32 i = first; i < last; i++) {
        u32 block_index = fb->blocks[i];
        const DolCfgBlock* block = &program->blocks[block_index];

        if (region && region->instruction_count >= limits->max_instructions) {
            bool scc_open = false;
            if (block->scc != DOLCFG_NO_BLOCK && i > first) {
                u32 previous = fb->blocks[i - 1u];
                scc_open = program->blocks[previous].scc == block->scc;
            }
            if (!scc_open || region->instruction_count >= ceiling) {
                region->end_reason = scc_open ? DOLREGION_END_SIZE_LIMIT
                                              : DOLREGION_END_SIZE_LIMIT;
                region = NULL;
            }
        }

        if (!region) {
            region = new_region(plan);
            if (!region)
                return false;
            u32* grown = (u32*)realloc(region->functions, sizeof(u32));
            if (!grown)
                return false;
            region->functions = grown;
            region->functions[region->function_count++] = function_index;
            if (program->functions[function_index].flags & DOLCFG_FUNC_PATCHABLE)
                region->patchable = true;
        }

        if (!region_push_block(region, program, plan, block_index))
            return false;
        region->estimated_ir_instructions =
            region->instruction_count * DOLREGION_IR_PER_GUEST_INSN;
    }

    if (plan->function_region[function_index] == DOLCFG_NO_BLOCK && region)
        plan->function_region[function_index] = region->id;
    plan->split_functions++;
    return true;
}

/* --- modes ---------------------------------------------------------------- */

static bool plan_fixed(DolRegionPlan* plan, const DolCfgProgram* program,
                       const DolRegionLimits* limits) {
    /* Deliberately CFG-blind: cut every N guest instructions in address order.
       This is the comparison arm, so it must not quietly benefit from any of
       the analysis the other modes use. */
    DolRegion* region = NULL;
    u32 previous_end = 0xFFFFFFFFu;

    for (u32 i = 0; i < program->block_count; i++) {
        const DolCfgBlock* block = &program->blocks[i];

        bool discontiguous = (previous_end != 0xFFFFFFFFu) &&
                             (block->start != previous_end);
        if (region &&
            (region->instruction_count >= limits->max_instructions || discontiguous)) {
            region->end_reason = discontiguous ? DOLREGION_END_SECTION_END
                                               : DOLREGION_END_FIXED_CHUNK;
            region = NULL;
        }
        if (!region) {
            region = new_region(plan);
            if (!region)
                return false;
        }
        if (!region_push_block(region, program, plan, i))
            return false;
        region->estimated_ir_instructions =
            region->instruction_count * DOLREGION_IR_PER_GUEST_INSN;
        if (block->function != DOLCFG_NO_BLOCK &&
            plan->function_region[block->function] == DOLCFG_NO_BLOCK) {
            plan->function_region[block->function] = region->id;
        }
        previous_end = block->end;
    }
    return true;
}

static bool plan_function(DolRegionPlan* plan, const DolCfgProgram* program,
                          const FunctionBlocks* fb,
                          const DolRegionLimits* limits) {
    for (u32 f = 0; f < program->function_count; f++) {
        if (program->functions[f].block_count == 0)
            continue;
        if (program->functions[f].instruction_count > limits->max_instructions) {
            if (!split_large_function(plan, program, fb, f, limits))
                return false;
            continue;
        }
        DolRegion* region = new_region(plan);
        if (!region)
            return false;
        if (!region_push_function(region, program, plan, fb, f))
            return false;
        region->end_reason = DOLREGION_END_FUNCTION_BOUNDARY;
    }
    return true;
}

typedef struct {
    u32 address;
    u32 function;
} FunctionByAddress;

static int compare_function_by_address(const void* a, const void* b) {
    const FunctionByAddress* left = (const FunctionByAddress*)a;
    const FunctionByAddress* right = (const FunctionByAddress*)b;
    if (left->address != right->address)
        return left->address < right->address ? -1 : 1;
    return (left->function > right->function) - (left->function < right->function);
}

/* Lowest-addressed unassigned function starting at or after `address`, within
   `gap` bytes of it. DOLCFG_NO_BLOCK when there is none. */
static u32 next_adjacent_function(const DolCfgProgram* program,
                                  const DolRegionPlan* plan,
                                  const FunctionByAddress* order, u32 order_count,
                                  u32 address, u32 gap) {
    u32 low = 0;
    u32 high = order_count;
    while (low < high) {
        u32 mid = low + (high - low) / 2u;
        if (order[mid].address < address)
            low = mid + 1u;
        else
            high = mid;
    }
    for (u32 i = low; i < order_count; i++) {
        if (order[i].address > address + gap)
            return DOLCFG_NO_BLOCK;
        u32 function = order[i].function;
        if (plan->function_region[function] != DOLCFG_NO_BLOCK)
            continue;
        if (program->functions[function].block_count == 0)
            continue;
        return function;
    }
    return DOLCFG_NO_BLOCK;
}

static bool plan_accretive(DolRegionPlan* plan, const DolCfgProgram* program,
                           const FunctionBlocks* fb, const CallGraph* graph,
                           const DolRegionLimits* limits, bool use_weights) {
    u64* candidate_weight = (u64*)calloc(
        program->function_count ? program->function_count : 1u, sizeof(u64));
    u8* is_candidate = (u8*)calloc(
        program->function_count ? program->function_count : 1u, sizeof(u8));
    u32* touched = (u32*)malloc(
        (program->function_count ? program->function_count : 1u) * sizeof(u32));
    FunctionByAddress* order = (FunctionByAddress*)malloc(
        (program->function_count ? program->function_count : 1u) * sizeof(*order));
    if (!candidate_weight || !is_candidate || !touched || !order) {
        free(candidate_weight); free(is_candidate); free(touched); free(order);
        return false;
    }
    for (u32 i = 0; i < program->function_count; i++) {
        order[i].address = program->functions[i].entry_address;
        order[i].function = i;
    }
    qsort(order, program->function_count, sizeof(*order),
          compare_function_by_address);

    /* Seed order.
     *
     * Address order is right for cfg mode: it is deterministic and no function
     * deserves priority. With a profile it is actively wrong. Accretion is
     * greedy, so whichever region forms first takes the shared neighbours, and
     * in address order that is decided by link layout rather than by what
     * executes.
     *
     * The measured sweep is the argument: merging on all edges equally cut
     * static crossings 21% and moved the runtime dispatcher rate 0.8%, because
     * uniform merging removes overwhelmingly cold boundaries. Hot code has to
     * choose first for merging to reach the boundaries that actually execute.
     *
     * Ties break on function index, so the plan stays reproducible. */
    u32* seed_order = (u32*)malloc(
        (program->function_count ? program->function_count : 1u) * sizeof(u32));
    if (!seed_order) {
        free(candidate_weight); free(is_candidate); free(touched); free(order);
        return false;
    }
    for (u32 i = 0; i < program->function_count; i++)
        seed_order[i] = i;
    if (use_weights) {
        /* Insertion sort by descending weight: the array is already in address
           order and a profile makes only a small fraction non-zero, so this
           stays close to linear in practice. */
        for (u32 i = 1; i < program->function_count; i++) {
            u32 key = seed_order[i];
            u64 key_weight = program->functions[key].weight;
            u32 j = i;
            while (j > 0 &&
                   program->functions[seed_order[j - 1u]].weight < key_weight) {
                seed_order[j] = seed_order[j - 1u];
                j--;
            }
            seed_order[j] = key;
        }
    }

    for (u32 s = 0; s < program->function_count; s++) {
        u32 seed = seed_order[s];
        if (plan->function_region[seed] != DOLCFG_NO_BLOCK)
            continue;
        if (program->functions[seed].block_count == 0)
            continue;

        if (program->functions[seed].instruction_count > limits->max_instructions) {
            if (!split_large_function(plan, program, fb, seed, limits)) {
                free(candidate_weight); free(is_candidate); free(touched);
                free(order); free(seed_order);
                return false;
            }
            continue;
        }

        DolRegion* region = new_region(plan);
        if (!region) {
            free(candidate_weight); free(is_candidate); free(touched);
            return false;
        }
        if (!region_push_function(region, program, plan, fb, seed)) {
            free(candidate_weight); free(is_candidate); free(touched);
            return false;
        }

        u32 touched_count = 0;
        DolRegionEndReason reason = DOLREGION_END_NO_CANDIDATE;

        /* Seed the candidate frontier from the seed function's neighbours. */
        for (u32 e = graph->offsets[seed]; e < graph->offsets[seed + 1u]; e++) {
            u32 target = graph->targets[e];
            if (plan->function_region[target] != DOLCFG_NO_BLOCK)
                continue;
            if (!is_candidate[target]) {
                is_candidate[target] = 1;
                touched[touched_count++] = target;
            }
            candidate_weight[target] += graph->weights[e];
        }

        for (;;) {
            if (region->function_count >= limits->max_functions) {
                reason = DOLREGION_END_SIZE_LIMIT;
                break;
            }

            u32 best = DOLCFG_NO_BLOCK;
            u64 best_weight = 0;
            bool blocked_by_size = false;
            bool blocked_by_cold = false;

            /* Address order with a strict > comparison makes ties resolve to
               the lowest-addressed candidate, which is what keeps the plan
               reproducible. */
            for (u32 c = 0; c < touched_count; c++) {
                u32 candidate = touched[c];
                if (!is_candidate[candidate])
                    continue;
                if (plan->function_region[candidate] != DOLCFG_NO_BLOCK)
                    continue;

                const DolCfgFunction* fn = &program->functions[candidate];
                if (fn->block_count == 0)
                    continue;

                if (region->instruction_count + fn->instruction_count >
                    limits->max_instructions) {
                    blocked_by_size = true;
                    continue;
                }
                if ((region->instruction_count + fn->instruction_count) *
                        DOLREGION_IR_PER_GUEST_INSN >
                    limits->max_ir_instructions) {
                    blocked_by_size = true;
                    continue;
                }
                /* Cold code does not belong in a hot region: merging it pays
                   the region's code-size budget for something that does not
                   run. Only meaningful when a profile is loaded. */
                if (use_weights && region->weight > 0 &&
                    fn->weight < limits->cold_weight_threshold) {
                    blocked_by_cold = true;
                    continue;
                }

                if (candidate_weight[candidate] > best_weight) {
                    best_weight = candidate_weight[candidate];
                    best = candidate;
                }
            }

            /* The call graph ran dry but the budget did not. Keep growing
               along addresses rather than closing a region at a tenth of its
               limit -- merging adjacent code adds no crossing and keeps the
               region a single contiguous run. */
            if (best == DOLCFG_NO_BLOCK && limits->merge_address_adjacent) {
                u32 next = next_adjacent_function(
                    program, plan, order, program->function_count,
                    region->guest_end, limits->max_adjacency_gap);
                if (next != DOLCFG_NO_BLOCK) {
                    const DolCfgFunction* fn = &program->functions[next];
                    u32 grown = region->instruction_count + fn->instruction_count;
                    if (grown > limits->max_instructions ||
                        grown * DOLREGION_IR_PER_GUEST_INSN >
                            limits->max_ir_instructions) {
                        blocked_by_size = true;
                    } else if (use_weights && region->weight > 0 &&
                               fn->weight < limits->cold_weight_threshold) {
                        blocked_by_cold = true;
                    } else {
                        best = next;
                    }
                }
            }

            if (best == DOLCFG_NO_BLOCK) {
                reason = blocked_by_size ? DOLREGION_END_SIZE_LIMIT
                       : blocked_by_cold ? DOLREGION_END_COLD
                                         : DOLREGION_END_NO_CANDIDATE;
                break;
            }

            if (!region_push_function(region, program, plan, fb, best)) {
                free(candidate_weight); free(is_candidate); free(touched);
                free(order); free(seed_order);
                return false;
            }
            is_candidate[best] = 0;

            for (u32 e = graph->offsets[best]; e < graph->offsets[best + 1u]; e++) {
                u32 target = graph->targets[e];
                if (plan->function_region[target] != DOLCFG_NO_BLOCK)
                    continue;
                if (!is_candidate[target]) {
                    is_candidate[target] = 1;
                    touched[touched_count++] = target;
                }
                candidate_weight[target] += graph->weights[e];
            }
        }

        region->end_reason = reason;
        for (u32 c = 0; c < touched_count; c++) {
            is_candidate[touched[c]] = 0;
            candidate_weight[touched[c]] = 0;
        }
    }

    free(candidate_weight);
    free(is_candidate);
    free(touched);
    free(order);
    free(seed_order);
    return true;
}

/* --- finalisation --------------------------------------------------------- */

static void finalise_regions(DolRegionPlan* plan, const DolCfgProgram* program) {
    plan->total_instructions = 0;
    plan->cross_region_edges = 0;

    for (u32 r = 0; r < plan->region_count; r++) {
        DolRegion* region = &plan->regions[r];
        plan->total_instructions += region->instruction_count;

        u32* sccs = (u32*)malloc((region->block_count ? region->block_count : 1u) *
                                 sizeof(u32));
        u32 scc_total = 0;

        for (u32 i = 0; i < region->block_count; i++) {
            u32 index = region->blocks[i];
            const DolCfgBlock* block = &program->blocks[index];

            if (sccs && block->scc != DOLCFG_NO_BLOCK)
                sccs[scc_total++] = block->scc;

            for (u32 s = 0; s < block->successor_count; s++) {
                u32 next = block->successors[s];
                if (next == DOLCFG_NO_BLOCK) {
                    /* An edge leaving the model still leaves the region. */
                    region->out_edges++;
                    continue;
                }
                if (plan->block_region[next] == region->id)
                    region->internal_edges++;
                else
                    region->out_edges++;
            }

            /* The call edge is counted separately because a CALL block's
               successor is its *return point*, not its callee -- so walking
               successors alone never sees the call at all. On Mario Kart that
               would hide 40,316 of the transfers the plan exists to remove,
               and co-locating a caller with its callee would score as no
               improvement whatsoever. */
            if ((block->terminator == DOLCFG_TERM_CALL ||
                 block->terminator == DOLCFG_TERM_TAIL_CALL) &&
                block->call_target) {
                u32 target = dolcfg_block_starting_at(program, block->call_target);
                if (target == DOLCFG_NO_BLOCK ||
                    plan->block_region[target] != region->id) {
                    region->out_edges++;
                } else {
                    region->internal_edges++;
                }
            }
        }

        if (sccs) {
            qsort(sccs, scc_total, sizeof(u32), compare_u32_asc);
            u32 distinct = 0;
            for (u32 i = 0; i < scc_total; i++) {
                if (i == 0 || sccs[i] != sccs[i - 1u])
                    distinct++;
            }
            region->scc_count = distinct;
            free(sccs);
        }
        plan->cross_region_edges += region->out_edges;
    }

    /* In-edges are the mirror of everyone else's out-edges. */
    for (u32 i = 0; i < program->block_count; i++) {
        const DolCfgBlock* block = &program->blocks[i];
        u32 from = plan->block_region[i];
        if (from == DOLCFG_NO_BLOCK)
            continue;
        for (u32 s = 0; s < block->successor_count; s++) {
            u32 next = block->successors[s];
            if (next == DOLCFG_NO_BLOCK)
                continue;
            u32 to = plan->block_region[next];
            if (to != DOLCFG_NO_BLOCK && to != from)
                plan->regions[to].in_edges++;
        }
        if ((block->terminator == DOLCFG_TERM_CALL ||
             block->terminator == DOLCFG_TERM_TAIL_CALL) &&
            block->call_target) {
            u32 target = dolcfg_block_starting_at(program, block->call_target);
            if (target != DOLCFG_NO_BLOCK) {
                u32 to = plan->block_region[target];
                if (to != DOLCFG_NO_BLOCK && to != from)
                    plan->regions[to].in_edges++;
            }
        }
    }
}

bool dolregion_plan_build(DolRegionPlan* plan, const DolCfgProgram* program,
                          DolRegionMode mode, const DolRegionLimits* limits,
                          FILE* diagnostics) {
    if (!plan || !program || !limits)
        return false;

    dolregion_plan_free(plan);
    plan->mode = mode;
    plan->limits = *limits;

    plan->block_region = (u32*)malloc(
        (program->block_count ? program->block_count : 1u) * sizeof(u32));
    plan->function_region = (u32*)malloc(
        (program->function_count ? program->function_count : 1u) * sizeof(u32));
    if (!plan->block_region || !plan->function_region) {
        if (diagnostics)
            fprintf(diagnostics, "error: out of memory planning regions\n");
        return false;
    }
    for (u32 i = 0; i < program->block_count; i++)
        plan->block_region[i] = DOLCFG_NO_BLOCK;
    for (u32 i = 0; i < program->function_count; i++)
        plan->function_region[i] = DOLCFG_NO_BLOCK;

    FunctionBlocks fb;
    memset(&fb, 0, sizeof(fb));
    CallGraph graph;
    memset(&graph, 0, sizeof(graph));
    bool ok = true;

    if (mode != DOLREGION_MODE_FIXED) {
        if (!build_function_blocks(program, &fb)) {
            if (diagnostics)
                fprintf(diagnostics, "error: out of memory grouping blocks\n");
            free_function_blocks(&fb);
            return false;
        }
    }

    switch (mode) {
    case DOLREGION_MODE_FIXED:
        ok = plan_fixed(plan, program, limits);
        break;
    case DOLREGION_MODE_FUNCTION:
        ok = plan_function(plan, program, &fb, limits);
        break;
    case DOLREGION_MODE_CFG:
    case DOLREGION_MODE_PGO: {
        bool any_weight = false;
        for (u32 i = 0; i < program->block_count && !any_weight; i++) {
            if (program->blocks[i].weight != 0)
                any_weight = true;
        }
        if (mode == DOLREGION_MODE_PGO && !any_weight) {
            /* Degrading silently would make an unprofiled build look profiled,
               which is the specific failure the PGO staleness gate exists to
               prevent elsewhere. Say it. */
            plan->profile_missing = true;
            if (diagnostics) {
                fprintf(diagnostics,
                        "warning: --region-mode pgo with no profile weights; "
                        "falling back to cfg ordering\n");
            }
        }
        if (!build_call_graph(program, &graph)) {
            if (diagnostics)
                fprintf(diagnostics, "error: out of memory building call graph\n");
            free_function_blocks(&fb);
            return false;
        }
        ok = plan_accretive(plan, program, &fb, &graph, limits,
                            mode == DOLREGION_MODE_PGO && any_weight);
        break;
    }
    default:
        ok = false;
        break;
    }

    free_call_graph(&graph);
    free_function_blocks(&fb);

    if (!ok) {
        if (diagnostics)
            fprintf(diagnostics, "error: region planning failed\n");
        return false;
    }

    finalise_regions(plan, program);
    return true;
}

/* --- report --------------------------------------------------------------- */

bool dolregion_write_report(const DolRegionPlan* plan,
                            const DolCfgProgram* program, const char* path,
                            FILE* diagnostics) {
    if (!plan || !program || !path)
        return false;

    FILE* out = fopen(path, "wb");
    if (!out) {
        if (diagnostics)
            fprintf(diagnostics, "error: cannot write region report '%s'\n", path);
        return false;
    }

    fputs("{\n", out);
    fputs("  \"schema\": \"dolrecomp.regions/1\",\n", out);
    fprintf(out, "  \"mode\": \"%s\",\n", dolregion_mode_name(plan->mode));
    fprintf(out, "  \"profile_missing\": %s,\n",
            plan->profile_missing ? "true" : "false");
    fputs("  \"limits\": {\n", out);
    fprintf(out, "    \"max_instructions\": %u,\n", plan->limits.max_instructions);
    fprintf(out, "    \"max_ir_instructions\": %u,\n",
            plan->limits.max_ir_instructions);
    fprintf(out, "    \"max_functions\": %u,\n", plan->limits.max_functions);
    fprintf(out, "    \"cold_weight_threshold\": %llu\n",
            (unsigned long long)plan->limits.cold_weight_threshold);
    fputs("  },\n", out);
    fputs("  \"totals\": {\n", out);
    fprintf(out, "    \"regions\": %u,\n", plan->region_count);
    fprintf(out, "    \"instructions\": %u,\n", plan->total_instructions);
    fprintf(out, "    \"split_functions\": %u,\n", plan->split_functions);
    fprintf(out, "    \"cross_region_edges\": %u\n", plan->cross_region_edges);
    fputs("  },\n", out);

    fputs("  \"regions\": [\n", out);
    for (u32 r = 0; r < plan->region_count; r++) {
        const DolRegion* region = &plan->regions[r];
        fprintf(out,
                "    {\"id\": %u, \"start\": \"0x%08X\", \"end\": \"0x%08X\", "
                "\"instructions\": %u, \"ir_estimate\": %u, \"blocks\": %u, "
                "\"functions\": %u, \"loops\": %u, \"sccs\": %u, "
                "\"in_edges\": %u, \"out_edges\": %u, \"internal_edges\": %u, "
                "\"indirect_sites\": %u, \"weight\": %llu, "
                "\"patchable\": %s, \"smc\": %s, \"end_reason\": \"%s\"",
                region->id, region->guest_start, region->guest_end,
                region->instruction_count, region->estimated_ir_instructions,
                region->block_count, region->function_count, region->loop_count,
                region->scc_count, region->in_edges, region->out_edges,
                region->internal_edges, region->indirect_sites,
                (unsigned long long)region->weight,
                region->patchable ? "true" : "false",
                region->contains_smc ? "true" : "false",
                dolregion_end_reason_name(region->end_reason));

        fputs(", \"function_addresses\": [", out);
        for (u32 f = 0; f < region->function_count; f++) {
            fprintf(out, "%s\"0x%08X\"", f ? ", " : "",
                    program->functions[region->functions[f]].entry_address);
        }
        fputs("]}", out);
        fputs(r + 1u < plan->region_count ? ",\n" : "\n", out);
    }
    fputs("  ]\n", out);
    fputs("}\n", out);

    if (fclose(out) != 0) {
        if (diagnostics)
            fprintf(diagnostics, "error: failed to close region report '%s'\n", path);
        return false;
    }
    return true;
}
