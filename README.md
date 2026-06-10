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

| Opcode | Implemented |
|--------|-------------|
| addi (Add Immediate) | Yes |
| addic (Add Immediate Carrying) | Yes |
| addis (Add Immediate Shifted) | Yes |
| cmpi / cmpwi (Compare Immediate) | Yes |
| cmpli / cmplwi (Compare Logical Immediate) | Yes |
| ori (OR Immediate) | Yes |
| oris (OR Immediate Shifted) | Yes |
| xori (XOR Immediate) | Yes |
| xoris (XOR Immediate Shifted) | Yes |
| andi. (AND Immediate) | Yes |
| andis. (AND Immediate Shifted) | Yes |
| lwz (Load Word and Zero) | Yes |
| lwzu (Load Word and Zero with Update) | Yes |
| lbz (Load Byte and Zero) | Yes |
| lbzu (Load Byte and Zero with Update) | Yes |
| stw (Store Word) | Yes |
| stwu (Store Word with Update) | Yes |
| stb (Store Byte) | Yes |
| stbu (Store Byte with Update) | Yes |
| lhz (Load Halfword and Zero) | Yes |
| lhzu (Load Halfword and Zero with Update) | Yes |
| lha (Load Halfword Algebraic) | Yes |
| sth (Store Halfword) | Yes |
| sthu (Store Halfword with Update) | Yes |
| b[l][a] (Branch) | Yes |
| bc[l][a] (Branch Conditional) | Yes |
| bclr / blr (Branch Conditional to Link Register) | Yes |
| bcctr / bctr (Branch Conditional to Count Register) | Yes |
| mfspr / mflr / mfctr (Move From SPR) | Yes |
| mtspr / mtlr / mtctr (Move To SPR) | Yes |

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
