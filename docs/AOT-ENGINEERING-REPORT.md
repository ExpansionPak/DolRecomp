# DolRecomp AOT 2.0 — Engineering Report

Branch `feature/llvm-aot-regions`, 73 commits on upstream
`fa0cf619e8d7eb8cba7eaf55267a12caaebb46aa`.

Every number here was measured on this host and is reproducible from the
commands in [AOT-PERFORMANCE-RESULTS.md](AOT-PERFORMANCE-RESULTS.md). Negative
and retracted results are included; nothing is extrapolated.

---

## 1. Headline

The region backend began the effort **60-70% behind the C backend** on Mario
Kart and ended at **parity with it**, on a module **4.9x smaller** than the one
it started with and builds **19x faster**.

| Mario Kart, one pinned scene | fps | module | build |
|---|---:|---:|---:|
| fixed-chunk `llvm` (the shipping LLVM path) | 29.80 | 320.0 MB | 351 s |
| `llvm-aot` as first built | 33.24 | 424.1 MB | ~930 s |
| **`llvm-aot` as it now stands** | **53.49** | **85.8 MB** | **44 s** |
| C backend (the semantic reference) | 50.63-53.01 | 65.3 MB | — |

Two changes produced essentially all of it. Everything else measured flat.

---

## 2. What actually worked

### 2.1 Guest state stays in `CPUState` (§5v)

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

### 2.2 `--memory-mode fast` (§5q)

`ram_size` is `GC_MAIN_RAM_SIZE` in this tree and in GXRuntime — assigned once,
carried across reset, never given another value — so the MEM1 bound folds to a
constant and the bounds check collapses to a single compare. The write journal
is null unless a runtime installs one, so its branch leaves the store path.

+6.7% / +6.7% / +5.0% fps across the three titles, 43 of 49 paired runs,
**p = 5.7e-08** combined.

Both assumptions are **verified once at dispatch entry**, not assumed. If either
fails the module refuses to run natively and the chassis keeps interpreting, so
a violated assumption costs speed and never guest memory.

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

* `benchmarks/compare_arms.py` drops runs whose `cycles_per_frame` or
  `bursts_per_mcycle` strays from the median of runs already seen. One Luigi's
  Mansion run read **134 fps** at 92.6 `bursts/Mcycle` against everyone else's
  153.8 — a different execution, not a fast one. Including it moved a −4.3%
  result to +46.4%.
* `benchmarks/paired_arms.py` compares alternating arms **pairwise** and reports
  a sign test. The unpaired 2x-spread guard is the right test for unpaired means
  and far too blunt for paired runs; where the two disagree, both are stated.

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
links DolRecomp's own `cpu.h`. Those three helpers were added to the vendored
runtimes; that edit lives outside this repository and will be lost if GXRuntime
is re-vendored.

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

* **AArch64 is not done and cannot be done here.** No native host. Cross-compile
  configures, but NEON paired-singles, fastmem addressing and the runtime ABI
  need a real execution environment. Recorded as not validatable, not estimated.
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
private ABI was built and measured negative (§5s); the win, if any, is in not
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
paired runtime A/B that reads +0.8% at p = 1.0 with `fallback` 0 throughout —
indistinguishable, which is what a no-op deletion should measure.

One bug was introduced and caught during that deletion. Removing a statement
under an unbraced `if` left the *following* `if` as its body, so `used_[MSR]`
stopped being set and `emitFPAvailable` loaded through a null pointer. The
compiler cannot see that shape; the two other instances in the same pass were
syntax errors and obvious. Found by bisecting against HEAD, after which the rest
of the deletion was audited for the same pattern.
