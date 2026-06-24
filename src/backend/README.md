# backend

The backend emits portable C for decoded PowerPC instructions.

Generated code is split into a manifest, a header, and chunk files so large
games can be compiled without producing one huge C file. The dispatcher handles
compiled block lookup and can call host patches before falling back to generated
code.

Host compilers do the final
machine-code generation from the emitted C
