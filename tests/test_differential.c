/* Differential test: the C backend against the LLVM backend.
 *
 * Both backends compile the same random guest sequences, emitted at different
 * guest base addresses so their func_<address> symbols coexist in one binary.
 * Each pair runs from a byte-identical randomised CPUState and the full
 * observable result is compared: every GPR, every FPR and paired-single lane as
 * a bit pattern rather than a float compare, LR, CTR, CR, XER, FPSCR, the
 * exception and reservation state, and the scratch memory both wrote through.
 *
 * NaN payloads and signed zero are why the float comparison is on bits. Two
 * backends that agree numerically but disagree on which NaN they produce have
 * still diverged, and a title can observe that.
 *
 * The seed is printed on failure so a divergence reproduces exactly.
 */

#include "cpu/cpu.h"

#include <stdio.h>
#include <string.h>

typedef void (*DiffFunction)(CPUState*);

typedef struct {
    DiffFunction c_backend;
    DiffFunction llvm_backend;
    u32 c_address;
    u32 llvm_address;
} DiffPair;

#include "differential_manifest.h"

/* Where generated loads and stores address through r31. Sits well inside RAM
   and clear of anything else the test touches. */
#define SCRATCH_ADDRESS 0x80010000u
#define SCRATCH_BYTES   4096u

static u64 rng_state;

static u32 next_random(void) {
    rng_state = rng_state * 6364136223846793005ull + 1442695040888963407ull;
    return (u32)(rng_state >> 33);
}

/* Deliberately biased toward awkward values. A uniform random 32 bits almost
   never produces a denormal, an infinity, or an exact power of two, and those
   are where backends disagree. */
static u32 awkward_word(void) {
    switch (next_random() % 8u) {
    case 0: return 0u;
    case 1: return 0xFFFFFFFFu;
    case 2: return 0x80000000u;
    case 3: return 0x7F800000u;  /* +inf as float bits */
    case 4: return 0x7FC00000u;  /* quiet NaN */
    case 5: return 0x00800000u;  /* smallest normal float */
    case 6: return 0x00000001u;  /* denormal */
    default: return next_random();
    }
}

static void randomise(CPUState* cpu) {
    for (u32 i = 0; i < 32; i++)
        cpu->gpr[i] = awkward_word();
    for (u32 i = 0; i < 32; i++) {
        u64 bits = ((u64)awkward_word() << 32) | awkward_word();
        memcpy(&cpu->fpr[i], &bits, sizeof(bits));
        bits = ((u64)awkward_word() << 32) | awkward_word();
        memcpy(&cpu->ps1[i], &bits, sizeof(bits));
    }
    cpu->cr = next_random();
    cpu->xer = next_random() & 0xE000007Fu;
    cpu->lr = next_random() & ~3u;
    cpu->ctr = next_random();
    /* Rounding mode and enables left at a sane default: a random FPSCR would
       compare two backends under a mode neither claims to support. */
    cpu->fpscr = 0;
    cpu->msr = 0x00002000u;  /* MSR[FP] set, or every FP op takes an exception */
    cpu->exception = 0;
    cpu->program_exception = 0;
    cpu->reserve_addr = 0;
    cpu->reserve_valid = false;
    cpu->downcount = 1000000;

    /* r31 is the addressing base every generated memory op uses. */
    cpu->gpr[31] = SCRATCH_ADDRESS;

    for (u32 i = 0; i < SCRATCH_BYTES; i += 4)
        mem_write32(cpu, SCRATCH_ADDRESS + i, awkward_word());
}

static int report(const char* what, u32 index, u64 expected, u64 actual) {
    fprintf(stderr,
            "  divergence in %s at pair %u: C=0x%016llX LLVM=0x%016llX\n",
            what, index, (unsigned long long)expected,
            (unsigned long long)actual);
    return 1;
}

static u64 float_bits(f64 value) {
    u64 bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int compare(const CPUState* a, const CPUState* b, u32 index) {
    int bad = 0;
    char label[32];

    for (u32 i = 0; i < 32; i++) {
        if (a->gpr[i] != b->gpr[i]) {
            snprintf(label, sizeof(label), "r%u", i);
            bad |= report(label, index, a->gpr[i], b->gpr[i]);
        }
    }
    for (u32 i = 0; i < 32; i++) {
        if (float_bits(a->fpr[i]) != float_bits(b->fpr[i])) {
            snprintf(label, sizeof(label), "f%u", i);
            bad |= report(label, index, float_bits(a->fpr[i]),
                          float_bits(b->fpr[i]));
        }
        if (float_bits(a->ps1[i]) != float_bits(b->ps1[i])) {
            snprintf(label, sizeof(label), "ps1[%u]", i);
            bad |= report(label, index, float_bits(a->ps1[i]),
                          float_bits(b->ps1[i]));
        }
    }
    if (a->cr != b->cr)       bad |= report("cr", index, a->cr, b->cr);
    if (a->xer != b->xer)     bad |= report("xer", index, a->xer, b->xer);
    if (a->lr != b->lr)       bad |= report("lr", index, a->lr, b->lr);
    if (a->ctr != b->ctr)     bad |= report("ctr", index, a->ctr, b->ctr);
    if (a->fpscr != b->fpscr) bad |= report("fpscr", index, a->fpscr, b->fpscr);
    if (a->exception != b->exception)
        bad |= report("exception", index, a->exception, b->exception);
    if (a->program_exception != b->program_exception)
        bad |= report("program_exception", index, a->program_exception,
                      b->program_exception);
    if (a->reserve_valid != b->reserve_valid)
        bad |= report("reserve_valid", index, a->reserve_valid, b->reserve_valid);
    if (a->reserve_addr != b->reserve_addr)
        bad |= report("reserve_addr", index, a->reserve_addr, b->reserve_addr);

    /* Memory the sequences wrote through r31. */
    if (memcmp(a->ram + (SCRATCH_ADDRESS - GC_RAM_BASE),
               b->ram + (SCRATCH_ADDRESS - GC_RAM_BASE), SCRATCH_BYTES) != 0) {
        for (u32 offset = 0; offset < SCRATCH_BYTES; offset += 4) {
            u32 left = mem_read32((CPUState*)a, SCRATCH_ADDRESS + offset);
            u32 right = mem_read32((CPUState*)b, SCRATCH_ADDRESS + offset);
            if (left != right) {
                snprintf(label, sizeof(label), "mem+0x%X", offset);
                bad |= report(label, index, left, right);
                break;  /* One is enough to identify the failure. */
            }
        }
    }
    return bad;
}

int main(void) {
    CPUState a;
    CPUState b;
    if (!cpu_init(&a) || !cpu_init(&b)) {
        fprintf(stderr, "differential: cannot allocate CPU state\n");
        return 1;
    }

    u32 failures = 0;
    for (u32 i = 0; i < DIFF_COUNT; i++) {
        /* Same seed for both arms of a pair, advanced per pair so each gets a
           different starting state. */
        rng_state = DIFF_SEED + i;
        randomise(&a);
        rng_state = DIFF_SEED + i;
        randomise(&b);

        a.pc = diff_pairs[i].c_address;
        b.pc = diff_pairs[i].llvm_address;

        diff_pairs[i].c_backend(&a);
        diff_pairs[i].llvm_backend(&b);

        if (compare(&a, &b, i)) {
            fprintf(stderr, "  reproduce with seed %llu, pair %u\n",
                    (unsigned long long)DIFF_SEED, i);
            failures++;
        }
    }

    cpu_free(&a);
    cpu_free(&b);

    if (failures) {
        fprintf(stderr, "differential: %u of %u pairs diverged\n",
                failures, (u32)DIFF_COUNT);
        return 1;
    }
    printf("differential: %u pairs agree (seed %llu)\n", (u32)DIFF_COUNT,
           (unsigned long long)DIFF_SEED);
    return 0;
}
