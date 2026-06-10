# test instructions for cross-checking against our decoder
# assemble with: powerpc-eabi-as -mregnames -o test.o test.s

.section .text
.globl _start
_start:

# --- addi ---
addi r3, r1, 16
addi r5, r2, -100
li r3, 42
li r0, -1
addi r31, r31, 1
addi r3, r4, 0

# --- ori ---
ori r4, r3, 0xFF00
ori r0, r0, 0
ori r3, r3, 0xFFFF
ori r10, r20, 0x1

# --- lwz ---
lwz r3, 0(r1)
lwz r4, -4(r1)
lwz r3, 256(r0)
lwz r31, 32767(r31)
lwz r0, -32768(r1)

# --- stw ---
stw r3, 0(r1)
stw r31, -8(r1)
stw r0, 12(r0)
stw r3, 32767(r4)

# --- branch (use labels so relocations resolve within section) ---
branch_b_back:
    nop
    b branch_b_back        # backward relative
    bl branch_b_back       # backward call
branch_b_fwd:
    b branch_b_fwd_target  # forward relative
    bl branch_b_fwd_target # forward call
    nop
    nop
    nop
    nop
branch_b_fwd_target:
    nop
