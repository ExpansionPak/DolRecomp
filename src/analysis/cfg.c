#include "analysis/cfg.h"

#include <stdlib.h>
#include <string.h>

/* A branch is unconditional when BO is 1z1zz -- no CTR decrement and no CR
   test. Everything else keeps a fallthrough edge, including the CTR-decrement
   forms, which are conditional on CTR reaching zero. */
static bool bo_is_unconditional(u8 bo) {
    return (bo & 0x14u) == 0x14u;
}

typedef struct {
    u32* items;
    u32 count;
    u32 capacity;
} AddrSet;

static bool addr_set_push(AddrSet* set, u32 value) {
    if (set->count == set->capacity) {
        u32 capacity = set->capacity ? set->capacity * 2u : 256u;
        u32* grown = (u32*)realloc(set->items, capacity * sizeof(u32));
        if (!grown)
            return false;
        set->items = grown;
        set->capacity = capacity;
    }
    set->items[set->count++] = value;
    return true;
}

static int compare_u32(const void* a, const void* b) {
    u32 left = *(const u32*)a;
    u32 right = *(const u32*)b;
    return (left > right) - (left < right);
}

static void addr_set_sort_unique(AddrSet* set) {
    if (set->count == 0)
        return;
    qsort(set->items, set->count, sizeof(u32), compare_u32);
    u32 out = 1;
    for (u32 i = 1; i < set->count; i++) {
        if (set->items[i] != set->items[out - 1])
            set->items[out++] = set->items[i];
    }
    set->count = out;
}

static bool addr_set_contains(const AddrSet* set, u32 value) {
    u32 low = 0;
    u32 high = set->count;
    while (low < high) {
        u32 mid = low + (high - low) / 2u;
        if (set->items[mid] == value)
            return true;
        if (set->items[mid] < value)
            low = mid + 1u;
        else
            high = mid;
    }
    return false;
}

static void addr_set_free(AddrSet* set) {
    free(set->items);
    set->items = NULL;
    set->count = 0;
    set->capacity = 0;
}

/* --- program lifetime ---------------------------------------------------- */

void dolcfg_init(DolCfgProgram* program) {
    if (!program)
        return;
    memset(program, 0, sizeof(*program));
}

void dolcfg_free(DolCfgProgram* program) {
    if (!program)
        return;
    free(program->blocks);
    free(program->functions);
    free(program->sections);
    free(program->sorted_starts);
    free(program->sorted_index);
    free(program->known);
    free(program->smc);
    memset(program, 0, sizeof(*program));
}

bool dolcfg_add_section(DolCfgProgram* program, const PPCInst* insts, u32 count,
                        u32 base_address, const char* label) {
    if (!program || !insts || count == 0)
        return false;

    if (program->section_count == program->section_capacity) {
        u32 capacity = program->section_capacity ? program->section_capacity * 2u : 8u;
        DolCfgSection* grown = (DolCfgSection*)realloc(
            program->sections, capacity * sizeof(*grown));
        if (!grown)
            return false;
        program->sections = grown;
        program->section_capacity = capacity;
    }

    DolCfgSection* section = &program->sections[program->section_count++];
    section->insts = insts;
    section->count = count;
    section->base_address = base_address;
    section->label = label;
    return true;
}

bool dolcfg_add_known_function(DolCfgProgram* program, u32 address,
                               const char* name, u32 flags) {
    if (!program)
        return false;
    if (program->known_count == program->known_capacity) {
        u32 capacity = program->known_capacity ? program->known_capacity * 2u : 256u;
        DolCfgKnownFunction* grown = (DolCfgKnownFunction*)realloc(
            program->known, capacity * sizeof(*grown));
        if (!grown)
            return false;
        program->known = grown;
        program->known_capacity = capacity;
    }

    DolCfgKnownFunction* entry = &program->known[program->known_count++];
    entry->address = address;
    entry->flags = flags;
    entry->name[0] = '\0';
    if (name)
        snprintf(entry->name, sizeof(entry->name), "%s", name);
    return true;
}

bool dolcfg_add_smc_range(DolCfgProgram* program, u32 start, u32 end) {
    if (!program)
        return false;
    if (program->smc_count == program->smc_capacity) {
        u32 capacity = program->smc_capacity ? program->smc_capacity * 2u : 64u;
        DolCfgSmcRange* grown =
            (DolCfgSmcRange*)realloc(program->smc, capacity * sizeof(*grown));
        if (!grown)
            return false;
        program->smc = grown;
        program->smc_capacity = capacity;
    }
    program->smc[program->smc_count].start = start;
    program->smc[program->smc_count].end = end;
    program->smc_count++;
    return true;
}

/* --- section helpers ----------------------------------------------------- */

static const DolCfgSection* section_for(const DolCfgProgram* program, u32 address) {
    for (u32 i = 0; i < program->section_count; i++) {
        const DolCfgSection* section = &program->sections[i];
        u32 end = section->base_address + section->count * 4u;
        if (address >= section->base_address && address < end)
            return section;
    }
    return NULL;
}

static bool address_is_smc(const DolCfgProgram* program, u32 address) {
    for (u32 i = 0; i < program->smc_count; i++) {
        if (address >= program->smc[i].start && address <= program->smc[i].end)
            return true;
    }
    return false;
}

/* Does this instruction end a basic block, and if so how? */
static bool classifies_as_terminator(const PPCInst* inst) {
    switch (inst->op) {
    case PPC_OP_B:
    case PPC_OP_BC:
    case PPC_OP_BCLR:
    case PPC_OP_BCCTR:
    case PPC_OP_SC:
    case PPC_OP_RFI:
        return true;
    default:
        return false;
    }
}

/* --- block construction -------------------------------------------------- */

static bool push_block(DolCfgProgram* program, const DolCfgBlock* block) {
    if (program->block_count == program->block_capacity) {
        u32 capacity = program->block_capacity ? program->block_capacity * 2u : 1024u;
        DolCfgBlock* grown =
            (DolCfgBlock*)realloc(program->blocks, capacity * sizeof(*grown));
        if (!grown)
            return false;
        program->blocks = grown;
        program->block_capacity = capacity;
    }
    program->blocks[program->block_count++] = *block;
    return true;
}

static bool collect_leaders(DolCfgProgram* program, AddrSet* leaders) {
    for (u32 s = 0; s < program->section_count; s++) {
        const DolCfgSection* section = &program->sections[s];
        if (!addr_set_push(leaders, section->base_address))
            return false;

        for (u32 i = 0; i < section->count; i++) {
            const PPCInst* inst = &section->insts[i];
            u32 address = section->base_address + i * 4u;

            /* Data is not code. A leader here would manufacture a block out of
               a jump table or a string. */
            if (inst->embedded_data)
                continue;

            /* The word after data resumes code. */
            if (i > 0 && section->insts[i - 1].embedded_data) {
                if (!addr_set_push(leaders, address))
                    return false;
            }

            if (!classifies_as_terminator(inst))
                continue;

            /* Everything after a control transfer starts a block. */
            if (i + 1u < section->count) {
                if (!addr_set_push(leaders, address + 4u))
                    return false;
            }

            /* Direct targets are constants in the word, so this is exact. */
            if ((inst->op == PPC_OP_B || inst->op == PPC_OP_BC) &&
                !addr_set_push(leaders, inst->branch_target)) {
                return false;
            }
        }
    }

    for (u32 i = 0; i < program->known_count; i++) {
        if (!addr_set_push(leaders, program->known[i].address))
            return false;
    }
    if (program->entry_point && !addr_set_push(leaders, program->entry_point))
        return false;

    addr_set_sort_unique(leaders);
    return true;
}

static void classify_terminator(DolCfgProgram* program, DolCfgBlock* block,
                                const PPCInst* inst, u32 next_address,
                                bool next_in_section) {
    block->successor_count = 0;
    block->successors[0] = DOLCFG_NO_BLOCK;
    block->successors[1] = DOLCFG_NO_BLOCK;
    block->successor_addresses[0] = 0;
    block->successor_addresses[1] = 0;
    block->call_target = 0;

    if (!inst) {
        /* No control-transfer instruction: the block ended because the next
           address is a leader (something branches there), or because code ran
           out. The first case falls through; only the second is unknown. */
        if (next_in_section) {
            block->terminator = DOLCFG_TERM_FALLTHROUGH;
            block->successor_addresses[0] = next_address;
            block->successor_count = 1;
        } else {
            block->terminator = DOLCFG_TERM_UNKNOWN;
        }
        return;
    }

    switch (inst->op) {
    case PPC_OP_B:
        if (inst->lk) {
            /* bl: the callee is a separate function; control comes back to the
               next instruction, which is this block's successor. */
            block->terminator = DOLCFG_TERM_CALL;
            block->call_target = inst->branch_target;
            if (next_in_section) {
                block->successor_addresses[0] = next_address;
                block->successor_count = 1;
            }
        } else {
            /* Reclassified to TAIL_CALL later, once function entries are
               known -- a b to another function's entry is a tail call. */
            block->terminator = DOLCFG_TERM_BRANCH;
            block->successor_addresses[0] = inst->branch_target;
            block->successor_count = 1;
        }
        break;

    case PPC_OP_BC:
        if (inst->lk)
            block->flags |= DOLCFG_BLOCK_CONDITIONAL_CALL;

        block->successor_addresses[0] = inst->branch_target;
        block->successor_count = 1;
        if (bo_is_unconditional(inst->bo)) {
            block->terminator = DOLCFG_TERM_BRANCH;
        } else {
            block->terminator = DOLCFG_TERM_COND_BRANCH;
            if (next_in_section) {
                block->successor_addresses[1] = next_address;
                block->successor_count = 2;
            }
        }
        break;

    case PPC_OP_BCLR:
        /* Usually a return. Never assumed to be: a conditional blr keeps its
           fallthrough, and Phase 4 handles the ones that are not returns at
           all through the indirect path. */
        block->terminator = DOLCFG_TERM_RETURN;
        if (!bo_is_unconditional(inst->bo) && next_in_section) {
            block->successor_addresses[0] = next_address;
            block->successor_count = 1;
        }
        program->indirect_site_count++;
        break;

    case PPC_OP_BCCTR:
        block->terminator = DOLCFG_TERM_INDIRECT;
        /* An indirect call returns to the next instruction; a conditional
           indirect branch falls through to it. Either way it is a successor. */
        if ((inst->lk || !bo_is_unconditional(inst->bo)) && next_in_section) {
            block->successor_addresses[0] = next_address;
            block->successor_count = 1;
        }
        program->indirect_site_count++;
        break;

    case PPC_OP_SC:
        block->terminator = DOLCFG_TERM_SYSTEM;
        if (next_in_section) {
            block->successor_addresses[0] = next_address;
            block->successor_count = 1;
        }
        break;

    case PPC_OP_RFI:
        block->terminator = DOLCFG_TERM_SYSTEM;
        break;

    default:
        block->terminator = DOLCFG_TERM_FALLTHROUGH;
        if (next_in_section) {
            block->successor_addresses[0] = next_address;
            block->successor_count = 1;
        }
        break;
    }
}

static bool build_blocks(DolCfgProgram* program, const AddrSet* leaders) {
    for (u32 s = 0; s < program->section_count; s++) {
        const DolCfgSection* section = &program->sections[s];
        u32 i = 0;

        while (i < section->count) {
            /* Skip runs of embedded data outright. */
            if (section->insts[i].embedded_data) {
                i++;
                continue;
            }

            u32 start = section->base_address + i * 4u;
            u32 j = i;
            const PPCInst* terminator_inst = NULL;

            while (j < section->count) {
                const PPCInst* inst = &section->insts[j];
                u32 address = section->base_address + j * 4u;

                if (inst->embedded_data)
                    break;
                if (j != i && addr_set_contains(leaders, address))
                    break;

                if (classifies_as_terminator(inst)) {
                    terminator_inst = inst;
                    j++;
                    break;
                }
                j++;
            }

            DolCfgBlock block;
            memset(&block, 0, sizeof(block));
            block.start = start;
            block.end = section->base_address + j * 4u;
            block.instruction_count = j - i;
            block.function = DOLCFG_NO_BLOCK;
            block.scc = DOLCFG_NO_BLOCK;

            for (u32 k = i; k < j; k++) {
                if (address_is_smc(program, section->base_address + k * 4u)) {
                    block.flags |= DOLCFG_BLOCK_SMC_SUSPECT;
                    break;
                }
            }

            u32 next_address = block.end;
            bool next_in_section =
                (j < section->count) && !section->insts[j].embedded_data;

            classify_terminator(program, &block, terminator_inst, next_address,
                                next_in_section);

            if (!push_block(program, &block))
                return false;

            i = j;
        }
    }

    return true;
}

/* --- address index ------------------------------------------------------- */

typedef struct {
    u32 start;
    u32 index;
} StartEntry;

static int compare_start_entry(const void* a, const void* b) {
    u32 left = ((const StartEntry*)a)->start;
    u32 right = ((const StartEntry*)b)->start;
    return (left > right) - (left < right);
}

static bool build_index(DolCfgProgram* program) {
    if (program->block_count == 0)
        return true;

    StartEntry* entries =
        (StartEntry*)malloc(program->block_count * sizeof(*entries));
    if (!entries)
        return false;
    for (u32 i = 0; i < program->block_count; i++) {
        entries[i].start = program->blocks[i].start;
        entries[i].index = i;
    }
    qsort(entries, program->block_count, sizeof(*entries), compare_start_entry);

    program->sorted_starts = (u32*)malloc(program->block_count * sizeof(u32));
    program->sorted_index = (u32*)malloc(program->block_count * sizeof(u32));
    if (!program->sorted_starts || !program->sorted_index) {
        free(entries);
        return false;
    }
    for (u32 i = 0; i < program->block_count; i++) {
        program->sorted_starts[i] = entries[i].start;
        program->sorted_index[i] = entries[i].index;
    }
    free(entries);
    return true;
}

u32 dolcfg_block_starting_at(const DolCfgProgram* program, u32 address) {
    if (!program || program->block_count == 0)
        return DOLCFG_NO_BLOCK;

    u32 low = 0;
    u32 high = program->block_count;
    while (low < high) {
        u32 mid = low + (high - low) / 2u;
        if (program->sorted_starts[mid] == address)
            return program->sorted_index[mid];
        if (program->sorted_starts[mid] < address)
            low = mid + 1u;
        else
            high = mid;
    }
    return DOLCFG_NO_BLOCK;
}

u32 dolcfg_block_at(const DolCfgProgram* program, u32 address) {
    if (!program || program->block_count == 0)
        return DOLCFG_NO_BLOCK;

    /* Largest start <= address, then a containment check. */
    u32 low = 0;
    u32 high = program->block_count;
    while (low < high) {
        u32 mid = low + (high - low) / 2u;
        if (program->sorted_starts[mid] <= address)
            low = mid + 1u;
        else
            high = mid;
    }
    if (low == 0)
        return DOLCFG_NO_BLOCK;

    u32 index = program->sorted_index[low - 1u];
    const DolCfgBlock* block = &program->blocks[index];
    if (address >= block->start && address < block->end)
        return index;
    return DOLCFG_NO_BLOCK;
}

static void resolve_edges(DolCfgProgram* program) {
    for (u32 i = 0; i < program->block_count; i++) {
        DolCfgBlock* block = &program->blocks[i];
        for (u32 s = 0; s < block->successor_count; s++) {
            block->successors[s] =
                dolcfg_block_starting_at(program, block->successor_addresses[s]);
        }
    }
}

/* --- functions ----------------------------------------------------------- */

static bool push_function(DolCfgProgram* program, const DolCfgFunction* fn) {
    if (program->function_count == program->function_capacity) {
        u32 capacity = program->function_capacity ? program->function_capacity * 2u : 256u;
        DolCfgFunction* grown =
            (DolCfgFunction*)realloc(program->functions, capacity * sizeof(*grown));
        if (!grown)
            return false;
        program->functions = grown;
        program->function_capacity = capacity;
    }
    program->functions[program->function_count++] = *fn;
    return true;
}

static bool build_functions(DolCfgProgram* program) {
    /* Entries come from three places, in this precedence: an explicit symbol
       map, the section entry point, and inferred bl targets. A map improves
       naming and boundaries but is never required. */
    AddrSet entries = {0};
    AddrSet indirect_entries = {0};

    for (u32 i = 0; i < program->known_count; i++) {
        if (!addr_set_push(&entries, program->known[i].address)) {
            addr_set_free(&entries);
            addr_set_free(&indirect_entries);
            return false;
        }
    }
    if (program->entry_point && !addr_set_push(&entries, program->entry_point)) {
        addr_set_free(&entries);
        addr_set_free(&indirect_entries);
        return false;
    }
    for (u32 i = 0; i < program->block_count; i++) {
        const DolCfgBlock* block = &program->blocks[i];
        if (block->terminator == DOLCFG_TERM_CALL && block->call_target) {
            if (!addr_set_push(&entries, block->call_target)) {
                addr_set_free(&entries);
                addr_set_free(&indirect_entries);
                return false;
            }
        }
    }

    /* Entries by elimination.
     *
     * A block that no direct edge in the whole program reaches is still
     * executed -- control gets there indirectly, through a vtable slot, a
     * function-pointer table or a jump table. On Mario Kart, seeding only from
     * bl targets left 59% of the code owned by no function, and the roots of
     * that were 22,010 blocks with no in-edge at all. Treating those as entry
     * points is what makes the model describe the whole title rather than the
     * directly-called part of it.
     *
     * This infers *entries*, never edges: nothing here claims to know which
     * indirect site reaches which entry. Phase 4 does that. */
    {
        u32* in_degree = (u32*)calloc(
            program->block_count ? program->block_count : 1u, sizeof(u32));
        if (!in_degree) {
            addr_set_free(&entries);
            return false;
        }
        for (u32 i = 0; i < program->block_count; i++) {
            const DolCfgBlock* block = &program->blocks[i];
            for (u32 s = 0; s < block->successor_count; s++) {
                if (block->successors[s] != DOLCFG_NO_BLOCK)
                    in_degree[block->successors[s]]++;
            }
        }
        for (u32 i = 0; i < program->block_count; i++) {
            if (in_degree[i] != 0)
                continue;
            if (!addr_set_push(&entries, program->blocks[i].start) ||
                !addr_set_push(&indirect_entries, program->blocks[i].start)) {
                free(in_degree);
                addr_set_free(&entries);
                addr_set_free(&indirect_entries);
                return false;
            }
        }
        free(in_degree);
        addr_set_sort_unique(&indirect_entries);
    }

    addr_set_sort_unique(&entries);

    for (u32 i = 0; i < entries.count; i++) {
        u32 address = entries.items[i];
        u32 block_index = dolcfg_block_starting_at(program, address);
        if (block_index == DOLCFG_NO_BLOCK)
            continue; /* Outside the loaded sections: a cross-module call. */

        DolCfgFunction fn;
        memset(&fn, 0, sizeof(fn));
        fn.entry_address = address;
        fn.entry_block = block_index;
        fn.first_block = block_index;
        fn.flags = addr_set_contains(&indirect_entries, address)
                       ? DOLCFG_FUNC_FROM_INDIRECT
                       : DOLCFG_FUNC_FROM_CALL;
        if (program->entry_point == address)
            fn.flags |= DOLCFG_FUNC_FROM_ENTRY;

        for (u32 k = 0; k < program->known_count; k++) {
            if (program->known[k].address != address)
                continue;
            fn.flags |= program->known[k].flags;
            if (program->known[k].name[0]) {
                fn.flags |= DOLCFG_FUNC_FROM_SYMBOL;
                snprintf(fn.name, sizeof(fn.name), "%s", program->known[k].name);
            }
        }

        program->blocks[block_index].flags |= DOLCFG_BLOCK_FUNCTION_ENTRY;
        if (!push_function(program, &fn)) {
            addr_set_free(&entries);
            addr_set_free(&indirect_entries);
            return false;
        }
    }
    addr_set_free(&entries);
    addr_set_free(&indirect_entries);

    /* Ownership by forward reachability from each entry, in address order, so
       the assignment is deterministic. A block already owned is left alone:
       first entry to reach it wins, which keeps shared tails attached to the
       lowest-addressed caller rather than flip-flopping. */
    u32* stack = (u32*)malloc((program->block_count ? program->block_count : 1u) *
                              sizeof(u32));
    if (!stack)
        return false;

    for (u32 f = 0; f < program->function_count; f++) {
        DolCfgFunction* fn = &program->functions[f];
        if (program->blocks[fn->entry_block].function != DOLCFG_NO_BLOCK)
            continue; /* Two entries on one block: the first one owns it. */

        /* Ownership is claimed at push time, not pop time. Claiming on pop
           lets a block with several predecessors sit on the stack more than
           once, and the stack is only block_count deep -- on a real title that
           overflows. Claiming on push makes each block enter the stack at most
           once, which bounds it by construction. */
        u32 top = 0;
        program->blocks[fn->entry_block].function = f;
        stack[top++] = fn->entry_block;

        while (top > 0) {
            u32 index = stack[--top];
            DolCfgBlock* block = &program->blocks[index];

            fn->block_count++;
            fn->instruction_count += block->instruction_count;
            if (block->start < program->blocks[fn->first_block].start)
                fn->first_block = index;
            if (block->terminator == DOLCFG_TERM_INDIRECT)
                fn->flags |= DOLCFG_FUNC_HAS_INDIRECT;
            if (block->flags & DOLCFG_BLOCK_SMC_SUSPECT)
                fn->flags |= DOLCFG_FUNC_HAS_SMC;

            for (u32 s = 0; s < block->successor_count; s++) {
                u32 next = block->successors[s];
                if (next == DOLCFG_NO_BLOCK)
                    continue;
                if (program->blocks[next].function != DOLCFG_NO_BLOCK)
                    continue;
                /* Another function's entry is not part of this one. */
                if (program->blocks[next].flags & DOLCFG_BLOCK_FUNCTION_ENTRY)
                    continue;
                program->blocks[next].function = f;
                stack[top++] = next;
            }
        }
    }
    /* Anything still unowned is a cycle every one of whose blocks has an
       in-edge from inside the cycle, so no zero-in-degree root pointed at it.
       It is still executed -- reached indirectly -- and region formation cannot
       place a block that belongs to no function, so the lowest-addressed
       survivor becomes an entry and the pass repeats until none are left.
       Each round claims at least one block, so this terminates. */
    u32 cursor = 0;
    for (;;) {
        /* The cursor only moves forward: a block passed over is already owned
           and cannot become unowned, so rescanning from zero each round would
           make this quadratic for no benefit. */
        while (cursor < program->block_count &&
               program->blocks[cursor].function != DOLCFG_NO_BLOCK) {
            cursor++;
        }
        if (cursor >= program->block_count)
            break;
        u32 seed = cursor;

        DolCfgFunction fn;
        memset(&fn, 0, sizeof(fn));
        fn.entry_address = program->blocks[seed].start;
        fn.entry_block = seed;
        fn.first_block = seed;
        fn.flags = DOLCFG_FUNC_FROM_INDIRECT;
        program->blocks[seed].flags |= DOLCFG_BLOCK_FUNCTION_ENTRY;
        if (!push_function(program, &fn)) {
            free(stack);
            return false;
        }

        u32 f = program->function_count - 1u;
        u32 top = 0;
        program->blocks[seed].function = f;
        stack[top++] = seed;

        while (top > 0) {
            u32 index = stack[--top];
            DolCfgBlock* block = &program->blocks[index];

            program->functions[f].block_count++;
            program->functions[f].instruction_count += block->instruction_count;
            if (block->start < program->blocks[program->functions[f].first_block].start)
                program->functions[f].first_block = index;
            if (block->terminator == DOLCFG_TERM_INDIRECT)
                program->functions[f].flags |= DOLCFG_FUNC_HAS_INDIRECT;
            if (block->flags & DOLCFG_BLOCK_SMC_SUSPECT)
                program->functions[f].flags |= DOLCFG_FUNC_HAS_SMC;

            for (u32 s = 0; s < block->successor_count; s++) {
                u32 next = block->successors[s];
                if (next == DOLCFG_NO_BLOCK)
                    continue;
                if (program->blocks[next].function != DOLCFG_NO_BLOCK)
                    continue;
                if (program->blocks[next].flags & DOLCFG_BLOCK_FUNCTION_ENTRY)
                    continue;
                program->blocks[next].function = f;
                stack[top++] = next;
            }
        }
    }

    free(stack);

    /* A plain b whose target is another function's entry is a tail call. This
       needs the entry set, so it cannot happen during classification. */
    for (u32 i = 0; i < program->block_count; i++) {
        DolCfgBlock* block = &program->blocks[i];
        if (block->terminator != DOLCFG_TERM_BRANCH || block->successor_count != 1)
            continue;
        u32 target = block->successors[0];
        if (target == DOLCFG_NO_BLOCK)
            continue;
        if ((program->blocks[target].flags & DOLCFG_BLOCK_FUNCTION_ENTRY) &&
            program->blocks[target].function != block->function) {
            block->terminator = DOLCFG_TERM_TAIL_CALL;
            block->call_target = program->blocks[target].start;
        }
    }

    /* Blocks no entry reached are only enterable indirectly. */
    for (u32 i = 0; i < program->block_count; i++) {
        if (program->blocks[i].function == DOLCFG_NO_BLOCK)
            program->blocks[i].flags |= DOLCFG_BLOCK_UNREACHED;
    }

    return true;
}

/* --- loops and SCCs ------------------------------------------------------ */

/* Tarjan, iterative: the recursive form overflows on a real title's call
   graph. Also fills loop headers, since a back edge into an SCC member is
   exactly a loop entry. */
typedef struct {
    u32* index;
    u32* lowlink;
    u32* stack;
    bool* on_stack;
    u32* work;
    u32* work_edge;
    u32 next_index;
    u32 stack_top;
} Tarjan;

static bool compute_sccs(DolCfgProgram* program) {
    u32 n = program->block_count;
    if (n == 0)
        return true;

    Tarjan t;
    memset(&t, 0, sizeof(t));
    t.index = (u32*)malloc(n * sizeof(u32));
    t.lowlink = (u32*)malloc(n * sizeof(u32));
    t.stack = (u32*)malloc(n * sizeof(u32));
    t.on_stack = (bool*)calloc(n, sizeof(bool));
    t.work = (u32*)malloc(n * sizeof(u32));
    t.work_edge = (u32*)malloc(n * sizeof(u32));
    if (!t.index || !t.lowlink || !t.stack || !t.on_stack || !t.work ||
        !t.work_edge) {
        free(t.index); free(t.lowlink); free(t.stack);
        free(t.on_stack); free(t.work); free(t.work_edge);
        return false;
    }

    for (u32 i = 0; i < n; i++)
        t.index[i] = DOLCFG_NO_BLOCK;
    t.next_index = 0;
    t.stack_top = 0;
    program->scc_count = 0;

    for (u32 root = 0; root < n; root++) {
        if (t.index[root] != DOLCFG_NO_BLOCK)
            continue;

        u32 work_top = 0;
        t.work[work_top] = root;
        t.work_edge[work_top] = 0;
        t.index[root] = t.lowlink[root] = t.next_index++;
        t.stack[t.stack_top++] = root;
        t.on_stack[root] = true;
        work_top++;

        while (work_top > 0) {
            u32 v = t.work[work_top - 1u];
            u32 edge = t.work_edge[work_top - 1u];

            if (edge < program->blocks[v].successor_count) {
                t.work_edge[work_top - 1u] = edge + 1u;
                u32 w = program->blocks[v].successors[edge];
                if (w == DOLCFG_NO_BLOCK)
                    continue;

                if (t.index[w] == DOLCFG_NO_BLOCK) {
                    t.index[w] = t.lowlink[w] = t.next_index++;
                    t.stack[t.stack_top++] = w;
                    t.on_stack[w] = true;
                    t.work[work_top] = w;
                    t.work_edge[work_top] = 0;
                    work_top++;
                } else if (t.on_stack[w]) {
                    if (t.index[w] < t.lowlink[v])
                        t.lowlink[v] = t.index[w];
                }
                continue;
            }

            work_top--;
            if (work_top > 0) {
                u32 parent = t.work[work_top - 1u];
                if (t.lowlink[v] < t.lowlink[parent])
                    t.lowlink[parent] = t.lowlink[v];
            }

            if (t.lowlink[v] == t.index[v]) {
                u32 members = 0;
                u32 w;
                do {
                    w = t.stack[--t.stack_top];
                    t.on_stack[w] = false;
                    program->blocks[w].scc = program->scc_count;
                    members++;
                } while (w != v);

                /* A single block is only cyclic if it branches to itself. */
                if (members == 1) {
                    bool self = false;
                    for (u32 s = 0; s < program->blocks[v].successor_count; s++) {
                        if (program->blocks[v].successors[s] == v)
                            self = true;
                    }
                    if (self)
                        program->blocks[v].flags |= DOLCFG_BLOCK_LOOP_HEADER;
                }
                program->scc_count++;
            }
        }
    }

    /* Within a multi-block SCC, an edge arriving from outside marks a loop
       header; the whole SCC is cyclic by definition. */
    for (u32 i = 0; i < n; i++) {
        const DolCfgBlock* block = &program->blocks[i];
        for (u32 s = 0; s < block->successor_count; s++) {
            u32 next = block->successors[s];
            if (next == DOLCFG_NO_BLOCK)
                continue;
            if (program->blocks[next].scc == block->scc && next <= i)
                program->blocks[next].flags |= DOLCFG_BLOCK_LOOP_HEADER;
        }
    }

    /* Loop depth: how many distinct multi-block SCCs, plus self-loops, a block
       participates in. A block is in at most one SCC, so depth is 0 or 1 here;
       nesting beyond that needs a dominator tree, which Phase 1 does not
       require and which is recorded as a Phase 2 refinement. */
    u32* scc_size = (u32*)calloc(program->scc_count ? program->scc_count : 1u,
                                 sizeof(u32));
    if (scc_size) {
        for (u32 i = 0; i < n; i++) {
            if (program->blocks[i].scc != DOLCFG_NO_BLOCK)
                scc_size[program->blocks[i].scc]++;
        }
        for (u32 i = 0; i < n; i++) {
            u32 scc = program->blocks[i].scc;
            bool cyclic = (scc != DOLCFG_NO_BLOCK && scc_size[scc] > 1u) ||
                          (program->blocks[i].flags & DOLCFG_BLOCK_LOOP_HEADER);
            program->blocks[i].loop_depth = cyclic ? 1u : 0u;
        }
        free(scc_size);
    }

    program->loop_count = 0;
    for (u32 i = 0; i < n; i++) {
        if (program->blocks[i].flags & DOLCFG_BLOCK_LOOP_HEADER)
            program->loop_count++;
    }

    free(t.index); free(t.lowlink); free(t.stack);
    free(t.on_stack); free(t.work); free(t.work_edge);
    return true;
}

/* --- build --------------------------------------------------------------- */

bool dolcfg_build(DolCfgProgram* program, FILE* diagnostics) {
    if (!program || program->section_count == 0) {
        if (diagnostics)
            fprintf(diagnostics, "error: CFG build needs at least one section\n");
        return false;
    }

    AddrSet leaders = {0};
    if (!collect_leaders(program, &leaders)) {
        addr_set_free(&leaders);
        if (diagnostics)
            fprintf(diagnostics, "error: out of memory collecting CFG leaders\n");
        return false;
    }

    bool ok = build_blocks(program, &leaders);
    addr_set_free(&leaders);
    if (!ok) {
        if (diagnostics)
            fprintf(diagnostics, "error: out of memory building CFG blocks\n");
        return false;
    }

    /* Order matters: ownership traversal and Tarjan both walk successor block
       indices, which only exist once the address edges are resolved. */
    if (!build_index(program)) {
        if (diagnostics)
            fprintf(diagnostics, "error: out of memory indexing CFG blocks\n");
        return false;
    }
    resolve_edges(program);

    if (!build_functions(program) || !compute_sccs(program)) {
        if (diagnostics)
            fprintf(diagnostics, "error: out of memory analysing CFG\n");
        return false;
    }
    return true;
}

bool dolcfg_load_profile(DolCfgProgram* program, const char* path,
                         u32* matched_out, u32* unmatched_out,
                         FILE* diagnostics) {
    if (!program || !path)
        return false;

    FILE* in = fopen(path, "rb");
    if (!in) {
        if (diagnostics)
            fprintf(diagnostics, "error: cannot read region profile '%s'\n", path);
        return false;
    }

    char line[512];
    u32 matched = 0;
    u32 unmatched = 0;

    while (fgets(line, sizeof(line), in)) {
        char* cursor = line;
        while (*cursor == ' ' || *cursor == '\t')
            cursor++;
        if (*cursor == '#' || *cursor == '\n' || *cursor == '\r' || *cursor == '\0')
            continue;

        char* end = NULL;
        unsigned long address = strtoul(cursor, &end, 0);
        if (end == cursor)
            continue;

        /* An address on its own means "hot" without saying how hot. */
        u64 count = 1;
        while (*end == ' ' || *end == '\t')
            end++;
        if (*end && *end != '#' && *end != '\n' && *end != '\r') {
            char* count_end = NULL;
            unsigned long long parsed = strtoull(end, &count_end, 0);
            if (count_end != end)
                count = (u64)parsed;
        }

        /* The profile names function entries, so weight the whole function.
           Per-block resolution would need the profile's own CFG, which is the
           generated module's, not the guest's. */
        u32 block = dolcfg_block_starting_at(program, (u32)address);
        if (block == DOLCFG_NO_BLOCK)
            block = dolcfg_block_at(program, (u32)address);
        if (block == DOLCFG_NO_BLOCK) {
            unmatched++;
            continue;
        }

        u32 function = program->blocks[block].function;
        if (function == DOLCFG_NO_BLOCK) {
            program->blocks[block].weight += count;
            matched++;
            continue;
        }

        /* Accumulate onto the function only. Spreading to its blocks here would
           be O(entries x blocks) -- tolerable for a 78-entry hot list, hours for
           a full 23,311-function profile. One pass afterwards does it in O(n). */
        program->functions[function].weight += count;
        matched++;
    }

    fclose(in);

    for (u32 i = 0; i < program->block_count; i++) {
        u32 function = program->blocks[i].function;
        if (function != DOLCFG_NO_BLOCK && program->functions[function].weight)
            program->blocks[i].weight += program->functions[function].weight;
    }

    if (matched_out)
        *matched_out = matched;
    if (unmatched_out)
        *unmatched_out = unmatched;

    if (diagnostics && unmatched) {
        fprintf(diagnostics,
                "warning: region profile '%s': %u of %u entries matched no known "
                "function; the profile may be from a different build\n",
                path, unmatched, matched + unmatched);
    }
    if (!matched) {
        if (diagnostics)
            fprintf(diagnostics,
                    "error: region profile '%s' matched nothing in this program\n",
                    path);
        return false;
    }
    return true;
}

const char* dolcfg_terminator_name(DolCfgTerminator kind) {
    switch (kind) {
    case DOLCFG_TERM_FALLTHROUGH: return "fallthrough";
    case DOLCFG_TERM_BRANCH:      return "branch";
    case DOLCFG_TERM_COND_BRANCH: return "cond-branch";
    case DOLCFG_TERM_CALL:        return "call";
    case DOLCFG_TERM_TAIL_CALL:   return "tail-call";
    case DOLCFG_TERM_RETURN:      return "return";
    case DOLCFG_TERM_INDIRECT:    return "indirect";
    case DOLCFG_TERM_SYSTEM:      return "system";
    case DOLCFG_TERM_UNKNOWN:
    default:                      return "unknown";
    }
}
