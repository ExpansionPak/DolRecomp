# test instructions for cross-checking against our decoder
# assemble with: powerpc-eabi-as -mregnames -o cross_check.o cross_check.s

.section .text
.globl _start
_start:

# --- D-form arithmetic / compare ---
mulli r3, r4, -7
subfic r4, r5, 1
addi r3, r1, 16
addic r4, r4, -1
addic. r5, r5, -1
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

# --- D-form loads/stores ---
lwz r3, 0(r1)
lwzu r4, 4(r1)
lbz r5, 8(r1)
lbzu r6, 12(r1)
lhz r7, 16(r1)
lhzu r8, 20(r1)
lha r9, -4(r1)
lhau r10, 24(r1)
stw r3, 28(r1)
stwu r4, 32(r1)
stb r5, 36(r1)
stbu r6, 40(r1)
sth r7, 44(r1)
sthu r8, 48(r1)
lmw r20, 52(r1)
stmw r20, 100(r1)

# --- branches / CR / SPR ---
b branch_target
bc 12, 2, branch_target
blr
bctr
cror 2, 3, 4
crorc 2, 3, 4
crxor 2, 3, 4
mcrf cr2, cr3
mfcr r10
mtcrf 0xff, r10
mflr r10
mtlr r10

# --- register compare / arithmetic ---
cmpw cr1, r3, r4
cmplw cr2, r3, r4
add r10, r11, r12
addc r11, r12, r13
adde r12, r13, r14
addze r13, r14
subf r14, r15, r16
subfc r15, r16, r17
subfe r16, r17, r18
subfze r17, r18
neg r18, r19
mullw r3, r4, r5
mulhw r6, r7, r8
mulhwu r9, r10, r11
divw r12, r13, r14
divwu r15, r16, r17

# --- register logical / shifts ---
and r19, r20, r21
andc r20, r21, r22
or r21, r22, r23
orc r22, r23, r24
xor r23, r24, r25
nand r24, r25, r26
nor r25, r26, r27
eqv r26, r27, r28
cntlzw r27, r28
extsb r28, r29
extsh r29, r30
slw r30, r31, r3
srw r31, r3, r4
sraw r3, r4, r5
srawi r4, r5, 7

# --- rotate/mask ---
rlwinm r5, r6, 5, 8, 23
rlwnm r6, r7, r8, 4, 27
rlwimi r7, r8, 8, 8, 15

# --- indexed loads/stores ---
lwzx r3, r4, r5
lwzux r6, r4, r5
lbzx r7, r4, r5
lbzux r8, r4, r5
lhzx r9, r4, r5
lhzux r10, r4, r5
lhax r11, r4, r5
lhaux r12, r4, r5
lwbrx r3, r4, r5
lhbrx r6, r7, r8
stwx r3, r4, r5
stwux r6, r4, r5
stbx r7, r4, r5
stbux r8, r4, r5
sthx r9, r4, r5
sthux r10, r4, r5
stwbrx r9, r10, r11
sthbrx r12, r13, r14
dcbz r15, r16

# --- FPU loads/stores ---
lfs f1, 0(r4)
lfsu f2, 4(r4)
lfd f3, 8(r4)
lfdu f4, 16(r4)
stfs f5, 20(r4)
stfsu f6, 24(r4)
stfd f7, 32(r4)
stfdu f8, 40(r4)
lfsx f9, r4, r5
lfsux f10, r4, r5
lfdx f11, r4, r5
lfdux f12, r4, r5
stfsx f13, r4, r5
stfsux f14, r4, r5
stfdx f15, r4, r5
stfdux f16, r4, r5

# --- FPU arithmetic / moves / compare ---
fadds f1, f2, f3
fsubs f4, f5, f6
fmuls f7, f8, f9
fdivs f10, f11, f12
fadd f13, f14, f15
fsub f16, f17, f18
fmul f19, f20, f21
fdiv f22, f23, f24
fmr f25, f26
fneg f27, f28
fabs f29, f30
fnabs f31, f0
frsp f1, f2
fcmpu cr2, f3, f4
fcmpo cr3, f5, f6

# --- paired-single memory ---
psq_l f1, 0(r4), 0, 0
psq_lu f3, 8(r4), 0, 0
psq_st f5, 16(r4), 0, 0
psq_stu f7, 24(r4), 0, 0
psq_lx f9, r4, r5, 0, 0
psq_lux f11, r4, r5, 0, 0
psq_stx f13, r4, r5, 0, 0
psq_stux f15, r4, r5, 0, 0

# --- arithmetic, string/atomic, FPSCR, and ordering ---
addme r3, r4
subfme r5, r6
lswi r7, r12, 13
lswx r9, r20, r21
stswi r12, r13, 17
stswx r14, r15, r16
lwarx r17, r18, r19
stwcx. r20, r21, r22
stfiwx f23, r24, r25
fres f1, f2
frsqrte f3, f4
ps_res f5, f6
ps_rsqrte f7, f8
fctiw f9, f10
fctiwz f11, f12
fmadd f13, f14, f15, f16
fmadds f17, f18, f19, f20
fmsub f21, f22, f23, f24
fmsubs f25, f26, f27, f28
fnmadd f29, f30, f31, f0
fnmadds f1, f2, f3, f4
fnmsub f5, f6, f7, f8
fnmsubs f9, f10, f11, f12
mffs f13
mcrfs cr2, cr3
mtfsfi 4, 10
mtfsf 0x5a, f14
sync
eieio
isync
addo r10, r11, r12
addco r11, r12, r13
addeo r12, r13, r14
addmeo r13, r14
addzeo r14, r15
subfo r15, r16, r17
subfco r16, r17, r18
subfeo r17, r18, r19
subfmeo r18, r19
subfzeo r19, r20
nego r20, r21
mullwo r21, r22, r23
divwo r22, r23, r24
divwuo r23, r24, r25
twi 4, r5, -2
tw 6, r7, r8
mcrxr cr2
mfmsr r9
mtmsr r10
mfsr r11, 3
mfsrin r12, r13
mtsr 4, r14
mtsrin r15, r16
dcbst r17, r18
dcbf r19, r20
dcbtst r21, r22
dcbt r23, r24
dcbi r25, r26
icbi r27, r28
tlbsync
sc
rfi
mftb r3, 268
dcbz_l r5, r6
tlbie r7
eciwx r8, r9, r10
ecowx r11, r12, r13

branch_target:
nop
