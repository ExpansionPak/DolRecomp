# DolRecomp AOT 2.0 — Engineering Report

Branch `feature/llvm-aot-regions`, 73 commits on upstream
`fa0cf619e8d7eb8cba7eaf55267a12caaebb46aa`.

Every number here was measured on this host. Negative and retracted results are
included; nothing is extrapolated.

Method: each configuration is built into its own directory (the module cache
keys on backend and binary hash, not on region settings, so two configurations
sharing an output directory silently collide). Throughput is frames over wall
time with Dolphin's throttle disabled, since `fps` in `status.txt` stays 0
headless and pins at 1.00 windowed. Arms alternate and are compared pairwise
with a sign test, dropping any run whose `cycles_per_frame` or
`bursts_per_mcycle` strays from the median -- those executed a different scene.
Titles are supplied locally and none is committed.

---

## 1. Headline

Two targets, two different stories, and the second one is not a footnote.

**On x86-64** the region backend began **60-70% behind the C backend** on Mario
Kart and ended at **parity with it**, on a module **4.9x smaller** than the one
it started with and builds **19x faster**.

| Mario Kart, one pinned scene, x86-64 | fps | module | build |
|---|---:|---:|---:|
| fixed-chunk `llvm` (the shipping LLVM path) | 29.80 | 320.0 MB | 351 s |
| `llvm-aot` as first built | 33.24 | 424.1 MB | ~930 s |
| **`llvm-aot` as it now stands** | **53.49** | **85.8 MB** | **44 s** |
| C backend (the semantic reference) | 50.63-53.01 | 65.3 MB | — |

Two changes produced essentially all of it. Everything else measured flat.

Profile-guided optimisation then adds **+5.6% to +18.9%** on top, validated on
three titles and 34 of 36 paired runs (p = 1.9e-08), two of them with held-out
measurement scenes. It needs a per-title profile, so it is a build-pipeline
step rather than a default (2.3).

**On AArch64 it is about 2x behind Dolphin's own JIT**, and that gap is the
honest headline for that target. Measured on a Raspberry Pi 4, Luigi's Mansion
in the mansion foyer, single core, five alternating pairs per comparison with
`CPUThread` pinned per run:

| Luigi's Mansion, mansion foyer, AArch64 | fps | vs JIT |
|---|---:|---:|
| `llvm-aot` as this branch stands | 4.76 | 2.77x behind |
| with alias metadata (2.4) | 5.48 | 2.41x behind |
| with alias metadata and a per-title profile | 6.71 | 1.97x behind |
| Dolphin's `JitArm64`, same scene | **13.20** | — |

Nothing structural explains that gap: the JIT keeps guest registers in host
registers across a block, decrements its cycle counter once per block, and uses
fastmem instead of a bounds check per access. Section 7 has the measurements
behind each. The x86-64 result above does not transfer to this target, and
neither does its parity-with-C conclusion -- the AArch64 comparison against the
C backend is 5.69 against 5.86, a 3.0% deficit rather than parity.

---

## 2. What actually worked

### 2.1 Guest state stays in `CPUState`

The emitter used to promote every guest slot it touched to an alloca at region
entry. That hands the register allocator far more simultaneously-live values
than x86-64 has registers, so it spilled them straight back to the stack —
replacing "load from `CPUState` when needed" with "load at entry, store to
stack, reload from stack": one extra copy, plus a large frame.

| instructions touching the stack | |
|---|---:|
| promoting `llvm-aot` | 29.1-33.5% |
| C backend | 3.2-5.0% |
| **after the change** | **2.5%** |

| | fps | pairs | sign test |
|---|---:|---:|---:|
| Mario Kart | **+60.9%** | — | parity with C backend |
| Luigi's Mansion | **+26.7%** | 6/6 | p = 0.0312 |
| Skyward Sword | **+30.9%** | 13/13 | p = 0.0002 |

Gains order by how much spill each title had to lose: Mario Kart's promoting
module was the largest and gains most, Luigi's Mansion the smallest and gains
least. That is what the mechanism predicts, and it is the reason to believe the
mechanism rather than only the outcome.

### 2.2 `--memory-mode fast`

`ram_size` is `GC_MAIN_RAM_SIZE` in this tree and in GXRuntime — assigned once,
carried across reset, never given another value — so the MEM1 bound folds to a
constant and the bounds check collapses to a single compare. The write journal
is null unless a runtime installs one, so its branch leaves the store path.

+6.7% / +6.7% / +5.0% fps across the three titles, 43 of 49 paired runs,
**p = 5.7e-08** combined.

Both assumptions are **verified once at dispatch entry**, not assumed. If either
fails the module refuses to run natively and the chassis keeps interpreting, so
a violated assumption costs speed and never guest memory.

### 2.3 Profile-guided optimisation

`DOLRECOMP_LLVM_PGO=gen` instruments, a run writes a `.profraw`, and
`DOLRECOMP_LLVM_PGO=use` with `DOLRECOMP_LLVM_PROFILE` applies the merged
profile. Codegen PGO had never been measured before this -- the only PGO
previously tested was region *seeding*, which is unrelated and was a dead end.

| title | scene design | fps | pairs | sign test |
|---|---|---|---|---|
| Mario Kart | **held out** (5 courses profiled, 2 measured) | +12.5% / +18.9% | 14/16 | p = 0.0042 |
| Luigi's Mansion | **held out** (`foyer` profiled, `bench` measured) | **+5.6%** | 10/10 | p = 0.0020 |
| Skyward Sword | same scene (only one gameplay state exists) | **+11.9%** | 10/10 | p = 0.0020 |

Combined **34 of 36 pairs, p = 1.9e-08**, `fallback` 0 on every run.

It carries to AArch64, measured the same way on a Raspberry Pi 4 with
`CPUThread` pinned per run, on top of the alias metadata and MEM1 hoist:

| title, AArch64 | fps | pairs |
|---|---:|---:|
| Luigi's Mansion, mansion foyer | 5.50 to 6.71, **+21.9%** | 5/5 |
| Pokemon Colosseum, intro sequence | 15.69 to 19.65, **+25.2%** | 5/5 |

Both are **same-scene** and therefore upper bounds, unlike the held-out x86-64
figures above -- the honest comparison for Luigi's Mansion is the +5.6% held-out
row, not these. One Colosseum pair was discarded for sampling a scene
transition: fps standard deviation 3.24 and 6.06 against 0.08-0.23 everywhere
else. Including it the figure is +27.9%, so the exclusion does not carry the
result.

Two of the three use held-out measurement scenes, so generalisation is measured
rather than assumed. Skyward Sword could not be: its only savestates are
`gameplay` and `title`, and a title screen shares almost no code with gameplay.

The spread tracks module size, as block placement predicts and matching the
ordering seen in 2.1: Luigi's Mansion is the smallest module and gains least.

It pays more here than it would have before 2.1, because with the spill gone
what remains is dominated by the MEM1/MEM2/slow-path branch chain that every
guest load and store walks.

**Toolchain note.** The profile runtime must come from the same LLVM that
instruments. A system clang newer than the backend's LLVM produces a `.profraw`
the backend refuses to read, and the error appears at the *use* build, long
after the profiling run is over. `build_module.sh` derives it from `LLVM_DIR`.

Not a default in the sense the other options are, since it needs a per-title
profile. Unmeasured: whether the gain holds on a scene much heavier than
anything in the profile set.

### 2.4 Guest RAM and `CPUState` declared disjoint

Every guest register access goes through `CPUState`, and guest memory is
reached through a pointer loaded out of `CPUState`. Nothing told LLVM the two
are different objects, so a guest store looked like it might clobber `cr` or a
gpr, and the next instruction that read one reloaded it.

Two alias scopes in one domain say otherwise: `CPUState` accesses are tagged
`cpustate`/noalias-`guestmem`, guest RAM accesses the reverse. Every `CPUState`
access already funnels through `bytePtr()` and every guest RAM access through
`endianLoad`/`endianStore`, so this is four tag sites. Anything untagged --
helper calls, MMIO, external reads and writes -- stays conservative and
may-alias, which is what keeps those paths correct without enumerating them.
The two budget counters take `NoAlias` as well: they are allocas created in the
wrapper and passed only to the body, but the guard updates them inside the
hottest loops.

**+15.0% on AArch64** (Luigi's Mansion foyer, single core, 5 of 5 pairs), and
the module is 1.44 MB smaller -- redundant loads and stores going away rather
than a layout accident. Unmeasured on x86-64, where the same reasoning applies
but the register file is not the constraint it is here.

It generalises, and by more than the title it came from. Pokemon Colosseum
shares no code with Luigi's Mansion and was never profiled while developing
this; it gains **+22.6%** from this change and the MEM1 hoist (2.5) together,
5 of 5 pairs, on a module 5.2% smaller:

| Colosseum, intro sequence, single core | fps |
|---|---:|
| before both changes | 12.84 |
| after both changes | **15.74** |

Luigi's Mansion gains about 17% from the same pair. Frame sizes match across
arms at ~1.38 MB and guest cycles rise from 21.1 to 26.4 billion, so this is
more guest work per wall-second rather than a lighter scene. Deriving both
changes from one title's profile did not fit them to it.

The change had to be made three times before it measured anything. The object
cache does not hash the emitter source, so the first two attempts returned
byte-identical objects and would have been written up as "no effect" had the
hashes not been checked. `DOLLLVM_CACHE_VERSION` exists for this and has now
bitten four times.

### 2.5 MEM1's base loaded once per region

Every guest memory access loaded `ctx->ram` before indexing it. The pointer is
fixed for the whole burst -- the chassis sets it before dispatch and nothing the
module can call reallocates MEM1 -- but LLVM cannot hoist that load, because the
alias scope in 2.4 covers all of `CPUState` as one blob and a store to `cr` or a
gpr blocks it.

Loading it once in the prologue is **+1.7%** on Luigi's Mansion (5 of 5 pairs)
and 590 KB off the module. Modest on its own because LLVM already CSEs these
loads within a block; what this adds is the hoist across blocks and loops, which
it could not prove for itself.

MEM2 deliberately keeps its per-access load. `cpu_alloc_mem2` can allocate exram
after a region has started running, so caching that pointer would be a bug
rather than a missed optimisation.

---

## 3. What did not work

Ten interventions measured flat or negative. They are listed because the pattern
is the finding.

| | result |
|---|---|
| Larger regions (256/512/1024) | dispatcher rate flat within 1%, 33 runs |
| PGO-driven region formation | plans a different program, moves nothing |
| Static crossing count as a proxy | falls 21% while runtime rate moves 0.8% |
| `bctr` / jump-table specialisation | 0.17% of weighted execution |
| Address-adjacency merging | 2.2x build time, +6.3% size |
| Barrier store narrowing | −4.3% size for +50% build; two earlier versions unsound |
| Emitter-level cross-region inlining | +0.017% module size |
| ThinLTO (`--lto thin`) | ~6% smaller, runtime effect **title-dependent** |
| Register-passed GPR3-GPR10 | **−2.3% fps**, +6.1% size |
| LLVM's own `-O3` pipeline | module +0.07%, spill traffic **unchanged** |

Most of these reshape control flow that was already direct calls. They were
rearranging a structure whose dominant cost was the structure itself. The `-O3`
result is the cleanest proof: swapping in the exact pipeline clang uses changed
the spill ratio by nothing, because no pass can undo a live set larger than the
machine.

**ThinLTO is the one to be careful about.** It is ~6% smaller on both titles
tested, but layered on top of the memory mode it *costs* 4.1% on Luigi's Mansion
and *gains* 2.5% on Mario Kart — both statistically significant, in opposite
directions. It stays off by default, and a per-title measurement is the only way
to know which side a given title falls on.

---

## 4. The measurement, which had to be fixed twice

Three claims were made and retracted during this work. The corrections matter
more than the claims.

* **A −22.1% dispatcher improvement** came from two Luigi's Mansion arms running
  different scenes. Retracted; a comparability guard on cycles/frame was added.
* **A −12% size / −23% build win** from barrier narrowing was reported before
  the benchmark returned. The module did not run — Mario Kart hung at boot.
  Real, and worthless.
* **A +20.8% C-versus-LLVM figure** was assembled from noise: the same-backend
  comparability filter is invalid across backends (`bursts/Mcycle` differs
  because 182 chunks is not 2,033 regions; `cycles/frame` differs because the
  backends charge guest cycles differently). Applied naively it kept two
  outliers and left one arm at n=1. The real figure was +59.5%.

Two guards came out of this and are now in the tooling:

* **Outlier rejection on the invariants, not on fps.** A run whose
  `cycles_per_frame` or `bursts_per_mcycle` strays from the median executed a
  different scene and is dropped. One Luigi's Mansion run read **134 fps** at
  92.6 `bursts/Mcycle` against everyone else's 153.8 — a different execution,
  not a fast one. Including it moved a −4.3% result to +46.4%.
* **Pairwise comparison with a sign test.** Arms alternate, so run *i* of each
  saw the same machine state; comparing within pairs cancels the drift behind
  the 17-25% unpaired spreads. The unpaired 2x-spread guard is right for
  unpaired means and far too blunt for paired runs; where the two disagree,
  both are stated.
* Neither invariant survives crossing *backends* — the C module has 182 chunks
  against 2,033 regions, and the backends charge guest cycles differently — so
  cross-backend comparisons reject outliers within each arm against its own
  median instead.

Measuring on the Pi added five more ways to produce a confident wrong number,
all of which yield a plausible framerate rather than an obvious failure:

* **A module can be loaded and still not execute.** The largest error in this
  report was not a bad statistic but a bad subject: every backend comparison
  taken on the Pi measured Dolphin's JIT against itself, because the static
  module stopped dispatching after boot while still being loaded, named in the
  log, and reporting healthy counters. Nothing in the framerate, the scene
  guards or the pairing could have caught it. What caught it was asking perf
  which shared object the samples landed in, and finding the module absent. Any
  comparison of two backends should establish that the code under test is
  running before it reports a number -- a profile by object, or a counter that
  demonstrably advances with wall time.

* **A capped scene cannot show a difference.** Both backends hold Luigi's
  Mansion's title screen at the 59.9 fps cap, so any comparison taken there
  reports parity no matter what the backends do. The scene has to be one that
  is actually CPU-bound — the mansion foyer runs 13 fps single-core — before
  the measurement can say anything at all.
* **Uncapped, the arms stop looking at the same thing.** With the speed limiter
  off the faster arm is further into the game at every wall-clock instant, so
  it is rendering different scenery; sampling more instants does not fix it.
  Frame sizes in one such run clustered at 0.64 MB / 43 fps and 1.2 MB / 13 fps
  — two scenes, averaged into one meaningless mean.
* **Booting from a savestate silently bypasses the module.** Loading state at
  boot leaves `native=0 bursts=0 cycles=0`: the recompiled code never executes
  and the emulator's own core runs the game. Both arms then measure the same
  thing and agree beautifully. Any run whose counters are zero has to be
  discarded rather than averaged.
* **A scripted controller can pause the game.** Driving the menus by replaying
  a fixed sequence of button presses kept pressing Start after the game had
  started; Start opens the pause menu, and a paused game renders cheaply at the
  frame cap. The fix was to make the drive closed-loop — read the screen, press
  Start only when the frame is small or the game is provably paused, and stop
  pressing once gameplay is detected.

The sample-count lesson also repeated, in the direction that matters. A first
dual-core batch had `llvm-aot` ahead in all three pairs, +2.0%, which is the
kind of result that gets written down. Extending the same comparison to five
pairs reversed the sign to −0.9%. Three pairs all pointing one way is p = 0.125
and cannot carry a claim, however tidy it looks.

---

## 5. The finding that reframed the effort

**The C backend's throughput was never measured until late.** Every runtime
number in this project compared LLVM builds to other LLVM builds. The brief
designates the C backend the semantic reference; nothing was ever positioned
against it, so "faster than the previous `llvm-aot` build" silently stood in for
"fast".

When it was finally measured, the C backend was **60-70% ahead of both LLVM
configurations**, on a module 4.9x smaller than even the fixed-chunk build. That
single comparison explained eight flat results at once and pointed straight at
the spill.

It also required repairing the C backend to measure it at all: three inline
helpers (`ppc_fp_available_inline`, `ppc_psq_load_inline`,
`ppc_psq_store_inline`) exist in DolRecomp's `cpu.h` but in no vendored
GXRuntime here, so the C backend **would not build against any ModernGekko
checkout on this machine**. The differential suite never noticed, because it
links DolRecomp's own `cpu.h`.

The first fix added those helpers to the vendored runtimes. That was wrong: the
vendored GXRuntime is a nested git submodule, so the edit would have been
discarded by any `git submodule update`. The emitter now calls what the runtime
actually declares instead -- the paired-single wrappers were pure pass-throughs,
and the FP one's MSR[FP] fast path is spelled out in the generated C. Verified
by reverting all three GXRuntime edits and building Mario Kart's C module
against pristine headers, so nothing outside this repository has to be
maintained.

**The lesson, stated plainly: measure against the reference backend in Phase 0.**

---

## 6. Compliance with the brief

| Constraint | Status |
|---|---|
| No Rust; C for everything but the LLVM backend | Held. C++ confined to `src/backend/llvm/`. |
| C backend not replaced | Held, and now measured — it is the performance reference too. |
| Fixed-chunk LLVM path retained | Held. `--backend llvm` unchanged and still builds. |
| `llvm-aot` reaches parity before replacing it | **Exceeded**: 53.49 vs 29.80 fps, +79.5%. |
| No runtime guest-code generation | Held. No executable memory is written. |
| ModernGekko ABI preserved | Held. `void func_XXXXXXXX(CPUState*)` wrappers, dispatcher, hooks and `staticrecomp_get_module` unchanged. |
| Exact PowerPC semantics | Differential suite green across 5 seeds; 23/23 ctest. |
| No copyrighted binaries committed | Held. Titles are local; CI uses synthetic fixtures. |
| Tests not weakened to pass | Held. Test count rose 19 → 23; coverage was **added** (MEM1 boundary, differential call paths). |

Two compatibility details are worth naming:

* **Patchability is now explicit.** A direct cross-region call bypasses
  `dolrecomp_dispatch_replacement`. That is sound today only because
  ModernGekko never defines `DOLRECOMP_ENABLE_REPLACEMENTS`. Setting it now
  suppresses every direct external transfer *and* emits the matching header
  define, so the two cannot disagree. The previous state was a mod that would
  install and silently do nothing.
* **Lockstep needs `--memory-mode safe`.** ModernGekko's lockstep verifier is
  the one consumer that installs a write journal. A fast-mode module makes it
  inert, and says so on stderr rather than failing quietly.

---

## 7. What is owed

* **AArch64 runs, but nothing has been measured about the backends there.**
  This report previously claimed parity between `llvm-aot` and the C backend on
  a Raspberry Pi 4. That claim is withdrawn: both arms were measured while the
  static module was not executing the game.

  Cross-compilation itself works. An x86-64 Windows host emits 1,829 AArch64
  objects for Luigi's Mansion in 30 s, the Pi links them in 7.3 s, the module
  loads, and the title plays. What does not happen is execution. Profiling the
  emulator in the measured scene, sampled by shared object:

  | | share of CPU |
  |---|---:|
  | Dolphin's `JitArm64` generated code | 41.6% |
  | `moderngekko-run` itself | 33.6% |
  | `gGLME01_recomp.so` | **absent** |

  The list reaches 0.05% before the module appears at all, and the module's own
  counters agree: `native=1641 bursts=77 cycles=418897`, unchanged across 55 s,
  180 s and 400 s runs, in the attract loop and in the mansion foyer alike. A
  Gekko issues 486 million cycles a second; the module accounts for roughly 419
  thousand of them, once, during boot, and then never runs again.

  The withdrawn figures -- 13.20 vs 13.03 single-core, 19.29 vs 19.46 dual-core
  -- were therefore Dolphin's JIT measured against itself. That is why every
  configuration landed on parity, and why a three-pair advantage reversed sign
  at five pairs: two modules that never execute cannot differ.

  Why dispatch stopped after boot is now known, and it was not this project's
  bug: the ModernGekko build on that machine had no static-recomp integration in
  its ARM64 JIT at all. `Jit64` calls `StaticRecompShouldYieldAt` at block
  boundaries so the static core regains control; the `JitArm64` in that tree
  never did, so the first fall back into the JIT kept the CPU permanently.
  Against a current runtime the same module executes 16.3 billion guest cycles
  where it previously executed 419 thousand, and the numbers below are the
  first on this target with the recompiled code actually running.

  | Luigi's Mansion, mansion foyer, single core | fps |
  |---|---:|
  | `llvm-aot` as this branch stands | 4.76 |
  | with alias metadata (2.4) | **5.48** |
  | with alias metadata and a per-title profile | **6.71** |
  | Dolphin's `JitArm64`, same scene | 13.20 |

  Five alternating pairs per comparison, twenty samples per run, `CPUThread`
  pinned per run and recorded in each result line. +15.0% and +21.9%, 5 of 5
  pairs each. The static recompiler remains about **2x behind the JIT** on this
  target, and the profile above is same-scene, so it is an upper bound.

  What the JIT does that this does not is not structural. Its register cache
  keeps guest registers in host registers across a block where every read here
  goes to `CPUState`; it decrements a cycle counter once per block where the
  budget guard here is per loop header; and fastmem removes the bounds check
  that costs four instructions per guest access. In the measured hot loop those
  came to roughly 67% and 28% of module time, against 0.3% for the guest's own
  memory traffic.

  Cross-compiling from an x86-64 build machine needs the AArch64 target
  registered and its CodeGen/AsmParser/Desc/Info components linked; relaxing the
  triple guard alone leaves `lookupTarget` reporting a missing-component problem
  as an unsupported triple. That is on PR #15, with a test that emits for
  `aarch64-unknown-linux-gnu` and checks `e_machine`.
* **Dual core is worth more than any change measured here, and is not ours.**
  Dolphin's CPU/GPU thread split is **+15.6%** on Colosseum with the full stack
  applied (19.71 to 22.78 fps, 4 of 4 pairs, same module, core mode alternated),
  and about +16% on Luigi's Mansion. It is a runtime setting with correctness
  tradeoffs in timing-sensitive titles, so it is context for anyone reading the
  AArch64 numbers rather than something this branch changes -- and it is why
  every figure in this report names its core mode.
* **`stfs` diverges between backends** on overflow and denormal inputs. Excluded
  from the default differential pool, reproduces with `--stfs`. One backend is
  wrong about Gekko and it is not yet known which. This is the oldest open
  correctness item.
* **An unexplained dispatcher-rate difference**: the state-in-memory arm reads
  168.7 `bursts/Mcycle` against the C backend's 173.0 on Mario Kart. It may be a
  slightly divergent scene; it is not understood.
* **Luigi's Mansion is a noisy rig** — six of twelve pairs were rejected in one
  comparison. Results there rest on fewer samples than the other two titles.
* **The object cache key does not hash the emitter source.** Every codegen
  change still requires bumping `DOLLLVM_CACHE_VERSION` by hand, or measurements
  silently compare identical binaries. This bit three times.

### Where the next gain probably is

The C backend is still 24% smaller (65.3 MB against 85.8 MB) at equal speed, and
it gets ThinLTO across the whole module while region objects bypass it entirely
— they are `EXTERNAL_OBJECT` pre-built natives, which CMake's IPO property does
not touch. Whether closing that gap buys anything is unmeasured, and on current
evidence a smaller module does not reliably mean a faster one.

The per-call state round trip remains the largest identified cost: calls are
7.79% and returns 10.95% of weighted execution. The entry-side half of the
private ABI was built and measured negative (section 3); the win, if any, is in not
materializing on the way *out*, which needs a staleness analysis this emitter
has got wrong twice.

---

## 8. Code health

Removing the promoting emitter made 584 net lines unreachable, all now deleted:
the materialization barriers, both dataflow analyses and their buffers, the four
sync/reload helpers and their 23 call sites, and two measured-and-rejected
feature flags. `materialize()` is now the guest PC and the cycles owed.

Verified three ways: **byte-identical IR** from the test fixture before and
after, a full Mario Kart build at **exactly the same 85,770,752 bytes**, and a
paired runtime A/B over 7 pairs reading −0.7% mean at p = 1.0, 3 of 7 favouring
the post-deletion module, `fallback` 0 on every run — indistinguishable, which
is what a no-op deletion should measure.

One bug was introduced and caught during that deletion. Removing a statement
under an unbraced `if` left the *following* `if` as its body, so `used_[MSR]`
stopped being set and `emitFPAvailable` loaded through a null pointer. The
compiler cannot see that shape; the two other instances in the same pass were
syntax errors and obvious. Found by bisecting against HEAD, after which the rest
of the deletion was audited for the same pattern.
