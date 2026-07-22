#include <stdio.h>
#include <string.h>

#include "cpu/cpu.h"
#include "dedicated_cases.h"

extern void (*const dedicated_funcs[])(CPUState*);

static u32 ext_read(CPUState* cpu, u32 ea, u8 rid) {
    (void)cpu;
    return 0xABCD0000u | ((u32)rid << 8) | (ea & 0xFFu);
}

static void ext_write(CPUState* cpu, u32 ea, u32 value, u8 rid) {
    (void)ea; (void)value; (void)rid;
    cpu->external_value ^= 0x11111111u;
}

static int expect_u32(const char* name, const char* field, u32 got, u32 want) {
    if (got == want)
        return 0;
    printf("FAIL  %-8s %-10s got=%08X want=%08X\n", name, field, got, want);
    return 1;
}

int main(void) {
    CPUState cpu;
    if (!cpu_init(&cpu))
        return 2;
    cpu.external_read32 = ext_read;
    cpu.external_write32 = ext_write;

    int failures = 0;
    for (unsigned i = 0; i < dedicated_case_count; i++) {
        const DedicatedCase* c = &dedicated_cases[i];
        cpu_reset(&cpu);
        cpu.external_read32 = ext_read;
        cpu.external_write32 = ext_write;
        cpu.pc = c->address;
        cpu.lr = 0x81234567u;
        cpu.ctr = 0x82345678u;
        cpu.cr = 0x20000000u;
        cpu.msr = 0x0000B032u;
        cpu.srr0 = 0x81230002u;
        cpu.srr1 = 0x0000F032u;
        cpu.sr[3] = 0xCAFEBABEu;
        cpu.sr[8] = 0x0BADBEEFu;
        cpu.gpr[3] = 0xFFFFFFFFu;
        cpu.gpr[4] = 0u;
        cpu.gpr[10] = 0xA5A5A5A5u;
        cpu.gpr[13] = 0x80000000u;
        cpu.gpr[14] = 0x12345678u;
        cpu.gpr[15] = 0x87654321u;
        cpu.gpr[16] = 0x80000000u;
        cpu.gpr[19] = 0x8001F234u;
        cpu.gpr[20] = 0x80001000u;
        cpu.gpr[21] = 0x40u;
        cpu.gpr[23] = 0x13572468u;
        cpu.hid2 = PPC_HID2_PSE | PPC_HID2_LSQE | PPC_HID2_LCE;
        cpu.ear = 0x80000005u;
        cpu.timebase = 0x1122334455667788ull;
        mem_write32(&cpu, 0x80001040u, 0xDEADBEEFu);

        dedicated_funcs[i](&cpu);

        if (!strcmp(c->name, "bl")) {
            failures += expect_u32(c->name, "pc", cpu.pc, 0x81010068u);
            failures += expect_u32(c->name, "lr", cpu.lr, c->address + 4u);
        } else if (!strcmp(c->name, "beq")) {
            failures += expect_u32(c->name, "pc", cpu.pc, 0x81010068u);
        } else if (!strcmp(c->name, "blr")) {
            failures += expect_u32(c->name, "pc", cpu.pc, 0x81234564u);
        } else if (!strcmp(c->name, "bctr")) {
            failures += expect_u32(c->name, "pc", cpu.pc, 0x82345678u);
        } else if (!strcmp(c->name, "tw") || !strcmp(c->name, "twi")) {
            failures += expect_u32(c->name, "exception", cpu.exception, PPC_EXC_PROGRAM);
            failures += expect_u32(c->name, "program", cpu.program_exception, PPC_PROGRAM_TRAP);
            failures += expect_u32(c->name, "pc", cpu.pc, PPC_VECTOR_PROGRAM);
            failures += expect_u32(c->name, "srr0", cpu.srr0, c->address);
        } else if (!strcmp(c->name, "sc")) {
            failures += expect_u32(c->name, "exception", cpu.exception, PPC_EXC_SYSTEM_CALL);
            failures += expect_u32(c->name, "pc", cpu.pc, PPC_VECTOR_SYSTEM_CALL);
            failures += expect_u32(c->name, "srr0", cpu.srr0, c->address + 4u);
        } else if (!strcmp(c->name, "rfi")) {
            failures += expect_u32(c->name, "pc", cpu.pc, 0x81230000u);
            failures += expect_u32(c->name, "msr", cpu.msr, 0x0000F032u);
        } else if (!strcmp(c->name, "mfmsr")) {
            failures += expect_u32(c->name, "gpr9", cpu.gpr[9], 0x0000B032u);
        } else if (!strcmp(c->name, "mtmsr")) {
            failures += expect_u32(c->name, "msr", cpu.msr, 0xA5A5A5A5u);
        } else if (!strcmp(c->name, "mfsr")) {
            failures += expect_u32(c->name, "gpr11", cpu.gpr[11], 0xCAFEBABEu);
        } else if (!strcmp(c->name, "mfsrin")) {
            failures += expect_u32(c->name, "gpr12", cpu.gpr[12], 0x0BADBEEFu);
        } else if (!strcmp(c->name, "mtsr")) {
            failures += expect_u32(c->name, "sr4", cpu.sr[4], 0x12345678u);
        } else if (!strcmp(c->name, "mtsrin")) {
            failures += expect_u32(c->name, "sr8", cpu.sr[8], 0x87654321u);
        } else if (!strcmp(c->name, "mftb")) {
            failures += expect_u32(c->name, "gpr17", cpu.gpr[17], 0x55667788u);
        } else if (!strcmp(c->name, "mftbu")) {
            failures += expect_u32(c->name, "gpr18", cpu.gpr[18], 0x11223344u);
        } else if (!strcmp(c->name, "tlbie")) {
            failures += expect_u32(c->name, "tlb_count", cpu.tlb_invalidate_count, 1u);
            failures += expect_u32(c->name, "tlb_vps", cpu.tlb_last_vps, 0x001Fu);
        } else if (!strcmp(c->name, "dcbz_l")) {
            failures += expect_u32(c->name, "mem", mem_read32(&cpu, 0x80001040u), 0u);
            failures += expect_u32(c->name, "lcache", cpu.locked_cache_valid[(0x80001040u >> 5) & 511u], 1u);
        } else if (!strcmp(c->name, "eciwx")) {
            failures += expect_u32(c->name, "gpr22", cpu.gpr[22], 0xABCD0540u);
            failures += expect_u32(c->name, "ext_reads", cpu.external_read_count, 1u);
        } else if (!strcmp(c->name, "ecowx")) {
            failures += expect_u32(c->name, "ext_writes", cpu.external_write_count, 1u);
            failures += expect_u32(c->name, "ext_value", cpu.external_value, 0x02463579u);
        } else {
            failures += expect_u32(c->name, "exception", cpu.exception, 0u);
            failures += expect_u32(c->name, "pc", cpu.pc, c->address + 4u);
        }
    }

    printf("dedicated emitted-control differential: %u cases, %d unexpected\n",
           dedicated_case_count, failures);
    cpu_free(&cpu);
    return failures ? 1 : 0;
}
