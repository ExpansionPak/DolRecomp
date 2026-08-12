# AOT Region Backend — Implementation Notes

Working branch: `feature/llvm-aot-regions`
Base: `ExpansionPak/DolRecomp` `main` @ `fa0cf61`

This document records what the codebase actually looks like, what was decided,
and what is still open. It is updated as phases land. Performance numbers live
in [AOT-PERFORMANCE-RESULTS.md](AOT-PERFORMANCE-RESULTS.md).

---

## 1. Current architecture findings

These were established by reading the tree at `fa0cf61`, not assumed from the
brief. Several assumptions in the original plan turned out to be **out of date**
— they are called out explicitly in §2 because they change what work is left.

### 1.1 Module map

| Area | Files | Notes |
|---|---|---|
| Frontend | `src/frontend/decoder.c` (65 KB), `container/{dol,rel,rpx,disc_extract}.c` | 236 opcodes; DOL/REL/RPX loading; REL self-relocation and cross-module imports |
| Analysis | `src/analysis/{embedded_data,smc,symbol_map}.c` | Embedded-data detection, SMC *detection only*, CodeWarrior MAP parsing |
| IR | `src/ir/dolir.{h,c}`, `dolir_builder.c` (63 KB) | Typed SSA-shaped IR, see §1.2 |
| C backend | `src/backend/{emitter,c_cfg,dispatch,codegen,symbols}.c` | Reference backend, split-chunk C |
| LLVM backend | `src/backend/llvm/*.{cpp,h}` (~90 KB) | See §1.3 |
| App | `src/app/{cli,pipeline,paths,database,setup}.c` | `pipeline.c` is 51 KB and owns chunking |

### 1.2 DolIR is already SSA-shaped

`DolIRFunction` holds blocks; blocks hold `DolIRInstruction` plus one
`DolIRTerminator`. The IR already has:

- `DOLIR_OP_PHI` and a value/type table (`value_types`, `value_count`)
- `DOLIR_OP_STATE_READ` / `DOLIR_OP_STATE_WRITE` against a flat
  `DolIRStateSlot` space covering GPR0–31, FPR0–31, **PS1_0–31**, PC, LR, CTR,
  CR, XER, FPSCR, MSR, SRR0/1, DAR, DSISR, EAR, HID2, TIMEBASE, SR0–15,
  GQR0–7, EXCEPTION, PROGRAM_EXCEPTION, RESERVE_ADDR, RESERVE_VALID, DOWNCOUNT
- An effect lattice: `READ_STATE`, `WRITE_STATE`, `READ_MEMORY`, `WRITE_MEMORY`,
  `MAY_EXIT`, `MAY_RAISE`, `BARRIER`
- Terminator kinds: `BRANCH`, `COND_BRANCH`, `INDIRECT`, `RETURN`, `SIDE_EXIT`,
  `FALLBACK`, `SYSTEM_CALL`, `RFI`, with a `linked` flag and both block-index
  and guest-address target forms

**Consequence:** Phase 2 does not need a new IR. It needs region-level
*container* structure above `DolIRFunction`, live-in/live-out sets, and an
explicit barrier representation. Building a fresh IR was rejected (§6).

### 1.3 The LLVM backend is not a naive chunk translator

`FunctionEmitter` (`llvm_function_emitter.{h,cpp}`) already implements a good
part of what the brief describes as missing:

- **Per-slot `AllocaInst` with `used_[]` / `dirty_[]` tracking** — guest state is
  held in allocas that LLVM's `mem2reg` promotes to SSA registers, and unread
  slots are never loaded. This is functionally close to "SSA state", *within one
  emitted function*.
- `materialize(pc)` / `syncState()` / `reloadState()` / `reloadUsedState()` and
  `continueAfterRuntimeBoundary()` — a partial-sync mechanism already exists.
- `emitBudgetGuard()` with `guard_cycles_` and a `guard_steps_` **termination
  backstop for zero-cycle loops** — the brief asks for exactly this; it is done.
- `directDestination()` / `externalDestination()` / `rangeFor()` — direct
  branching within a chunk and range-aware external transfer already exist.
- `scanLoopHeaders()`, `scanContinuations()` — loop headers and continuations
  are already recognised.
- IR-instrumentation PGO (`DOLRECOMP_LLVM_PGO=gen|use`) **with a positive
  staleness gate** (`DOLRECOMP_LLVM_PGO_STALE=error|warn|off`) that detects a
  profile diverged from the DOL rather than silently degrading.
- `dolllvm_codegen_fingerprint()` — a cache key over LLVM version, target CPU
  and features, reloc/code model and pass pipeline.

### 1.4 What *is* actually fixed-size

`src/backend/codegen.h`:

```c
#define EMIT_CHUNK_INSTRUCTIONS 4096u
```

Both backends split the code section into arbitrary 4096-instruction chunks.
`pipeline.c` drives this and hands each chunk to `dolir_build_chunk()`
(`test_dolir.c` confirms the entry point name). `DolLLVMFunctionRange` is passed
in so the emitter can tell intra-chunk from cross-chunk targets.

**This is the real defect.** A 4096-instruction boundary falls wherever it
falls: through a hot loop, between a hot caller and callee, mid-SCC. Everything
that crosses it degrades to a state materialization plus a dispatcher round
trip, regardless of how good the intra-chunk lowering is.

### 1.5 Runtime interface

`CPUState` (`src/cpu/cpu.h`) is the public ABI shared with ModernGekko: 32 GPRs,
32 FPRs, 32 `ps1` lanes, the SPR file, `ram`/`ram_size`, `exram`/`mem2` union,
`downcount`, and callback slots (`external_read/write`, `external_read32/write32`,
`external_pointer`, `instruction_fallback`, `host_call`, `cache_control`).
Generated functions are `void func_XXXXXXXX(CPUState*)`. Replacements go through
`dolrecomp_dispatch_replacement(CPUState*, u32 address)` behind
`DOLRECOMP_ENABLE_REPLACEMENTS`.

`g_mem_write_journal` is a **global function pointer checked on stores** — this
is the unconditional journal branch Phase 5 must remove from production builds.

### 1.6 Build and platform reality

- CMake ≥ 3.16; C11 core, C++17 only when `DOLRECOMP_ENABLE_LLVM=ON`.
- **LLVM is pinned to 19 or 20** (`CMakeLists.txt` hard-errors outside that).
  The dev machine's `C:\Program Files\LLVM` is clang 22 and ships no CMake
  package; the usable toolchain is `clang+llvm-20.1.8-x86_64-pc-windows-msvc`.
- Baseline: **19/19 ctest pass** with LLVM enabled (20/20 after the Phase 0
  test). Recorded in the results doc.

---

## 2. Assumptions in the brief that the code contradicts

Correcting these matters, because they move effort from "build" to "extend".

| Brief assumes | Reality | Effect |
|---|---|---|
| Guest state is repeatedly loaded/stored through `CPUState` | Already allocas + `used_`/`dirty_`, promoted by mem2reg | Phase 2 shrinks to *cross-region* state, live-in/live-out ABI, and a unified barrier |
| No termination backstop for zero-cycle loops | `guard_steps_` exists | Preserve, don't build |
| PGO needs adding | Instrumentation PGO + staleness gate already upstream | Phase 6 extends it into *region formation*, not into existing pass weighting |
| Cache key needs creating | `dolllvm_codegen_fingerprint()` exists | Phase 6 *widens* it (region plan, LTO, mod policy, memory mode, PGO hash) |
| Direct branch lowering missing | Exists within a chunk | Phase 3 is about crossing *region* boundaries |

The genuinely missing pieces are: CFG-aware region formation (§1.4), a
cross-region internal ABI, indirect/BLR specialization, memory access
classification, and bitcode/ThinLTO.

---

## 3. Compatibility requirements (non-negotiable)

1. C backend stays the semantic reference and differential-testing target.
2. Existing fixed-chunk LLVM path stays available until the region path reaches
   correctness **and** performance parity. New mode is additive: `llvm-aot`.
3. **No runtime guest-code generation.** Inline caches update *data* only —
   target pointers, counters, metadata. No executable memory is written.
4. ModernGekko public ABI preserved: `void func_XXXXXXXX(CPUState*)`,
   `dolrecomp_dispatch_replacement`, hooks, mods, callbacks, exceptions.
5. Exact PowerPC semantics — paired-single, FP rounding and exceptional values,
   CR, XER CA/OV, reservations, exceptions, endianness, address wrapping,
   MEM1/MEM2, MMIO, REL relocations, SMC detection.
6. No copyrighted binaries committed. CI runs on synthetic fixtures only.
7. C stays C, C++ stays confined to the LLVM backend. No Rust.

---

## 4. Design decisions

### D1 — Regions are a layer *above* `DolIRFunction`, not a replacement
A region owns an ordered set of `DolIRFunction`s plus edge metadata. Rejected
alternative in §6.

### D2 — One auditable materialization barrier
A single `DolIRBarrier` record (kind, affected slots, guest PC) rather than ad
hoc flushes. Every barrier site must be attributable to one of: unknown
indirect transfer, exception/interrupt, MMIO or state-observing helper, mod hook
or replacement boundary, debugger/instrumentation, dispatcher return, explicit
compatibility boundary, SMC handling, unsupported-instruction fallback.

### D3 — Private internal ABI via `fastcc` + LLVM aggregates
Public wrapper keeps `void func_XXXXXXXX(CPUState*)`. Internal region entries use
`fastcc` and pass only live state, returning multi-value aggregates. This avoids
freezing a huge C-style signature and lets ThinLTO inline across regions.

### D4 — Instrumentation is compile-time-gated in generated code
Compile-side counters are always collected (negligible against an LLVM run) and
only *written* with `--perf-report`. Runtime counters live behind
`DOLRECOMP_PERF` in the generated `dolrecomp_perf.h` and compile to `((void)0)`
otherwise, so a shipping module carries no counter store on a memory fast path.
Counters are plain `u64` assuming the single generated guest CPU thread;
`DOLRECOMP_PERF_ATOMIC` is available for multi-threaded hosts.

### D5 — One X-macro is the source of truth for counters
`DOLRECOMP_PERF_COUNTERS` in `src/common/perf.h` generates the struct, the JSON
object, the console table, the reset path and the generated header together, so
they cannot drift. `test_perf.c` asserts compile-side counters do **not** leak
into the guest module's header.

### D6 — Guarded fastmem before mapped fastmem
Target-independent guarded fast paths land and get benchmarked first. Reserved
address-space / fault-assisted fastmem is a later, optional, host-gated mode.
Memory work does not block on a perfect signal-handler design.

---

## 5. Phase checklist

- [x] **Phase 0a** — counter subsystem, `--perf-report` JSON + console summary,
      generated `dolrecomp_perf.h`, `test_perf` (6 cases). 20/20 ctest green.
- [ ] **Phase 0b** — benchmark harness + synthetic benchmarks
- [ ] **Phase 0c** — untouched baseline numbers recorded
- [ ] **Phase 1** — whole-title CFG/call-graph model; region planner
      (`fixed`/`function`/`cfg`/`pgo`); `--emit-region-report`
- [ ] **Phase 2** — region SSA state, live-in/out, barrier framework, internal ABI
- [ ] **Phase 3** — direct cross-region calls, tail transfers, mod policies
- [ ] **Phase 4** — indirect target sets, jump tables, per-site caches, BLR
      shadow returns, O(1) fallback dispatch
- [ ] **Phase 5** — memory access classification, const RAM/MMIO, guarded
      fastmem, journaling modes
- [ ] **Phase 6** — bitcode, ThinLTO, PGO-driven regions, wider cache keys,
      AArch64 Linux, Apple Silicon
- [ ] **Final** — performance gates, engineering report

---

## 6. Rejected approaches

**Replacing DolIR with a new region IR.** DolIR already has PHIs, a typed value
table, a state-slot space that covers paired singles and the full SPR set, and
an effect lattice. `dolir_builder.c` is 63 KB of instruction-accurate lowering
carrying the exact FP/paired-single semantics the project exists to preserve.
Rewriting it would put every semantic guarantee back on the table to buy
structure that can be added above it instead.

**Making `llvm-aot` the default immediately.** The brief requires the fixed path
stay available until parity is proven. Default flips only after the differential
suite and the performance gates are both green.

**Runtime recompilation for SMC.** Out of scope by constraint. SMC stays
detected and conservatively routed; the build report carries SMC status.

**Treating any table-shaped data region as a jump table.** Requires negative
tests before any recovery is trusted; misidentification silently corrupts
control flow.

---

## 7. Known risks

| Risk | Mitigation |
|---|---|
| Region merging changes mod interception points | `--mod-policy compatible` default; sealed mode is explicit opt-in and warns |
| ThinLTO internalizes a symbol a mod patches | Patchability metadata on public wrappers; link statistics report every function blocked from direct linking and why |
| Inline caches racing under a multi-threaded host | Data-only caches, documented thread policy, invalidation hook on replacement change |
| Code-size blowup from inlining hot callees | Region size limits, hot/cold splitting, <25% growth target with documented exceptions |
| Profile treated as exhaustive | Specialized targets always fall through to the generic dispatcher |
| Clean ThinLTO build time regressing dev loop | Non-LTO path retained; cache-hit build times recorded separately |

---

## 8. Open questions / needs manual confirmation

1. **Base of record.** This branch sits on upstream `ExpansionPak/DolRecomp`
   `main`. The MKDD project instead pins its submodules to
   `dougchansan/recomp-bench` branches `mkdd/dolrecomp` and `mkdd/moderngekko`,
   whose `.gitmodules` states they carry local work absent upstream (4-player
   local multiplayer, savestate menu, live internal resolution, DSP savestate
   stamping, ultrawide, LLVM instrumentation). The LLVM-instrumentation part is
   already upstream at `fa0cf61`; the rest is not. **Confirm whether this work
   should be rebased onto `mkdd/dolrecomp`.**
2. **Upstream contribution policy.** `README.md` states: "No AI code is used in
   DolRecomp. This is human hand-made project." This branch is
   AI-assisted. That is a decision for the repository owner about
   whether/how this lands upstream; it does not affect a private fork.
3. Whether `--mod-policy sealed` should ever be selectable for a shipping title
   build, or stay a benchmarking-only mode.
4. Which MMIO ranges ModernGekko wants specialized at compile time versus kept
   behind the generic callback.

---

## 9. Areas requiring platform access not available on the dev machine

Recorded so they are not silently reported as done:

- **AArch64 Linux** — no Linux host available here; cross-compilation can be
  configured but a real execution environment is required to validate NEON
  paired-single lowering, fastmem address calculation and the runtime ABI.
- **Apple Silicon arm64 / x86-64 macOS** — no macOS host available.
- **ThreadSanitizer / Valgrind** — not available on the Windows dev host.

These need either CI runners or a second machine before their deliverables can
be marked complete.
