# DolRecomp

DolRecomp is a static recompiler for GameCube and Wii.


This project is CPU only. You will need to supply your own runtime or use ModernGekko's template: ![ModernGekko-Template](https://github.com/ExpansionPak/ModernGekko-Template)

## Actual Progress

Moved to recomp projects channel in ExpansionPak discord, where you can see all the recomps made by the community!

## Build

Requirements:

- CMake 3.16 or newer
- A C11 compiler
- zlib, optional but required for compressed RPX sections (Wii U)

LLVM object generation additionally requires LLVM 19 or 20 development files:

```sh
cmake -S . -B build-llvm -DDOLRECOMP_ENABLE_LLVM=ON \
  -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm
cmake --build build-llvm --config Release
ctest --test-dir build-llvm -C Release --output-on-failure
```

it doesn't work without it trust me

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

LLVM/ModernGekko hot-range profiles can use separate thresholds for normal
`native_cycles` samples and NativeCore `module_miss` samples. By default,
`--range-profile-miss-min-samples` inherits `--range-profile-min-samples`, so
existing direct CLI behavior is unchanged. Set it explicitly when module-miss
promotion should be more selective, for example:

```sh
./dolrecomp --backend=llvm --runtime=moderngekko \
  --range-profile runtime.csv \
  --range-profile-min-samples 1 \
  --range-profile-miss-min-samples 256 \
  --range-profile-neighbors 0 \
  --range-profile-call-closure-depth 3 \
  main.dol SUKE01 build
```

`--range-profile-call-closure-depth` bounds how many direct-call levels are pulled
in after the profiled seeds. The direct DolRecomp default remains unlimited for
compatibility. A depth of 0 is safe: omitted callees leave through the normal
ModernGekko structured side-exit/fallback path instead of creating unresolved
native calls.

Function maps are optional. Existing address-only generation remains the default:

```sh
dolrecomp.exe --map path\to\main.map path\to\main.dol SUKE01 build

./dolrecomp --map path/to/main.map path/to/main.dol SUKE01 build
```

When a MAP is supplied, the generated `<name>_symbols.h` exposes
`DOLRECOMP_SYMBOL_<name>` and `DOLRECOMP_SYMBOL_SIZE_<name>`. Names that are not
valid C identifiers are sanitized, and collisions receive an address suffix.

Module-local function replacements are enabled by defining
`DOLRECOMP_ENABLE_REPLACEMENTS` before including the generated header and
implementing `dolrecomp_dispatch_replacement`:

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

Without a MAP, the same replacement dispatcher can use literal guest addresses.
An explicitly supplied MAP that cannot be parsed or does not match any executable
section is rejected.

Disc extraction is available as a subcommand for future installer/launcher work. It accepts `.iso` and `.wbfs` only:

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

<sub>rewrite this portion soon<sub>

## CPU Coverage

the whole damn thing this readme is long enough dude


## Contribution

See CONTRIBUTING.md

# Notices

- No AI code is used in DolRecomp. This is human hand-made project by a group of passionate developers, who want the best for the Retro Gaming community.

note: we have been accepting AI contribution (controlled in areas we want it to be), the rule changed a bit ago but the readme wasn't updated.

tldr: low quality code is not accepted. the full AI contribution guidelines will probably be added to the contribution markdown, but for now there it is


- SMC is currently *unhandled*. You will need to patch the functions manually. DolRecomp will highlight suspicious 
instructions for review. Patching it out at analysis-time may silently break real behavior, so we're leaving that alone

- Wii U support is unfinished.
