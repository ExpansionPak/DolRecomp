#include "backend/llvm/llvm_backend.h"
#include "ir/dolir_builder.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #x); return 1; } } while (0)

static u32 encode_spr(u16 spr) {
    return ((spr & 31u) << 5) | ((spr >> 5) & 31u);
}

static u32 mfspr(u8 destination, u16 spr) {
    return 0x7C0002A6u | (u32(destination) << 21) | (encode_spr(spr) << 11);
}

static u32 mtspr(u8 source, u16 spr) {
    return 0x7C0003A6u | (u32(source) << 21) | (encode_spr(spr) << 11);
}

static bool add_chunk(DolIRModule* module, const u32* words, u32 count,
                      u32 address) {
    PPCInst* instructions = new PPCInst[count];
    for (u32 i = 0; i < count; i++)
        instructions[i] = ppc_decode(words[i], address + i * 4u);
    const bool result = dolir_build_chunk(module, instructions, count, address);
    delete[] instructions;
    return result;
}

int main(int argc, char** argv) {
    CHECK(argc == 3);
    DolIRModule module;
    dolir_module_init(&module);

    const u32 main_words[] = {
        0x38600000u, 0x00000000u, 0x38800000u, 0x7C841A14u,
        0x90610000u, 0x38630001u, 0x2C03000Au, 0x4180FFF4u,
        0xEE32A4FAu, 0x4E800020u,
    };
    CHECK(add_chunk(&module, main_words, 10, 0x80001000u));

    const u32 spr_words[] = {
        mtspr(3, 273), mfspr(4, 273), 0x4E800020u,
    };
    CHECK(add_chunk(&module, spr_words, 3, 0x80002000u));

    const u32 segment_words[] = {
        0x7DC401A4u, 0x7D6304A6u, 0x7DE081E4u, 0x7D806D26u,
        0x4E800020u,
    };
    CHECK(add_chunk(&module, segment_words, 5, 0x80002100u));

    const u32 fpscr_words[] = {
        0xFFE0004Cu, 0xFDA0048Eu, 0x4E800020u,
    };
    CHECK(add_chunk(&module, fpscr_words, 3, 0x80002200u));

    const u32 lswx_words[] = {
        0x7D34AC2Au, 0x4E800020u,
    };
    CHECK(add_chunk(&module, lswx_words, 2, 0x80002300u));

    const u32 cache_words[] = {
        0x7C11906Cu, 0x4E800020u,
    };
    CHECK(add_chunk(&module, cache_words, 2, 0x80002400u));

    const u32 trap_words[] = {
        0x0C85FFFEu, 0x38630001u, 0x4E800020u,
    };
    CHECK(add_chunk(&module, trap_words, 3, 0x80002500u));

    const u32 sc_words[] = {0x44000002u};
    CHECK(add_chunk(&module, sc_words, 1, 0x80002600u));

    const u32 rfi_words[] = {0x4C000064u};
    CHECK(add_chunk(&module, rfi_words, 1, 0x80002700u));

    const u32 dcbz_l_words[] = {
        0x100537ECu, 0x4E800020u,
    };
    CHECK(add_chunk(&module, dcbz_l_words, 2, 0x80002800u));

    const u32 ecowx_words[] = {
        0x7D6C6B6Cu, 0x4E800020u,
    };
    CHECK(add_chunk(&module, ecowx_words, 2, 0x80002900u));

    const u32 float_words[] = {
        0xEC22182Au, 0xEC853028u, 0xECE80272u, 0xED4B6024u,
        0xFDAE782Au, 0xFE119028u, 0xFE740572u, 0xFED7C024u,
        0x4E800020u,
    };
    CHECK(add_chunk(&module, float_words, 9, 0x80002A00u));

    const u32 paired_words[] = {
        0x1022182Au, 0x10E80272u, 0x11AE83FAu, 0x10A03030u,
        0x110D7000u, 0x10853460u, 0x4E800020u,
    };
    CHECK(add_chunk(&module, paired_words, 7, 0x80002B00u));

    // Runtime boundaries must not reset the dispatcher budget.
    const u32 budget_words[] = {
        0x38630001u, 0x00000000u, 0x2C032710u, 0x4180FFF4u, 0x4E800020u,
    };
    CHECK(add_chunk(&module, budget_words, 5, 0x80002C00u));

    // External tail branches share the same budget across chunks.
    const u32 cross_chunk_a[] = {0x48000100u};
    const u32 cross_chunk_b[] = {0x4BFFFF00u};
    CHECK(add_chunk(&module, cross_chunk_a, 1, 0x80002D00u));
    CHECK(add_chunk(&module, cross_chunk_b, 1, 0x80002E00u));

    CHECK(dolir_verify(&module, stderr));
    DolLLVMOptions options{};
    options.optimization_level = 2;
    options.verify = 1;
    options.emit_ir = 1;
    options.ir_path = argv[2];
    const DolLLVMFunctionRange ranges[] = {
        {0x80002D00u, 0x80002D04u},
        {0x80002E00u, 0x80002E04u},
    };
    options.function_ranges = ranges;
    options.function_range_count = 2;
    CHECK(dolllvm_emit_object(&module, argv[1], &options, stderr));
    FILE* object = std::fopen(argv[1], "rb");
    CHECK(object != nullptr);
    unsigned char magic[4]{};
    CHECK(std::fread(magic, 1, sizeof(magic), object) == sizeof(magic));
    std::fclose(object);
    // The object format follows the default target triple, so this cannot assume
    // ELF: a Windows host emits COFF, whose x86-64 objects begin with the machine
    // type IMAGE_FILE_MACHINE_AMD64 (0x8664) stored little-endian.
#if defined(_WIN32)
    CHECK(magic[0] == 0x64 && magic[1] == 0x86);
#else
    CHECK(magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F');
#endif
    std::ifstream ir(argv[2]);
    const std::string irText((std::istreambuf_iterator<char>(ir)),
                             std::istreambuf_iterator<char>());
    CHECK(irText.find("define hidden void @func_80001000") !=
          std::string::npos);

    // The same module emitted for AArch64, which is a cross-compile on every
    // host this suite currently runs on. Accepting the triple is not enough on
    // its own: if the AArch64 target is not registered, lookupTarget cannot see
    // it and reports it as an unsupported triple, so emission fails here rather
    // than producing a wrong object.
    const std::string aarch64Path = std::string(argv[1]) + ".aarch64.o";
    DolLLVMOptions aarch64Options = options;
    aarch64Options.emit_ir = 0;
    aarch64Options.ir_path = nullptr;
    aarch64Options.target_triple = "aarch64-unknown-linux-gnu";
    CHECK(dolllvm_emit_object(&module, aarch64Path.c_str(), &aarch64Options,
                              stderr));
    FILE* aarch64Object = std::fopen(aarch64Path.c_str(), "rb");
    CHECK(aarch64Object != nullptr);
    unsigned char header[20]{};
    const bool headerRead =
        std::fread(header, 1, sizeof(header), aarch64Object) == sizeof(header);
    std::fclose(aarch64Object);
    CHECK(headerRead);
    // An explicit triple selects the object format too, so this is ELF even on
    // a Windows host. e_machine sits at offset 18 and is EM_AARCH64 (183).
    CHECK(header[0] == 0x7f && header[1] == 'E' && header[2] == 'L' &&
          header[3] == 'F');
    CHECK(header[4] == 2);   // ELFCLASS64
    CHECK(header[5] == 1);   // ELFDATA2LSB
    CHECK(header[18] == 0xB7 && header[19] == 0x00);
    dolir_module_free(&module);
    return 0;
}
