#include "cpu/cpu.h"

#include <string.h>
#include <stdio.h>

void func_80001000(CPUState* cpu);
void func_80002000(CPUState* cpu);
void func_80002100(CPUState* cpu);
void func_80002200(CPUState* cpu);
void func_80002300(CPUState* cpu);
void func_80002400(CPUState* cpu);
void func_80002500(CPUState* cpu);
void func_80002600(CPUState* cpu);
void func_80002700(CPUState* cpu);
void func_80002800(CPUState* cpu);
void func_80002900(CPUState* cpu);
void func_80002A00(CPUState* cpu);
void func_80002B00(CPUState* cpu);
void func_80002C00(CPUState* cpu);
void func_80002D00(CPUState* cpu);
void func_80003000(CPUState* cpu);

static u32 fallback_count;
static int fallback_bad;
static u32 cache_count;
static u8 cache_operation;
static u32 cache_address;
static u32 cache_cia;
static u32 external_write_count;

static void fallback(CPUState* cpu, u32 raw, u32 cia) {
    fallback_bad |= raw != 0;
    fallback_count++;
    cpu->gpr[5] = 0x12345678u;
    cpu->pc = cia + 4u;
}

static void cache_control(CPUState* cpu, u8 operation, u32 ea, u32 cia) {
    (void)cpu;
    cache_count++;
    cache_operation = operation;
    cache_address = ea;
    cache_cia = cia;
}

static void external_write32(CPUState* cpu, u32 ea, u32 value, u8 rid) {
    external_write_count++;
    cpu->external_addr = ea;
    cpu->external_value = value;
    cpu->external_rid = rid;
}

static void prepare_call(CPUState* cpu, u32 address) {
    cpu->pc = address;
    cpu->lr = 0x81234567u;
    cpu->exception = 0;
    cpu->program_exception = 0;
}

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void) {
    CPUState cpu;
    CHECK(cpu_init(&cpu));
    cpu.pc = 0x80001000u;
    cpu.lr = 0x81234567u;
    cpu.gpr[1] = GC_RAM_BASE;
    cpu.fpr[18] = 2.0;
    cpu.fpr[19] = 3.0;
    cpu.fpr[20] = 4.0;
    cpu.msr = 1u << 13;
    cpu.downcount = 0;
    cpu.instruction_fallback = fallback;
    func_80001000(&cpu);
    CHECK(cpu.gpr[3] == 10);
    CHECK(mem_read32(&cpu, GC_RAM_BASE) == 9);
    CHECK(cpu.pc == 0x81234564u);
    CHECK(cpu.fpr[17] == 10.0 && cpu.ps1[17] == 10.0);
    CHECK(cpu.gpr[5] == 0x12345678u && fallback_count == 1 && !fallback_bad);
    CHECK(cpu.downcount < 0 && cpu.downcount > -100);

    prepare_call(&cpu, 0x80002000u);
    cpu.gpr[3] = 0xA5A55A5Au;
    func_80002000(&cpu);
    CHECK(cpu.gpr[4] == 0xA5A55A5Au);
    CHECK(cpu.spr[273] == 0xA5A55A5Au);
    CHECK(cpu.pc == 0x81234564u);

    prepare_call(&cpu, 0x80002100u);
    cpu.gpr[14] = 0x12345678u;
    cpu.gpr[15] = 0xA0000000u;
    cpu.gpr[16] = 0xA0000000u;
    cpu.gpr[13] = 0xA0000000u;
    cpu.sr[3] = 0x87654321u;
    cpu.sr[10] = 0;
    func_80002100(&cpu);
    CHECK(cpu.sr[4] == 0x12345678u);
    CHECK(cpu.gpr[11] == 0x87654321u);
    CHECK(cpu.sr[10] == 0xA0000000u);
    CHECK(cpu.gpr[12] == 0xA0000000u);
    prepare_call(&cpu, 0x80002100u);
    cpu.msr = 1u << 14;
    cpu.gpr[14] = 0xDEADBEEFu;
    cpu.sr[4] = 0x10203040u;
    func_80002100(&cpu);
    CHECK(cpu.exception & PPC_EXC_PROGRAM);
    CHECK(cpu.program_exception & PPC_PROGRAM_PRIV);
    CHECK(cpu.sr[4] == 0x10203040u);
    cpu.msr = 0;

    prepare_call(&cpu, 0x80002200u);
    cpu.msr = 1u << 13;
    cpu.fpscr = 0;
    cpu.fpr[13] = 0;
    func_80002200(&cpu);
    CHECK((cpu.fpscr & 1u) != 0);
    u64 ffs_bits;
    memcpy(&ffs_bits, &cpu.fpr[13], sizeof(ffs_bits));
    CHECK((u32)ffs_bits == cpu.fpscr);
    CHECK((ffs_bits >> 32) == 0xFFF80000u);

    prepare_call(&cpu, 0x80002300u);
    cpu.xer = 5;
    cpu.gpr[20] = GC_RAM_BASE + 0x100u;
    cpu.gpr[21] = 3;
    mem_write8(&cpu, GC_RAM_BASE + 0x103u, 0x11);
    mem_write8(&cpu, GC_RAM_BASE + 0x104u, 0x22);
    mem_write8(&cpu, GC_RAM_BASE + 0x105u, 0x33);
    mem_write8(&cpu, GC_RAM_BASE + 0x106u, 0x44);
    mem_write8(&cpu, GC_RAM_BASE + 0x107u, 0x55);
    func_80002300(&cpu);
    CHECK(cpu.gpr[9] == 0x11223344u);
    CHECK(cpu.gpr[10] == 0x55000000u);

    prepare_call(&cpu, 0x80002400u);
    cpu.cache_control = cache_control;
    cpu.gpr[17] = GC_RAM_BASE + 0x200u;
    cpu.gpr[18] = 0x24u;
    func_80002400(&cpu);
    CHECK(cache_count == 1);
    CHECK(cache_operation == PPC_CACHE_DCBST);
    CHECK(cache_address == GC_RAM_BASE + 0x224u);
    CHECK(cache_cia == 0x80002400u);

    prepare_call(&cpu, 0x80002500u);
    cpu.gpr[3] = 40;
    cpu.gpr[5] = 1;
    func_80002500(&cpu);
    CHECK(cpu.gpr[3] == 41);
    CHECK(cpu.exception == 0);
    prepare_call(&cpu, 0x80002500u);
    cpu.gpr[3] = 40;
    cpu.gpr[5] = 0xFFFFFFFEu;
    func_80002500(&cpu);
    CHECK(cpu.gpr[3] == 40);
    CHECK(cpu.exception & PPC_EXC_PROGRAM);
    CHECK(cpu.program_exception & PPC_PROGRAM_TRAP);
    CHECK(cpu.srr0 == 0x80002500u);

    prepare_call(&cpu, 0x80002600u);
    func_80002600(&cpu);
    CHECK(cpu.exception & PPC_EXC_SYSTEM_CALL);
    CHECK(cpu.srr0 == 0x80002604u);
    CHECK(cpu.pc == PPC_VECTOR_SYSTEM_CALL);

    prepare_call(&cpu, 0x80002700u);
    cpu.msr = 0;
    cpu.srr0 = 0x80004003u;
    cpu.srr1 = 0x00002000u;
    func_80002700(&cpu);
    CHECK(cpu.exception == 0);
    CHECK(cpu.pc == 0x80004000u);
    CHECK(cpu.msr == 0x00002000u);

    prepare_call(&cpu, 0x80002800u);
    cpu.msr = 0;
    cpu.hid2 = PPC_HID2_LCE;
    cpu.gpr[5] = GC_RAM_BASE + 0x300u;
    cpu.gpr[6] = 4;
    for (u32 i = 0; i < 32; i++)
        mem_write8(&cpu, GC_RAM_BASE + 0x300u + i, 0x5Au);
    func_80002800(&cpu);
    for (u32 i = 0; i < 32; i++)
        CHECK(mem_read8(&cpu, GC_RAM_BASE + 0x300u + i) == 0);

    prepare_call(&cpu, 0x80002900u);
    cpu.ear = 0x80000007u;
    cpu.external_write32 = external_write32;
    cpu.gpr[11] = 0xCAFEBABEu;
    cpu.gpr[12] = 0x1000u;
    cpu.gpr[13] = 0x20u;
    func_80002900(&cpu);
    CHECK(external_write_count == 1);
    CHECK(cpu.external_addr == 0x1020u);
    CHECK(cpu.external_value == 0xCAFEBABEu);
    CHECK(cpu.external_rid == 7);

    prepare_call(&cpu, 0x80002A00u);
    cpu.msr = 1u << 13;
    cpu.fpr[2] = 1.25;
    cpu.fpr[3] = 2.5;
    cpu.fpr[5] = 7.0;
    cpu.fpr[6] = 1.5;
    cpu.fpr[8] = 3.0;
    cpu.fpr[9] = 2.0;
    cpu.fpr[11] = 7.0;
    cpu.fpr[12] = 2.0;
    cpu.fpr[14] = 1.25;
    cpu.fpr[15] = 2.5;
    cpu.fpr[17] = 7.0;
    cpu.fpr[18] = 1.5;
    cpu.fpr[20] = 3.0;
    cpu.fpr[21] = 2.0;
    cpu.fpr[23] = 7.0;
    cpu.fpr[24] = 2.0;
    func_80002A00(&cpu);
    CHECK(cpu.fpr[1] == 3.75 && cpu.ps1[1] == 3.75);
    CHECK(cpu.fpr[4] == 5.5 && cpu.ps1[4] == 5.5);
    CHECK(cpu.fpr[7] == 6.0 && cpu.ps1[7] == 6.0);
    CHECK(cpu.fpr[10] == 3.5 && cpu.ps1[10] == 3.5);
    CHECK(cpu.fpr[13] == 3.75);
    CHECK(cpu.fpr[16] == 5.5);
    CHECK(cpu.fpr[19] == 6.0);
    CHECK(cpu.fpr[22] == 3.5);

    prepare_call(&cpu, 0x80002B00u);
    cpu.msr = 1u << 13;
    cpu.fpr[2] = 1.25;
    cpu.ps1[2] = 2.0;
    cpu.fpr[3] = 2.5;
    cpu.ps1[3] = 4.0;
    cpu.fpr[5] = 10.0;
    cpu.ps1[5] = 20.0;
    cpu.fpr[6] = 3.0;
    cpu.ps1[6] = 4.0;
    cpu.fpr[8] = 2.0;
    cpu.ps1[8] = 3.0;
    cpu.fpr[9] = 4.0;
    cpu.ps1[9] = 5.0;
    cpu.fpr[14] = 2.0;
    cpu.ps1[14] = 3.0;
    cpu.fpr[15] = 4.0;
    cpu.ps1[15] = 5.0;
    cpu.fpr[16] = 1.0;
    cpu.ps1[16] = 2.0;
    func_80002B00(&cpu);
    CHECK(cpu.fpr[1] == 3.75 && cpu.ps1[1] == 6.0);
    CHECK(cpu.fpr[7] == 8.0 && cpu.ps1[7] == 15.0);
    CHECK(cpu.fpr[13] == 9.0 && cpu.ps1[13] == 17.0);
    CHECK(cpu.fpr[5] == ppc_approx_reciprocal(3.0));
    CHECK(cpu.ps1[5] == ppc_approx_reciprocal(4.0));
    CHECK(cpu.fpr[4] == cpu.fpr[5] && cpu.ps1[4] == cpu.ps1[6]);

    CHECK(fallback_count == 1);

    // A fallback in the loop must not restart the dispatcher budget.
    prepare_call(&cpu, 0x80002C00u);
    cpu.gpr[3] = 0;
    cpu.downcount = 0;
    fallback_count = 0;
    func_80002C00(&cpu);
    fprintf(stderr, "budget guard: %u iterations, downcount %lld\n",
            cpu.gpr[3], (long long)cpu.downcount);
    CHECK(cpu.gpr[3] > 0);
    CHECK(cpu.gpr[3] < 1000);
    CHECK(cpu.pc >= 0x80002C00u && cpu.pc <= 0x80002C10u);
    CHECK(cpu.pc != cpu.lr);
    CHECK(cpu.downcount < 0 && cpu.downcount > -1024);
    CHECK(fallback_count == (u32)cpu.gpr[3]);

    // Re-entry starts a new budget and continues from the saved PC.
    const u32 first_pass = cpu.gpr[3];
    func_80002C00(&cpu);
    CHECK(cpu.gpr[3] > first_pass);

    prepare_call(&cpu, 0x80002D00u);
    cpu.downcount = 0;
    func_80002D00(&cpu);
    CHECK(cpu.pc == 0x80002D00u || cpu.pc == 0x80002E00u);
    CHECK(cpu.downcount <= -128 && cpu.downcount >= -512);

    // MEM1 boundary, which --memory-mode fast reduces to a compare against a
    // constant. Each case is checked for the value actually written or read,
    // not merely for "did not crash": a bound that is off by one word writes
    // outside the RAM allocation, and that is the failure this mode could
    // plausibly introduce.
    {
        const u32 last_word = GC_RAM_BASE + GC_MAIN_RAM_SIZE - 4u;

        // Fully inside: the last addressable word must round-trip.
        prepare_call(&cpu, 0x80003000u);
        cpu.gpr[3] = last_word;
        cpu.gpr[4] = 0xA5A5A5A5u;
        cpu.gpr[5] = 0;
        func_80003000(&cpu);
        CHECK(mem_read32(&cpu, last_word) == 0xA5A5A5A5u);
        CHECK(cpu.gpr[5] == 0xA5A5A5A5u);

        // Straddling the end: three bytes inside, one past. Must take the slow
        // path rather than the RAM path, so the last word keeps its value.
        prepare_call(&cpu, 0x80003000u);
        cpu.gpr[3] = last_word + 1u;
        cpu.gpr[4] = 0x5A5A5A5Au;
        func_80003000(&cpu);
        CHECK(mem_read32(&cpu, last_word) == 0xA5A5A5A5u);

        // Entirely past the end.
        prepare_call(&cpu, 0x80003000u);
        cpu.gpr[3] = GC_RAM_BASE + GC_MAIN_RAM_SIZE;
        cpu.gpr[4] = 0x5A5A5A5Au;
        func_80003000(&cpu);
        CHECK(mem_read32(&cpu, last_word) == 0xA5A5A5A5u);
    }

    cpu_free(&cpu);
    return 0;
}
