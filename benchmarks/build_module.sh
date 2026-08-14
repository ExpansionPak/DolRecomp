#!/usr/bin/env bash
# Build a game module for one backend configuration, into a directory that is
# unique to that configuration, and verify afterwards that the backend actually
# used was the one requested.
#
# Why this exists: moderngekko-port keys its module cache on
# `backend=<c|llvm>` plus the dolrecomp binary hash. Region settings reach
# dolrecomp through the environment, so they are NOT part of that key. Two
# region configurations built into the same --output directory collide, and the
# second silently reuses the first -- which would quietly invalidate any sweep
# over region size.
#
# So the output directory carries a slug derived from the full configuration,
# and the generated manifest is checked: region builds list chunks/region_*.o,
# fixed builds list chunks/chunk_*.o. A mismatch fails loudly rather than
# producing a module that is not what the caller asked for.
#
# Usage:
#   build_module.sh <game-root> <out-root> <backend> [region-mode] [max-instructions] [max-ir] [lto]
#
# lto: off | thin. Under thin the manifest names bitcode, so the link runs
# ThinLTO inside lld -- which makes it a different artifact from the same
# region settings, hence part of the slug.
#
# backend: c | llvm | llvm-aot
set -uo pipefail

GAME="${1:?usage: build_module.sh <game-root> <out-root> <backend> [mode] [max-instr] [max-ir]}"
OUT_ROOT="${2:?missing out-root}"
BACKEND="${3:?missing backend}"
REGION_MODE="${4:-}"
MAX_INSTR="${5:-}"
MAX_IR="${6:-}"
LTO="${7:-}"
MEM="${8:-}"
REGARGS="${9:-}"
PIPE="${10:-}"

MG_ROOT="${MG_ROOT:-C:/Users/douglaswhittingham/luigis-mansion-recomp/lib/ModernGekko}"
PORT="$MG_ROOT/build/moderngekko-port.exe"
TOOLCHAIN="${TOOLCHAIN:-clang}"

# CMake cannot find a resource compiler for clang in GNU-driver mode on Windows
# by itself; without this the configure dies at project() talking about
# CMAKE_RC_COMPILER and never mentions the real cause.
export RC="${RC:-C:/Program Files/LLVM/bin/llvm-rc.exe}"
export DOLRECOMP_LLVM_CACHE="${DOLRECOMP_LLVM_CACHE:-$OUT_ROOT/objcache}"

# The slug is the cache key moderngekko-port should have had.
SLUG="$BACKEND"
[ -n "$REGION_MODE" ] && SLUG="$SLUG-$REGION_MODE"
[ -n "$MAX_INSTR" ] && SLUG="$SLUG-i$MAX_INSTR"
[ -n "$MAX_IR" ] && SLUG="$SLUG-ir$MAX_IR"
[ -n "$LTO" ] && SLUG="$SLUG-lto$LTO"
[ -n "$MEM" ] && SLUG="$SLUG-mem$MEM"
[ -n "$REGARGS" ] && SLUG="$SLUG-ra$REGARGS"
[ -n "$PIPE" ] && SLUG="$SLUG-p$PIPE"
OUT="$OUT_ROOT/$SLUG"

# moderngekko-port validates --backend against its own c|llvm list, so an AOT
# build asks for llvm and overrides it out of band.
PORT_BACKEND="$BACKEND"
unset DOLRECOMP_FORCE_BACKEND DOLRECOMP_REGION_MODE
unset DOLRECOMP_REGION_MAX_INSTRUCTIONS DOLRECOMP_REGION_MAX_IR DOLRECOMP_LTO
unset DOLRECOMP_MEMORY_MODE DOLRECOMP_REG_ARGS DOLRECOMP_LLVM_PIPELINE
if [ "$BACKEND" = "llvm-aot" ]; then
  PORT_BACKEND="llvm"
  export DOLRECOMP_FORCE_BACKEND=llvm-aot
  [ -n "$REGION_MODE" ] && export DOLRECOMP_REGION_MODE="$REGION_MODE"
  [ -n "$MAX_INSTR" ] && export DOLRECOMP_REGION_MAX_INSTRUCTIONS="$MAX_INSTR"
  [ -n "$MAX_IR" ] && export DOLRECOMP_REGION_MAX_IR="$MAX_IR"
  if [ -n "$LTO" ]; then
    export DOLRECOMP_LTO="$LTO"
    # ThinLTO's backend spawns one thread per core and holds several modules
    # live at once. On Mario Kart (444 MB of objects) that link died with exit 1
    # and no diagnostic at all -- the signature of the linker being killed
    # rather than rejecting anything. The same link ran clean once the machine
    # was quiet, so it is a footprint problem, not a bad-bitcode problem.
    # Bound it. LDFLAGS is read at configure time by the module template.
    if [ "$LTO" = thin ]; then
      export LDFLAGS="${LDFLAGS:-} -Wl,/opt:lldltojobs=${LTO_JOBS:-8}"
    fi
  fi
else
  export DOLRECOMP_FORCE_BACKEND="$BACKEND"
fi

# moderngekko-port runs whatever dolrecomp.exe sits next to it, so a build can
# silently use a stale recompiler -- which is how a --lto thin run once produced
# 1724 objects and zero bitcode with no error anywhere. Keep the sibling binary
# current with the one just compiled.
DOLRECOMP_EXE="${DOLRECOMP_EXE:-$(dirname "$0")/../build/dolrecomp.exe}"
if [ -f "$DOLRECOMP_EXE" ]; then
  if [ "$DOLRECOMP_EXE" -nt "$(dirname "$PORT")/dolrecomp.exe" ]; then
    cp "$DOLRECOMP_EXE" "$(dirname "$PORT")/dolrecomp.exe" || exit 1
    echo "[$SLUG] refreshed dolrecomp.exe beside moderngekko-port"
  fi
else
  echo "[$SLUG] WARNING: no dolrecomp.exe at $DOLRECOMP_EXE; using whatever is beside the port"
fi

[ -n "$MEM" ] && export DOLRECOMP_MEMORY_MODE="$MEM"
[ -n "$REGARGS" ] && export DOLRECOMP_REG_ARGS="$REGARGS"
[ -n "$PIPE" ] && export DOLRECOMP_LLVM_PIPELINE="$PIPE"

mkdir -p "$OUT"
echo "[$SLUG] building into $OUT"
start=$(date +%s)
"$PORT" build "$GAME" --backend "$PORT_BACKEND" --toolchain "$TOOLCHAIN" \
  --output "$OUT" > "$OUT/build.log" 2>&1
status=$?
elapsed=$(( $(date +%s) - start ))

if [ $status -ne 0 ]; then
  echo "[$SLUG] BUILD FAILED after ${elapsed}s"
  grep -E "error|Error|FAILED|missing" "$OUT/build.log" | head -5
  exit 1
fi

MODULE=$(find "$OUT" -name "*_recomp.dll" -not -path "*module-build*" | head -1)
MANIFEST=$(find "$OUT" -name "generated.c" -path "*dolrecomp-output*" | head -1)
if [ -z "$MODULE" ] || [ -z "$MANIFEST" ]; then
  echo "[$SLUG] BUILD PRODUCED NO MODULE"
  exit 1
fi

# grep -c prints 0 AND exits non-zero when nothing matches, so a trailing
# `|| echo 0` appends a second line and the arithmetic below chokes on it.
regions=$(grep -c 'chunks/region_' "$MANIFEST" 2>/dev/null); regions=${regions:-0}
chunks=$(grep -c 'chunks/chunk_' "$MANIFEST" 2>/dev/null); chunks=${chunks:-0}

# The check that makes the cache hazard survivable: confirm the units in the
# manifest are the kind this configuration asked for.
if [ "$BACKEND" = "llvm-aot" ] && [ "$regions" -eq 0 ]; then
  echo "[$SLUG] WRONG BACKEND: asked for llvm-aot, manifest has $chunks fixed chunks and no regions."
  echo "   A stale module was reused. Use a fresh --output directory."
  exit 1
fi
if [ "$BACKEND" = "llvm" ] && [ "$regions" -gt 0 ]; then
  echo "[$SLUG] WRONG BACKEND: asked for fixed llvm, manifest has $regions regions."
  exit 1
fi

size=$(stat -c%s "$MODULE")
units=$(( regions + chunks ))
echo "[$SLUG] ok in ${elapsed}s: $units units ($regions regions / $chunks chunks), module $size bytes"
echo "MODULE=$MODULE"
