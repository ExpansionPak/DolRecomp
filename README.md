# DolRecomp

DolRecomp is is a tool to statically recompile GameCube DOL executables into C code that can be compiled for any platform. It functions similarly to [N64Recomp](https://github.com/N64Recomp/N64Recomp) (Which targets N64 binaries), and is a modern successor to [GCRecompiler](https://github.com/ExpansionPak/GCRecompiler). It is also a rewrite of "DolRecomp", Which was the original successor to GCRecompiler was hardcoded for [**Paper Mario: The Thousand-Year Door**](https://en.wikipedia.org/wiki/Paper_Mario:_The_Thousand-Year_Door) only. This newer rewrite aims to work with any GameCube DOL executable.

## structure

```
src/
  frontend/     - DOL/REL loading, PPC disassembly, control flow analysis
  backend/      - code generation, target arch emission (x86_64, arm64, etc)
  core/         - shared types and the minimal CPU support ABI used by generated code/tests

tools/          - standalone utilities
docs/           - notes, specs, rambling design docs
scripts/        - build helpers, CI stuff, whatever
include/        - public headers if we end up needing them
```

## Opcodes

The current CPU opcode set has 236 implemented opcodes.

| Area | Opcodes |
|------|---------|
| Immediate arithmetic | addi, addic, addic., addis, mulli, subfic |
| Register arithmetic | add, addo, addc, addco, adde, addeo, addme, addmeo, addze, addzeo, divw, divwo, divwu, divwuo, mulhw, mulhwu, mullw, mullwo, neg, nego, subf, subfo, subfc, subfco, subfe, subfeo, subfme, subfmeo, subfze, subfzeo |
| Compare / branch / CR | b[l][a], bc[l][a], bclr/blr, bcctr/bctr, cmp/cmpw, cmpi/cmpwi, cmpl/cmplw, cmpli/cmplwi, crand, crandc, creqv, crnand, crnor, cror, crorc, crxor, mcrf, mcrxr, mfcr, mtcrf, rfi, sc, tw, twi |
| Logical / rotate / shift | and, andc, andi., andis., cntlzw, eqv, extsb, extsh, nand, nor, or, orc, ori, oris, rlwimi, rlwinm, rlwnm, slw, sraw, srawi, srw, xor, xori, xoris |
| Loads | lbz, lbzu, lbzx, lbzux, lfd, lfdu, lfdux, lfdx, lfs, lfsu, lfsux, lfsx, lha, lhau, lhax, lhaux, lhbrx, lhz, lhzu, lhzx, lhzux, lmw, lswi, lswx, lwarx, lwbrx, lwz, lwzu, lwzx, lwzux |
| Stores | stb, stbu, stbux, stbx, stfd, stfdu, stfdux, stfdx, stfiwx, stfs, stfsu, stfsux, stfsx, sth, sthbrx, sthu, sthux, sthx, stmw, stswi, stswx, stw, stwbrx, stwcx., stwu, stwux, stwx |
| Floating point | fabs, fadd, fadds, fcmpo, fcmpu, fctiw, fctiwz, fdiv, fdivs, fmadd, fmadds, fmr, fmsub, fmsubs, fmul, fmuls, fneg, fnabs, fnmadd, fnmadds, fnmsub, fnmsubs, fres, frsp, frsqrte, fsel, fsub, fsubs |
| FPSCR control | mcrfs, mffs, mtfsb0, mtfsb1, mtfsf, mtfsfi |
| Paired-single memory | psq_l, psq_lu, psq_lux, psq_lx, psq_st, psq_stu, psq_stux, psq_stx |
| Paired-single arithmetic | ps_abs, ps_add, ps_cmpo0, ps_cmpo1, ps_cmpu0, ps_cmpu1, ps_div, ps_madd, ps_madds0, ps_madds1, ps_merge00, ps_merge01, ps_merge10, ps_merge11, ps_mr, ps_msub, ps_mul, ps_muls0, ps_muls1, ps_nabs, ps_neg, ps_nmadd, ps_nmsub, ps_res, ps_rsqrte, ps_sel, ps_sub, ps_sum0, ps_sum1 |
| Cache / memory control | dcbf, dcbi, dcbst, dcbt, dcbtst, dcbz, dcbz_l, eieio, icbi, isync, sync, tlbie, tlbsync |
| SPR / system moves | eciwx, ecowx, mfmsr, mfspr/mflr/mfctr/mfxer, mfsr, mfsrin, mftb/mftbu, mtmsr, mtspr/mtlr/mtctr/mtxer, mtsr, mtsrin |

## building

DolRecomp is pure C with no external submodules, so if you have VSCode you can simply build the project by pressing `Ctrl + Shift + P` (`Command + Shift + P` on macOS) and selecting `CMake: Build`.

On windows. If you prefer using the command line, you can use the following commands in this exact order

```
mkdir build
cd build
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build
```

## usage

Download the local GameTDB/WiiTDB title list first:

```
dolrecomp.exe --setup
```

Wii DOLs need a 6-character title ID so DolRecomp can look up the game name through `database\titles.txt`:

```
dolrecomp.exe -j14 path\to\main.dol SUKE01 build
```

GameCube DOLs skip the title ID and write to a generic generated folder:

```
dolrecomp.exe --gamecube path\to\main.dol build
```

If the last argument is a directory, output goes under `<title-id>_generated\` for Wii or `generated\` for GameCube. If the last argument ends in `.c`, that exact C file is used. `-jN` controls how many worker jobs write split C chunks.

If `database\titles.txt` is missing, DolRecomp warns and uses GameCube mode.

## contributing

todo
