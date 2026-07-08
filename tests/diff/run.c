/*
 * Runner / comparator for the emitted-code acceptance differential.
 *
 * For each probe: install its input CPU state, execute the emitted func_<addr>
 * (compiled from the REAL DolRecomp emitter output, see gen.c), then compare the
 * resulting architectural state against the probe's real-Gekko expectations.
 *
 * Classification per expectation:
 *   PASS  - non-xfail expectation matched (correct codegen).
 *   FAIL  - non-xfail expectation diverged: an UNEXPECTED bug. (exit nonzero)
 *   XFAIL - xfail (catalogued defect) diverged as expected. (green)
 *   XPASS - xfail matched: the emitter appears fixed -> retire catalog. (exit nonzero)
 *
 * The build compiles this and the generated probes with -ffp-contract=off
 * -fno-fast-math so the host FPU matches Gekko IEEE-754 round-once semantics.
 */

#include <stdio.h>
#include <string.h>

#include "common/types.h"
#include "cpu/cpu.h"
#include "probe.h"
#include "probes.inc"

/* defined in the generated probes file */
extern void (*const probe_funcs[])(CPUState*);

static u32 f32bits(f64 v) { float f = (float)v; u32 b; memcpy(&b, &f, sizeof(b)); return b; }
static u64 f64bits(f64 v) { u64 b; memcpy(&b, &v, sizeof(b)); return b; }

static const char* kind_name(OutKind k) {
    switch (k) {
    case O_GPR:        return "gpr";
    case O_FPR_SINGLE: return "fpr.s";
    case O_FPR_DOUBLE: return "fpr.d";
    case O_PS0_SINGLE: return "ps0";
    case O_PS1_SINGLE: return "ps1";
    case O_CR:         return "cr";
    case O_XER:        return "xer";
    case O_PC:         return "pc";
    case O_LR:         return "lr";
    case O_CTR:        return "ctr";
    case O_MSR:        return "msr";
    case O_SRR0:       return "srr0";
    case O_SRR1:       return "srr1";
    case O_SR:         return "sr";
    case O_FPSCR:      return "fpscr";
    case O_EXCEPTION:  return "exc";
    case O_PROGRAM_EXCEPTION: return "prog";
    case O_RESERVE_VALID: return "resv";
    case O_MEM32:      return "mem32";
    default:           return "?";
    }
}

int main(void) {
    CPUState cpu;
    if (!cpu_init(&cpu)) {
        fprintf(stderr, "cpu_init failed\n");
        return 2;
    }

    int pass = 0, fail = 0, xfail = 0, xpass = 0;

    printf("emitted-code acceptance differential (host vs real-PPC seed vectors)\n");
    printf("%-5s %-10s %-6s %-7s %-18s %-18s %s\n",
           "stat", "probe", "field", "idx", "got", "want", "note");

    for (unsigned i = 0; i < probe_count; i++) {
        const Probe* p = &probes[i];

        cpu_reset(&cpu);
        for (int r = 0; r < 32; r++) cpu.gpr[r] = p->gpr[r];
        for (int r = 0; r < 32; r++) {
            if (p->fp_single_inputs) {
                float a, b;
                u32 ba = (u32)p->fpr[r], bb = (u32)p->ps1[r];
                memcpy(&a, &ba, sizeof a);
                memcpy(&b, &bb, sizeof b);
                cpu.fpr[r] = (f64)a;
                cpu.ps1[r] = (f64)b;
            } else {
                memcpy(&cpu.fpr[r], &p->fpr[r], sizeof(f64));
                memcpy(&cpu.ps1[r], &p->ps1[r], sizeof(f64));
            }
        }
        cpu.cr = p->cr; cpu.xer = p->xer; cpu.fpscr = p->fpscr;
        cpu.ctr = p->ctr; cpu.lr = p->lr; cpu.hid2 = p->hid2;
        cpu.msr = p->msr; cpu.srr0 = p->srr0; cpu.srr1 = p->srr1;
        for (int r = 0; r < 16; r++) cpu.sr[r] = p->sr[r];
        for (int r = 0; r < 8; r++) cpu.gqr[r] = p->gqr[r];
        cpu.reserve_addr = p->reserve_addr;
        cpu.reserve_valid = p->reserve_valid;
        if (p->mem_len) {
            u32 off = p->mem_base - GC_RAM_BASE;
            for (u32 j = 0; j < p->mem_len; j++) cpu.ram[off + j] = p->mem_in[j];
        }
        cpu.pc = p->address;

        probe_funcs[i](&cpu);

        for (unsigned e = 0; e < p->expect_count; e++) {
            const Expect* x = &p->expects[e];
            u64 actual = 0;
            switch (x->kind) {
            case O_GPR:        actual = cpu.gpr[x->index]; break;
            case O_FPR_SINGLE:
            case O_PS0_SINGLE: actual = f32bits(cpu.fpr[x->index]); break;
            case O_FPR_DOUBLE: actual = f64bits(cpu.fpr[x->index]); break;
            case O_PS1_SINGLE: actual = f32bits(cpu.ps1[x->index]); break;
            case O_CR:         actual = cpu.cr; break;
            case O_XER:        actual = cpu.xer; break;
            case O_PC:         actual = cpu.pc; break;
            case O_LR:         actual = cpu.lr; break;
            case O_CTR:        actual = cpu.ctr; break;
            case O_MSR:        actual = cpu.msr; break;
            case O_SRR0:       actual = cpu.srr0; break;
            case O_SRR1:       actual = cpu.srr1; break;
            case O_SR:         actual = cpu.sr[x->index]; break;
            case O_FPSCR:      actual = cpu.fpscr; break;
            case O_EXCEPTION:  actual = cpu.exception; break;
            case O_PROGRAM_EXCEPTION: actual = cpu.program_exception; break;
            case O_RESERVE_VALID: actual = cpu.reserve_valid ? 1u : 0u; break;
            case O_MEM32:      actual = read_be32(cpu.ram + (p->mem_base - GC_RAM_BASE) + x->index); break;
            }

            bool match = (actual == x->expect);
            const char* stat;
            if (x->xfail) {
                if (match) { stat = "XPASS"; xpass++; }
                else       { stat = "XFAIL"; xfail++; }
            } else {
                if (match) { stat = "PASS"; pass++; }
                else       { stat = "FAIL"; fail++; }
            }

            printf("%-5s %-10s %-6s %-7u 0x%016llX 0x%016llX  %s\n",
                   stat, p->name, kind_name(x->kind), x->index,
                   (unsigned long long)actual, (unsigned long long)x->expect,
                   x->note ? x->note : "");
        }
    }

    printf("\nsummary: %d PASS, %d XFAIL(known emitter defect), "
           "%d FAIL(unexpected), %d XPASS(defect appears fixed)\n",
           pass, xfail, fail, xpass);
    if (fail)  printf("  -> FAIL: emitted code diverges from real PPC on a case we expected correct.\n");
    if (xpass) printf("  -> XPASS: a catalogued defect now matches; retire its xfail in probes.inc.\n");

    cpu_free(&cpu);
    return (fail || xpass) ? 1 : 0;
}
