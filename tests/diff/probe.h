#ifndef DOLRECOMP_DIFF_PROBE_H
#define DOLRECOMP_DIFF_PROBE_H

/*
 * Emitted-code acceptance differential -- shared probe schema.
 *
 * One data-only probe table (probes.inc) is the single source of truth, consumed
 * by two engines:
 *   - gen.c  : decodes each probe's raw word(s) and emits one func_<addr> through
 *              the REAL DolRecomp emitter (the system under test).
 *   - run.c  : sets the probe's input CPU state, runs the emitted func, and checks
 *              the resulting architectural state against per-probe expectations.
 *
 * The expectations encode real-Gekko truth (oracle), derived independently of the
 * emitter: real-PPC-verified constants (tests/dolphin/source/main.c) plus
 * unambiguous PEM/Gekko-manual facts. Because the oracle does not share the
 * emitter's code, a divergence is caught whether or not the bug was anticipated.
 *
 * An expectation flagged `xfail` is a KNOWN, catalogued DolRecomp emitter defect:
 * a divergence there is expected (XFAIL, green), a match means the emitter got
 * fixed (XPASS -> retire the catalog entry, red). Any non-xfail divergence is an
 * UNEXPECTED bug (FAIL, red) -- this is the "issues beyond our knowledge" guard.
 *
 * Single-instruction probes catch local opcode semantics. Sequence probes catch
 * propagation bugs where one opcode's wrong output only becomes visible when a
 * later opcode consumes it.
 */

#include "common/types.h"

typedef enum {
    O_GPR,          /* index = GPR number; expect = u32 value                 */
    O_FPR_SINGLE,   /* index = FPR number; expect = bits of (f32)fpr[index]   */
    O_FPR_DOUBLE,   /* index = FPR number; expect = bits of (f64)fpr[index]   */
    O_PS0_SINGLE,   /* paired-single lane 0 == (f32)fpr[index]                */
    O_PS1_SINGLE,   /* paired-single lane 1 == (f32)ps1[index]                */
    O_CR,           /* expect = u32 condition register                        */
    O_XER,          /* expect = u32 XER                                       */
    O_PC,           /* expect = u32 PC                                        */
    O_LR,           /* expect = u32 LR                                        */
    O_CTR,          /* expect = u32 CTR                                       */
    O_MSR,          /* expect = u32 MSR                                       */
    O_SRR0,         /* expect = u32 SRR0                                      */
    O_SRR1,         /* expect = u32 SRR1                                      */
    O_SR,           /* index = SR number; expect = u32 value                  */
    O_FPSCR,        /* expect = u32 FPSCR                                      */
    O_EXCEPTION,    /* expect = u32 exception bitset                          */
    O_PROGRAM_EXCEPTION, /* expect = u32 program-exception cause bits           */
    O_RESERVE_VALID,/* expect = boolean reservation-valid state                */
    O_MEM32         /* index = byte offset in mem window; expect = big-endian word */
} OutKind;

typedef struct {
    OutKind     kind;
    u32         index;
    u64         expect;
    bool        xfail;   /* true = known emitter defect (catalogued)          */
    const char* note;    /* short reason, shown in output                     */
} Expect;

typedef struct {
    const char* name;
    u32         raw;       /* one PowerPC machine word, if raw_words == NULL */
    const u32*  raw_words; /* optional multi-instruction sequence            */
    unsigned    raw_count; /* number of raw_words entries                    */
    u32         address;   /* guest addr the probe lives at (-> func_<addr>)  */

    /* input architectural state (anything unset is zero) */
    u32 gpr[32];
    u64 fpr[32];           /* raw f64 bit patterns                            */
    u64 ps1[32];           /* raw f64 bit patterns (paired-single lane 1)     */
    u32 cr, xer, fpscr, ctr, lr, hid2;
    u32 msr, srr0, srr1;
    u32 sr[16];
    u32 gqr[8];
    u32 reserve_addr;
    bool reserve_valid;

    /* If true, fpr[]/ps1[] entries are SINGLE-precision bit patterns (u32) that the
     * runner widens to double on load -- lets us transcribe the dolphin producer's
     * single-precision constants verbatim. If false, they are raw f64 bit patterns. */
    bool fp_single_inputs;

    /* optional guest-memory window (cached address) */
    u32         mem_base;
    u32         mem_len;
    const u8*   mem_in;    /* initial bytes, big-endian as in guest RAM       */

    const Expect* expects;
    unsigned      expect_count;
} Probe;

#define EXPECTS(a) .expects = (a), .expect_count = (unsigned)(sizeof(a) / sizeof((a)[0]))

extern const Probe   probes[];
extern const unsigned probe_count;

#endif /* DOLRECOMP_DIFF_PROBE_H */
