/*
 * Probe corpus for the emitted-code acceptance differential.
 *
 * Each probe is one PowerPC instruction with curated input state and the
 * real-Gekko-correct outputs (the oracle). Raw encodings come from
 * tests/cross_check.c (assembled by devkitPPC). Output values come from
 * tests/dolphin/source/main.c (real-PPC-verified constants) and unambiguous
 * PEM/Gekko-manual facts; FP-helper estimates (fres/frsqrte/ps_res/ps_rsqrte,
 * the single-rounded FMA tie, fctiw) are cross-checked against Dolphin.
 *
 * Classification:
 *   non-xfail -> we expect the emitter to be correct; a divergence is a FAIL,
 *                i.e. an UNEXPECTED / newly found error.
 *   xfail     -> catalogued DolRecomp emitter defect (golden = real Gekko).
 *                A divergence is expected (XFAIL); a match is XPASS (fixed).
 *
 * FP inputs use single-precision bit patterns when .fp_single_inputs = true
 * (widened to double by the runner), else raw f64 bit patterns.
 */

/* ===== original catalogued set ===== */
static const Expect e_add[]   = { { O_GPR, 10, 30, false, 0 } };
static const Expect e_mulli[] = { { O_GPR, 3, 14, false, 0 } };
static const Expect e_srawi[] = { { O_GPR, 4, 1, false, 0 }, { O_XER, 0, 0u, false, "CA clear" } };
static const Expect e_addic[] = { { O_GPR, 4, 0, false, 0 }, { O_XER, 0, 0x20000000u, false, "CA set" } };
static const Expect e_fadds[] = { { O_FPR_SINGLE, 1, 0x40700000u, false, 0 } };
static const Expect e_fmuls[] = { { O_FPR_SINGLE, 7, 0x40C00000u, false, 0 } };
static const Expect e_fadd[]  = { { O_FPR_DOUBLE, 13, 0x400E000000000000ull, false, 0 } };
static const Expect e_lfs[]   = { { O_PS0_SINGLE, 1, 0x3F800000u, false, "load reaches ps0" },
                                  { O_PS1_SINGLE, 1, 0x3F800000u, false,  "lfs must also set ps1 (emitter leaves it stale)" } };
static const Expect e_faddsdr[] = { { O_FPR_SINGLE, 1, 0x4B000001u, false, "single op must round once, not per-operand" } };
static const Expect e_psadddr[] = { { O_PS0_SINGLE, 1, 0x4B000001u, true, "ps lane must round once, not per-operand" } };

/* ===== integer / logical / shift / rotate (verified constants) ===== */
static const Expect e_subfic[] = { { O_GPR, 4, 0xFFFFFFFCu, false, 0 }, { O_XER, 0, 0u, false, "borrow clears CA" } };
static const Expect e_addis[]  = { { O_GPR, 5, 0x12340000u, false, 0 } };
static const Expect e_and[]    = { { O_GPR, 19, 0x00F000F0u, false, 0 } };
static const Expect e_andc[]   = { { O_GPR, 20, 0x0F000F00u, false, 0 } };
static const Expect e_or[]     = { { O_GPR, 21, 0x12345678u, false, 0 } };
static const Expect e_xor[]    = { { O_GPR, 23, 0x55555555u, false, 0 } };
static const Expect e_nand[]   = { { O_GPR, 24, 0xF0F0F0F0u, false, 0 } };
static const Expect e_nor[]    = { { O_GPR, 25, 0x00FFFFFFu, false, 0 } };
static const Expect e_eqv[]    = { { O_GPR, 26, 0xFF0000FFu, false, 0 } };
static const Expect e_cntlzw[] = { { O_GPR, 27, 8, false, 0 } };
static const Expect e_extsb[]  = { { O_GPR, 28, 0xFFFFFF80u, false, 0 } };
static const Expect e_extsh[]  = { { O_GPR, 29, 0xFFFF8001u, false, 0 } };
static const Expect e_slw[]    = { { O_GPR, 30, 0x80000000u, false, 0 } };
static const Expect e_slw32[]  = { { O_GPR, 30, 0u, false, "shift 32 clears" } };
static const Expect e_srw[]    = { { O_GPR, 31, 1, false, 0 } };
static const Expect e_srw32[]  = { { O_GPR, 31, 0u, false, "shift 32 clears" } };
static const Expect e_sraw[]   = { { O_GPR, 3, 0xC0000000u, false, 0 }, { O_XER, 0, 0x20000000u, false, "CA from shifted-out 1" } };
static const Expect e_sraw32[] = { { O_GPR, 3, 0xFFFFFFFFu, false, 0 }, { O_XER, 0, 0x20000000u, false, "shift 32 sign fills + CA" } };
static const Expect e_rlwinm[] = { { O_GPR, 5, 0x008ACF00u, false, 0 } };
static const Expect e_rlwnm[]  = { { O_GPR, 6, 0x0ABCDEF0u, false, 0 } };
static const Expect e_rlwimi[] = { { O_GPR, 7, 0xAA56AA00u, false, 0 } };

/* ===== compare / CR ===== */
static const Expect e_cmpi[]  = { { O_CR, 0, 0x40000000u, false, "GT in CR0" } };
static const Expect e_cmpli[] = { { O_CR, 0, 0x40000000u, false, "unsigned GT" } };
static const Expect e_cmp[]   = { { O_CR, 0, 0x08000000u, false, "signed LT in CR1" } };
static const Expect e_cmpl[]  = { { O_CR, 0, 0x00400000u, false, "unsigned GT in CR2" } };
static const Expect e_cror[]  = { { O_CR, 0, 0x30000000u, false, 0 } };
static const Expect e_crand[] = { { O_CR, 0, 0x10000000u, false, 0 } };
static const Expect e_crxor[] = { { O_CR, 0, 0x30000000u, false, 0 } };
static const Expect e_mcrf[]  = { { O_CR, 0, 0x12445678u, false, 0 } };
static const Expect e_mtcrf[] = { { O_CR, 0, 0x12345678u, false, 0 } };
static const Expect e_mfcr[]  = { { O_GPR, 10, 0xA5A50000u, false, 0 } };

/* ===== carry / overflow ===== */
static const Expect e_addc[]   = { { O_GPR, 11, 0u, false, 0 }, { O_XER, 0, 0x20000000u, false, "carry" } };
static const Expect e_adde[]   = { { O_GPR, 12, 0u, false, 0 }, { O_XER, 0, 0x20000000u, false, "includes+keeps CA" } };
static const Expect e_addze[]  = { { O_GPR, 13, 0u, false, 0 }, { O_XER, 0, 0x20000000u, false, "carry" } };
static const Expect e_subfc[]  = { { O_GPR, 15, 0u, false, 0 }, { O_XER, 0, 0x20000000u, false, "equal sets CA" } };
static const Expect e_subfe[]  = { { O_GPR, 16, 0xFFFFFFFEu, false, 0 }, { O_XER, 0, 0u, false, "borrow clears CA" } };
static const Expect e_subfze[] = { { O_GPR, 17, 0u, false, 0 }, { O_XER, 0, 0x20000000u, false, "carry" } };
static const Expect e_neg[]    = { { O_GPR, 18, 0xFFFFFFFBu, false, 0 } };
static const Expect e_mulhw[]  = { { O_GPR, 6, 0xFFFFFFFFu, false, 0 } };
static const Expect e_mulhwu[] = { { O_GPR, 9, 1, false, 0 } };
static const Expect e_divw[]   = { { O_GPR, 12, 0xFFFFFFFDu, false, "trunc toward zero" } };
static const Expect e_divwu[]  = { { O_GPR, 15, 3, false, 0 } };
static const Expect e_addo[]   = { { O_GPR, 10, 0x80000000u, false, 0 }, { O_XER, 0, 0xC0000000u, false, "OV+SO" } };
static const Expect e_divwo[]  = { { O_GPR, 22, 0xFFFFFFFFu, false, 0 }, { O_XER, 0, 0xC0000000u, false, "div0 OV+SO" } };
static const Expect e_addme[]  = { { O_GPR, 3, 4, false, 0 } };
static const Expect e_subfme[] = { { O_GPR, 5, 0xFFFFFFF9u, false, 0 } };

/* ===== FP scalar (verified) ===== */
static const Expect e_fsubs[]  = { { O_FPR_SINGLE, 4, 0x40B00000u, false, 0 } };
static const Expect e_fdivs[]  = { { O_FPR_SINGLE, 10, 0x40600000u, false, 0 } };
static const Expect e_fsub[]   = { { O_FPR_DOUBLE, 16, 0x4016000000000000ull, false, 0 } };
static const Expect e_fmul[]   = { { O_FPR_DOUBLE, 19, 0x4018000000000000ull, false, 0 } };
static const Expect e_fdiv[]   = { { O_FPR_DOUBLE, 22, 0x400C000000000000ull, false, 0 } };
static const Expect e_fneg[]   = { { O_FPR_DOUBLE, 27, 0xC004000000000000ull, false, 0 } };
static const Expect e_fabs[]   = { { O_FPR_DOUBLE, 29, 0x4004000000000000ull, false, 0 } };
static const Expect e_fnabs[]  = { { O_FPR_DOUBLE, 31, 0xC004000000000000ull, false, 0 } };
static const Expect e_fsel[]   = { { O_FPR_DOUBLE, 1, 0x4010000000000000ull, false, "neg selects fB" } };
static const Expect e_fmr[]    = { { O_FPR_DOUBLE, 25, 0x8000000000000000ull, false, "preserve -0" } };
static const Expect e_fctiw[]  = { { O_FPR_DOUBLE, 9, 0xFFF8000000000002ull, false, "2.5 ties to even" } };
static const Expect e_fctiwz[] = { { O_FPR_DOUBLE, 11, 0xFFF80000FFFFFFFEull, false, "-2.9 trunc" } };

/* ===== FMA (verified; emitter uses ppc_fma -> expect correct) ===== */
static const Expect e_fmaddsc[]    = { { O_FPR_SINGLE, 17, 0x41200000u, false, 0 } };
static const Expect e_fmaddstie[]  = { { O_FPR_SINGLE, 17, 0xBF55BF17u, false, "single-round tie" } };
static const Expect e_fmsubs[]     = { { O_FPR_SINGLE, 25, 0x41300000u, false, 0 } };
static const Expect e_fnmadds[]    = { { O_FPR_SINGLE, 1, 0xC1200000u, false, 0 } };
static const Expect e_fnmsubs[]    = { { O_FPR_SINGLE, 9, 0xC1300000u, false, 0 } };
static const Expect e_fmaddc[]     = { { O_FPR_DOUBLE, 13, 0x4024000000000000ull, false, 0 } };
static const Expect e_fmaddcancel[]= { { O_FPR_DOUBLE, 13, 0xB970000000000000ull, false, "fused cancellation" } };

/* ===== estimates (exercise the Dolphin-derived tables in cpu.c) ===== */
static const Expect e_fres0[]      = { { O_FPR_DOUBLE, 1, 0x7FF0000000000000ull, false, "fres(+0)=+inf" } };
static const Expect e_frsqrteneg[] = { { O_FPR_DOUBLE, 3, 0x7FF8000000000000ull, false, "frsqrte(-x)=QNaN" } };
static const Expect e_psres[]      = { { O_PS0_SINGLE, 5, 0x3EAAA800u, false, 0 }, { O_PS1_SINGLE, 5, 0x3E7FF800u, false, 0 } };
static const Expect e_psrsqrte[]   = { { O_PS0_SINGLE, 7, 0x3F13CA00u, false, 0 }, { O_PS1_SINGLE, 7, 0x3EFFF400u, false, 0 } };

/* ===== NEW findings (session 58), grounded in Dolphin Interpreter_FloatingPoint.cpp ===== */
/* frsp: Dolphin stores ppc_state.ps[FD].Fill(rounded) -> BOTH lanes; emitter
 * (emitter.c:1037) writes fpr only, leaving ps1 stale. */
static const Expect e_frsp_ps1[] = {
    { O_PS0_SINGLE, 1, 0x3FC00000u, false, "frsp ps0 rounds to single" },
    { O_PS1_SINGLE, 1, 0x3FC00000u, true,  "NEW: frsp must broadcast result to ps1 (Dolphin Fill); emitter leaves ps1 stale" },
};
/* fmadds family: Dolphin fmaddsx stores Fill(result), so both lanes receive
 * the rounded single result. The full-state DOL oracle verifies this under
 * both Interpreter and JIT. */
static const Expect e_fmadds_ps1[] = {
    { O_FPR_SINGLE, 17, 0x41200000u, false, "fmadds ps0 result ok" },
    { O_PS1_SINGLE, 17, 0x41200000u, false, "fmadds broadcasts result to ps1" },
};

/* ===== Slice-2 seed broadening: memory/control/system families ===== */
static const Expect e_lwz_mem[]   = { { O_GPR, 3, 0x12345678u, false, 0 } };
static const Expect e_lwzu_mem[]  = { { O_GPR, 4, 0xDEADBEEFu, false, 0 }, { O_GPR, 1, 0x80010004u, false, "update rA" } };
static const Expect e_lbz_mem[]   = { { O_GPR, 5, 0x000000ABu, false, 0 } };
static const Expect e_lha_mem[]   = { { O_GPR, 9, 0xFFFFFF80u, false, "sign extend" } };
static const Expect e_stw_mem[]   = { { O_MEM32, 0x1C, 0xCAFEBABEu, false, 0 } };
static const Expect e_stwu_mem[]  = { { O_MEM32, 0x20, 0x0BADF00Du, false, 0 }, { O_GPR, 1, 0x80010020u, false, "update rA" } };
static const Expect e_lwbrx_mem[] = { { O_GPR, 3, 0x78563412u, false, "byte-reversed load" } };
static const Expect e_sthbrx_mem[]= { { O_MEM32, 0x38, 0x0000CDABu, false, "byte-reversed halfword store in low half" } };
static const Expect e_stfiwx_mem[]= { { O_MEM32, 0x40, 0xDEADBEEFu, false, "store low f64 word" } };
static const Expect e_psq_l_mem[] = { { O_PS0_SINGLE, 1, 0x3F800000u, false, 0 }, { O_PS1_SINGLE, 1, 0x40000000u, false, 0 } };
static const Expect e_psq_st_mem[]= { { O_MEM32, 0x50, 0x40400000u, false, "ps0" }, { O_MEM32, 0x54, 0x40800000u, false, "ps1" } };
static const Expect e_lwarx_mem[] = { { O_GPR, 17, 0x89ABCDEFu, false, 0 }, { O_RESERVE_VALID, 0, 1u, false, "reservation armed" } };
static const Expect e_mcrxr_sys[] = { { O_CR, 0, 0x12E45678u, false, "copy XER[SO/OV/CA] to CR2" }, { O_XER, 0, 0u, false, "clear XER SO/OV/CA" } };
static const Expect e_mfmsr_sys[] = { { O_GPR, 9, 0x12345678u, false, 0 } };
static const Expect e_mtmsr_sys[] = { { O_MSR, 0, 0xA5A5A5A5u, false, 0 } };
static const Expect e_mfsr_sys[]  = { { O_GPR, 11, 0xCAFEBABEu, false, 0 } };
static const Expect e_mtsr_sys[]  = { { O_SR, 4, 0x0BADF00Du, false, 0 } };
static const Expect e_sync_sys[]  = { { O_GPR, 3, 0xA5A5A5A5u, false, "state preserved" }, { O_EXCEPTION, 0, 0u, false, 0 } };
static const Expect e_twi_false[] = { { O_EXCEPTION, 0, 0u, false, "false trap condition" } };
static const Expect e_twi_true[]  = {
    { O_EXCEPTION, 0, 0x00000001u, false, "PROGRAM exception" },
    { O_PROGRAM_EXCEPTION, 0, 0x00020000u, false, "trap cause" },
    { O_PC, 0, 0x00000700u, false, "program vector" },
    { O_SRR0, 0, 0x80004880u, false, "faulting instruction" },
};
static const Expect e_sc_sys[] = {
    { O_EXCEPTION, 0, 0x00000008u, false, "system call exception" },
    { O_PC, 0, 0x00000C00u, false, "system-call vector" },
    { O_SRR0, 0, 0x80004904u, false, "SC saves next instruction" },
};
static const Expect e_b_abs[] = { { O_PC, 0, 0x80004A48u, false, "relative branch target" } };

/* ===== propagation sequences: producer/consumer chains ===== */
static const Expect e_seq_lfs_psqst[] = {
    { O_MEM32, 0x40, 0x3F800000u, false, "lfs ps0 consumed by psq_st" },
    { O_MEM32, 0x44, 0x3F800000u, false,  "propagated lfs ps1 lane must store 1.0" },
};
static const Expect e_seq_frsp_psqst[] = {
    { O_MEM32, 0x40, 0x3FC00000u, false, "frsp ps0 consumed by psq_st" },
    { O_MEM32, 0x44, 0x3FC00000u, true,  "propagated frsp ps1 lane must store rounded result" },
};
static const Expect e_seq_fmadds_psqst[] = {
    { O_MEM32, 0x40, 0x41200000u, false, "fmadds ps0 consumed by psq_st" },
    { O_MEM32, 0x44, 0x41200000u, false, "propagated fmadds broadcasts to ps1" },
};
static const Expect e_seq_addic_adde[] = {
    { O_GPR, 4, 0u, false, "producer addic result" },
    { O_GPR, 5, 0u, false, "consumer adde used producer CA" },
    { O_XER, 0, 0x20000000u, false, "carry propagated through both ops" },
};
static const Expect e_seq_cmp_beq[] = {
    { O_GPR, 7, 0u, false, "branch skipped fallthrough write" },
    { O_GPR, 8, 2u, false, "branch target executed" },
    { O_PC, 0, 0x80004B10u, false, "sequence reached function end" },
};
static const Expect e_seq_lwzu_stw[] = {
    { O_GPR, 1, 0x80010004u, false, "lwzu updated base" },
    { O_GPR, 4, 0xDEADBEEFu, false, "lwzu loaded value" },
    { O_MEM32, 0x08, 0xDEADBEEFu, false, "stw consumed updated base and loaded value" },
};
static const Expect e_seq_fadds_stfs[] = {
    { O_MEM32, 0x40, 0x4B000001u, false, "propagated single op must round once before store" },
};
static const Expect e_seq_psadd_psqst[] = {
    { O_MEM32, 0x40, 0x4B000001u, true, "propagated ps0 op must round once before store" },
    { O_MEM32, 0x44, 0x4B000001u, true, "propagated ps1 op must round once before store" },
};

static const u8 mem_one_f[] = { 0x3F, 0x80, 0x00, 0x00 }; /* 1.0f, big-endian */
static const u8 mem_lwz[]    = { 0x12, 0x34, 0x56, 0x78 };
static const u8 mem_lwzu[]   = { 0, 0, 0, 0, 0xDE, 0xAD, 0xBE, 0xEF };
static const u8 mem_lbz[]    = { 0, 0, 0, 0, 0, 0, 0, 0, 0xAB };
static const u8 mem_lha[]    = { 0x00, 0x00, 0x00, 0x00, 0xFF, 0x80 };
static const u8 mem_blank64[] = { 0 };
static const u8 mem_word[]   = { 0x12, 0x34, 0x56, 0x78 };
static const u8 mem_ps_pair[]= { 0x3F, 0x80, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00 };
static const u8 mem_lwarx[]  = { 0x89, 0xAB, 0xCD, 0xEF };

static const u32 seq_lfs_psqst[]    = { 0xC0240000u, 0xF0250000u };
static const u32 seq_frsp_psqst[]   = { 0xFC201018u, 0xF0250000u };
static const u32 seq_fmadds_psqst[] = { 0xEE32A4FAu, 0xF2250000u };
static const u32 seq_addic_adde[]   = { 0x3084FFFFu, 0x7CA53114u };
static const u32 seq_cmp_beq[]      = { 0x2C030000u, 0x41820008u, 0x38E00001u, 0x39000002u };
static const u32 seq_lwzu_stw[]     = { 0x84810004u, 0x90810004u };
static const u32 seq_fadds_stfs[]   = { 0xEC22182Au, 0xD0250000u };
static const u32 seq_psadd_psqst[]  = { 0x1022182Au, 0xF0250000u };

#define D_125 0x3FF4000000000000ull
#define D_25  0x4004000000000000ull
#define D_30  0x4008000000000000ull
#define D_20  0x4000000000000000ull
#define D_70  0x401C000000000000ull
#define D_15  0x3FF8000000000000ull

const Probe probes[] = {
    /* ---- catalogued classes ---- */
    { .name = "add",      .raw = 0x7D4B6214, .address = 0x80003000, .gpr = { [11] = 10, [12] = 20 }, EXPECTS(e_add) },
    { .name = "mulli",    .raw = 0x1C64FFF9, .address = 0x80003040, .gpr = { [4] = 0xFFFFFFFEu }, EXPECTS(e_mulli) },
    { .name = "srawi",    .raw = 0x7CA43E70, .address = 0x80003080, .gpr = { [5] = 0x00000080u }, EXPECTS(e_srawi) },
    { .name = "addic",    .raw = 0x3084FFFF, .address = 0x800030C0, .gpr = { [4] = 1 }, EXPECTS(e_addic) },
    { .name = "fadds",    .raw = 0xEC22182A, .address = 0x80003100, .fpr = { [2] = D_125, [3] = D_25 }, EXPECTS(e_fadds) },
    { .name = "fmuls",    .raw = 0xECE80272, .address = 0x80003140, .fpr = { [8] = D_30, [9] = D_20 }, EXPECTS(e_fmuls) },
    { .name = "fadd",     .raw = 0xFDAE782A, .address = 0x80003180, .fpr = { [14] = D_125, [15] = D_25 }, EXPECTS(e_fadd) },
    { .name = "lfs.ps1",  .raw = 0xC0240000, .address = 0x800031C0, .gpr = { [4] = 0x80010000u }, .ps1 = { [1] = 0x4040000000000000ull },
      .mem_base = 0x80010000u, .mem_len = 4, .mem_in = mem_one_f, EXPECTS(e_lfs) },
    { .name = "fadds.dr", .raw = 0xEC22182A, .address = 0x80003200, .fpr = { [2] = 0x4160000000000000ull, [3] = 0x3FE0000010000000ull }, EXPECTS(e_faddsdr) },
    { .name = "ps_add.dr",.raw = 0x1022182A, .address = 0x80003240, .fpr = { [2] = 0x4160000000000000ull, [3] = 0x3FE0000010000000ull }, EXPECTS(e_psadddr) },

    /* ---- integer / logical / shift / rotate ---- */
    { .name = "subfic",   .raw = 0x20850001, .address = 0x80003280, .gpr = { [5] = 5 }, EXPECTS(e_subfic) },
    { .name = "addis",    .raw = 0x3CA01234, .address = 0x800032C0, EXPECTS(e_addis) },
    { .name = "and",      .raw = 0x7E93A838, .address = 0x80003300, .gpr = { [20] = 0xF0F0F0F0u, [21] = 0x0FF00FF0u }, EXPECTS(e_and) },
    { .name = "andc",     .raw = 0x7EB4B078, .address = 0x80003340, .gpr = { [21] = 0x0FF00FF0u, [22] = 0x00FF00FFu }, EXPECTS(e_andc) },
    { .name = "or",       .raw = 0x7ED5BB78, .address = 0x80003380, .gpr = { [22] = 0x12340000u, [23] = 0x00005678u }, EXPECTS(e_or) },
    { .name = "xor",      .raw = 0x7F17CA78, .address = 0x800033C0, .gpr = { [24] = 0xAAAA5555u, [25] = 0xFFFF0000u }, EXPECTS(e_xor) },
    { .name = "nand",     .raw = 0x7F38D3B8, .address = 0x80003400, .gpr = { [25] = 0xFFFFFFFFu, [26] = 0x0F0F0F0Fu }, EXPECTS(e_nand) },
    { .name = "nor",      .raw = 0x7F59D8F8, .address = 0x80003440, .gpr = { [26] = 0xF0000000u, [27] = 0x0F000000u }, EXPECTS(e_nor) },
    { .name = "eqv",      .raw = 0x7F7AE238, .address = 0x80003480, .gpr = { [27] = 0xFFFF0000u, [28] = 0xFF00FF00u }, EXPECTS(e_eqv) },
    { .name = "cntlzw",   .raw = 0x7F9B0034, .address = 0x800034C0, .gpr = { [28] = 0x00F00000u }, EXPECTS(e_cntlzw) },
    { .name = "extsb",    .raw = 0x7FBC0774, .address = 0x80003500, .gpr = { [29] = 0x00000080u }, EXPECTS(e_extsb) },
    { .name = "extsh",    .raw = 0x7FDD0734, .address = 0x80003540, .gpr = { [30] = 0x00008001u }, EXPECTS(e_extsh) },
    { .name = "slw",      .raw = 0x7FFE1830, .address = 0x80003580, .gpr = { [31] = 1, [3] = 31 }, EXPECTS(e_slw) },
    { .name = "slw.32",   .raw = 0x7FFE1830, .address = 0x800035C0, .gpr = { [31] = 1, [3] = 32 }, EXPECTS(e_slw32) },
    { .name = "srw",      .raw = 0x7C7F2430, .address = 0x80003600, .gpr = { [3] = 0x80000000u, [4] = 31 }, EXPECTS(e_srw) },
    { .name = "srw.32",   .raw = 0x7C7F2430, .address = 0x80003640, .gpr = { [3] = 0x80000000u, [4] = 32 }, EXPECTS(e_srw32) },
    { .name = "sraw",     .raw = 0x7C832E30, .address = 0x80003680, .gpr = { [4] = 0x80000001u, [5] = 1 }, EXPECTS(e_sraw) },
    { .name = "sraw.32",  .raw = 0x7C832E30, .address = 0x800036C0, .gpr = { [4] = 0x80000001u, [5] = 32 }, EXPECTS(e_sraw32) },
    { .name = "rlwinm",   .raw = 0x54C52A2E, .address = 0x80003700, .gpr = { [6] = 0x12345678u }, EXPECTS(e_rlwinm) },
    { .name = "rlwnm",    .raw = 0x5CE64136, .address = 0x80003740, .gpr = { [7] = 0x89ABCDEFu, [8] = 36 }, EXPECTS(e_rlwnm) },
    { .name = "rlwimi",   .raw = 0x5107421E, .address = 0x80003780, .gpr = { [7] = 0xAA00AA00u, [8] = 0x12345678u }, EXPECTS(e_rlwimi) },

    /* ---- compare / CR ---- */
    { .name = "cmpi",     .raw = 0x2C03FFFF, .address = 0x800037C0, .gpr = { [3] = 7 }, EXPECTS(e_cmpi) },
    { .name = "cmpli",    .raw = 0x28038000, .address = 0x80003800, .gpr = { [3] = 0xFFFFFFFFu }, EXPECTS(e_cmpli) },
    { .name = "cmp",      .raw = 0x7C832000, .address = 0x80003840, .gpr = { [3] = 0xFFFFFFFFu, [4] = 0 }, EXPECTS(e_cmp) },
    { .name = "cmpl",     .raw = 0x7D032040, .address = 0x80003880, .gpr = { [3] = 0xFFFFFFFFu, [4] = 0 }, EXPECTS(e_cmpl) },
    { .name = "cror",     .raw = 0x4C432382, .address = 0x800038C0, .cr = 0x10000000u, EXPECTS(e_cror) },
    { .name = "crand",    .raw = 0x4C432202, .address = 0x80003900, .cr = 0x10000000u, EXPECTS(e_crand) },
    { .name = "crxor",    .raw = 0x4C432182, .address = 0x80003940, .cr = 0x10000000u, EXPECTS(e_crxor) },
    { .name = "mcrf",     .raw = 0x4D0C0000, .address = 0x80003980, .cr = 0x12345678u, EXPECTS(e_mcrf) },
    { .name = "mtcrf",    .raw = 0x7D4FF120, .address = 0x800039C0, .gpr = { [10] = 0x12345678u }, EXPECTS(e_mtcrf) },
    { .name = "mfcr",     .raw = 0x7D400026, .address = 0x80003A00, .cr = 0xA5A50000u, EXPECTS(e_mfcr) },

    /* ---- carry / overflow ---- */
    { .name = "addc",     .raw = 0x7D6C6814, .address = 0x80003A40, .gpr = { [12] = 0xFFFFFFFFu, [13] = 1 }, EXPECTS(e_addc) },
    { .name = "adde",     .raw = 0x7D8D7114, .address = 0x80003A80, .gpr = { [13] = 0xFFFFFFFFu, [14] = 0 }, .xer = 0x20000000u, EXPECTS(e_adde) },
    { .name = "addze",    .raw = 0x7DAE0194, .address = 0x80003AC0, .gpr = { [14] = 0xFFFFFFFFu }, .xer = 0x20000000u, EXPECTS(e_addze) },
    { .name = "subfc",    .raw = 0x7DF08810, .address = 0x80003B00, .gpr = { [16] = 5, [17] = 5 }, EXPECTS(e_subfc) },
    { .name = "subfe",    .raw = 0x7E119110, .address = 0x80003B40, .gpr = { [17] = 7, [18] = 5 }, .xer = 0x20000000u, EXPECTS(e_subfe) },
    { .name = "subfze",   .raw = 0x7E320190, .address = 0x80003B80, .gpr = { [18] = 0 }, .xer = 0x20000000u, EXPECTS(e_subfze) },
    { .name = "neg",      .raw = 0x7E5300D0, .address = 0x80003BC0, .gpr = { [19] = 5 }, EXPECTS(e_neg) },
    { .name = "mulhw",    .raw = 0x7CC74096, .address = 0x80003C00, .gpr = { [7] = 0xFFFFFFFFu, [8] = 2 }, EXPECTS(e_mulhw) },
    { .name = "mulhwu",   .raw = 0x7D2A5816, .address = 0x80003C40, .gpr = { [10] = 0xFFFFFFFFu, [11] = 2 }, EXPECTS(e_mulhwu) },
    { .name = "divw",     .raw = 0x7D8D73D6, .address = 0x80003C80, .gpr = { [13] = 0xFFFFFFF9u, [14] = 2 }, EXPECTS(e_divw) },
    { .name = "divwu",    .raw = 0x7DF08B96, .address = 0x80003CC0, .gpr = { [16] = 7, [17] = 2 }, EXPECTS(e_divwu) },
    { .name = "addo",     .raw = 0x7D4B6614, .address = 0x80003D00, .gpr = { [11] = 0x7FFFFFFFu, [12] = 1 }, EXPECTS(e_addo) },
    { .name = "divwo.0",  .raw = 0x7ED7C7D6, .address = 0x80003D40, .gpr = { [23] = 0xFFFFFFFFu, [24] = 0 }, EXPECTS(e_divwo) },
    { .name = "addme",    .raw = 0x7C6401D4, .address = 0x80003D80, .gpr = { [4] = 5 }, EXPECTS(e_addme) },
    { .name = "subfme",   .raw = 0x7CA601D0, .address = 0x80003DC0, .gpr = { [6] = 5 }, EXPECTS(e_subfme) },

    /* ---- FP scalar ---- */
    { .name = "fsubs",    .raw = 0xEC853028, .address = 0x80003E00, .fpr = { [5] = D_70, [6] = D_15 }, EXPECTS(e_fsubs) },
    { .name = "fdivs",    .raw = 0xED4B6024, .address = 0x80003E40, .fpr = { [11] = D_70, [12] = D_20 }, EXPECTS(e_fdivs) },
    { .name = "fsub",     .raw = 0xFE119028, .address = 0x80003E80, .fpr = { [17] = D_70, [18] = D_15 }, EXPECTS(e_fsub) },
    { .name = "fmul",     .raw = 0xFE740572, .address = 0x80003EC0, .fpr = { [20] = D_30, [21] = D_20 }, EXPECTS(e_fmul) },
    { .name = "fdiv",     .raw = 0xFED7C024, .address = 0x80003F00, .fpr = { [23] = D_70, [24] = D_20 }, EXPECTS(e_fdiv) },
    { .name = "fneg",     .raw = 0xFF60E050, .address = 0x80003F40, .fpr = { [28] = D_25 }, EXPECTS(e_fneg) },
    { .name = "fabs",     .raw = 0xFFA0F210, .address = 0x80003F80, .fpr = { [30] = 0xC004000000000000ull }, EXPECTS(e_fabs) },
    { .name = "fnabs",    .raw = 0xFFE00110, .address = 0x80003FC0, .fpr = { [0] = D_25 }, EXPECTS(e_fnabs) },
    { .name = "fsel",     .raw = 0xFC2220EE, .address = 0x80004000, .fpr = { [2] = 0xBFF0000000000000ull, [3] = D_30, [4] = 0x4010000000000000ull }, EXPECTS(e_fsel) },
    { .name = "fmr",      .raw = 0xFF20D090, .address = 0x80004040, .fpr = { [26] = 0x8000000000000000ull }, EXPECTS(e_fmr) },
    { .name = "fctiw",    .raw = 0xFD20501C, .address = 0x80004080, .fpr = { [10] = D_25 }, EXPECTS(e_fctiw) },
    { .name = "fctiwz",   .raw = 0xFD60601E, .address = 0x800040C0, .fpr = { [12] = 0xC007333333333333ull }, EXPECTS(e_fctiwz) },

    /* ---- FMA ---- */
    { .name = "fmadds",   .raw = 0xEE32A4FA, .address = 0x80004100, .fp_single_inputs = true,
      .fpr = { [18] = 0x40000000u, [19] = 0x40400000u, [20] = 0x40800000u }, EXPECTS(e_fmaddsc) },
    { .name = "fmadds.tie",.raw = 0xEE32A4FA, .address = 0x80004140, .fp_single_inputs = true,
      .fpr = { [18] = 0x42480000u, [19] = 0xBC88CC38u, [20] = 0x1B1C72A0u }, EXPECTS(e_fmaddstie) },
    { .name = "fmsubs",   .raw = 0xEF3AE6F8, .address = 0x80004180, .fp_single_inputs = true,
      .fpr = { [26] = 0x40A00000u, [27] = 0x40400000u, [28] = 0x40800000u }, EXPECTS(e_fmsubs) },
    { .name = "fnmadds",  .raw = 0xEC2220FE, .address = 0x800041C0, .fp_single_inputs = true,
      .fpr = { [2] = 0x40000000u, [3] = 0x40400000u, [4] = 0x40800000u }, EXPECTS(e_fnmadds) },
    { .name = "fnmsubs",  .raw = 0xED2A62FC, .address = 0x80004200, .fp_single_inputs = true,
      .fpr = { [10] = 0x40A00000u, [11] = 0x40400000u, [12] = 0x40800000u }, EXPECTS(e_fnmsubs) },
    { .name = "fmadd",    .raw = 0xFDAE83FA, .address = 0x80004240, .fpr = { [14] = D_20, [15] = D_30, [16] = 0x4010000000000000ull }, EXPECTS(e_fmaddc) },
    { .name = "fmadd.can",.raw = 0xFDAE83FA, .address = 0x80004280, .fpr = { [14] = 0x3FF0000000000001ull, [15] = 0x3FEFFFFFFFFFFFFEull, [16] = 0xBFF0000000000000ull }, EXPECTS(e_fmaddcancel) },

    /* ---- estimates (Dolphin tables in cpu.c) ---- */
    { .name = "fres.0",   .raw = 0xEC201030, .address = 0x800042C0, .fpr = { [2] = 0 }, EXPECTS(e_fres0) },
    { .name = "frsqrte.-",.raw = 0xFC602034, .address = 0x80004300, .fpr = { [4] = 0xC010000000000000ull }, EXPECTS(e_frsqrteneg) },
    { .name = "ps_res",   .raw = 0x10A03030, .address = 0x80004340, .fp_single_inputs = true, .fpr = { [6] = 0x40400000u }, .ps1 = { [6] = 0x40800000u }, EXPECTS(e_psres) },
    { .name = "ps_rsqrte",.raw = 0x10E04034, .address = 0x80004380, .fp_single_inputs = true, .fpr = { [8] = 0x40400000u }, .ps1 = { [8] = 0x40800000u }, EXPECTS(e_psrsqrte) },

    /* ---- NEW findings (Dolphin-grounded ps1 divergences) ---- */
    { .name = "frsp.ps1", .raw = 0xFC201018, .address = 0x800043C0, .fpr = { [2] = D_15 }, .ps1 = { [1] = D_70 }, EXPECTS(e_frsp_ps1) },
    { .name = "fmadds.ps1",.raw = 0xEE32A4FA, .address = 0x80004400, .fp_single_inputs = true,
      .fpr = { [18] = 0x40000000u, [19] = 0x40400000u, [20] = 0x40800000u }, .ps1 = { [17] = 0x40A00000u }, EXPECTS(e_fmadds_ps1) },

    /* ---- Slice-2 broadening: memory/control/system ---- */
    { .name = "lwz.mem",   .raw = 0x80610000, .address = 0x80004440, .gpr = { [1] = 0x80010000u },
      .mem_base = 0x80010000u, .mem_len = 4, .mem_in = mem_lwz, EXPECTS(e_lwz_mem) },
    { .name = "lwzu.mem",  .raw = 0x84810004, .address = 0x80004480, .gpr = { [1] = 0x80010000u },
      .mem_base = 0x80010000u, .mem_len = 8, .mem_in = mem_lwzu, EXPECTS(e_lwzu_mem) },
    { .name = "lbz.mem",   .raw = 0x88A10008, .address = 0x800044C0, .gpr = { [1] = 0x80010000u },
      .mem_base = 0x80010000u, .mem_len = 9, .mem_in = mem_lbz, EXPECTS(e_lbz_mem) },
    { .name = "lha.mem",   .raw = 0xA921FFFC, .address = 0x80004500, .gpr = { [1] = 0x80010008u },
      .mem_base = 0x80010000u, .mem_len = 6, .mem_in = mem_lha, EXPECTS(e_lha_mem) },
    { .name = "stw.mem",   .raw = 0x9061001C, .address = 0x80004540, .gpr = { [1] = 0x80010000u, [3] = 0xCAFEBABEu },
      .mem_base = 0x80010000u, .mem_len = 1, .mem_in = mem_blank64, EXPECTS(e_stw_mem) },
    { .name = "stwu.mem",  .raw = 0x94810020, .address = 0x80004580, .gpr = { [1] = 0x80010000u, [4] = 0x0BADF00Du },
      .mem_base = 0x80010000u, .mem_len = 1, .mem_in = mem_blank64, EXPECTS(e_stwu_mem) },
    { .name = "lwbrx.mem", .raw = 0x7C642C2C, .address = 0x800045C0, .gpr = { [4] = 0x80010000u, [5] = 0 },
      .mem_base = 0x80010000u, .mem_len = 4, .mem_in = mem_word, EXPECTS(e_lwbrx_mem) },
    { .name = "sthbrx.mem",.raw = 0x7D8D772C, .address = 0x80004600, .gpr = { [12] = 0x0000ABCDu, [13] = 0x80010000u, [14] = 0x3Au },
      .mem_base = 0x80010000u, .mem_len = 1, .mem_in = mem_blank64, EXPECTS(e_sthbrx_mem) },
    { .name = "stfiwx.mem",.raw = 0x7EF8CFAE, .address = 0x80004640, .gpr = { [24] = 0x80010000u, [25] = 0x40u },
      .fpr = { [23] = 0xFFF80000DEADBEEFull }, .mem_base = 0x80010000u, .mem_len = 1, .mem_in = mem_blank64, EXPECTS(e_stfiwx_mem) },
    { .name = "psq_l.mem", .raw = 0xE0240000, .address = 0x80004680, .gpr = { [4] = 0x80010000u }, .hid2 = 0xA0000000u,
      .mem_base = 0x80010000u, .mem_len = 8, .mem_in = mem_ps_pair, EXPECTS(e_psq_l_mem) },
    { .name = "psq_st.mem",.raw = 0xF0A40010, .address = 0x800046C0, .gpr = { [4] = 0x80010040u }, .hid2 = 0xA0000000u,
      .fp_single_inputs = true, .fpr = { [5] = 0x40400000u }, .ps1 = { [5] = 0x40800000u },
      .mem_base = 0x80010000u, .mem_len = 1, .mem_in = mem_blank64, EXPECTS(e_psq_st_mem) },
    { .name = "lwarx.mem", .raw = 0x7E329828, .address = 0x80004700, .gpr = { [18] = 0x80010000u, [19] = 0 },
      .mem_base = 0x80010000u, .mem_len = 4, .mem_in = mem_lwarx, EXPECTS(e_lwarx_mem) },
    { .name = "mcrxr",     .raw = 0x7D000400, .address = 0x80004740, .cr = 0x12345678u, .xer = 0xE0000000u, EXPECTS(e_mcrxr_sys) },
    { .name = "mfmsr",     .raw = 0x7D2000A6, .address = 0x80004780, .msr = 0x12345678u, EXPECTS(e_mfmsr_sys) },
    { .name = "mtmsr",     .raw = 0x7D400124, .address = 0x800047C0, .gpr = { [10] = 0xA5A5A5A5u }, EXPECTS(e_mtmsr_sys) },
    { .name = "mfsr",      .raw = 0x7D6304A6, .address = 0x80004800, .sr = { [3] = 0xCAFEBABEu }, EXPECTS(e_mfsr_sys) },
    { .name = "mtsr",      .raw = 0x7DC401A4, .address = 0x80004840, .gpr = { [14] = 0x0BADF00Du }, EXPECTS(e_mtsr_sys) },
    { .name = "twi.true",  .raw = 0x0C85FFFE, .address = 0x80004880, .gpr = { [5] = 0xFFFFFFFEu }, EXPECTS(e_twi_true) },
    { .name = "twi.false", .raw = 0x0C85FFFE, .address = 0x800048C0, .gpr = { [5] = 7 }, EXPECTS(e_twi_false) },
    { .name = "sc",        .raw = 0x44000002, .address = 0x80004900, EXPECTS(e_sc_sys) },
    { .name = "sync",      .raw = 0x7C0004AC, .address = 0x80004940, .gpr = { [3] = 0xA5A5A5A5u }, EXPECTS(e_sync_sys) },
    { .name = "b.rel",     .raw = 0x480000C8, .address = 0x80004980, EXPECTS(e_b_abs) },

    /* ---- Propagation sequences: later instructions consume earlier results ---- */
    { .name = "seq.lfs",    .raw_words = seq_lfs_psqst, .raw_count = 2, .address = 0x80004A00,
      .gpr = { [4] = 0x80010000u, [5] = 0x80010040u }, .hid2 = 0xA0000000u,
      .ps1 = { [1] = 0x4040000000000000ull },
      .mem_base = 0x80010000u, .mem_len = 4, .mem_in = mem_one_f, EXPECTS(e_seq_lfs_psqst) },
    { .name = "seq.frsp",   .raw_words = seq_frsp_psqst, .raw_count = 2, .address = 0x80004A20,
      .gpr = { [5] = 0x80010040u }, .hid2 = 0xA0000000u,
      .fpr = { [2] = D_15 }, .ps1 = { [1] = D_70 },
      .mem_base = 0x80010000u, .mem_len = 1, .mem_in = mem_blank64, EXPECTS(e_seq_frsp_psqst) },
    { .name = "seq.fmadd",  .raw_words = seq_fmadds_psqst, .raw_count = 2, .address = 0x80004A40,
      .gpr = { [5] = 0x80010040u }, .hid2 = 0xA0000000u, .fp_single_inputs = true,
      .fpr = { [18] = 0x40000000u, [19] = 0x40400000u, [20] = 0x40800000u },
      .ps1 = { [17] = 0x40A00000u },
      .mem_base = 0x80010000u, .mem_len = 1, .mem_in = mem_blank64, EXPECTS(e_seq_fmadds_psqst) },
    { .name = "seq.carry",  .raw_words = seq_addic_adde, .raw_count = 2, .address = 0x80004A60,
      .gpr = { [4] = 1u, [5] = 0xFFFFFFFFu, [6] = 0u }, EXPECTS(e_seq_addic_adde) },
    { .name = "seq.crbr",   .raw_words = seq_cmp_beq, .raw_count = 4, .address = 0x80004B00,
      .gpr = { [3] = 0u, [7] = 0u, [8] = 0u }, EXPECTS(e_seq_cmp_beq) },
    { .name = "seq.update", .raw_words = seq_lwzu_stw, .raw_count = 2, .address = 0x80004B20,
      .gpr = { [1] = 0x80010000u },
      .mem_base = 0x80010000u, .mem_len = 8, .mem_in = mem_lwzu, EXPECTS(e_seq_lwzu_stw) },
    { .name = "seq.fstore", .raw_words = seq_fadds_stfs, .raw_count = 2, .address = 0x80004B40,
      .gpr = { [5] = 0x80010040u },
      .fpr = { [2] = 0x4160000000000000ull, [3] = 0x3FE0000010000000ull },
      .mem_base = 0x80010000u, .mem_len = 1, .mem_in = mem_blank64, EXPECTS(e_seq_fadds_stfs) },
    { .name = "seq.psstor", .raw_words = seq_psadd_psqst, .raw_count = 2, .address = 0x80004B60,
      .gpr = { [5] = 0x80010040u }, .hid2 = 0xA0000000u,
      .fpr = { [2] = 0x4160000000000000ull, [3] = 0x3FE0000010000000ull },
      .ps1 = { [2] = 0x4160000000000000ull, [3] = 0x3FE0000010000000ull },
      .mem_base = 0x80010000u, .mem_len = 1, .mem_in = mem_blank64, EXPECTS(e_seq_psadd_psqst) },
};

const unsigned probe_count = (unsigned)(sizeof(probes) / sizeof(probes[0]));
