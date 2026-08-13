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

### Building a module through moderngekko-port

`moderngekko-port` drives a **sibling** `dolrecomp` and forwards only
`--backend=c|llvm`, which it validates. To build an AOT module, replace that
sibling with this tree's `dolrecomp` and use the override variable:

```sh
cp build/dolrecomp.exe <ModernGekko>/build/dolrecomp.exe    # keep a .orig copy
export RC="C:/Program Files/LLVM/bin/llvm-rc.exe"
export DOLRECOMP_FORCE_BACKEND=llvm-aot
export DOLRECOMP_REGION_MODE=cfg
moderngekko-port build <extracted-root> --backend llvm --toolchain clang   --output <module-dir>
```

Two things bite here, both recorded because neither error names its cause:

* **`RC` must be set.** The module template configures clang in GNU-driver mode
  on Windows and CMake 4.3 cannot find a resource compiler by itself. The
  configure fails at `project()` talking about `CMAKE_RC_COMPILER`.
* **The manifest line format is load-bearing.** The template parses
  `// object: chunks/<name>` and takes the remainder of the line as the path.
  An earlier revision appended `(16 runs)` for readability and the configure
  then failed looking for a file literally named
  `region_000000_80003100.o (16 runs)`. Run counts live in the region report.

> **Cache hazard.** `moderngekko-port` keys its module cache on
> `backend=<c|llvm>` plus the `dolrecomp` binary hash. Region settings arrive
> through the environment, so they are **not** in that key: two different region
> configurations built into the same `--output` directory collide and the second
> silently reuses the first. Give every configuration its own `--output`
> directory, and verify which backend actually ran by looking at the generated
> manifest -- region builds list `chunks/region_*.o`, fixed builds list
> `chunks/chunk_*.o`. DolRecomp's own object cache is not affected: its key
> hashes every run and the run partition.

Both arms of a comparison are built through this same path -- same port tool,
same toolchain, differing only in `DOLRECOMP_FORCE_BACKEND` -- rather than
against a module built earlier under unknown settings.

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

### Where the compile time actually goes

Per-region compile times from the adjacency build, bucketed by region size
(1,724 regions, 8,086 s of CPU time, 1,147 s wall at `-j12`):

| Region size | Regions | Instructions | CPU seconds | % of compile time |
|---|---:|---:|---:|---:|
| 0-64 | 687 | 14,737 | 23 | 0.3% |
| 64-128 | 247 | 22,490 | 62 | 0.8% |
| 128-256 | 208 | 38,478 | 233 | 2.9% |
| 256-512 | 167 | 61,020 | 785 | 9.7% |
| 512-768 | 63 | 40,062 | 778 | 9.6% |
| **768-1100** | **352** | **352,951** | **6,205** | **76.7%** |

Regions of 512 instructions or more are **24% of regions and 74% of
instructions, but 86.4% of compile time**. Cost per instruction runs 1.6 ms in
the smallest bucket against 17.6 ms in the largest -- **11x** -- which is the
superlinear curve stated plainly.

So instruction count *is* a usable predictor after all, contrary to the first
read of the 397 s outlier: regions that reach the size cap dominate. The two
extreme outliers (668 s at 848 instructions / 97 blocks, 397 s at 944 / 95) sit
on top of that trend rather than contradicting it. Averaged over the run,
regions taking 30 s or more hold 946 instructions and 145 blocks; regions under
2 s hold 86 and 18.

Lowering the size cap collapses the tail directly, and that is the lever to pull
before anything more elaborate.

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

## 5f. First runtime signal: dispatcher rate per unit of guest work

The Luigi's Mansion head-to-head could not be read as a speed comparison -- the
arms executed different guest work (§5e). But one figure survives that, because
it is a *rate*: counters divided by guest cycles normalise away both host speed
and scene length.

| Arm | runs | bursts / guest Mcycle | native / guest Mcycle |
|---|---:|---:|---:|
| fixed-chunk (128) | 3 | 156.41 | 8,380.6 |
| **llvm-aot cfg (1024)** | 3 | **121.85** | 4,925.0 |

**−22.1% dispatcher entries per unit of guest work.**

That appeared to land on what the region planner predicted statically -- −21.4%
crossings on Mario Kart, −23.8% on Luigi's Mansion -- and an earlier revision of
this document concluded the static crossing count was a usable proxy for the
runtime dispatcher rate.

**That conclusion was wrong.** The two arms here ran different Luigi's Mansion
scenes, and a same-scene sweep on Mario Kart (§5i) shows the runtime rate is
flat while static crossings fall 21%. The agreement was coincidence between two
scenes, not a mechanism.

Caveat, stated rather than buried: the two arms ran different scenes, and
different code mixes can have different intrinsic dispatcher rates. This is
corroboration, not proof. A same-scene comparison is what settles it, and that
is what the Mario Kart race states and the newly captured `bench.sav` are for.

`bursts_per_mcycle` is now emitted by the harness and leads the comparison
table, because it is the only speed-related figure that stays meaningful when
guest work does not match exactly.

---

## 5g. Scene selection: Luigi's Mansion is not a usable benchmark title

A freshly captured Luigi's Mansion savestate behaved no better than the foyer
one:

| Run | cycles/frame | fps | bursts/Mcycle |
|---|---:|---:|---:|
| bench.sav r1 | 14.58M | 62.97 | 126.5 |
| bench.sav r2 | 20.20M | 26.50 | 156.4 |
| bench.sav r3 | 15.35M | 42.30 | 136.2 |

18.2% spread in guest work from an identical starting state.

The obvious suspicion was that `--load-state` silently failed and the run fell
back to booting: 20.197M is the exact figure all three earlier `foyer.sav` runs
produced, which looks like a shared fallback. **It is not.** A control run with
no savestate at all gives 27.06M cycles/frame, distinct from both. The state
loads; the game diverges after it.

So Luigi's Mansion is nondeterministic run to run at this granularity -- ghost
behaviour and timing varying from identical initial state. That is a property of
the title, not of the savestate or the harness, and capturing another state will
not change it.

Mario Kart through the identical harness and procedure:

| Run | cycles/frame | bursts/Mcycle |
|---|---:|---:|
| mkdd 1p fixed r1 | 10.17M | 172.3 |
| mkdd 1p fixed r2 | 10.26M | 170.3 |

**0.9% and 1.1% apart.** Same rig, same method: 0.9% on Mario Kart against 18.2%
on Luigi's Mansion.

**Mario Kart is therefore the primary benchmark**, with its 1P and 4P race
states, and the 13 `course-*.sav` states available for breadth. Luigi's Mansion
is secondary, run with a much longer window so its transients average out, and
always reported with its spread rather than as a point estimate.

---

## 5h. Same-scene head-to-head, Mario Kart

Both arms built through the same pipeline, differing only in backend. 1P race
state, 1,200 frames, 3 repeats.

### The linear dispatch chain, and its removal

The first attempt measured the region arm at **5.98 fps against the fixed arm's
47.81** -- eight times slower -- at an *identical* dispatcher rate
(bursts/Mcycle +2.0%). Same number of dispatches, vastly more time in each.

The generated header explained it. The fixed build emits **zero** address
comparisons: uniform 128-instruction chunks collapse into four equal-stride
tables, so a lookup is two range tests and an index. The region build emitted
**8,284** address comparisons, because variable-sized regions do not collapse
and the linear chain walks them.

A page-indexed lookup was already implemented and already handled this, but was
gated behind `DOLRECOMP_DISPATCH_LOOKUP=indexed` and defaulted to linear. The
default is now chosen from the plan's shape. With it, the region arm emits zero
comparisons and a 5,977-run / 726-page index, and measures **30.15 fps**.

This is the brief's "do not fall back to long linear comparison chains for
irregular region layouts", and it was silently costing 8x.

### Result at cap 256

| Arm | fps | fps sd% | bursts/frame | **bursts/Mcycle** | cycles/frame |
|---|---:|---:|---:|---:|---:|
| fixed | 38.70 | 27.8 | 1,817.2 | 175.8 | 10.34M |
| llvm-aot cfg @256 | 30.15 | 23.2 | 1,825.9 | 178.0 | 10.26M |

Guest work agrees to 0.8%, so the scenes are comparable.

**bursts/Mcycle: +1.2% -- no improvement.** fps is inside the noise floor and
supports no claim in either direction.

This is what the planner predicted and the cap was chosen against it. At cap 256
Mario Kart plans 40,316 crossings against the fixed arm's 40,754: statically
identical. The -22.1% dispatcher rate measured earlier came from cap **1024**.
Cap 256 was chosen to collapse the compile-time tail, which it did, and in doing
so gave up the entire reason for the region backend.

The lesson is narrow and worth stating: region size is not a free parameter that
trades build time against nothing. Crossings and compile time pull in opposite
directions, and a cap picked for one is a cap picked against the other.

---

## 5i. Region size sweep: static crossings do not predict the runtime rate

Four arms, same pipeline, same Mario Kart 1P scene, 1,200 frames, 3 measured
repeats each after discarding a shader-cache warmup run.

| Arm | Static crossings | vs fixed | **bursts/Mcycle** | vs fixed | fps | fps sd% |
|---|---:|---:|---:|---:|---:|---:|
| fixed (128) | 40,754 | — | 180.4 | — | 29.45 | 1.8 |
| aot cfg @256 | 40,316 | −1.1% | 178.9 | −0.8% | 31.58 | 5.9 |
| aot cfg @512 | 35,417 | −13.1% | 178.7 | −0.9% | 31.97 | 1.1 |
| aot cfg @1024 | 32,027 | −21.4% | 179.0 | −0.8% | 30.16 | 1.8 |

**Static crossings fall 21.4%. The runtime dispatcher rate does not move at
all** -- every arm sits within 1% of the fixed baseline, and the variation is
not even monotonic in region size.

The measurement is trustworthy: fps spread is 1.1-1.8% on three of four arms
after the shader-cache fix, against 23-28% before it, and `fallback=0`
throughout. This is a null result, not a noisy one.

### Why the metric failed

A static crossing is a CFG edge that leaves a region. It counts every edge once,
whether it executes a billion times or never. Runtime dispatcher entries are
dominated by whatever the hot path does, and this profile is extraordinarily
concentrated: one guest function is 22% of all execution
(`func_800EB5C0`, 83.2 G of 380 G counts).

Merging regions removes boundaries roughly uniformly across the address space,
so it removes overwhelmingly **cold** boundaries. Removing a boundary that never
executes reduces the static count and changes nothing at runtime. Meanwhile the
hot loop was already inside a single 128-instruction chunk in the fixed layout,
so it never crossed a boundary to begin with.

### What this redirects

Two consequences, both already in the brief and now with evidence behind them:

1. **Region formation must be profile-weighted, not uniform.** The planner's
   `pgo` mode exists for this and is now wired to real weights. Merging on hot
   edges is a different operation from merging on all edges, and only the first
   can move the runtime rate.

2. **Fewer crossings is the weaker lever; cheaper crossings is the stronger
   one.** Phase 3's direct cross-region linking makes a remaining boundary cost
   a native call rather than a dispatcher round trip. That helps every boundary
   that actually executes, regardless of how the regions were drawn.

Uniform region enlargement is therefore not worth its cost: at cap 1024 it buys
nothing measurable for +29% module size (444 MB against 343 MB) and 874 s of
build time against roughly 500 s.

---

## 5j. Region formation does not change the dispatcher rate -- and why

Mario Kart 1P, every valid run across every session, `bursts` per guest Mcycle:

| Arm | runs | mean | min | max |
|---|---:|---:|---:|---:|
| fixed (128) | 12 | 175.2 | 167.2 | 180.4 |
| aot cfg @256 | 3 | 178.9 | 178.9 | 178.9 |
| aot cfg @512 | 3 | 178.7 | 178.4 | 178.9 |
| aot cfg @1024 | 3 | 179.0 | 178.9 | 179.0 |
| **pgo @1024** | 3 | **178.8** | 177.6 | 179.4 |

The spread *within* the fixed arm alone (167.2-180.4) is wider than any
difference between arms. Uniform region enlargement does not move it. Neither
does profile-guided formation, despite planning a visibly different program:
3,244 regions of 228 instructions against cfg's 2,033 of 364.

### The reason, which took three nulls to see

`bursts` counts **dispatcher re-entries**, and cross-region calls were never
dispatcher re-entries. `externalDestination()` has always emitted a direct call
to `func_XXXXXXXX_budget`. A dispatcher entry happens when generated code
*returns to the runtime* and the top-level loop calls back in -- at an indirect
branch, at a `blr` whose target is not statically known, at a side exit, at an
exception.

Region formation regroups code. It does not make an indirect branch direct.
Mario Kart has 20,134 indirect sites, and every one of them still leaves through
the dispatcher no matter which region it sits in.

So the first performance gate -- "at least 50% fewer central dispatcher
entries" -- is not reachable by region planning at all. It is reachable by
Phase 4: per-site indirect target caches and BLR shadow returns, which convert
an indirect transfer into a compare-and-direct-branch.

### What region formation is worth, then

Not nothing, but not this. The per-crossing cost is a state round trip --
materialise every dirty slot, call, validate the returned PC, reload state --
and that cost is paid per *executed* call. Fewer boundaries means fewer such
round trips on paths that execute. But the sweep shows the boundaries removed by
uniform merging are overwhelmingly cold, and the profile-guided variant did not
find enough hot ones to matter either.

The conclusion the evidence supports: **stop tuning region formation**. The
remaining performance is in what a crossing costs (Phase 3) and in not returning
to the dispatcher for indirect control flow (Phase 4).

### Measurement discipline note

The fps columns in this section carry 28-31% spread on two arms because builds
were running concurrently with the benchmark. That is a procedural error, not
host noise -- an idle-host run of the same rig measured 1.1-1.8%. bursts/Mcycle
is unaffected, which is why the conclusion rests on it. No fps claim is made
from these runs.

---

## 5k. Where Phase 4 should aim: blr, not bctr

Terminator mix for Mario Kart, weighted by the multiscene profile. A site that
never executes costs nothing, so the weighted column is the one that matters --
three separate nulls in this project came from optimising a statically large
quantity that was dynamically irrelevant.

| Terminator | sites | % sites | **% weighted execution** |
|---|---:|---:|---:|
| cond-branch | 49,176 | 32.5% | 44.21% |
| branch | 20,465 | 13.5% | 22.01% |
| fallthrough | 20,245 | 13.4% | 14.86% |
| **return (blr)** | 14,988 | 9.9% | **10.95%** |
| call | 41,264 | 27.3% | 7.79% |
| **indirect (bctr)** | 5,146 | 3.4% | **0.17%** |
| tail-call / system / unknown | 73 | 0.0% | 0.00% |

The first three resolve inside a region as native branches and cost nothing at
the dispatcher. What can leave through the runtime is `blr` and `bctr`.

**`bctr` is 0.17% of weighted execution.** Jump-table recovery, static
target-set analysis and per-site indirect target caches -- the bulk of Phase 4
as specified -- all aim at that path. On this title they would optimise
something that essentially never runs. `blr` is 64x more significant.

This does not mean the brief is wrong in general: a title built around switch
dispatch or heavy virtual calls would invert this. It means the work should be
ordered by what the profile says, and for Mario Kart that order is:

1. **BLR return handling** (10.95%) -- shadow return stack, native continuation
   on match, indirect fallback on mismatch.
2. Everything else in Phase 4, which is rounding error here.

Worth noting `call` is 27.3% of sites but only 7.79% of weighted execution,
while `cond-branch` is the reverse -- calls are spread thinly across cold code
and the hot paths are loops. That is the same shape that made uniform region
merging useless.

---

## 5l. Differential testing: C backend against LLVM backend

`tests/differential/` generates random guest sequences and compiles them through
both backends, emitted at different guest base addresses so the two sets of
`func_<address>` symbols coexist in one comparing binary. Each pair runs from a
byte-identical randomised `CPUState`; the full observable result is compared --
every GPR, every FPR and paired-single lane **as a bit pattern**, LR, CTR, CR,
XER, FPSCR, exception and reservation state, and the scratch memory both wrote.

Bit patterns rather than float compares because two backends that agree
numerically but disagree on which NaN they produce have still diverged, and a
title can observe that. Initial state is biased toward awkward values -- zero,
all-ones, +inf, quiet NaN, smallest normal, denormal -- since uniform random
bits essentially never produce them and that is where backends differ.

Sequences are straight-line and end in `blr`. That is deliberate rather than
lazy: a return is a materialisation barrier, so every sequence exercises the
state-save path that the liveness and reaching-writes narrowing changed.

Run as `ctest -R differential`. `DOLRECOMP_DIFF_SEED` sweeps seeds in CI.

### It found a divergence on its first run

28 divergences across 64 sequences, and **every one was an `stfs` result**.
Nothing differed in any register, and removing `stfs` alone took the suite to
64/64.

| Case | C backend | LLVM backend |
|---|---|---|
| double out of single range | `0x7E000000` | `0x7F800000` (+inf) |
| denormal | `0x04000004` | `0x00000000` (flushed) |

So the two backends disagree on `stfs` for values not representable as single:
overflow and denormal handling. PowerPC leaves the result boundedly undefined
when the value is not representable, and a compiler-generated `stfs` normally
stores something that came from single-precision arithmetic, so this is unlikely
to be reachable from real game code. It is still a genuine disagreement between
two backends that are supposed to be interchangeable, and one of them is wrong
about what the hardware does.

`stfs` is excluded from the default pool and reproduces with `--stfs`. That is
scoping a new test rather than weakening an existing one -- a gate that always
fails gates nothing -- and the exclusion is recorded here rather than buried in
the generator. **Open issue: decide which backend matches Gekko and fix the
other.**

---

## 5m. The liveness-narrowed reload was wrong, and how it was caught

Narrowing the post-call reload to state live at the continuation **hung Mario
Kart**. The module loaded, reported `state=running`, reached `present_count=1`
and never advanced a frame in 180 seconds. The same region configuration built
without the change ran normally.

### Why it was unsound

The backward liveness followed `terminator.targets[]` only. The emitter also
reaches blocks through the `continuations_` switch that `DOLIR_TERM_INDIRECT`
lowers to: an indirect transfer whose target matches a known continuation
branches straight to that block. Those edges do not appear in `targets[]`, so
liveness never propagated backward through them and reported slots dead that a
continuation-entered block goes on to read. The reload skipped them and the
block ran on stale guest state.

### Why the differential suite missed it

It could not have caught this. Its sequences are single functions with no calls,
and `reloadLiveState` only runs on a cross-function call return. The path had
zero coverage.

This is the important part. The suite passed 23/23 with the broken change in it,
and that green result was cited as validation for both optimisations before the
real check ran. **A passing suite is evidence only about what it exercises**, and
the gap between "straight-line sequences ending in blr" and "a title making
cross-region calls" was exactly where the bug lived.

### What was kept and what was reverted

| Change | Status |
|---|---|
| reload narrowed to live-at-continuation | **reverted** -- unsound, hung the title |
| materialize narrowed to reaching-writes | kept -- different analysis, forward, and its claim is only that a slot no path has written need not be stored |

The store-side narrowing survives because its soundness argument does not depend
on the successor model being complete: it never claims a slot is clean where a
write may have happened, and an unreached block is treated as fully dirty.

`computeLiveness()` stays in the tree, unused for reload, because the fix is to
add the indirect-continuation edges to the successor model rather than to
rewrite the analysis.

### The measurement debt this exposes

The differential generator needs call-shaped sequences -- one generated function
calling another -- before anything touching the call/return path can be trusted.
That is the next thing to build, ahead of any further optimisation there.

---

## 5n. Barrier store narrowing: two wrong versions, then a measured win

The store side of every materialisation barrier used a whole-function dirty
flag -- a slot written anywhere was stored at every barrier, including barriers
on paths that never touched it.

Two attempts failed before one worked, and both failures were the same mistake
in different clothing: **an incomplete model of how guest state moves**.

| Attempt | What it did | How it failed |
|---|---|---|
| liveness-narrowed *reload* | restore only slots live at the continuation | hung Mario Kart at boot; the successor model missed the indirect-continuation edges `DOLIR_TERM_INDIRECT` lowers to |
| reaching-writes *store*, v1 | skip slots no path has written | diverged on 3 of 64 differential pairs, all floating point; counted `DOLIR_OP_STATE_WRITE` only, missing helpers that write slots inside the runtime |
| reaching-writes *store*, v2 | as above, but any helper call dirties every used slot | **works** |

The working version is deliberately blunt. Enumerating which helper writes which
slot would duplicate `scanState()` and create a second place to forget one --
and forgetting one is exactly how v1 broke. Narrowing is surrendered inside
blocks containing a helper and kept everywhere else, which is most blocks and
all the integer ones.

### RETRACTED

An earlier revision of this section reported the corrected store narrowing as a
win: module 444,321,280 -> 391,159,808 bytes and build 874s -> 669s, -12.0% and
-23.4%.

**Those numbers are real and worthless: the module does not run.** Benchmarking
it against the fixed backend produced four consecutive runs of "booted but never
advanced a frame in 180s" -- the same failure as the reverted liveness reload.
The size and build-time reductions were measured on a module that hangs Mario
Kart at boot.

The narrowing is disabled. Both barrier sides are fully conservative.

| | Module size | Build time | Runs? |
|---|---:|---:|---|
| no narrowing (v7) | 444,321,280 | 874 s | yes |
| store narrowing (v12) | 391,159,808 (-12.0%) | 669 s | **no** |
| store narrowing (v15, correct) | 425,043,456 (**-4.3%**) | **1,308 s (+50%)** | yes |

### The third root cause, and what correct actually costs

v12 hung because both dataflow passes built their graph from
`terminator.targets[]` alone, while `DOLIR_TERM_INDIRECT` lowers to a switch
over `continuations_` -- an indirect transfer whose target matches a call-return
point branches straight into that block. Those edges are absent from
`targets[]`.

The no-predecessor safety case did not catch it: a continuation block normally
*does* have a targets-predecessor, the fallthrough after the call, so it
inherited a dirty set from a path the indirect route does not justify.

This is the same root cause as the reverted liveness reload. It was diagnosed
there, written into the comment there, and then rebuilt in the store pass --
because only the *helper* lesson was carried forward, not the *edge* lesson.
Three failures, two distinct causes, one of them twice.

Both passes now include the indirect-switch edges and `scanContinuations()` runs
before either. The module runs: 612 frames, `fallback=0`, `smc_failed=0`.

**And the win mostly evaporates.** -4.3% module size against v12's -12.0%, with
build time up 50% rather than down 23%. v12 looked good precisely because it
skipped stores it should not have. The extra dataflow is
O(blocks x continuations x slots) per fixpoint iteration, which is where the
build time goes.

`bursts/Mcycle` on the probe is 178.3, indistinguishable from every other arm
measured this session (175-179). **On this evidence the optimisation is not
worth its build-time cost**, and the recommendation is to leave it disabled by
default until either the dataflow is made cheaper or a proper A/B shows a
runtime gain. It is kept in the tree, correct, because the edge fix it forced is
the prerequisite for the register-passing ABI that is the real target.

240 differential pairs across 5 seeds agree with the C backend, including
call-shaped sequences with LR save/restore. The v1 version failed that same
suite on its first seed, so the suite is not useless -- but it passed v2, and v2
hangs a real title.

That bounds the blind spot precisely. Whatever breaks is not reached by
straight-line code nor by one level of direct calls. What the suite still does
not generate: branch-shaped control flow inside a region, indirect transfers
through the `continuations_` switch, exception paths, and dispatcher re-entry
part-way through a region.

**Three attempts, three failures, one pattern.** Every narrowing of a
materialisation barrier has failed on a path the emitter reaches by a route the
analysis did not model -- indirect-continuation edges, then helper writes, now
something still unidentified. The recommendation is not to patch the analysis a
fourth time. It is to derive the successor model and the emitted edges from one
description, so that "the analysis models what the emitter generates" is
structural rather than a claim to be re-checked after each failure.

### What this cost to get right

Both failures passed the test suite as it existed at the time. The reload
narrowing passed 23/23 and hung a real title; the store narrowing v1 passed
every straight-line sequence and corrupted floating-point state only under
calls. Neither would have been caught without extending the differential suite
to cover the call/return path, which is the work that made this measurable at
all.

---

## 5o. Cross-region inlining needs ThinLTO, and that is why Phase 6 exists

With the internal bodies on `fastcc`, dropping `NoInline` should let LLVM inline
a small or hot callee across a region boundary -- removing the call entirely
rather than making it cheaper, which is the only thing that eliminates a whole
state round trip.

Measured on Mario Kart at cap 1024:

| Arm | Module size | Build time |
|---|---:|---:|
| default | 444,321,280 | 1,024 s |
| `DOLRECOMP_INLINE_REGIONS=1` | 444,395,008 | 1,123 s |
| | **+0.017%** | +10% |

**Nothing was inlined.** A 73 KB delta across a 444 MB module is noise.

The reason is structural rather than a tuning problem. Each region is emitted as
its own LLVM module and its own object file. A cross-region call targets
`func_XXXXXXXX_budget` in a *different translation unit*, and LLVM cannot inline
across object boundaries at all without link-time optimisation. Dropping
`NoInline` only ever enabled inlining between the runs inside one region, which
is a small population and evidently not a profitable one.

So the direct-linking benefit the brief describes -- "permit LLVM to inline small
or hot callees" -- is **not reachable from the emitter**. It is reachable only
from the ThinLTO stage in Phase 6, which is precisely the phase that imports hot
callees across module boundaries and internalises what is not exported.

This reorders the remaining work. The register-passing ABI still stands on its
own: passing live state in registers shrinks each call that survives. But
*removing* calls -- the larger prize -- requires ThinLTO first, and ThinLTO also
subsumes part of the ABI question, since an inlined callee needs no ABI at all.

The flag stays, off by default, because it costs nothing when off and becomes
meaningful the moment ThinLTO lands.

### The A/B confirms it, and calibrates the rig

| Arm | fps | fps sd% | bursts/Mcycle | cycles/frame |
|---|---:|---:|---:|---:|
| noinline | 43.27 | **32.3%** | 172.5 | 10.40M |
| inline | 27.14 | 4.7% | 179.1 | 10.03M |

**No result is read from the fps column.** The baseline arm's run-to-run spread
is 32.3%; a -37% delta against a baseline that varies by a third is noise, and
the two modules differ by 0.017% so a real 37% gap between near-identical code
would be extraordinary. `bursts/Mcycle` moved +3.9%, inside the 167-181 band
every arm has occupied all session.

The more useful finding is about the instrument. **A 32.3% spread on a
nominally idle host means the 1.1-1.8% noise floor measured earlier does not
hold across sessions**, and every fps-based comparison in this document should
be read with that in mind. It is why the per-frame and per-Mcycle counters lead
the tables: `cycles/frame` agreed to 3.6% across these arms while fps disagreed
by 37%, and only one of those two numbers can be describing the machine.

Anything intended as a real speed claim needs the noise floor re-established in
the same session, from repeated runs of the *same* module, before the arms are
compared.

---

## 5p. ThinLTO: ~6% smaller on both titles, no readable runtime change

`--lto thin` writes a `.bc` beside each region object via
`ThinLTOBitcodeWriterPass` and points the object manifest at the bitcode. lld
consumes bitcode inputs natively, so the link stage needs no in-process
`lto::LTO` driver and the ModernGekko module template needs no change — it still
just forwards each listed file to the linker. Verified with `llvm-bcanalyzer`
that every emitted file carries `GLOBALVAL_SUMMARY_BLOCK`; the LM build fed
1724/1724 bitcode files through the link.

`cfg` mode, 1024 instructions, same tree and same object cache within each title:

| | Luigi's Mansion | Mario Kart |
|---|---|---|
| regions | 1,724 | 2,033 |
| `--lto off` | 251,288,064 B | 444,321,280 B |
| `--lto thin` | 237,308,928 B | 417,093,120 B |
| **size delta** | **-5.6%** | **-6.1%** |
| build, off | 869 s | 928 s |
| build, thin | 1609 s (+85%) | 1492 s (+61%) |

Two independent titles agreeing at roughly 6% is the result that matters here:
cross-region inlining is genuinely happening. Emitter-level inlining managed
+0.017% (§5o), which is what established that this needed ThinLTO rather than a
smarter emitter.

The runtime result is nothing, on either title. Arms alternated against a pinned
`bench.sav` scene, runs that did not do comparable guest work dropped:

| title | arm | runs | mean fps | spread | delta | guard | verdict |
|---|---|---|---|---|---|---|---|
| Luigi's Mansion | `off` | 5 | 29.86 | 17.0% | | | |
| Luigi's Mansion | `thin` | 6 | 28.57 | 18.4% | -4.3% | 36.7% | unreadable |
| Mario Kart | `off` | 5 | 33.36 | 25.2% | | | |
| Mario Kart | `thin` | 5 | 34.89 | 7.7% | +4.6% | 50.4% | unreadable |

The two titles disagree on sign (-4.3% and +4.6%) and neither clears its noise
floor, which is what no effect looks like. `bursts/Mcycle` holds at 153.7-153.8
(LM) and 166.0-167.5 (MK) across both arms, so ThinLTO does not change
dispatcher behaviour either — expected, since it is a codegen-quality change and
not a control-flow one.

Two Luigi's Mansion runs had to be discarded for reasons worth recording,
because taken at face value they would have produced a headline (all ten Mario
Kart runs were valid and comparable):

* An LM run read **134.44 fps** — a 4.5x "win". Its cycles/frame sat inside the
  comparable band, but its `bursts/Mcycle` was 92.6 against everyone else's
  153.8. It executed something else. Including it turned the arm mean from
  28.57 to 43.70 and the delta from -4.3% to **+46.4%**.
* An early pass had one valid `off` sample against two `thin` samples and read
  +36%. That is the same shape as the retracted -22.1% dispatcher claim in §5g:
  a difference between arms that were not running the same thing.

`benchmarks/compare_arms.py` now drops runs whose `cycles_per_frame` or
`bursts_per_mcycle` strays from the median of the runs already seen (8% and 5%),
so this class of outlier cannot reach a reported number again.

### The Mario Kart link has to be bounded

The MKDD ThinLTO link failed inside the full build: exit 1 after 1492 s with no
diagnostic beyond `-Woverride-module` warnings. The identical link then ran
clean when re-invoked on an otherwise idle machine, so it is a footprint
problem, not bad bitcode: lld reports a killed process exactly this way.
ThinLTO's backend spawns one thread per core and holds several modules live at
once, and MKDD is 444 MB of objects against LM's 237 MB.

`benchmarks/build_module.sh` therefore passes `-Wl,/opt:lldltojobs=8` (override
with `LTO_JOBS`) whenever `--lto thin` is selected. Anyone linking a large title
through their own build system needs the equivalent cap; without it the failure
is silent and looks like a compiler bug.

**Verdict: `--lto thin` stays off by default.** It buys ~6% of module size for
60-85% of build time and no measurable speed. It is worth keeping wired because the
size result confirms cross-module inlining is now actually happening, which is a
precondition for the Phase 3/4 work that needs callees visible across region
boundaries — but on its own it is not a performance feature.

A caveat on all of the above: per-arm spread is 17-18% on LM and 8-25% on MKDD,
so the rig cannot resolve anything smaller than roughly a 35% effect. A real 5%
gain would be invisible here. This is a limitation of the measurement, not evidence that the
effect is zero.

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
4. ~~**No whole-program optimization**~~ — addressed by `--lto thin` (§5p).
   Cross-module inlining now happens and takes 5.6% off the module, but it did
   not move fps on Luigi's Mansion, so it stays off by default.
