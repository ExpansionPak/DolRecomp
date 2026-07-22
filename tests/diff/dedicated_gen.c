#include <stdio.h>
#include <stdlib.h>

#include "backend/emitter.h"
#include "dedicated_cases.h"
#include "frontend/decoder.h"

int main(int argc, char** argv) {
    FILE* out = argc > 1 ? fopen(argv[1], "w") : stdout;
    if (!out) {
        perror("open generated C");
        return 2;
    }
    emit_header(out);
    for (unsigned i = 0; i < dedicated_case_count; i++) {
        const DedicatedCase* c = &dedicated_cases[i];
        PPCInst inst = ppc_decode(c->raw, c->address);
        if (inst.op == PPC_OP_UNKNOWN) {
            fprintf(stderr, "%s decoded as unknown\n", c->name);
            return 2;
        }
        emit_function(out, &inst, 1, c->address);
    }
    fprintf(out, "void (*const dedicated_funcs[])(CPUState*) = {\n");
    for (unsigned i = 0; i < dedicated_case_count; i++)
        fprintf(out, "    func_%08X,\n", dedicated_cases[i].address);
    fprintf(out, "};\n");
    emit_footer(out);
    if (out != stdout)
        fclose(out);
    return 0;
}
