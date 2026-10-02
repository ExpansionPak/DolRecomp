# DolRecomp

DolRecomp is a static recompiler for GameCube and Wii PowerPC code, with early Wii U support.

It handles the CPU side of recompilation. Graphics, audio, input, platform integration, and the rest of the runtime are up to whatever is hosting the generated code. If you want a starting point for that, see [ModernGekko-Template](https://github.com/ExpansionPak/ModernGekko-Template).

## What it supports

- GameCube DOLs
- Wii DOLs
- REL modules, including folders of RELs with imports between them
- Wii U RPX files through the Espresso CPU profile
- C and LLVM code generation backends
- ModernGekko native modules through the LLVM backend
- Optional linker MAP files for generated symbol names
- ISO/WBFS extraction helpers

## Building

You need:

- CMake 3.16 or newer
- a C11 compiler
- zlib if you need compressed RPX sections

For a normal build:

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The LLVM backend additionally needs LLVM 19 or 20 development files:

```sh
cmake -S . -B build-llvm \
  -DDOLRECOMP_ENABLE_LLVM=ON \
  -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm

cmake --build build-llvm --config Release
ctest --test-dir build-llvm -C Release --output-on-failure
```

Yes, you actually need the LLVM development files. It does not work without them, trust me.

Windows builds are tested with MSVC and MinGW. ModernGekko does not build under MSVC currently. Linux and macOS are supported as well. If devkitPro is installed, CMake also checks its MSYS2 zlib path. devkitPro is recommended if you are doing Wii U work.

## Usage

Run DolRecomp with no arguments to print the full option list:

```sh
./dolrecomp
```

On Windows, use `dolrecomp.exe` instead.

### GameCube

GameCube DOLs do not need a title ID:

```sh
./dolrecomp --gamecube path/to/main.dol build
```

### Wii

Wii DOLs use a six-character title ID:

```sh
./dolrecomp path/to/main.dol SUKE01 build
```

### REL modules

RELs can be compiled individually or as a folder:

```sh
./dolrecomp path/to/module.rel SUKE01 build
./dolrecomp path/to/rel_folder SUKE01 build
./dolrecomp --gamecube path/to/rel_folder build
```

Folder mode searches recursively, assigns stable virtual addresses, applies self-relocations, and resolves imports between RELs compiled together.

If you need to override the first automatically assigned REL address, use `--rel-base`:

```sh
./dolrecomp --rel-base 0x80500000 path/to/rel_folder SUKE01 build
```

### Wii U

Wii U uses the Espresso CPU profile and takes an RPX:

```sh
./dolrecomp --cpu espresso path/to/main.rpx build
```

`--gamecube` cannot be used with the Espresso profile.

## ModernGekko / LLVM native modules

The LLVM backend can generate native modules for ModernGekko:

```sh
./dolrecomp --backend=llvm --runtime=moderngekko \
  --targets host \
  path/to/main.dol SUKE01 build
```

Useful options include `--native-abi`, `--targets`, `--semantics`, and the range-profile options described below. Run DolRecomp with no arguments for the complete list.

### Profile-guided range builds

ModernGekko runtime profiles can be used to build only the ranges that matter instead of compiling the entire executable.

`native_cycles` entries decide which hot ranges are selected. `module_miss` entries can also pull in ranges that the current module keeps falling out of.

By default, `--range-profile-miss-min-samples` uses the same value as `--range-profile-min-samples`. Set it separately if you want module misses to be more selective:

```sh
./dolrecomp --backend=llvm --runtime=moderngekko \
  --range-profile runtime.csv \
  --range-profile-min-samples 1 \
  --range-profile-miss-min-samples 256 \
  --range-profile-neighbors 0 \
  --range-profile-call-closure-depth 3 \
  main.dol SUKE01 build
```

`--range-profile-call-closure-depth` controls how many levels of direct calls are pulled in from the selected ranges. A value of `0` is valid; calls into ranges that were not selected leave through the normal ModernGekko fallback path.

## Function maps and replacements

Passing a linker MAP file gives generated functions readable names:

```sh
./dolrecomp --map path/to/main.map path/to/main.dol SUKE01 build
```

The generated `<name>_symbols.h` exposes `DOLRECOMP_SYMBOL_<name>` and `DOLRECOMP_SYMBOL_SIZE_<name>` macros. Invalid C identifier characters are sanitized, and name collisions get an address suffix.

Function replacements can use those symbols by defining `DOLRECOMP_ENABLE_REPLACEMENTS` before including the generated header:

```c
#include "generated_symbols.h"
#define DOLRECOMP_ENABLE_REPLACEMENTS
#include "generated.h"

int dolrecomp_dispatch_replacement(CPUState* ctx, u32 address) {
    switch (address) {
    case DOLRECOMP_SYMBOL_GameUpdate:
        game_update_mod(ctx);
        return 1;
    default:
        return 0;
    }
}
```

You can also use literal guest addresses if you do not have a MAP file.

## Disc extraction

DolRecomp has a small extraction helper for installer/launcher workflows:

```sh
./dolrecomp extract game.iso extracted
./dolrecomp extract game.wbfs extracted
```

GameCube ISO extraction is built in. Wii ISO/WBFS extraction uses Wiimms ISO Tool (`wit`) when needed. (maybe make a Wii extractor soon?)

`--setup` can download the local title database and offer to install `wit` if it is missing:

```sh
./dolrecomp --setup
```

You can also provide `wit` manually:

```sh
./dolrecomp extract --wit /path/to/wit game.wbfs extracted
```

The title database is only needed for Wii title names in CLI output. GameCube mode does not need it.

## Output layout

DolRecomp accepts either a C filename or an output directory:

- `output.c` writes that exact split C output set.
- A directory with a Wii title writes `<output>/<title-id>_generated/<title-id>.c`.
- GameCube and Wii U directory output goes to `<output>/generated/generated.c`.
- If no output is given, generated code is written under the current directory.

LLVM builds also emit their generated objects alongside the generated module source.

## Current limitations

### Self-modifying code

SMC is not handled automatically. DolRecomp reports suspicious instructions so they can be reviewed and patched manually. Trying to remove them automatically during analysis can break real behavior, so the project intentionally leaves that decision to the port.

### Wii U

Wii U support is still a work in progress. The Espresso profile and RPX path exist, but this should not be treated as complete Wii U recompilation support yet.

It's also probably never gonna be picked up again but like oh well

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

If you are changing the LLVM backend, run the LLVM build and test suite before opening a PR. The CI covers Linux, Windows, macOS, GCC/Clang, MinGW, and MSVC configurations where applicable.
