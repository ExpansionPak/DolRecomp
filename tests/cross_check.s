# test instructions for cross-checking against our decoder
# assemble with: powerpc-eabi-as -mregnames -o cross_check.o cross_check.s

.section .text
.globl _start
_start:

# --- immediate arithmetic / compare ---
addi r3, r1, 16
addic r4, r4, -1
lis r5, 0x1234
cmpwi r3, -1
cmplwi r3, 0x8000

# --- immediate logical ---
ori r4, r3, 0xFF00
oris r5, r4, 0x1234
xori r6, r5, 0xFFFF
xoris r7, r6, 0x8000
andi. r8, r7, 0x00FF
andis. r9, r7, 0x00FF

# --- D-form loads ---
lwz r3, 0(r1)
lwzu r4, 4(r1)
lbz r5, 8(r1)
lbzu r6, 12(r1)
lhz r7, 16(r1)
lhzu r8, 20(r1)
lha r9, -4(r1)

# --- D-form stores ---
stw r3, 24(r1)
stwu r4, 28(r1)
stb r5, 32(r1)
stbu r6, 36(r1)
sth r7, 40(r1)
sthu r8, 44(r1)

# --- branches and SPR moves ---
b branch_target
bc 12, 2, branch_target
blr
bctr
mflr r10
mtlr r10

branch_target:
nop
