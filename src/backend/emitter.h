#ifndef DOLRECOMP_EMITTER_H
#define DOLRECOMP_EMITTER_H

#include "../core/types.h"
#include "../frontend/decoder.h"
#include <stdio.h>

// well it's kinda in the name.
// similar approach to N64Recomp's emitter

// emit the boilerplate header (includes, typedefs, etc)
void emit_header(FILE* out);

// emit a single recompiled function as C code
void emit_function(FILE* out, const PPCInst* insts, u32 count, u32 func_addr);

// emit a single instruction as C code
void emit_instruction(FILE* out, const PPCInst* inst);

// emit the boilerplate footer
void emit_footer(FILE* out);

#endif /* DOLRECOMP_EMITTER_H */
