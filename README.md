# DolRecomp

DolRecomp is a static recompiler for GameCube, Wii, and experimental Wii U CPU code. It reads executable code sections, decodes PowerPC instructions, and emits split C that can be compiled and tested on a PC.

The current project is CPU only. You will need to supply your own runtime.

## Important!

GitHub Issues are for reproducible bugs, build failures, incorrect output, missing instruction support, and other actionable problems with the tool

Please include enough information to reproduce the issue, such as:
- the input type being tested(Dol, RPX)
- the failing instruction or function, if known
- the commit/build used
- steps to reproduce

Vague reports such as "it doesn't work" are not actionable and may be closed. Issues are not the place for non-technical complaints or drama.

apparently this wasn't as obvious as i'd hope it to be..

## Status

- GameCube/Wii DOL loading works.
- GameCube/Wii REL loading works for single modules and folders of modules.
- Wii U RPX loading works for executable sections. Compressed RPX sections need zlib.
- DOL/RPX/REL frontends validate executable entry points before codegen.
- The decoder currently recognizes 236 PowerPC/Gekko/Broadway/Espresso opcodes. (Espresso may need more looking at)
- The backend emits C in split chunks with `-jN` worker support.
- Generated dispatch can hand known function addresses to host patches before entering compiled code.
- The generated C is a compile target only, no runtime

## Build

Requirements:

- CMake 3.16 or newer
- A C11 compiler
- zlib, optional but required for compressed RPX sections (Wii U)

From the repo root:

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Windows, Clang, GCC/MinGW, and MSVC-style generators should all work. If devkitPro is installed, CMake also checks its MSYS2 zlib location.

devkitPro is highly recommended for Wii U recomps.

## Usage

For help run

```sh
# Windows
dolrecomp.exe

# MacOS/Linux
./dolrecomp
```

to output a list of commands and arguments.

Set up the local title database if you want Wii title names in CLI output. Setup also offers to download Wiimms ISO Tools if `wit` is missing:

<sub>this was just a fun extra thing, just run in gamecube mode if you want to skip it</sub>

```sh
# Windows
dolrecomp.exe --setup

# MacOS/Linux
./dolrecomp --setup
```

### Gamecube
GameCube DOLs do not use a title ID:

```sh
dolrecomp.exe --gamecube path\to\main.dol build

./dolrecomp --gamecube path/to/main.dol build
```

### Wii
Wii DOLs require a six-character title ID:

```sh
dolrecomp.exe path\to\main.dol SUKE01 build

./dolrecomp path/to/main.dol SUKE01 build
```

### REL Modules

REL modules can be compiled one at a time or as a folder. Folder mode finds `.rel` files recursively, assigns stable virtual bases, and resolves imports between modules in that folder:

```sh
# Windows
dolrecomp.exe path\to\module.rel SUKE01 build
dolrecomp.exe path\to\rel_folder SUKE01 build
dolrecomp.exe --gamecube path\to\rel_folder build

# MacOS/Linux
./dolrecomp path/to/module.rel SUKE01 build
./dolrecomp path/to/rel_folder SUKE01 build
./dolrecomp --gamecube path/to/rel_folder build
```

Use `--rel-base 0x80500000` only when you need to override the first auto-assigned REL address. REL support applies self-relocations, and imports between modules compiled together.

### Wii U
Wii U uses the Espresso CPU profile and takes an RPX:

```sh
# Windows
dolrecomp.exe --cpu espresso path\to\main.rpx build

# MacOS/Linux
./dolrecomp --cpu espresso path/to/main.rpx build
```

You cannot specify --gamecube while using espresso.

### Additional Info

Disc extraction is available as a subcommand for future installer work. It accepts `.iso` and `.wbfs` only:

```sh
# Windows
dolrecomp.exe extract game.iso extracted
dolrecomp.exe extract game.wbfs extracted

# MacOS/Linux
./dolrecomp extract game.iso extracted
./dolrecomp extract game.wbfs extracted
```

GameCube ISO extraction is built in. Wii ISO/WBFS extraction uses Wiimms ISO Tool (`wit`) when needed. Run `dolrecomp.exe --setup` to install a local copy, or pass a path manually:

```sh
# Windows
dolrecomp.exe extract --wit C:\path\to\wit.exe game.wbfs extracted

# MacOS/Linux
./dolrecomp extract --wit ./path/to/wit game.wbfs extracted
```

Output rules:

- If the last argument ends in `.c`, that exact split-output manifest is used.
- If the last argument is a directory, Wii output goes under `<title-id>_generated` folder.
- GameCube and Wii U directory output goes under `generated` folder.
- `-jN` controls how many worker jobs write split C chunks.
- If `database/titles.txt` is missing during Wii mode, DolRecomp prints a warning and uses GameCube mode.

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

## Contribution

Forks are welcome. Contribution will not be accepted at the very moment, because this codebase changes rapidly.

# Notices

- This repo is still not finished, but it's good enough to work with now. Any problems you run into with it can be reported in the Discord with it's relative channel (#dolrecomp)

- SMC is currently *unhandled*. You will need to patch the functions manually. DolRecomp will highlight suspicious 
instructions for review. Patching it out at analysis-time may silently break real behavior, so we're leaving that alone

- Wii U support is not actively being worked on, it was just used as a small experiment that kinda ended up working out 
pretty well on the 1 game I tried it on (Kirby and the Rainbow Curse). Don't expect it to work super good or anything

<sub>though it may be picked up in the future<sub>
