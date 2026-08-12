#ifndef DOLRECOMP_ANALYSIS_CFG_H
#define DOLRECOMP_ANALYSIS_CFG_H

/* Whole-title control-flow and call-graph model.
 *
 * The fixed-chunk backends cut the program every N instructions, which puts a
 * state materialization and a dispatcher round trip wherever the cut lands --
 * through a hot loop as readily as through cold code. Choosing better cut
 * points needs a model of where control actually flows, which is what this is.
 *
 * Scope and honesty about it:
 *
 *   Direct control flow is recovered exactly. Every b/bc target is a constant
 *   in the instruction word, so block boundaries and direct edges are facts.
 *
 *   Indirect control flow is NOT resolved here. bclr/bcctr sites are recorded
 *   as indirect exits with no successors. Phase 4 attaches target sets to those
 *   sites; until then a region must end at one. Anything else would be guessing
 *   at control flow, which corrupts the program silently rather than loudly.
 *
 *   Function entries are *inferred* -- from bl targets, from an explicit symbol
 *   map, and from section entry points. A MAP improves the model but is never
 *   required, and its absence must not change correctness, only region quality.
 *
 * Embedded data is excluded up front: PPCInst carries an embedded_data flag
 * from the existing analysis pass, and those words never become blocks.
 */

#include "common/types.h"
#include "frontend/decoder.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* Falls out of the bottom into the next block. */
    DOLCFG_TERM_FALLTHROUGH,
    /* b / ba: one known successor, no link. */
    DOLCFG_TERM_BRANCH,
    /* bc / bca: taken target plus fallthrough. */
    DOLCFG_TERM_COND_BRANCH,
    /* bl / bla: a call. Successor is the return point, not the callee. */
    DOLCFG_TERM_CALL,
    /* b to a known function entry with no link: a tail call. */
    DOLCFG_TERM_TAIL_CALL,
    /* bclr -- usually a return, but never assume it. */
    DOLCFG_TERM_RETURN,
    /* bcctr / bclrl and friends: unresolved indirect transfer. */
    DOLCFG_TERM_INDIRECT,
    /* sc, rfi, tw/twi: leaves through the runtime. */
    DOLCFG_TERM_SYSTEM,
    /* Ran into embedded data, a section end, or an undecodable word. */
    DOLCFG_TERM_UNKNOWN,
} DolCfgTerminator;

enum {
    DOLCFG_BLOCK_FUNCTION_ENTRY = 1u << 0,
    DOLCFG_BLOCK_LOOP_HEADER    = 1u << 1,
    /* Reached only by an indirect edge or not reached at all: region formation
       must not assume it is contiguous with its neighbours. */
    DOLCFG_BLOCK_UNREACHED      = 1u << 2,
    /* Overlaps a range the SMC pass flagged as possibly self-modifying. */
    DOLCFG_BLOCK_SMC_SUSPECT    = 1u << 3,
    /* Conditional branch that is also a link (bcl): both call and condition. */
    DOLCFG_BLOCK_CONDITIONAL_CALL = 1u << 4,
};

#define DOLCFG_NO_BLOCK 0xFFFFFFFFu

typedef struct {
    u32 start;              /* guest address, inclusive */
    u32 end;                /* guest address, exclusive */
    u32 instruction_count;
    u32 function;           /* owning function index, or DOLCFG_NO_BLOCK */
    u32 flags;

    DolCfgTerminator terminator;
    /* Successor block indices. successors[0] is the taken/only target,
       successors[1] the fallthrough of a conditional. DOLCFG_NO_BLOCK when the
       edge leaves the model (unresolved indirect, or outside any section). */
    u32 successors[2];
    u32 successor_count;
    /* Guest addresses of the same, kept even when the target is outside the
       loaded sections so cross-module edges stay visible. */
    u32 successor_addresses[2];

    /* Call target for CALL/TAIL_CALL terminators, or 0. */
    u32 call_target;

    /* Loop nesting depth, 0 for straight-line code. */
    u32 loop_depth;
    /* Strongly-connected-component id; blocks sharing one are mutually
       reachable and should not be split across regions when hot. */
    u32 scc;

    /* Profile weight, 0 when no profile is loaded. */
    u64 weight;
} DolCfgBlock;

enum {
    DOLCFG_FUNC_FROM_SYMBOL   = 1u << 0,  /* named by a MAP */
    DOLCFG_FUNC_FROM_CALL     = 1u << 1,  /* inferred from a bl target */
    DOLCFG_FUNC_FROM_ENTRY    = 1u << 2,  /* section/module entry point */
    /* No direct edge in the whole program reaches this block, so control can
       only arrive indirectly -- a vtable slot, a function-pointer table, a
       jump-table entry. It is an entry point by elimination. */
    DOLCFG_FUNC_FROM_INDIRECT = 1u << 6,
    DOLCFG_FUNC_HAS_INDIRECT  = 1u << 3,
    DOLCFG_FUNC_HAS_SMC       = 1u << 4,
    /* Externally visible: a mod or replacement may intercept it, so it keeps a
       public wrapper even under aggressive linking. */
    DOLCFG_FUNC_PATCHABLE     = 1u << 5,
};

typedef struct {
    u32 entry_address;
    u32 entry_block;
    u32 first_block;        /* index of lowest-addressed owned block */
    u32 block_count;
    u32 instruction_count;
    u32 flags;
    u64 weight;
    char name[64];          /* from a MAP, else empty */
} DolCfgFunction;

typedef struct {
    const PPCInst* insts;
    u32 count;
    u32 base_address;
    const char* label;
} DolCfgSection;

/* Build inputs, supplied before dolcfg_build(). */
typedef struct {
    u32 address;
    u32 flags;
    char name[64];
} DolCfgKnownFunction;

typedef struct {
    u32 start;
    u32 end;
} DolCfgSmcRange;

typedef struct {
    DolCfgBlock* blocks;
    u32 block_count;
    u32 block_capacity;

    DolCfgKnownFunction* known;
    u32 known_count;
    u32 known_capacity;

    DolCfgSmcRange* smc;
    u32 smc_count;
    u32 smc_capacity;

    DolCfgFunction* functions;
    u32 function_count;
    u32 function_capacity;

    DolCfgSection* sections;
    u32 section_count;
    u32 section_capacity;

    u32 entry_point;

    /* Sorted block start addresses, parallel to a block index, so address
       lookup is a binary search rather than a scan. */
    u32* sorted_starts;
    u32* sorted_index;

    u32 scc_count;
    u32 loop_count;
    u32 indirect_site_count;
} DolCfgProgram;

void dolcfg_init(DolCfgProgram* program);
void dolcfg_free(DolCfgProgram* program);

/* Adds a decoded section. The instruction array must outlive the program. */
bool dolcfg_add_section(DolCfgProgram* program, const PPCInst* insts, u32 count,
                        u32 base_address, const char* label);

/* Declares a known function entry ahead of the build, from a MAP or an entry
   point. `name` may be NULL. Entries outside any added section are ignored. */
bool dolcfg_add_known_function(DolCfgProgram* program, u32 address,
                               const char* name, u32 flags);

/* Marks an address range as SMC-suspect, so blocks overlapping it are flagged
   and region formation can end at them. */
bool dolcfg_add_smc_range(DolCfgProgram* program, u32 start, u32 end);

/* Builds blocks, edges, functions, loops and SCCs. Deterministic: the same
   sections and known-function set always produce the same numbering. */
bool dolcfg_build(DolCfgProgram* program, FILE* diagnostics);

/* Loads execution weights and attaches them to functions and their blocks.
 *
 * The file is one entry per line, `<address> <count>`, with `#` comments and
 * blank lines ignored -- the shape ModernGekko's hot-entry lists already use:
 *
 *     0x800EB5C0  # 83,166,563,414
 *     0x800E6FC0 197140421
 *
 * A count after the address is used when present; an address with no count is
 * treated as hot with weight 1, so a bare hot-entry list still works.
 *
 * Deliberately not an LLVM .profdata reader. Keeping the analysis layer in C
 * and free of an LLVM dependency matters more than avoiding one conversion
 * step, and a text format is something a test can write by hand. Convert with
 * benchmarks/profdata_to_weights.py.
 *
 * Call after dolcfg_build(), which is when functions and blocks exist.
 * Addresses outside any known function are counted and reported, not fatal:
 * a profile from a different build should degrade, loudly, rather than fail. */
bool dolcfg_load_profile(DolCfgProgram* program, const char* path,
                         u32* matched_out, u32* unmatched_out,
                         FILE* diagnostics);

/* Block index containing `address`, or DOLCFG_NO_BLOCK. */
u32 dolcfg_block_at(const DolCfgProgram* program, u32 address);

/* Block index whose start is exactly `address`, or DOLCFG_NO_BLOCK. */
u32 dolcfg_block_starting_at(const DolCfgProgram* program, u32 address);

const char* dolcfg_terminator_name(DolCfgTerminator kind);

#ifdef __cplusplus
}
#endif

#endif /* DOLRECOMP_ANALYSIS_CFG_H */
