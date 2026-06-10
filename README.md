# DolRecomp

DolRecomp is is a tool to statically recompile GameCube DOL executables into C code that can be compiled for any platform. It functions similarly to [N64Recomp](https://github.com/N64Recomp/N64Recomp) (Which targets N64 binaries), and is a modern successor to [GCRecompiler](https://github.com/ExpansionPak/GCRecompiler). It is also a rewrite of "DolRecomp", Which was the original successor to GCRecompiler was hardcoded for [**Paper Mario: The Thousand-Year Door**](https://en.wikipedia.org/wiki/Paper_Mario:_The_Thousand-Year_Door) only. This newer rewrite aims to work with any GameCube DOL executable.

## structure

```
src/
  frontend/     - DOL/REL loading, PPC disassembly, control flow analysis
  backend/      - code generation, target arch emission (x86_64, arm64, etc)
  runtime/      - the glue that makes recompiled code actually work (HW regs, memory, OS)
  core/         - shared types, config, logging, the stuff everything else depends on

tools/          - standalone utilities
docs/           - notes, specs, rambling design docs
scripts/        - build helpers, CI stuff, whatever
include/        - public headers if we end up needing them
```

## Opcodes

Here are the currently supported opcodes

| Opcode    | Implemented |
|-----------|--------|
| addi (Add Immediate)   | Yes  |
| addic (Add Immediate Carrying)    |  Yes   |
| addis (Add Immediate Shifted)   | Yes |
| cmpi (Compare Immediate)   | Yes |
| ori (OR Immediate)  | Yes |
| oris (OR Immediate Shifted)  | No |
| lwz (Load Word and Zero)  | Yes |
| stw (Store)  | Yes |
| b[l][a] (Branch)  | Yes |
| bc[l][a] (Branch Conditional)  | Yes |

## building

DolRecomp is pure C++ with no external submodules, so if you have VSCode you can simply build the project by pressing `Ctrl + Shift + P` (`Command + Shift + P` on macOS) and selecting `CMake: Build`.

On windows. If you prefer using the command line, you can use the following commands in this exact order

```
mkdir build
cd build
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build
```

## contributing

no