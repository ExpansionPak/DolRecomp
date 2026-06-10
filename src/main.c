#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/types.h"
#include "frontend/dol.h"
#include "frontend/decoder.h"
#include "backend/emitter.h"
#include "runtime/runtime.h"

static void print_usage(const char* argv0) {
    fprintf(stderr, "usage: %s <input.dol> [output.c]\n", argv0);
    fprintf(stderr, "\n");
    fprintf(stderr, "loads a GameCube DOL and emits recompiled C code.\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* input_path  = argv[1];
    const char* output_path = argc > 2 ? argv[2] : NULL;

    printf("loading: %s\n", input_path);

    DOLFile dol;
    if (!dol_load(&dol, input_path))
        return 1;
    dol_print_info(&dol);

    FILE* out = stdout;
    if (output_path) {
        if (fopen_s(&out, output_path, "w") != 0) {
            fprintf(stderr, "error: can't open output '%s'\n", output_path);
            dol_free(&dol);
            return 1;
        }
        printf("\nwriting output to: %s\n", output_path);
    } else {
        printf("\n--- recompiled output ---\n\n");
    }

    emit_header(out);

    for (int s = 0; s < DOL_NUM_TEXT; s++) {
        if (dol.header.text_sizes[s] == 0) continue;

        const u8* section_data = dol_get_text_section(&dol, s);
        if (!section_data) continue;

        u32 base_addr  = dol.header.text_addresses[s];
        u32 section_sz = dol.header.text_sizes[s];
        u32 num_insts  = section_sz / 4;

        printf("decoding text[%d]: %u instructions at 0x%08X\n",
               s, num_insts, base_addr);

        PPCInst* insts = (PPCInst*)malloc(num_insts * sizeof(PPCInst));
        if (!insts) {
            fprintf(stderr, "error: out of memory\n");
            break;
        }

        u32 decoded = 0, unknown = 0;
        for (u32 i = 0; i < num_insts; i++) {
            u32 raw  = read_be32(section_data + i * 4);
            u32 addr = base_addr + i * 4;
            insts[i] = ppc_decode(raw, addr);
            decoded++;
            if (insts[i].op == PPC_OP_UNKNOWN) unknown++;
        }

        printf("  %u decoded, %u known, %u unknown\n",
               decoded, decoded - unknown, unknown);

        // TODO: function boundary detection
        emit_function(out, insts, num_insts, base_addr);

        free(insts);
    }

    emit_footer(out);

    if (output_path && out != stdout) {
        fclose(out);
        printf("done!\n");
    }

    dol_free(&dol);
    return 0;
}
