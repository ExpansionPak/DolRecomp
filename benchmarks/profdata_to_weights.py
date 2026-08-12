#!/usr/bin/env python3
"""Convert an LLVM .profdata into the address/count list DolRecomp's region
planner reads.

The generated module names each guest function `func_<ADDRESS>`, so an IR
instrumentation profile collected from it carries guest addresses in its
function names. That is what makes the conversion possible at all -- and why
DolRecomp itself does not need to link LLVM's profile reader to use a profile.

Entries whose names are not `func_<hex>` are runtime code (the GX runtime, the
chassis dispatcher, the float helpers) rather than guest functions, and are
skipped: they have no guest address to attach a weight to.

    profdata_to_weights.py <profile.profdata> --out weights.txt \\
        [--llvm-profdata <path>] [--top N]
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

# `func_800EB5C0` and the variants the emitter appends, e.g. `func_..._budget`.
FUNC_NAME = re.compile(r"^func_([0-9A-Fa-f]{8})(?:_.*)?$")
COUNT_LINE = re.compile(r"^\s*(?:Maximum function count|Function count|Total count):\s*(\d+)")


def find_profdata_tool(explicit):
    if explicit:
        return explicit
    for candidate in (
        "llvm-profdata",
        r"C:/lm/extern/clang+llvm-20.1.8-x86_64-pc-windows-msvc/bin/llvm-profdata.exe",
        r"C:/Program Files/LLVM/bin/llvm-profdata.exe",
    ):
        found = shutil.which(candidate) or (candidate if Path(candidate).exists() else None)
        if found:
            return found
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("profile")
    parser.add_argument("--out", required=True)
    parser.add_argument("--llvm-profdata")
    parser.add_argument("--top", type=int, default=0,
                        help="keep only the N hottest guest functions (0 = all)")
    args = parser.parse_args()

    tool = find_profdata_tool(args.llvm_profdata)
    if not tool:
        print("error: llvm-profdata not found; pass --llvm-profdata", file=sys.stderr)
        return 1

    result = subprocess.run([tool, "show", "--all-functions", "--counts", args.profile],
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f"error: llvm-profdata failed: {result.stderr.strip()[:400]}", file=sys.stderr)
        return 1

    weights = {}
    current = None
    skipped = 0
    for line in result.stdout.splitlines():
        stripped = line.strip()
        if stripped.startswith("Hash:") or not stripped:
            continue
        # Function names appear on their own line, ending in ':'.
        if stripped.endswith(":") and " " not in stripped[:-1]:
            name = stripped[:-1]
            match = FUNC_NAME.match(name)
            if match:
                current = int(match.group(1), 16)
            else:
                current = None
                skipped += 1
            continue
        if current is None:
            continue
        counts = COUNT_LINE.match(line)
        if counts:
            # Several records can map to one guest address (the emitter splits
            # some functions), so take the largest rather than the first.
            value = int(counts.group(1))
            weights[current] = max(weights.get(current, 0), value)

    if not weights:
        print("error: no func_<address> entries found in the profile; is it from "
              "a DolRecomp-generated module?", file=sys.stderr)
        return 1

    ordered = sorted(weights.items(), key=lambda kv: -kv[1])
    if args.top:
        ordered = ordered[:args.top]

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", encoding="utf-8") as handle:
        handle.write(f"# generated from {Path(args.profile).name}\n")
        handle.write(f"# {len(ordered)} guest functions, {skipped} non-guest records skipped\n")
        for address, count in ordered:
            handle.write(f"0x{address:08X} {count}\n")

    print(f"{len(ordered)} guest functions written to {out} "
          f"({skipped} non-guest records skipped)")
    print(f"hottest: 0x{ordered[0][0]:08X} = {ordered[0][1]:,}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
