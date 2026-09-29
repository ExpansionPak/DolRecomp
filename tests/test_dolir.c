#include "ir/dolir_builder.h"
#include "cpu/cpu.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #x); return false; } } while (0)

static PPCInst decode(u32 raw, u32 address) {
    return ppc_decode(raw, address);
}

static bool test_native_loop(void) {
    PPCInst insts[] = {
        decode(0x38600000u, 0x80001000u),
        decode(0x38630001u, 0x80001004u),
        decode(0x2C03000Au, 0x80001008u),
        decode(0x4180FFF8u, 0x8000100Cu),
        decode(0x4E800020u, 0x80001010u),
    };
    DolIRModule module;
    dolir_module_init(&module);
    CHECK(dolir_build_chunk(&module, insts, 5, 0x80001000u));
    CHECK(dolir_verify(&module, stderr));
    CHECK(module.functions[0].blocks[0].raw == 0x38600000u);
    CHECK(module.functions[0].blocks[0].terminator.kind ==
          DOLIR_TERM_FALLTHROUGH);
    CHECK(module.functions[0].blocks[3].terminator.kind == DOLIR_TERM_COND_BRANCH);
    CHECK(module.functions[0].blocks[3].terminator.targets[0] == 1);
    CHECK(module.functions[0].blocks[4].terminator.kind == DOLIR_TERM_INDIRECT);
    dolir_module_free(&module);
    return true;
}

static bool test_memory_and_vector(void) {
    PPCInst insts[] = {
        decode(0x80640000u, 0x80002000u),
        decode(0x90640004u, 0x80002004u),
        decode(0x1022182Au, 0x80002008u),
        decode(0x10221C20u, 0x8000200Cu),
    };
    DolIRModule module;
    dolir_module_init(&module);
    CHECK(dolir_build_chunk(&module, insts, 4, 0x80002000u));
    CHECK(dolir_verify(&module, stderr));
    bool load = false;
    bool store = false;
    bool vector = false;
    for (u32 b = 0; b < module.functions[0].block_count; b++) {
        DolIRBlock* block = &module.functions[0].blocks[b];
        for (u32 i = 0; i < block->instruction_count; i++) {
            load |= block->instructions[i].op == DOLIR_OP_GUEST_LOAD;
            store |= block->instructions[i].op == DOLIR_OP_GUEST_STORE;
            if (block->instructions[i].op == DOLIR_OP_GUEST_LOAD ||
                block->instructions[i].op == DOLIR_OP_GUEST_STORE)
                CHECK(block->instructions[i].address_domain ==
                      DOLIR_ADDRESS_UNKNOWN);
            vector |= block->instructions[i].type == DOLIR_TYPE_V2F32 ||
                      block->instructions[i].type == DOLIR_TYPE_V2F64;
        }
    }
    CHECK(load && store && vector);
    dolir_module_free(&module);
    return true;
}

static bool test_static_memory_provenance(void) {
    PPCInst insts[] = {
        decode(0x3C808000u, 0x80002500u),
        decode(0x80640600u, 0x80002504u),
        decode(0x90640604u, 0x80002508u),
        decode(0x4E800020u, 0x8000250Cu),
    };
    DolIRModule module;
    dolir_module_init(&module);
    CHECK(dolir_build_chunk(&module, insts, 4, 0x80002500u));
    CHECK(dolir_verify(&module, stderr));
    u32 memory_ops = 0;
    for (u32 b = 0; b < module.functions[0].block_count; b++) {
        DolIRBlock* block = &module.functions[0].blocks[b];
        for (u32 i = 0; i < block->instruction_count; i++) {
            DolIRInstruction* instruction = &block->instructions[i];
            if (instruction->op != DOLIR_OP_GUEST_LOAD &&
                instruction->op != DOLIR_OP_GUEST_STORE)
                continue;
            CHECK(instruction->address_domain == DOLIR_ADDRESS_MEM1);
            CHECK(instruction->address_lower ==
                  GC_RAM_BASE + 0x600u + memory_ops * 4u);
            CHECK(instruction->address_upper == instruction->address_lower);
            memory_ops++;
        }
    }
    CHECK(memory_ops == 2);
    dolir_module_free(&module);

    PPCInst mem2_insts[] = {
        decode(0x3C809000u, 0x80002520u),
        decode(0x80640600u, 0x80002524u),
        decode(0x90640604u, 0x80002528u),
        decode(0x4E800020u, 0x8000252Cu),
    };
    dolir_module_init(&module);
    CHECK(dolir_build_chunk(&module, mem2_insts, 4, 0x80002520u));
    CHECK(dolir_verify(&module, stderr));
    memory_ops = 0;
    for (u32 b = 0; b < module.functions[0].block_count; b++) {
        DolIRBlock* block = &module.functions[0].blocks[b];
        for (u32 i = 0; i < block->instruction_count; i++) {
            DolIRInstruction* instruction = &block->instructions[i];
            if (instruction->op != DOLIR_OP_GUEST_LOAD &&
                instruction->op != DOLIR_OP_GUEST_STORE)
                continue;
            CHECK(instruction->address_domain == DOLIR_ADDRESS_MEM2);
            CHECK(instruction->address_lower ==
                  WII_MEM2_BASE + 0x600u + memory_ops * 4u);
            CHECK(instruction->address_upper == instruction->address_lower);
            memory_ops++;
        }
    }
    CHECK(memory_ops == 2);
    dolir_module_free(&module);

    PPCInst bounded_mem2_insts[] = {
        decode(0x3C809000u, 0x80002540u), // lis r4, 0x9000
        decode(0x70A503FCu, 0x80002544u), // andi. r5, r5, 0x03fc
        decode(0x7C842A14u, 0x80002548u), // add r4, r4, r5
        decode(0x80640000u, 0x8000254Cu), // lwz r3, 0(r4)
        decode(0x90640004u, 0x80002550u), // stw r3, 4(r4)
        decode(0x4E800020u, 0x80002554u),
    };
    dolir_module_init(&module);
    CHECK(dolir_build_chunk(&module, bounded_mem2_insts, 6, 0x80002540u));
    CHECK(dolir_verify(&module, stderr));
    memory_ops = 0;
    for (u32 b = 0; b < module.functions[0].block_count; b++) {
        DolIRBlock* block = &module.functions[0].blocks[b];
        for (u32 i = 0; i < block->instruction_count; i++) {
            DolIRInstruction* instruction = &block->instructions[i];
            if (instruction->op != DOLIR_OP_GUEST_LOAD &&
                instruction->op != DOLIR_OP_GUEST_STORE)
                continue;
            CHECK(instruction->address_domain == DOLIR_ADDRESS_MEM2);
            CHECK(instruction->address_lower == WII_MEM2_BASE + memory_ops * 4u);
            CHECK(instruction->address_upper ==
                  WII_MEM2_BASE + 0x3fcu + memory_ops * 4u);
            memory_ops++;
        }
    }
    CHECK(memory_ops == 2);
    dolir_module_free(&module);

    PPCInst bounded_mem1_insts[] = {
        decode(0x3C808000u, 0x80002560u), // lis r4, 0x8000
        decode(0x70A503FCu, 0x80002564u), // andi. r5, r5, 0x03fc
        decode(0x7C842A14u, 0x80002568u), // add r4, r4, r5
        decode(0x80640000u, 0x8000256Cu),
        decode(0x4E800020u, 0x80002570u),
    };
    dolir_module_init(&module);
    CHECK(dolir_build_chunk(&module, bounded_mem1_insts, 5, 0x80002560u));
    CHECK(dolir_verify(&module, stderr));
    bool bounded_mem1 = false;
    for (u32 b = 0; b < module.functions[0].block_count; b++) {
        DolIRBlock* block = &module.functions[0].blocks[b];
        for (u32 i = 0; i < block->instruction_count; i++) {
            DolIRInstruction* instruction = &block->instructions[i];
            if (instruction->op != DOLIR_OP_GUEST_LOAD)
                continue;
            bounded_mem1 = instruction->address_domain == DOLIR_ADDRESS_MEM1 &&
                           instruction->address_lower == GC_RAM_BASE &&
                           instruction->address_upper == GC_RAM_BASE + 0x3fcu;
        }
    }
    CHECK(bounded_mem1);
    dolir_module_free(&module);

    PPCInst branch_insts[] = {
        decode(0x3C808000u, 0x80002600u),
        decode(0x48000004u, 0x80002604u),
        decode(0x80640600u, 0x80002608u),
        decode(0x4E800020u, 0x8000260Cu),
    };
    dolir_module_init(&module);
    CHECK(dolir_build_chunk(&module, branch_insts, 4, 0x80002600u));
    CHECK(dolir_verify(&module, stderr));
    DolIRBlock* target = &module.functions[0].blocks[2];
    bool unknown = false;
    for (u32 i = 0; i < target->instruction_count; i++)
        if (target->instructions[i].op == DOLIR_OP_GUEST_LOAD)
            unknown = target->instructions[i].address_domain ==
                      DOLIR_ADDRESS_UNKNOWN;
    CHECK(unknown);
    dolir_module_free(&module);
    return true;
}

static bool test_mem2_and_bounded_memory_provenance(void) {
    PPCInst exact_insts[] = {
        decode(0x3C809000u, 0x80002620u),
        decode(0x80640600u, 0x80002624u),
        decode(0x90640604u, 0x80002628u),
        decode(0x4E800020u, 0x8000262Cu),
    };
    DolIRModule module;
    dolir_module_init(&module);
    CHECK(dolir_build_chunk(&module, exact_insts, 4, 0x80002620u));
    CHECK(dolir_verify(&module, stderr));
    u32 exact_memory_ops = 0;
    for (u32 b = 0; b < module.functions[0].block_count; b++) {
        DolIRBlock* block = &module.functions[0].blocks[b];
        for (u32 i = 0; i < block->instruction_count; i++) {
            DolIRInstruction* instruction = &block->instructions[i];
            if (instruction->op != DOLIR_OP_GUEST_LOAD &&
                instruction->op != DOLIR_OP_GUEST_STORE)
                continue;
            CHECK(instruction->address_domain == DOLIR_ADDRESS_MEM2);
            CHECK(instruction->address_lower ==
                  WII_MEM2_BASE + 0x600u + exact_memory_ops * 4u);
            CHECK(instruction->address_upper == instruction->address_lower);
            exact_memory_ops++;
        }
    }
    CHECK(exact_memory_ops == 2);
    dolir_module_free(&module);

    PPCInst bounded_insts[] = {
        decode(0x70650FFFu, 0x80002640u), // andi. r5,r3,0xfff
        decode(0x3C859000u, 0x80002644u), // addis r4,r5,0x9000
        decode(0x80640000u, 0x80002648u), // lwz r3,0(r4)
        decode(0x90640004u, 0x8000264Cu), // stw r3,4(r4)
        decode(0x4E800020u, 0x80002650u),
    };
    dolir_module_init(&module);
    CHECK(dolir_build_chunk(&module, bounded_insts, 5, 0x80002640u));
    CHECK(dolir_verify(&module, stderr));
    u32 bounded_memory_ops = 0;
    for (u32 b = 0; b < module.functions[0].block_count; b++) {
        DolIRBlock* block = &module.functions[0].blocks[b];
        for (u32 i = 0; i < block->instruction_count; i++) {
            DolIRInstruction* instruction = &block->instructions[i];
            if (instruction->op != DOLIR_OP_GUEST_LOAD &&
                instruction->op != DOLIR_OP_GUEST_STORE)
                continue;
            CHECK(instruction->address_domain == DOLIR_ADDRESS_MEM2);
            CHECK(instruction->address_lower ==
                  WII_MEM2_BASE + bounded_memory_ops * 4u);
            CHECK(instruction->address_upper ==
                  WII_MEM2_BASE + 0xFFFu + bounded_memory_ops * 4u);
            bounded_memory_ops++;
        }
    }
    CHECK(bounded_memory_ops == 2);
    dolir_module_free(&module);
    return true;
}

static bool test_float_record_and_paired_compare(void) {
    PPCInst insts[] = {
        decode(0xFE119029u, 0x80003000u),
        decode(0x1022182Bu, 0x80003004u),
        decode(0x110D7000u, 0x80003008u),
    };
    DolIRModule module;
    dolir_module_init(&module);
    CHECK(dolir_build_chunk(&module, insts, 3, 0x80003000u));
    CHECK(dolir_verify(&module, stderr));
    u32 cr_writes = 0;
    u32 exact_paired = 0;
    for (u32 b = 0; b < module.functions[0].block_count; b++) {
        DolIRBlock* block = &module.functions[0].blocks[b];
        CHECK(block->terminator.kind != DOLIR_TERM_FALLBACK);
        for (u32 i = 0; i < block->instruction_count; i++) {
            DolIRInstruction* instruction = &block->instructions[i];
            if (instruction->op == DOLIR_OP_STATE_WRITE &&
                instruction->aux >= DOLIR_STATE_CR0 &&
                instruction->aux <= DOLIR_STATE_CR7)
                cr_writes++;
            if (instruction->op == DOLIR_OP_HELPER_CALL &&
                instruction->aux == DOLIR_HELPER_EXACT_PAIRED) {
                exact_paired++;
                CHECK(instruction->exact_fp);
                CHECK(dolir_state_mask_test(instruction->state_uses,
                                            DOLIR_STATE_FPSCR));
                CHECK(dolir_state_mask_test(instruction->state_defs,
                                            DOLIR_STATE_FPSCR));
            }
        }
    }
    CHECK(cr_writes == 2 && exact_paired == 2);
    dolir_module_free(&module);
    return true;
}

static bool test_segment_registers(void) {
    PPCInst insts[] = {
        decode(0x7D6304A6u, 0x80004000u),
        decode(0x7DC401A4u, 0x80004004u),
        decode(0x7D806D26u, 0x80004008u),
        decode(0x7DE081E4u, 0x8000400Cu),
    };
    DolIRModule module;
    dolir_module_init(&module);
    CHECK(dolir_build_chunk(&module, insts, 4, 0x80004000u));
    CHECK(dolir_verify(&module, stderr));
    for (u32 b = 0; b < module.functions[0].block_count; b++)
        CHECK(module.functions[0].blocks[b].terminator.kind != DOLIR_TERM_FALLBACK);
    dolir_module_free(&module);
    return true;
}

static bool test_cache_timing(void) {
    PPCInst dcbst = decode(0x7C11906Cu, 0x80005000u);
    PPCInst icbi = decode(0x7C1BE7ACu, 0x80005004u);
    CHECK(dolir_instruction_cycle_cost(&dcbst) == 5);
    CHECK(dolir_instruction_cycle_cost(&icbi) == 4);
    return true;
}

int main(void) {
    if (!test_native_loop() || !test_memory_and_vector() ||
        !test_static_memory_provenance() ||
        !test_mem2_and_bounded_memory_provenance() ||
        !test_float_record_and_paired_compare() || !test_segment_registers() ||
        !test_cache_timing())
        return 1;
    puts("dolir tests passed");
    return 0;
}
