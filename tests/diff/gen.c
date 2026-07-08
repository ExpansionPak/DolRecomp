/*
 * Probe generator for the emitted-code acceptance differential.
 *
 * For every probe in probes.inc, decode its raw word(s) and emit one func_<addr>
 * through the REAL DolRecomp emitter (emitter.c). Also emit a probe_funcs[]
 * pointer array so the runner can invoke each generated function by index.
 *
 * Output is a self-contained C file (it includes "cpu/cpu.h" via the emitter
 * header and links against cpu/cpu.c at build time).
 *
 * usage: gen <output.c>
 */

#include <stdio.h>
#include <stdlib.h>

#include "common/types.h"
#include "frontend/decoder.h"
#include "backend/emitter.h"
#include "probe.h"
#include "probes.inc"

int main(int argc, char** argv) {
    FILE* out = (argc > 1) ? fopen(argv[1], "w") : stdout;
    if (!out) {
        perror(argc > 1 ? argv[1] : "stdout");
        return 1;
    }

    emit_header(out);

    for (unsigned i = 0; i < probe_count; i++) {
        const Probe* p = &probes[i];
        unsigned count = p->raw_words ? p->raw_count : 1u;
        if (count == 0 || count > 64) {
            fprintf(stderr, "probe '%s': invalid instruction count %u\n",
                    p->name, count);
            if (out != stdout) fclose(out);
            return 1;
        }

        PPCInst insts[64];
        for (unsigned j = 0; j < count; j++) {
            u32 raw = p->raw_words ? p->raw_words[j] : p->raw;
            u32 addr = p->address + j * 4u;
            insts[j] = ppc_decode(raw, addr);
            if (insts[j].op == PPC_OP_UNKNOWN) {
                fprintf(stderr, "probe '%s'[%u]: raw 0x%08X decoded as unknown\n",
                        p->name, j, raw);
                if (out != stdout) fclose(out);
                return 1;
            }
        }

        emit_function(out, insts, count, p->address);
    }

    fprintf(out, "/* index -> generated function, in probe-table order */\n");
    fprintf(out, "void (*const probe_funcs[])(CPUState*) = {\n");
    for (unsigned i = 0; i < probe_count; i++)
        fprintf(out, "    func_%08X,\n", probes[i].address);
    fprintf(out, "};\n");

    emit_footer(out);

    if (out != stdout) fclose(out);
    return 0;
}
