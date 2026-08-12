# AOT Region Backend — Performance Results

Every number here is reproducible from the commands given. Nothing is
extrapolated, and any measurement that could not be taken on this host is marked
**not measured** rather than estimated.

---

## 1. Environment

| | |
|---|---|
| Host CPU | AMD Ryzen 9 9950X3D, 16 cores / 32 threads |
| RAM | 125.6 GB |
| OS | Windows 11 Pro 10.0.26200 |
| Compiler | clang 20.1.8 (`C:\lm\extern\clang+llvm-20.1.8-x86_64-pc-windows-msvc`) |
| LLVM | 20.1.8 |
| Host triple | `x86_64-pc-windows-msvc` |
| Generator | Ninja, `CMAKE_BUILD_TYPE=Release` |
| Target triple | default (host); `DOLRECOMP_LLVM_TARGET` unset |
| Target CPU / features | LLVM defaults; not yet overridable (Phase 6 adds `--target-cpu` / `--target-features`) |
| PGO | off (`DOLRECOMP_LLVM_PGO` unset) |
| LTO | off (not yet implemented; Phase 6) |
| Region mode | `fixed` (only mode that exists at this commit) |
| Mod policy | compatible (only mode that exists) |
| Memory mode | safe (only mode that exists) |

> The system LLVM at `C:\Program Files\LLVM` is clang 22.1.5 and ships no CMake
> package. DolRecomp's CMake hard-errors outside LLVM 19–20, so it cannot be
> used. All results use the 20.1.8 tree above.

### Commits

| | |
|---|---|
| Upstream base | `fa0cf619e8d7eb8cba7eaf55267a12caaebb46aa` (`ExpansionPak/DolRecomp` `main`) |
| Phase 0a | `ee3c1ebe66e65eca2f3fad9cd9e4d483804a60ff` |
| Branch | `feature/llvm-aot-regions` |

### Workload identity

| Title | Path | SHA-256 |
|---|---|---|
| Mario Kart: Double Dash!! (USA) | `extracted/GM4E01/sys/main.dol` | `E96B8578451B9157E2B68FE5E918EBB572940C3EA54D6C8C7D45C24382BF12AE` |

Supplied locally. **Not committed**, and not required by CI.

---

## 2. Reproduction commands

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DDOLRECOMP_ENABLE_LLVM=ON \
  -DLLVM_DIR="C:/lm/extern/clang+llvm-20.1.8-x86_64-pc-windows-msvc/lib/cmake/llvm" \
  -DCMAKE_C_COMPILER="C:/lm/extern/clang+llvm-20.1.8-x86_64-pc-windows-msvc/bin/clang.exe" \
  -DCMAKE_CXX_COMPILER="C:/lm/extern/clang+llvm-20.1.8-x86_64-pc-windows-msvc/bin/clang++.exe"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

```sh
# C backend
build/dolrecomp --gamecube --backend c -j8 \
  extracted/GM4E01/sys/main.dol out-c --perf-report mkdd-c.json

# Fixed-chunk LLVM backend
DOLRECOMP_LLVM_CACHE=./llvmcache \
build/dolrecomp --gamecube --backend llvm -j12 \
  extracted/GM4E01/sys/main.dol out-llvm --perf-report mkdd-llvm.json
```

---

## 3. Correctness baseline

`ctest` at `fa0cf61`, LLVM enabled: **19/19 passed** (3.43 s).
After Phase 0a adds `test_perf`: **20/20 passed**.

No test was deleted, skipped or weakened.

---

## 4. Untouched baseline — Mario Kart: Double Dash!!

Both backends translate the same 742,616 guest instructions.

| | C backend | Fixed-chunk LLVM |
|---|---:|---:|
| Regions (chunks) | 182 | 5,803 |
| Guest instructions | 742,616 | 742,616 |
| Guest instructions per region | 4,080.3 | 128.0 |
| Generated code bytes | 195,919,659 (187 MB C source) | 362,681,990 (346 MB objects) |
| Output files | 182 chunks + header | 5,803 objects + header |
| Recompile wall time | 0.50 s (`-j8`) | see §5 |

The two "code bytes" figures are **not comparable to each other** — one is C
source text, the other native object files. They are each comparable only
against their own future numbers.

### Why 128

`src/app/pipeline.c` documents the existing measurement (LLVM-EXPERIMENTS
E002/E003, Mario Kart). A chunk becomes exactly one LLVM function, so chunk size
is the scope over which the register allocator must keep the promoted guest
register file live:

| Chunk instructions | `.text` bytes | Speed | Δ |
|---:|---:|---:|---|
| 1024 | 1,012,522,870 | 0.3288 | — |
| 256 | 450,227,766 | 0.4404 | +33.9% |
| 128 | 345,215,974 | 0.5192 | +57.9% |

Monotonic, disjoint confidence ranges at every step.

**This is the finding that motivates the whole region effort.** The current
backend buys tolerable code size by cutting the program every 128 instructions,
and pays a state materialization plus a dispatcher round trip at every cut. A
CFG-aware region ends at a boundary chosen for control flow instead of an
arbitrary instruction count, so it does not have to make that trade uniformly:
hot loops and hot caller/callee pairs stay whole, and cold code is where the
cuts land.

---

## 5. Build time

`-j12`, cache directory `DOLRECOMP_LLVM_CACHE`.

| Scenario | Wall time |
|---|---:|
| C backend, `-j8` | 0.50 s |
| LLVM, clean — 5,803 misses | 512 s |
| LLVM, partial cache — 1,202 hits / 4,601 misses | 213 s |
| LLVM, full cache hit — 5,803 hits | _measurement in flight_ |

The clean LLVM build is ~1000× the C backend's wall time for the same 742,616
guest instructions. That is the budget any ThinLTO stage in Phase 6 has to fit
inside without making the development loop unusable, which is why the non-LTO
path is retained and why cache-hit time is tracked separately.

---

## 5a. Whole-title CFG model

`build/cfg_stats <main.dol>`. Both titles supplied locally, neither committed.

| | Mario Kart: Double Dash!! | Luigi's Mansion |
|---|---:|---:|
| Sections | 2 | 2 |
| Code instructions (non-data) | 740,889 | 529,738 |
| Covered by blocks | 740,889 (100.00%) | 529,738 (100.00%) |
| Basic blocks | 151,357 | 103,409 |
| Functions | 16,141 | 13,861 |
| SCCs | 128,442 | 91,533 |
| Loop headers | 5,304 | 3,189 |
| Indirect sites | 20,134 | 14,003 |
| Blocks owned by no function | 0 | 0 |

MKDD terminator mix: 49,176 conditional branches, 41,264 calls, 20,465
branches, 20,245 fallthroughs, 14,988 returns, 5,146 indirect, 38 system,
21 tail calls, 14 unknown.

### Function entries cannot come from `bl` targets alone

Seeding function entries only from direct call targets left **59.47% of Mario
Kart's code owned by no function**. The roots of that were 22,010 blocks with no
in-edge anywhere in the program -- reached only through a vtable slot, a
function-pointer table, or a jump table, which is what a C++ title looks like.

Treating a block that no direct edge reaches as an entry point by elimination
brought unowned code to 0.11%, and seeding the residual -- cycles where every
member has an in-edge from inside the cycle -- closed it to **0.00%** on both
titles.

This infers *entries*, never edges. Nothing here claims to know which indirect
site reaches which entry; that is Phase 4's job. But it means region formation
sees the whole title rather than the directly-called fraction of it.

> The counts above are from the corrected `cfg_stats` described in §5b. The
> earlier revision reported 161,404 blocks / 29,021 functions for MKDD, which
> was the looser embedded-data predicate splitting the address space more than
> the backends do.

---

## 5b. Region planning

`build/cfg_stats <main.dol> --compare-modes --region-max-instructions N`

Region crossings are the metric: each one is a boundary control flow has to
traverse, which under the current backend means a state materialization and a
dispatcher round trip. Internal edges are the mirror -- control flow that stays
inside one compiled unit and can be a native branch.

All three modes run at the same size limit and through the identical edge
model, so only the choice of boundary differs.

### Limit 1024

| Title | Mode | Regions | Instr/region | **Crossings** | Internal edges |
|---|---|---:|---:|---:|---:|
| MKDD | fixed | 774 | 957.2 | 40,754 | 186,164 |
| MKDD | function | 16,160 | 45.8 | 44,818 | 182,100 |
| MKDD | **cfg** | 8,928 | 83.0 | **31,506** | 195,412 |
| Luigi's Mansion | fixed | 909 | 582.8 | 31,882 | 121,751 |
| Luigi's Mansion | function | 13,864 | 38.2 | 35,471 | 118,162 |
| Luigi's Mansion | **cfg** | 7,520 | 70.4 | **24,015** | 129,618 |

**CFG accretion removes 22.7% of crossings on MKDD and 24.7% on Luigi's
Mansion** against the CFG-blind arm at the same size limit.

**`function` mode is worse than `fixed`** -- +10.0% crossings on MKDD, +11.3% on
Luigi's Mansion. Cutting at every function boundary produces more crossings than
cutting arbitrarily at a large granularity, because most functions are small
(38-46 instructions) and every call then leaves its region. This is the sharpest
result in the phase: the mechanism that pays is *accreting callers with
callees*, not respecting function boundaries. Phase 3's direct-linking work
should be scoped accordingly.

> **Correction.** An earlier revision of this document reported 33.0% / 33.6%.
> Those numbers came from a defect in `cfg_stats`, not from the planner: it
> classified a word as embedded data whenever `embedded_data_word()` matched,
> while `pipeline.c` requires the word to have *failed to decode* as well. The
> looser predicate marked decodable instructions as data, which fragmented the
> address space and forced the `fixed` arm to break at every fabricated
> discontinuity -- 13,281 regions of 54.7 instructions instead of 774 of 957.2.
> That made the baseline look far worse than it is. `cfg_stats` now uses the
> pipeline's predicate verbatim and the table above is the corrected
> measurement. The planner itself did not change.

### Call edges are counted explicitly

A `CALL` block's successor is its *return point*, not its callee, so walking
successors alone never sees the call. On MKDD that would have hidden 40,316 of
the transfers the plan exists to remove, and co-locating a caller with its
callee would have scored as no improvement at all. Call and tail-call edges are
therefore resolved to the callee's region and counted separately.

---

## 6. Runtime counters

**Not measured at this commit.** The Phase 0a runtime counters exist and compile
out correctly, but nothing emits `DOLRECOMP_PERF_INC()` into generated code yet —
that lands with the region backend, which is what those counters are for.

Reporting a runtime column here would be reporting zeros as if they were
observations. The performance gates in §7 are therefore all still open.

---

## 7. Performance completion gates

Baseline is the fixed-chunk LLVM backend. All gates open at this commit.

| Gate | Target | Status |
|---|---|---|
| Dispatcher entries in hot gameplay | ≥50% fewer | open |
| Full `CPUState` materializations | ≥50% fewer | open |
| Cross-region transfers needing returned-PC validation | ≥50% fewer | open |
| Ordinary RAM ops on a direct/compact fast path | ≥80% | open |
| Generic slow memory helper calls | material reduction | open |
| CPU-thread time, primary benchmark | ≥15% lower | open |
| Second representative workload | no regression >5% | open |
| Correctness divergence | none | open |
| Code size vs fixed LLVM | prefer <25% growth | open |

---

## 8. Platform status

| Target | Status |
|---|---|
| x86-64 Windows | building and tested (this host) |
| x86-64 Linux | available via WSL2 Ubuntu — not yet measured |
| AArch64 Linux | no native host; cross-compile only |
| arm64 macOS | excluded (machine reserved for other work) |
| x86-64 macOS | no host |

AArch64 cross-compilation can be configured, but NEON paired-single lowering,
fastmem address calculation and the runtime ABI need a real execution
environment before that deliverable can be called done. Recorded in
[AOT-REGION-IMPLEMENTATION.md](AOT-REGION-IMPLEMENTATION.md) §9.

---

## 9. Remaining bottlenecks

Identified, not yet addressed:

1. **128-instruction chunk boundaries** (§4) — the dominant architectural cost.
2. **`g_mem_write_journal` checked on every store** (`src/cpu/cpu.h`) — an
   unconditional branch on a global function pointer in the store path. Phase 5
   removes it from production builds via explicit journaling modes.
3. **No cross-chunk direct calls by default** — gated behind
   `DOLRECOMP_UNSAFE_DIRECT_CALLS` because it bypasses chassis dispatch
   validation. Phase 3 makes this safe and default.
4. **No whole-program optimization** — objects are emitted independently with no
   final link-time inlining or internalization. Phase 6 adds ThinLTO.
