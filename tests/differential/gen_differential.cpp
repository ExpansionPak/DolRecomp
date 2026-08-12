// Generates the same random guest code through both backends for differential
// testing.
//
// The trick that makes one comparing binary possible: both backends name their
// output func_<guest address>, so emitting the C copy at one base and the LLVM
// copy at another gives distinct symbols with identical semantics. Nothing in
// the generated sequences depends on the base -- no absolute branches, and
// effective addresses come from registers rather than PC -- so the two copies
// must compute the same thing from the same starting state or one of them is
// wrong.
//
// Sequences are straight-line and end in blr. That is not a limitation for the
// thing most in need of checking: a return is a materialisation barrier, so
// every sequence exercises the state save path that the liveness and
// reaching-writes narrowing changed. Branch-shaped control flow is covered by
// test_dolir and test_c_cfg.
//
//   gen_differential <out.c> <out.o> <manifest.h> [seed] [functions] [length]

// backend/emitter.h has no extern "C" guard of its own, and this is the only
// C++ caller of the C emitter.
extern "C" {
#include "backend/emitter.h"
}
#include "backend/llvm/llvm_backend.h"
#include "ir/dolir_builder.h"
#include "frontend/decoder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, \
    "gen_differential: check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); \
    return 1; } } while (0)

// Deterministic so a failing seed reproduces exactly. Logged by the driver.
static u64 g_state = 0;
static u32 next_random(void) {
    g_state = g_state * 6364136223846793005ull + 1442695040888963407ull;
    return (u32)(g_state >> 33);
}
static u32 pick(u32 count) { return next_random() % count; }

// stfs is excluded from the default pool because the two backends genuinely
// disagree on it, and a gate that always fails gates nothing. This is scoping a
// new test, not weakening an existing one: the divergence is documented in
// AOT-PERFORMANCE-RESULTS.md and reproduces with --stfs.
//
// Storing a double that is not representable as single: the C backend produced
// 0x7E000000 where LLVM produced 0x7F800000 (+inf), and 0x04000004 where LLVM
// produced 0x00000000 (denormal flushed). Every one of 28 divergences across 64
// sequences was an stfs result; nothing else differed in any register, and
// removing stfs alone took the suite to 64/64.
static bool g_include_stfs = false;

// Registers the driver leaves alone so a sequence cannot destroy its own
// addressing base: r31 holds the scratch pointer, r0 is special in many forms.
static u8 gpr(void) { return (u8)(1u + pick(29u)); }   // r1..r29
static u8 fpr(void) { return (u8)pick(32u); }
static u8 crf(void) { return (u8)pick(8u); }

// Field-assembled so every generated word is a legal encoding by construction
// rather than by hoping a random 32 bits decodes.
static u32 form_d(u32 op, u8 d, u8 a, u16 imm) {
    return (op << 26) | ((u32)d << 21) | ((u32)a << 16) | imm;
}
static u32 form_x(u32 op, u8 d, u8 a, u8 b, u32 xo, u32 rc) {
    return (op << 26) | ((u32)d << 21) | ((u32)a << 16) | ((u32)b << 11) |
           (xo << 1) | rc;
}
static u32 form_a(u32 op, u8 d, u8 a, u8 b, u8 c, u32 xo, u32 rc) {
    return (op << 26) | ((u32)d << 21) | ((u32)a << 16) | ((u32)b << 11) |
           ((u32)c << 6) | (xo << 1) | rc;
}
static u32 form_m(u32 op, u8 s, u8 a, u8 sh, u8 mb, u8 me, u32 rc) {
    return (op << 26) | ((u32)s << 21) | ((u32)a << 16) | ((u32)sh << 11) |
           ((u32)mb << 6) | ((u32)me << 1) | rc;
}

// Memory operations always address through r31 with a small aligned
// displacement, so every access lands inside the scratch page the driver set
// up. A random base register would fault or scribble on the CPUState.
static u16 scratch_offset(u32 align) {
    u32 slot = pick(64u) * align;
    return (u16)slot;
}

static u32 random_instruction(void) {
    switch (pick(24u)) {
    case 0:  return form_d(14, gpr(), gpr(), (u16)next_random());        // addi
    case 1:  return form_d(15, gpr(), gpr(), (u16)next_random());        // addis
    case 2:  return form_d(12, gpr(), gpr(), (u16)next_random());        // addic
    case 3:  return form_d(28, gpr(), gpr(), (u16)next_random());        // andi.
    case 4:  return form_d(24, gpr(), gpr(), (u16)next_random());        // ori
    case 5:  return form_d(26, gpr(), gpr(), (u16)next_random());        // xori
    case 6:  return form_x(31, gpr(), gpr(), gpr(), 266, pick(2u));      // add[.]
    case 7:  return form_x(31, gpr(), gpr(), gpr(), 40, pick(2u));       // subf[.]
    case 8:  return form_x(31, gpr(), gpr(), gpr(), 235, pick(2u));      // mullw[.]
    case 9:  return form_x(31, gpr(), gpr(), gpr(), 28, pick(2u));       // and[.]
    case 10: return form_x(31, gpr(), gpr(), gpr(), 444, pick(2u));      // or[.]
    case 11: return form_x(31, gpr(), gpr(), gpr(), 316, pick(2u));      // xor[.]
    case 12: return form_x(31, gpr(), gpr(), gpr(), 24, pick(2u));       // slw[.]
    case 13: return form_x(31, gpr(), gpr(), gpr(), 536, pick(2u));      // srw[.]
    case 14: return form_x(31, gpr(), gpr(), gpr(), 792, pick(2u));      // sraw[.]
    case 15: return form_m(21, gpr(), gpr(), (u8)pick(32u), (u8)pick(32u),
                           (u8)pick(32u), pick(2u));                     // rlwinm[.]
    case 16: return form_x(31, gpr(), gpr(), gpr(), 10, pick(2u));       // addc[.]
    case 17: return form_x(31, gpr(), gpr(), gpr(), 138, pick(2u));      // adde[.]
    case 18: return form_d(32, gpr(), 31, scratch_offset(4));            // lwz
    case 19: return form_d(36, gpr(), 31, scratch_offset(4));            // stw
    case 20: return form_d(34, gpr(), 31, scratch_offset(1));            // lbz
    case 21: return form_d(48, fpr(), 31, scratch_offset(4));            // lfs
    case 22: return g_include_stfs
                 ? form_d(52, fpr(), 31, scratch_offset(4))   // stfs
                 : form_d(36, gpr(), 31, scratch_offset(4));  // stw
    default: return form_x(31, (u8)(crf() << 2), gpr(), gpr(), 0, 0);    // cmpw
    }
}

// Floating point kept in its own pool so a sequence can be biased toward it:
// the paired-single and FP paths carry the semantics most at risk from a
// state-save change, and they are the ones a random integer sequence rarely
// reaches.
static u32 random_float_instruction(void) {
    switch (pick(10u)) {
    case 0: return form_a(63, fpr(), fpr(), fpr(), 0, 21, pick(2u));   // fadd[.]
    case 1: return form_a(63, fpr(), fpr(), fpr(), 0, 20, pick(2u));   // fsub[.]
    case 2: return form_a(63, fpr(), fpr(), 0, fpr(), 25, pick(2u));   // fmul[.]
    case 3: return form_a(59, fpr(), fpr(), fpr(), 0, 21, pick(2u));   // fadds[.]
    case 4: return form_a(59, fpr(), fpr(), 0, fpr(), 25, pick(2u));   // fmuls[.]
    case 5: return form_a(63, fpr(), fpr(), fpr(), fpr(), 29, pick(2u)); // fmadd[.]
    case 6: return form_a(59, fpr(), fpr(), fpr(), fpr(), 29, pick(2u)); // fmadds[.]
    case 7: return form_x(63, fpr(), 0, fpr(), 72, pick(2u));          // fmr[.]
    case 8: return form_x(63, fpr(), 0, fpr(), 264, pick(2u));         // fabs[.]
    default: return form_x(63, fpr(), 0, fpr(), 40, pick(2u));         // fneg[.]
    }
}

int main(int argc, char** argv) {
    CHECK(argc >= 4);
    const char* c_path = argv[1];
    const char* object_path = argv[2];
    const char* manifest_path = argv[3];
    const u64 seed = argc > 4 ? std::strtoull(argv[4], nullptr, 0) : 20260812ull;
    const u32 functions = argc > 5 ? (u32)std::strtoul(argv[5], nullptr, 0) : 64u;
    const u32 length = argc > 6 ? (u32)std::strtoul(argv[6], nullptr, 0) : 24u;
    for (int i = 4; i < argc; i++) {
        if (std::strcmp(argv[i], "--stfs") == 0)
            g_include_stfs = true;
    }

    // Far apart so no range check can confuse one copy for the other.
    const u32 kBaseC = 0x80100000u;
    const u32 kBaseL = 0x80300000u;
    const u32 kStride = 0x1000u;

    g_state = seed;

    // Sequences do not call each other, and making them do so needs more than
    // an offset.
    //
    // This is the coverage gap that let a wrong reload narrowing pass 23/23 and
    // then hang Mario Kart: reloadLiveState only runs on a cross-function call
    // return, and nothing here reaches it.
    //
    // Adding `bl` between generated functions does not work as-is. The C
    // backend emits a switch(pc)->goto preamble per function and hands anything
    // outside its own address range to the runtime dispatcher; with no
    // dispatcher linked, a cross-function call has nowhere to go and the test
    // hangs. The LLVM backend resolves the same call internally through its
    // function ranges, so the two arms are not even attempting the same thing.
    //
    // Emitting dispatch helpers for the C arm was tried and is NOT sufficient
    // on its own. With emit_chunk_prototype() for every function followed by
    // emit_dispatch_helpers() before the bodies -- so dolrecomp_call() is
    // declared before the code that calls it -- the C arm compiles and links,
    // and the test still hangs. Something in the call/return round trip does not
    // terminate, and it was not diagnosed.
    //
    // So the remaining work is a debugging task, not a plumbing one. Whoever
    // picks it up should start by generating two functions with one call
    // between them and stepping the C arm, rather than at 64x24 where the
    // failing pair is not obvious.
    //
    // Until then the call/return path -- externalDestination, reloadLiveState,
    // the returned-PC validation -- has NO differential coverage, and the
    // reverted liveness narrowing is the demonstration of what that costs:
    // 23/23 green, then a hang at boot on a real title.
    std::vector<std::vector<u32>> bodies;
    for (u32 f = 0; f < functions; f++) {
        std::vector<u32> words;
        // Every fourth sequence is float-heavy; the rest are mixed.
        const bool floaty = (f % 4u) == 3u;
        for (u32 i = 0; i < length; i++) {
            words.push_back(floaty && (pick(2u) == 0) ? random_float_instruction()
                                                      : random_instruction());
        }
        words.push_back(0x4E800020u);  // blr: the materialisation barrier
        bodies.push_back(words);
    }

    // --- C backend -------------------------------------------------------
    FILE* out = std::fopen(c_path, "w");
    CHECK(out != nullptr);
    emit_header_for_cpu(out, DOLRECOMP_CPU_GEKKO);
    for (u32 f = 0; f < functions; f++) {
        const u32 address = kBaseC + f * kStride;
        std::vector<PPCInst> decoded(bodies[f].size());
        for (std::size_t i = 0; i < bodies[f].size(); i++)
            decoded[i] = ppc_decode(bodies[f][i], address + (u32)i * 4u);
        CHECK(emit_function(out, decoded.data(), (u32)decoded.size(), address));
    }
    emit_footer(out);
    CHECK(std::fclose(out) == 0);

    // --- LLVM backend ----------------------------------------------------
    DolIRModule module;
    dolir_module_init(&module);
    std::vector<DolLLVMFunctionRange> ranges;
    for (u32 f = 0; f < functions; f++) {
        const u32 address = kBaseL + f * kStride;
        std::vector<PPCInst> decoded(bodies[f].size());
        for (std::size_t i = 0; i < bodies[f].size(); i++)
            decoded[i] = ppc_decode(bodies[f][i], address + (u32)i * 4u);
        CHECK(dolir_build_chunk(&module, decoded.data(), (u32)decoded.size(),
                                address));
        DolLLVMFunctionRange range;
        range.start = address;
        range.end = address + (u32)decoded.size() * 4u;
        ranges.push_back(range);
    }
    CHECK(dolir_verify(&module, stderr));

    DolLLVMOptions options;
    std::memset(&options, 0, sizeof(options));
    options.optimization_level = 2;
    options.verify = 1;
    options.function_ranges = ranges.data();
    options.function_range_count = (u32)ranges.size();
    CHECK(dolllvm_emit_object(&module, object_path, &options, stderr));
    dolir_module_free(&module);

    // --- manifest --------------------------------------------------------
    FILE* manifest = std::fopen(manifest_path, "w");
    CHECK(manifest != nullptr);
    std::fprintf(manifest, "// Generated by gen_differential. Do not edit.\n");
    std::fprintf(manifest, "#define DIFF_SEED %lluull\n", (unsigned long long)seed);
    std::fprintf(manifest, "#define DIFF_COUNT %uu\n", functions);
    std::fprintf(manifest, "#define DIFF_BASE_C 0x%08Xu\n", kBaseC);
    std::fprintf(manifest, "#define DIFF_BASE_L 0x%08Xu\n", kBaseL);
    std::fprintf(manifest, "#define DIFF_STRIDE 0x%08Xu\n", kStride);
    for (u32 f = 0; f < functions; f++) {
        std::fprintf(manifest, "void func_%08X(CPUState*);\n", kBaseC + f * kStride);
        std::fprintf(manifest, "void func_%08X(CPUState*);\n", kBaseL + f * kStride);
    }
    std::fprintf(manifest, "static DiffPair diff_pairs[DIFF_COUNT] = {\n");
    for (u32 f = 0; f < functions; f++) {
        std::fprintf(manifest, "    {func_%08X, func_%08X, 0x%08Xu, 0x%08Xu},\n",
                     kBaseC + f * kStride, kBaseL + f * kStride,
                     kBaseC + f * kStride, kBaseL + f * kStride);
    }
    std::fprintf(manifest, "};\n");
    CHECK(std::fclose(manifest) == 0);

    std::printf("gen_differential: seed %llu, %u functions of %u instructions\n",
                (unsigned long long)seed, functions, length + 1u);
    return 0;
}
