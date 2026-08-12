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

### Accretion also follows addresses when the call graph runs dry

Call-graph-only accretion was connectivity-bound, not size-bound: a function
reached only indirectly that itself calls nothing has no call-graph neighbours,
so it became a region of one. Regions averaged 70-83 instructions against a
limit of 1024, and the plan emitted 7,520 compilation units for Luigi's Mansion
where the fixed arm needed 909.

Extending a region to the next unassigned function within 256 bytes of its end
costs no crossing, keeps the region a single contiguous run, and exploits the
fact that adjacent functions usually came from the same translation unit:

| Title | Regions before | Regions after | Instr/region | Crossings before | Crossings after |
|---|---:|---:|---:|---:|---:|
| MKDD | 8,928 | **2,033** | 83 → 364 | 31,506 | 32,027 |
| Luigi's Mansion | 7,520 | **1,724** | 70 → 307 | 24,015 | 24,287 |

**4.4x fewer compilation units for 1.1-1.7% more crossings.** The small
regression is greedy loss -- an address merge occasionally consumes a function
that later call-graph accretion wanted -- and is worth it, because unit count
drives object size and compile time.

### Size-limit sweep, Luigi's Mansion

| Limit | fixed crossings | cfg regions | cfg instr/region | cfg crossings | vs fixed |
|---:|---:|---:|---:|---:|---:|
| 512 | 34,431 | 2,081 | 254.6 | 26,837 | −22.1% |
| 1024 | 31,882 | 1,724 | 307.3 | 24,287 | −23.8% |
| 2048 | 29,977 | 1,626 | 325.8 | 22,129 | −26.2% |
| 4096 | 27,924 | 1,632 | 324.6 | 20,747 | −25.7% |

Region count and mean size **plateau at ~1,630 regions of ~325 instructions**
beyond limit 2048, while crossings keep falling. Neither `max_instructions`
(1024+) nor `max_functions` (64) is binding at that point -- the 256-byte
adjacency gap is, because regions stop growing at data holes. Widening the gap
is the next tuning lever, and it is a size-versus-crossings trade that needs the
runtime numbers to settle rather than more static analysis.

### Call edges are counted explicitly

A `CALL` block's successor is its *return point*, not its callee, so walking
successors alone never sees the call. On MKDD that would have hidden 40,316 of
the transfers the plan exists to remove, and co-locating a caller with its
callee would have scored as no improvement at all. Call and tail-call edges are
therefore resolved to the callee's region and counted separately.

---

## 5c. Runtime baseline — Luigi's Mansion through ModernGekko

`benchmarks/run_title_benchmark.py`, headless, Null graphics, no audio, 15 s
warmup then a 45 s window. Module: the existing fixed-chunk LLVM build in the
LM project's `llvmcur/`.

| Arm | fps | speed | bursts | cycles | native | fallback |
|---|---:|---:|---:|---:|---:|---:|
| unthrottled (`EmulationSpeed = 0`) | 40.18 | 0.96 | 2,293,379 | 28,555,556,317 | 108,653,909 | 0 |
| throttled (`EmulationSpeed = 1`) | 41.67 | 1.00 | 2,344,430 | 29,157,296,752 | 110,993,626 | 0 |

### FPS is a usable metric after all — because the title is CPU-bound

The throttled and unthrottled arms are within 3.6% of each other, and the
unthrottled one is marginally *slower*. Removing the real-time cap changes
nothing, which means the cap was never what limited the run: **Luigi's Mansion
under this recompiler sits at roughly 1.0x real time on a 9950X3D**. There is no
headroom being thrown away, so frames-per-second moves when the CPU work moves.

That also sets the noise floor. Run-to-run spread is ~3.5%, so a single pair of
runs cannot resolve the brief's 15% target with confidence, let alone a 5%
regression. Comparisons need repeats and a savestate-pinned scene rather than
the boot sequence these numbers came from.

`fps` in `status.txt` stays 0 headless regardless; the figure above is derived
from `frame_count` over measured wall time, which is populated either way.

### Dispatcher entries per frame

`bursts` is dispatcher re-entries. At 1,810 frames that is **1,267 bursts per
frame** on the fixed-chunk backend -- the number the brief's first performance
gate asks to halve, and the one the region work targets directly. It is
deterministic across runs in a way frame timing is not, so it is the primary
comparison and fps is the corroborating one.

`fallback=0` and `smc_failed=0` on both arms: no instruction fell back to the
interpreter and no self-modifying-code path failed, which is the free
correctness signal from the same run.

---

## 5d. Build cost — Luigi's Mansion, and what it costs to make regions bigger

| Backend | Units | Instr/unit | Clean build | Object bytes | Crossings |
|---|---:|---:|---:|---:|---:|
| fixed-chunk LLVM (shipped) | 4,164 | 128.0 | 859 s | 235,179,859 | — |
| llvm-aot cfg, no adjacency | 7,520 | 70.4 | **524 s** | 247,014,853 | 24,015 |
| llvm-aot cfg, adjacency | 1,724 | 307.3 | 1,147 s | 262,695,877 | 24,287 |

### The adjacency merge does not pay for itself

This is a negative result and it reverses the framing in the commit that
introduced it. Cutting compilation units 4.4x (7,520 -> 1,724) cost:

- **2.2x build time** (524 s -> 1,147 s)
- **+6.3% object bytes** (247 MB -> 263 MB)
- **+1.1% crossings** (24,015 -> 24,287)

Strictly worse on every measured axis except the unit count itself, and unit
count is not a goal -- it was a proxy for build cost, and the proxy was wrong.

The cause is the effect `pipeline.c` already documented for chunk sizes: a
region becomes an LLVM function, and both compile time and generated code grow
superlinearly with the scope the register allocator has to keep the guest
register file live across. Growing regions from 70 to 307 instructions
reproduced the same curve that made 1024-instruction chunks untenable.

The compile-time tail is where it shows worst. Per-region times in the
adjacency build:

    668 s, 397 s, 119 s, 114 s, 108 s, 93 s, 90 s, 83 s, ...

A single region took **668 seconds** against a median under a second. The
brief's requirement that a region end at "excessive IR or compile-time size" is
not satisfiable from instruction count alone -- region 526 is 944 instructions
and 95 blocks, unremarkable by size, and took 397 s.

### Consequence

Smaller regions win on build cost while giving up almost nothing in crossings.
The next tuning step is a lower default region size with adjacency off or
tightly bounded, chosen against crossings-per-build-second rather than against
unit count. That is measured in §5e.

---

## 5e. Measurement methodology, and two things that had to be fixed

Two defects in the first harness produced numbers that looked like results.

### Savestate loads stalled the run

Three Luigi's Mansion runs and one Mario Kart run returned **0 frames** and were
written out as `0.00 fps`. `frame_count` was frozen at 2776 with a stale `speed`
value: the runtime reports `booted=1, state=running` while a 30-45 MB savestate
is still being restored, so a fixed warmup expired before a single frame
advanced. A mean over those rows would have dragged every comparison toward zero
while looking like data.

Measurement now waits for `frame_count` to actually move before starting the
clock, and runs are marked valid/invalid (no frame progress, or a speed reading
frozen across the whole window). Invalid runs are excluded from means and listed
separately.

### Time-boxing measured different guest work every run

With the stall fixed, three repeats still gave sd 86.9% (LM) and 26.2% (MKDD 1P).
The per-run numbers show why:

| Run | fps | frames | cycles/frame |
|---|---:|---:|---:|
| lm-foyer r1 | 19.38 | 873 | 20,801,406 |
| lm-foyer r2 | 19.27 | 868 | 21,219,888 |
| lm-foyer r3 | 77.63 | 3,496 | 9,345,147 |
| mkdd-1p r1 | 57.12 | 2,572 | 10,265,489 |
| mkdd-1p r2 | 83.52 | 3,761 | 9,893,494 |
| mkdd-1p r3 | 52.25 | 2,353 | 10,435,600 |

Two different failures hide in there:

* **Mario Kart** holds cycles/frame at 10.2M ±2% while fps swings 52-84. Guest
  work per frame is stable; the spread is **host contention** -- builds were
  running on the same machine. Benchmarks need a quiet host.
* **Luigi's Mansion** does 21M cycles/frame twice and 9.3M once. That is a
  *different scene*, not a faster run: time-boxing means a faster arm covers
  more of the game, so what is being measured changes with the result.

The harness now measures the wall time for a **fixed frame count**, so every arm
executes the same guest instructions and only host time varies, and reports
counters per frame (bursts, cycles, native, native_exc, hook_fb) so host speed
and scene length drop out of the comparison entirely.

### Baseline, fixed-chunk LLVM module

Recorded before the AOT comparison, 3 repeats, time-boxed 45 s (superseded
methodology, kept for provenance):

| Scene | fps | sd% | bursts/frame | cycles/frame | fallback |
|---|---:|---:|---:|---:|---:|
| lm-foyer | 38.76 | 86.9 | 1,928.0 | see above | 0 |
| mkdd-1p-race | 64.30 | 26.2 | 1,822.7 | 10.2M | 0 |
| mkdd-4p-race | 48.69 | 7.3 | 2,308.7 | 10.3M | 0 |

4-player split screen costs **+27% bursts/frame** over 1 player at essentially
the same cycles/frame, which is what a second and third viewport does to
dispatcher pressure. `fallback=0` throughout.

There is no 2-player savestate in the MKDD project; only `race.sav` and
`race-4p.sav` exist, so 2P is absent rather than substituted.

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
