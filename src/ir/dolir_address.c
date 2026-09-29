#include "ir/dolir.h"

#include "cpu/cpu.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    u64 lower;
    u64 upper;
    bool bounded;
} AbstractValue;

static u32 type_width(DolIRType type) {
    switch (type) {
    case DOLIR_TYPE_I1: return 1;
    case DOLIR_TYPE_I8: return 8;
    case DOLIR_TYPE_I16: return 16;
    case DOLIR_TYPE_I32: return 32;
    case DOLIR_TYPE_I64: return 64;
    default: return 0;
    }
}

static u64 truncate_bits(u64 value, u32 width) {
    return width == 64 ? value : value & ((1ull << width) - 1ull);
}

static AbstractValue unknown_value(void) {
    AbstractValue value = {0, 0, false};
    return value;
}

static AbstractValue range_value(u64 lower, u64 upper, u32 width) {
    const u64 maximum = width == 64 ? UINT64_MAX : ((1ull << width) - 1ull);
    if (lower > upper || upper > maximum)
        return unknown_value();
    AbstractValue value = {lower, upper, true};
    return value;
}

static AbstractValue known_value(u64 bits, u32 width) {
    const u64 value = truncate_bits(bits, width);
    return range_value(value, value, width);
}

static bool exact_value(AbstractValue value) {
    return value.bounded && value.lower == value.upper;
}

static AbstractValue unary_value(const DolIRInstruction* instruction,
                                 AbstractValue operand) {
    const u32 width = type_width(instruction->type);
    if (!width)
        return unknown_value();
    switch (instruction->op) {
    case DOLIR_OP_NOT:
        return exact_value(operand) ? known_value(~operand.lower, width)
                                    : unknown_value();
    case DOLIR_OP_TRUNC:
    case DOLIR_OP_ZEXT:
        return operand.bounded ? range_value(operand.lower, operand.upper, width)
                               : unknown_value();
    case DOLIR_OP_CLZ: {
        if (!exact_value(operand))
            return unknown_value();
        u32 count = 0;
        while (count < width &&
               !(operand.lower & (1ull << (width - count - 1u))))
            count++;
        return known_value(count, width);
    }
    default:
        return unknown_value();
    }
}

static AbstractValue binary_value(const DolIRInstruction* instruction,
                                  AbstractValue left, AbstractValue right) {
    const u32 width = type_width(instruction->type);
    if (!width)
        return unknown_value();
    const u64 maximum = width == 64 ? UINT64_MAX : ((1ull << width) - 1ull);
    if (instruction->op == DOLIR_OP_AND &&
        !(exact_value(left) && exact_value(right))) {
        if (exact_value(left))
            return range_value(0, left.lower & maximum, width);
        if (exact_value(right))
            return range_value(0, right.lower & maximum, width);
    }
    if (left.bounded && right.bounded) {
        switch (instruction->op) {
        case DOLIR_OP_ADD:
            if (left.upper <= maximum - right.upper)
                return range_value(left.lower + right.lower,
                                   left.upper + right.upper, width);
            break;
        case DOLIR_OP_SUB:
            if (left.lower >= right.upper)
                return range_value(left.lower - right.upper,
                                   left.upper - right.lower, width);
            break;
        case DOLIR_OP_LSHR:
            if (exact_value(right) && right.lower < width)
                return range_value(left.lower >> right.lower,
                                   left.upper >> right.lower, width);
            break;
        case DOLIR_OP_SHL:
            if (exact_value(right) && right.lower < width &&
                left.upper <= (maximum >> right.lower))
                return range_value(left.lower << right.lower,
                                   left.upper << right.lower, width);
            break;
        default:
            break;
        }
    }
    if (!exact_value(left) || !exact_value(right))
        return unknown_value();
    switch (instruction->op) {
    case DOLIR_OP_ADD: return known_value(left.lower + right.lower, width);
    case DOLIR_OP_SUB: return known_value(left.lower - right.lower, width);
    case DOLIR_OP_MUL: return known_value(left.lower * right.lower, width);
    case DOLIR_OP_UDIV:
        return right.lower ? known_value(left.lower / right.lower, width)
                           : unknown_value();
    case DOLIR_OP_AND: return known_value(left.lower & right.lower, width);
    case DOLIR_OP_OR: return known_value(left.lower | right.lower, width);
    case DOLIR_OP_XOR: return known_value(left.lower ^ right.lower, width);
    case DOLIR_OP_SHL:
        return right.lower < width ? known_value(left.lower << right.lower, width)
                                   : unknown_value();
    case DOLIR_OP_LSHR:
        return right.lower < width ? known_value(left.lower >> right.lower, width)
                                   : unknown_value();
    case DOLIR_OP_ROTL: {
        u32 shift = (u32)right.lower & (width - 1u);
        if (!shift)
            return known_value(left.lower, width);
        return known_value((left.lower << shift) |
                           (left.lower >> (width - shift)), width);
    }
    case DOLIR_OP_ICMP_EQ: return known_value(left.lower == right.lower, 1);
    case DOLIR_OP_ICMP_NE: return known_value(left.lower != right.lower, 1);
    case DOLIR_OP_ICMP_ULT: return known_value(left.lower < right.lower, 1);
    case DOLIR_OP_ICMP_ULE: return known_value(left.lower <= right.lower, 1);
    default: return unknown_value();
    }
}

static DolIRAddressDomain classify_address(u32 address) {
    if (address >= 0xCC008000u && address < 0xCC009000u)
        return DOLIR_ADDRESS_FIFO;
    if (address >= 0xCC000000u && address < 0xCE000000u)
        return DOLIR_ADDRESS_MMIO;
    u32 normalized = address & ~0x40000000u;
    if (normalized >= GC_RAM_BASE &&
        normalized < GC_RAM_BASE + GC_MAIN_RAM_SIZE)
        return DOLIR_ADDRESS_MEM1;
    if (normalized >= WII_MEM2_BASE &&
        normalized < WII_MEM2_BASE + WII_MEM2_SIZE)
        return DOLIR_ADDRESS_MEM2;
    if (address < GC_RAM_BASE)
        return DOLIR_ADDRESS_PHYSICAL;
    if (address < 0xE0000000u)
        return DOLIR_ADDRESS_GUEST_DATA;
    return DOLIR_ADDRESS_UNKNOWN;
}

static u32 terminator_target_count(DolIRTerminatorKind kind) {
    if (kind == DOLIR_TERM_COND_BRANCH || kind == DOLIR_TERM_INDIRECT)
        return 2;
    return kind == DOLIR_TERM_BRANCH ? 1 : 0;
}

static void find_region_leaders(const DolIRFunction* function, bool* leaders) {
    leaders[0] = true;
    for (u32 index = 0; index < function->block_count; index++) {
        const DolIRTerminator* term = &function->blocks[index].terminator;
        if (term->kind == DOLIR_TERM_FALLBACK)
            leaders[index] = true;
        if (index + 1u < function->block_count &&
            term->kind != DOLIR_TERM_FALLTHROUGH)
            leaders[index + 1u] = true;
        for (u32 slot = 0; slot < terminator_target_count(term->kind); slot++)
            if (term->targets[slot] != DOLIR_NO_BLOCK)
                leaders[term->targets[slot]] = true;
    }
}

static AbstractValue instruction_value(const DolIRInstruction* instruction,
                                       const AbstractValue* values,
                                       const AbstractValue* state) {
    const u32 width = type_width(instruction->type);
    if (instruction->op == DOLIR_OP_CONSTANT && width)
        return known_value(instruction->immediate, width);
    if (instruction->op == DOLIR_OP_STATE_READ)
        return state[instruction->aux];
    if (instruction->operand_count == 1)
        return unary_value(instruction, values[instruction->operands[0]]);
    if (instruction->operand_count == 2)
        return binary_value(instruction, values[instruction->operands[0]],
                            values[instruction->operands[1]]);
    if (instruction->op == DOLIR_OP_SELECT && instruction->operand_count == 3) {
        AbstractValue condition = values[instruction->operands[0]];
        AbstractValue yes = values[instruction->operands[1]];
        AbstractValue no = values[instruction->operands[2]];
        if (exact_value(condition))
            return condition.lower ? yes : no;
        if (yes.bounded && no.bounded)
            return range_value(yes.lower < no.lower ? yes.lower : no.lower,
                               yes.upper > no.upper ? yes.upper : no.upper,
                               width);
    }
    return unknown_value();
}

static bool range_in_window(u32 lower, u32 upper, u32 base, u32 size) {
    return size && lower >= base && upper >= lower &&
           lower - base < size && upper - base < size;
}

static DolIRAddressDomain classify_range(u32 lower, u32 upper) {
    if (lower == upper)
        return classify_address(lower);
    for (u32 alias = 0; alias <= 0x40000000u; alias += 0x40000000u) {
        if (range_in_window(lower, upper, GC_RAM_BASE | alias, GC_MAIN_RAM_SIZE))
            return DOLIR_ADDRESS_MEM1;
        if (range_in_window(lower, upper, WII_MEM2_BASE | alias, WII_MEM2_SIZE))
            return DOLIR_ADDRESS_MEM2;
    }
    return DOLIR_ADDRESS_UNKNOWN;
}

void dolir_analyze_addresses(DolIRFunction* function) {
    if (!function || !function->block_count || !function->value_count)
        return;
    bool* leaders = (bool*)calloc(function->block_count, sizeof(*leaders));
    AbstractValue* values =
        (AbstractValue*)calloc(function->value_count, sizeof(*values));
    if (!leaders || !values) {
        free(leaders);
        free(values);
        return;
    }
    AbstractValue state[DOLIR_STATE_COUNT];
    memset(state, 0, sizeof(state));
    find_region_leaders(function, leaders);
    for (u32 block_index = 0; block_index < function->block_count; block_index++) {
        DolIRBlock* block = &function->blocks[block_index];
        if (leaders[block_index])
            memset(state, 0, sizeof(state));
        for (u32 index = 0; index < block->instruction_count; index++) {
            DolIRInstruction* instruction = &block->instructions[index];
            instruction->address_domain = DOLIR_ADDRESS_UNKNOWN;
            instruction->address_lower = 0;
            instruction->address_upper = 0;
            if ((instruction->op == DOLIR_OP_GUEST_LOAD ||
                 instruction->op == DOLIR_OP_GUEST_STORE) &&
                values[instruction->operands[0]].bounded &&
                values[instruction->operands[0]].upper <= UINT32_MAX) {
                u32 lower = (u32)values[instruction->operands[0]].lower;
                u32 upper = (u32)values[instruction->operands[0]].upper;
                instruction->address_domain = classify_range(lower, upper);
                if (instruction->address_domain != DOLIR_ADDRESS_UNKNOWN) {
                    instruction->address_lower = lower;
                    instruction->address_upper = upper;
                }
            }
            AbstractValue result =
                instruction_value(instruction, values, state);
            if (instruction->result)
                values[instruction->result] = result;
            if (instruction->op == DOLIR_OP_STATE_WRITE)
                state[instruction->aux] =
                    values[instruction->operands[0]];
            for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++)
                if (dolir_state_mask_test(instruction->state_defs,
                                          (DolIRStateSlot)slot) &&
                    !(instruction->op == DOLIR_OP_STATE_WRITE &&
                      instruction->aux == slot))
                    state[slot] = unknown_value();
        }
    }
    free(leaders);
    free(values);
}

const char* dolir_address_domain_name(DolIRAddressDomain domain) {
    static const char* names[] = {
        "unknown", "guest-data", "mem1", "mem2", "physical", "mmio", "fifo"
    };
    return domain <= DOLIR_ADDRESS_FIFO ? names[domain] : "invalid";
}
