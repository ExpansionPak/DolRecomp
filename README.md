# DolRecomp

DolRecomp is a static recompiler for GameCube, Wii, and experimental Wii U CPU code. It reads executable code sections, decodes PowerPC instructions, and emits split C that can be compiled and tested on a PC.

The current project is CPU only. You will need to supply your own runtime.

## Status

- GameCube/Wii DOL loading works.
- Wii U RPX loading works for executable sections. Compressed RPX sections need zlib.
- The decoder currently recognizes 236 PowerPC/Gekko/Broadway/Espresso opcodes.
- The backend emits C in split chunks with `-jN` worker support.
- Generated dispatch can hand known function addresses to host patches before entering compiled code.
- The generated C is a compile target and CPU behavior test surface, not a full game runtime yet.

## Build

Requirements:

- CMake 3.16 or newer
- A C11 compiler
- zlib, optional but required for compressed RPX sections (Wii U)

From the repo root:

```sh
cmake -S . -B build
cmake --build build --config Release -j 14
ctest --test-dir build -C Release --output-on-failure
```

On Windows, Clang, GCC/MinGW, and MSVC-style generators should all work. If devkitPro is installed, CMake also checks its MSYS2 zlib location.

devkitPro is highly recommended for Wii U recomps.

## Usage

Set up the local title database if you want Wii title names in CLI output. Setup also offers to download Wiimms ISO Tools if `wit` is missing:

```sh
dolrecomp.exe --setup
```

Wii DOLs require a six-character title ID:

```sh
dolrecomp.exe -j14 path\to\main.dol SUKE01 build
```

GameCube DOLs do not use a title ID:

```sh
dolrecomp.exe --gamecube path\to\main.dol build
```

Wii U uses the Espresso CPU profile and takes an RPX:

```sh
dolrecomp.exe --cpu espresso path\to\main.rpx build
```
You cannot specify --gamecube while using espresso.

Disc extraction is available as a subcommand for future installer work. It accepts `.iso` and `.wbfs` only:

```sh
dolrecomp.exe extract game.iso extracted
dolrecomp.exe extract game.wbfs extracted
```

GameCube ISO extraction is built in. Wii ISO/WBFS extraction uses Wiimms ISO Tool (`wit`) when needed. Run `dolrecomp.exe --setup` to install a local copy, or pass a path manually:

```sh
dolrecomp.exe extract --wit C:\tools\wit\wit.exe game.wbfs extracted
```

Output rules:

- If the last argument ends in `.c`, that exact split-output manifest is used.
- If the last argument is a directory, Wii output goes under `<title-id>_generated\`.
- GameCube and Wii U directory output goes under `generated\`.
- `-jN` controls how many worker jobs write split C chunks.
- If `database\titles.txt` is missing during Wii mode, DolRecomp prints a warning and uses GameCube mode.

## CPU Coverage

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


## Repository Layout

```text
src/
  frontend/     DOL/RPX loading and PowerPC decode
  backend/      split C output
  core/         CPU state and behavior helpers

tests/          decoder, CPU behavior, RPX, and codegen tests
docs/           project notes
tools/          optional developer utilities
```

## Legal Notes

DolRecomp is GPLv3. Copied code must be license compatible.

## Contribution

External contributions are paused while CPU behavior and codegen settle. Forks are welcome.

## Notice

This repo is still not finished, but it's good enough to work with now. Any problems you run into with it can be reported in the Discord with it's relative channel (#dolrecomp)
