#include "backend/llvm/llvm_function_emitter.h"
#include "cpu/cpu.h"

#include <cstdio>
#include <cstdlib>

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>

namespace dolllvm {

using namespace llvm;

// Off by default: measured -4.3% module size for +50% build time, with
// bursts/Mcycle unchanged. Correct, but not worth its cost as it stands. Kept
// because the indirect-switch edge fix it forced is the prerequisite for
// passing live state in registers.
//   DOLRECOMP_NARROW_BARRIERS=1   narrow barrier stores by reaching-writes
static bool narrowBarriers() {
  static const bool enabled = [] {
    const char *value = std::getenv("DOLRECOMP_NARROW_BARRIERS");
    return value && value[0] == '1';
  }();
  return enabled;
}

// Off by default, and the reason is measured rather than assumed: chunk size
// drives how much guest state the register allocator keeps live, and that is
// what made 1024-instruction chunks cost 3x the code size of 128 for a third
// less speed (pipeline.c, LLVM-EXPERIMENTS E002/E003). Inlining a callee into
// its caller has the same shape -- it merges two live ranges.
//
// Worth measuring precisely because fastcc makes it possible: with the internal
// bodies no longer NoInline, LLVM can inline a small or hot callee across a
// region boundary, which is Phase 3's direct-linking benefit rather than a
// dispatcher saving.
//   DOLRECOMP_INLINE_REGIONS=1    let LLVM inline internal region bodies
static bool inlineRegions() {
  static const bool enabled = [] {
    const char *value = std::getenv("DOLRECOMP_INLINE_REGIONS");
    return value && value[0] == '1';
  }();
  return enabled;
}

FunctionEmitter::FunctionEmitter(LLVMContext &context, Module &module,
                                 const DolIRFunction &source,
                                 const DolLLVMFunctionRange *ranges,
                                 u32 range_count)
    : context_(context), module_(module), source_(source), builder_(context),
      ranges_(ranges), range_count_(range_count) {}

bool FunctionEmitter::emit(raw_ostream &diagnostics) {
  auto *pointer = PointerType::getUnqual(context_);
  auto *type = FunctionType::get(Type::getVoidTy(context_),
                                 {pointer, pointer, pointer}, false);
  const std::string bodyName = std::string(source_.name) + "_budget";
  function_ = module_.getFunction(bodyName);
  if (!function_)
    function_ = Function::Create(type, GlobalValue::ExternalLinkage, bodyName,
                                 module_);
  if (function_->getFunctionType() != type || !function_->empty()) {
    diagnostics << "dolllvm: conflicting native body " << bodyName << "\n";
    return false;
  }
  // The internal body uses fastcc; the public wrapper below keeps the C
  // convention because that is the ModernGekko ABI and mods, hooks and the
  // dispatcher all call through it.
  //
  // This is the first step of the private internal ABI (D3). On its own it only
  // frees the register allocator to place the three pointer arguments, which is
  // marginal. The substantive version passes live guest state in registers
  // instead of through CPUState, and that needs correct cross-region live-in
  // and live-out sets -- the analysis this emitter has now got wrong twice, so
  // it is deliberately not attempted here.
  function_->setCallingConv(CallingConv::Fast);
  function_->setVisibility(GlobalValue::HiddenVisibility);
  function_->setDSOLocal(true);
  if (!inlineRegions())
    function_->addFnAttr(Attribute::NoInline);
  ctx_ = function_->getArg(0);
  ctx_->setName("ctx");
  guard_cycles_ = function_->getArg(1);
  guard_cycles_->setName("guard_cycles");
  guard_steps_ = function_->getArg(2);
  guard_steps_->setName("guard_steps");

  entry_ = BasicBlock::Create(context_, "entry", function_);
  for (u32 i = 0; i < source_.block_count; i++)
    blocks_.push_back(BasicBlock::Create(context_, blockName(i), function_));
  scanState();
  // scanContinuations() first: both dataflow passes need the indirect-switch
  // edges it discovers, and running them before it is what made the first two
  // attempts model a different graph than the emitter generates.
  scanContinuations();
  computeLiveness();
  computeReachingWrites();
  scanLoopHeaders();
  emitEntry();
  for (u32 i = 0; i < source_.block_count; i++)
    if (!emitBlock(i, diagnostics))
      return false;
  if (verifyFunction(*function_, &diagnostics))
    return false;
  return emitWrapper(diagnostics);
}

bool FunctionEmitter::emitWrapper(raw_ostream &diagnostics) {
  auto *pointer = PointerType::getUnqual(context_);
  auto *type = FunctionType::get(Type::getVoidTy(context_), {pointer}, false);
  Function *wrapper = module_.getFunction(source_.name);
  if (!wrapper)
    wrapper = Function::Create(type, GlobalValue::ExternalLinkage, source_.name,
                               module_);
  if (wrapper->getFunctionType() != type || !wrapper->empty()) {
    diagnostics << "dolllvm: conflicting native entry " << source_.name << "\n";
    return false;
  }
  wrapper->setCallingConv(CallingConv::C);
  wrapper->setVisibility(GlobalValue::HiddenVisibility);
  wrapper->setDSOLocal(true);
  wrapper->getArg(0)->setName("ctx");

  BasicBlock *entry = BasicBlock::Create(context_, "entry", wrapper);
  IRBuilder<> builder(entry);
  AllocaInst *guardCycles =
      builder.CreateAlloca(Type::getInt64Ty(context_), nullptr, "guard_cycles");
  AllocaInst *guardSteps =
      builder.CreateAlloca(Type::getInt64Ty(context_), nullptr, "guard_steps");
  builder.CreateStore(builder.getInt64(0), guardCycles);
  builder.CreateStore(builder.getInt64(0), guardSteps);
  CallInst *body =
      builder.CreateCall(function_, {wrapper->getArg(0), guardCycles, guardSteps});
  body->setCallingConv(CallingConv::Fast);
  builder.CreateRetVoid();
  return !verifyFunction(*wrapper, &diagnostics);
}

std::string FunctionEmitter::blockName(u32 index) const {
  char text[40];
  snprintf(text, sizeof(text), "guest_%08X_b%u",
           source_.blocks[index].guest_address, index);
  return text;
}

Type *FunctionEmitter::type(DolIRType t) {
  switch (t) {
  case DOLIR_TYPE_I1:
    return Type::getInt1Ty(context_);
  case DOLIR_TYPE_I8:
    return Type::getInt8Ty(context_);
  case DOLIR_TYPE_I16:
    return Type::getInt16Ty(context_);
  case DOLIR_TYPE_I32:
    return Type::getInt32Ty(context_);
  case DOLIR_TYPE_I64:
    return Type::getInt64Ty(context_);
  case DOLIR_TYPE_F32:
    return Type::getFloatTy(context_);
  case DOLIR_TYPE_F64:
    return Type::getDoubleTy(context_);
  case DOLIR_TYPE_V2F32:
    return FixedVectorType::get(Type::getFloatTy(context_), 2);
  case DOLIR_TYPE_V2F64:
    return FixedVectorType::get(Type::getDoubleTy(context_), 2);
  default:
    return Type::getVoidTy(context_);
  }
}

size_t FunctionEmitter::stateOffset(DolIRStateSlot slot) const {
  if (slot >= DOLIR_STATE_GPR0 && slot <= DOLIR_STATE_GPR31)
    return offsetof(CPUState, gpr) + 4u * (slot - DOLIR_STATE_GPR0);
  if (slot >= DOLIR_STATE_FPR0 && slot <= DOLIR_STATE_FPR31)
    return offsetof(CPUState, fpr) + 8u * (slot - DOLIR_STATE_FPR0);
  if (slot >= DOLIR_STATE_PS1_0 && slot <= DOLIR_STATE_PS1_31)
    return offsetof(CPUState, ps1) + 8u * (slot - DOLIR_STATE_PS1_0);
  if (slot >= DOLIR_STATE_SR0 && slot <= DOLIR_STATE_SR15)
    return offsetof(CPUState, sr) + 4u * (slot - DOLIR_STATE_SR0);
  if (slot >= DOLIR_STATE_GQR0 && slot <= DOLIR_STATE_GQR7)
    return offsetof(CPUState, gqr) + 4u * (slot - DOLIR_STATE_GQR0);
  switch (slot) {
  case DOLIR_STATE_PC:
    return offsetof(CPUState, pc);
  case DOLIR_STATE_LR:
    return offsetof(CPUState, lr);
  case DOLIR_STATE_CTR:
    return offsetof(CPUState, ctr);
  case DOLIR_STATE_CR:
    return offsetof(CPUState, cr);
  case DOLIR_STATE_XER:
    return offsetof(CPUState, xer);
  case DOLIR_STATE_FPSCR:
    return offsetof(CPUState, fpscr);
  case DOLIR_STATE_MSR:
    return offsetof(CPUState, msr);
  case DOLIR_STATE_SRR0:
    return offsetof(CPUState, srr0);
  case DOLIR_STATE_SRR1:
    return offsetof(CPUState, srr1);
  case DOLIR_STATE_DAR:
    return offsetof(CPUState, dar);
  case DOLIR_STATE_DSISR:
    return offsetof(CPUState, dsisr);
  case DOLIR_STATE_EAR:
    return offsetof(CPUState, ear);
  case DOLIR_STATE_HID2:
    return offsetof(CPUState, hid2);
  case DOLIR_STATE_TIMEBASE:
    return offsetof(CPUState, timebase);
  case DOLIR_STATE_EXCEPTION:
    return offsetof(CPUState, exception);
  case DOLIR_STATE_PROGRAM_EXCEPTION:
    return offsetof(CPUState, program_exception);
  case DOLIR_STATE_RESERVE_ADDR:
    return offsetof(CPUState, reserve_addr);
  case DOLIR_STATE_RESERVE_VALID:
    return offsetof(CPUState, reserve_valid);
  case DOLIR_STATE_DOWNCOUNT:
    return offsetof(CPUState, downcount);
  default:
    return 0;
  }
}

Value *FunctionEmitter::bytePtr(size_t offset) {
  return builder_.CreateInBoundsGEP(
      Type::getInt8Ty(context_), ctx_,
      ConstantInt::get(Type::getInt64Ty(context_), offset));
}

Value *FunctionEmitter::loadContext(DolIRStateSlot slot) {
  return builder_.CreateLoad(type(dolir_state_type(slot)),
                             bytePtr(stateOffset(slot)));
}

void FunctionEmitter::storeContext(DolIRStateSlot slot, Value *value) {
  builder_.CreateStore(value, bytePtr(stateOffset(slot)));
}

Value *FunctionEmitter::loadOffset(Type *valueType, size_t offset) {
  return builder_.CreateLoad(valueType, bytePtr(offset));
}

bool FunctionEmitter::liveAt(u32 block, DolIRStateSlot slot) const {
  if (live_in_.empty())
    return used_[slot];  // No liveness computed: fall back to the safe superset.
  std::size_t index = (std::size_t)block * DOLIR_STATE_COUNT + (std::size_t)slot;
  return index < live_in_.size() && live_in_[index] != 0;
}

// Which guest state slots are live on entry to each block.
//
// This exists to shrink the reload after a cross-region call. That reload
// previously restored every slot the function touches anywhere, because
// `used_` is a whole-function set -- so a call in a region that touches sixty
// slots paid sixty loads even when the continuation reads three.
//
// Only the reload side can use it. Materialisation before the call must still
// store every dirty slot: the callee reads guest state through CPUState and
// nothing here knows which slots it looks at. Narrowing that needs
// interprocedural information the emitter does not have.
//
// Conservative in three places, each of which would be a correctness bug the
// other way:
//   - a slot live out of any successor is live here;
//   - an unresolved successor (an exit, an indirect transfer, a call that may
//     not come back) makes everything the function uses live, because the
//     value may be observed through CPUState after we leave;
//   - a block whose terminator can raise makes everything live, since the
//     exception path materialises.
void FunctionEmitter::computeLiveness() {
  const u32 blocks = source_.block_count;
  if (blocks == 0)
    return;

  live_in_.assign((std::size_t)blocks * DOLIR_STATE_COUNT, 0);

  std::vector<unsigned char> gen((std::size_t)blocks * DOLIR_STATE_COUNT, 0);
  std::vector<unsigned char> kill((std::size_t)blocks * DOLIR_STATE_COUNT, 0);
  std::vector<unsigned char> escapes(blocks, 0);

  for (u32 b = 0; b < blocks; b++) {
    const DolIRBlock &block = source_.blocks[b];
    std::size_t base = (std::size_t)b * DOLIR_STATE_COUNT;
    for (u32 i = 0; i < block.instruction_count; i++) {
      const DolIRInstruction &inst = block.instructions[i];
      if (inst.op == DOLIR_OP_STATE_READ) {
        // Read before any write in this block: live coming in.
        if (!kill[base + inst.aux])
          gen[base + inst.aux] = 1;
      } else if (inst.op == DOLIR_OP_STATE_WRITE) {
        kill[base + inst.aux] = 1;
      } else if (inst.effects & (DOLIR_EFFECT_MAY_RAISE | DOLIR_EFFECT_BARRIER)) {
        // A helper that can raise or acts as a barrier observes CPUState.
        escapes[b] = 1;
      }
    }

    switch (block.terminator.kind) {
    case DOLIR_TERM_BRANCH:
    case DOLIR_TERM_COND_BRANCH:
      break;  // Successors are inside the region.
    default:
      escapes[b] = 1;  // Return, indirect, side exit, fallback, sc, rfi.
      break;
    }
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (u32 i = blocks; i-- > 0;) {
      const DolIRBlock &block = source_.blocks[i];
      std::size_t base = (std::size_t)i * DOLIR_STATE_COUNT;

      unsigned char out[DOLIR_STATE_COUNT] = {0};
      if (escapes[i]) {
        for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++)
          out[slot] = used_[slot] ? 1 : 0;
      }
      for (u32 s = 0; s < 2; s++) {
        u32 target = block.terminator.targets[s];
        if (target == DOLIR_NO_BLOCK || target >= blocks)
          continue;
        std::size_t tbase = (std::size_t)target * DOLIR_STATE_COUNT;
        for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++)
          out[slot] |= live_in_[tbase + slot];
      }
      // The indirect switch reaches every continuation block, so anything live
      // there is live out of an indirect terminator.
      if (block.terminator.kind == DOLIR_TERM_INDIRECT) {
        for (u32 continuation : continuations_) {
          if (continuation >= blocks)
            continue;
          std::size_t cbase = (std::size_t)continuation * DOLIR_STATE_COUNT;
          for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++)
            out[slot] |= live_in_[cbase + slot];
        }
      }

      for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
        unsigned char live = gen[base + slot] ||
                             (out[slot] && !kill[base + slot]);
        if (live && !live_in_[base + slot]) {
          live_in_[base + slot] = 1;
          changed = true;
        }
      }
    }
  }
}

bool FunctionEmitter::mayBeDirty(u32 block, DolIRStateSlot slot) const {
  if (dirty_in_.empty())
    return dirty_[slot];  // No analysis: fall back to the safe superset.
  std::size_t index = (std::size_t)block * DOLIR_STATE_COUNT + (std::size_t)slot;
  if (index >= dirty_in_.size())
    return dirty_[slot];
  // Written on a path to this block, or written by this block itself. The
  // second term is why this is safe without tracking position inside a block:
  // a barrier partway through still stores anything the block writes, even
  // writes that come after it.
  return dirty_in_[index] || writes_in_block_[index];
}

// Which guest state slots may have been written on some path from entry.
//
// materialize() stores every slot in `dirty_`, which is a whole-function flag:
// a slot written anywhere is stored at every barrier, including barriers on
// paths where it was never touched. A slot that no path to here has written
// still holds its entry value in CPUState, so storing it back writes the value
// that is already there.
//
// This narrows the store side. It cannot narrow it by "what the caller reads":
// every run start is a public func_XXXXXXXX the dispatcher may enter, and the
// runtime can snapshot CPUState at any exit -- savestates, mods, debugger,
// exception paths. Architectural state has to be complete whenever control
// leaves generated code. What it can do is skip stores that are provably
// redundant, which is a different and safe claim.
void FunctionEmitter::computeReachingWrites() {
  const u32 blocks = source_.block_count;
  if (blocks == 0)
    return;

  const std::size_t span = (std::size_t)blocks * DOLIR_STATE_COUNT;
  dirty_in_.assign(span, 0);
  writes_in_block_.assign(span, 0);

  for (u32 b = 0; b < blocks; b++) {
    const DolIRBlock &block = source_.blocks[b];
    std::size_t base = (std::size_t)b * DOLIR_STATE_COUNT;
    for (u32 i = 0; i < block.instruction_count; i++) {
      const DolIRInstruction &inst = block.instructions[i];
      if (inst.op == DOLIR_OP_STATE_WRITE) {
        writes_in_block_[base + inst.aux] = 1;
        continue;
      }
      // Anything that writes guest state without saying which slot makes the
      // whole block conservatively dirty.
      //
      // This is the fix for the first attempt, which counted STATE_WRITE only
      // and diverged from the C backend on 3 of 64 differential pairs. The
      // exact-float and paired-single helpers take a slot index and write it
      // inside the runtime; DOLIR_HELPER_PSQ_LOAD writes an FPR and its ps1
      // lane; SPR and FPSCR helpers write theirs. scanState() enumerates those
      // cases to build `used_`, and duplicating that enumeration here would be
      // a second place to forget one.
      //
      // Marking every used slot instead gives up narrowing inside blocks that
      // contain a helper, and keeps it for blocks that do not -- which is most
      // of them, and all of the integer ones. After being wrong twice about how
      // state moves, the conservative direction is the one to be wrong in.
      if (inst.op == DOLIR_OP_HELPER_CALL ||
          (inst.effects & DOLIR_EFFECT_WRITE_STATE)) {
        for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
          if (used_[slot])
            writes_in_block_[base + slot] = 1;
        }
      }
    }
  }

  // Predecessors: the terminator edges, plus the indirect-switch edges.
  //
  // DOLIR_TERM_INDIRECT lowers to a switch over `continuations_` -- an indirect
  // transfer whose target matches a known call-return point branches straight to
  // that block. Those edges do not appear in terminator.targets[], and leaving
  // them out is what broke the first two barrier-narrowing attempts: a
  // continuation block normally has a targets-predecessor too, so it did not
  // fall into the no-predecessor case, and it inherited a dirty set from the
  // fallthrough path that the indirect path does not justify.
  std::vector<std::vector<u32>> preds(blocks);
  for (u32 b = 0; b < blocks; b++) {
    for (u32 s = 0; s < 2; s++) {
      u32 target = source_.blocks[b].terminator.targets[s];
      if (target != DOLIR_NO_BLOCK && target < blocks)
        preds[target].push_back(b);
    }
    if (source_.blocks[b].terminator.kind == DOLIR_TERM_INDIRECT) {
      for (u32 continuation : continuations_) {
        if (continuation < blocks)
          preds[continuation].push_back(b);
      }
    }
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (u32 b = 0; b < blocks; b++) {
      std::size_t base = (std::size_t)b * DOLIR_STATE_COUNT;
      for (u32 p : preds[b]) {
        std::size_t pbase = (std::size_t)p * DOLIR_STATE_COUNT;
        for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
          if (dirty_in_[base + slot])
            continue;
          if (dirty_in_[pbase + slot] || writes_in_block_[pbase + slot]) {
            dirty_in_[base + slot] = 1;
            changed = true;
          }
        }
      }
    }
  }

  // A block reachable only indirectly has no predecessor edge in this model,
  // and its entry state is whatever the caller left. Treat every slot the
  // function writes as possibly dirty there rather than assuming clean.
  for (u32 b = 1; b < blocks; b++) {
    if (!preds[b].empty())
      continue;
    std::size_t base = (std::size_t)b * DOLIR_STATE_COUNT;
    for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++)
      dirty_in_[base + slot] = dirty_[slot] ? 1 : 0;
  }
}

// REVERTED to the conservative form. Narrowing this by liveness hung Mario Kart
// at boot: the module loaded, reported running, and never advanced a frame.
//
// computeLiveness() below is unsound for this purpose as written, because the
// successor model is incomplete. It follows terminator.targets[] only, but the
// emitter also reaches blocks through the `continuations_` switch that
// DOLIR_TERM_INDIRECT lowers to -- an indirect transfer whose target matches a
// known continuation branches straight to that block. Those edges do not appear
// in targets[], so liveness never propagates backward through them and reports
// slots dead that a continuation-entered block goes on to read. The reload then
// skips them and the block runs on stale guest state.
//
// The differential suite did not catch it and could not have: its sequences are
// single functions with no calls, and this path only runs on a cross-function
// call return. That coverage gap is the actual lesson here.
//
// Fixing this needs the indirect-continuation edges in the successor model.
// Until then the reload restores everything the function uses, which is what it
// did before and is always correct.
void FunctionEmitter::reloadLiveState(u32 block) {
  (void)block;
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    if (!used_[slot])
      continue;
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    builder_.CreateStore(loadContext(stateSlot), state_[slot]);
  }
}

void FunctionEmitter::scanState() {
  for (u32 b = 0; b < source_.block_count; b++) {
    const DolIRBlock &block = source_.blocks[b];
    for (u32 i = 0; i < block.instruction_count; i++) {
      const DolIRInstruction &inst = block.instructions[i];
      if (inst.op == DOLIR_OP_STATE_READ || inst.op == DOLIR_OP_STATE_WRITE)
        used_[inst.aux] = true;
      if (inst.op == DOLIR_OP_STATE_WRITE)
        dirty_[inst.aux] = true;
      if (inst.op == DOLIR_OP_HELPER_CALL &&
          inst.aux == DOLIR_HELPER_FP_AVAILABLE)
        used_[DOLIR_STATE_MSR] = true;
      if (inst.op == DOLIR_OP_HELPER_CALL &&
          inst.aux == DOLIR_HELPER_EXACT_FLOAT)
        scanExactFloat(inst.immediate);
      if (inst.op == DOLIR_OP_HELPER_CALL &&
          inst.aux == DOLIR_HELPER_EXACT_PAIRED)
        scanExactPaired(inst.immediate);
      if (inst.op == DOLIR_OP_HELPER_CALL &&
          inst.aux == DOLIR_HELPER_PSQ_LOAD) {
        u32 reg = inst.immediate & 0xFFu;
        used_[DOLIR_STATE_FPR0 + reg] = true;
        dirty_[DOLIR_STATE_FPR0 + reg] = true;
        used_[DOLIR_STATE_PS1_0 + reg] = true;
        dirty_[DOLIR_STATE_PS1_0 + reg] = true;
      }
      if (inst.op == DOLIR_OP_HELPER_CALL &&
          inst.aux == DOLIR_HELPER_STORE_CONDITIONAL) {
        used_[DOLIR_STATE_CR] = true;
        dirty_[DOLIR_STATE_CR] = true;
        used_[DOLIR_STATE_RESERVE_VALID] = true;
        dirty_[DOLIR_STATE_RESERVE_VALID] = true;
        used_[DOLIR_STATE_RESERVE_ADDR] = true;
      }
      if (inst.op == DOLIR_OP_HELPER_CALL &&
          (inst.aux == DOLIR_HELPER_FPSCR_UPDATED ||
           inst.aux == DOLIR_HELPER_FPSCR_BIT)) {
        used_[DOLIR_STATE_FPSCR] = true;
        dirty_[DOLIR_STATE_FPSCR] = true;
      }
      if (inst.op == DOLIR_OP_HELPER_CALL && inst.aux == DOLIR_HELPER_LSWX) {
        used_[DOLIR_STATE_XER] = true;
        for (u32 reg = 0; reg < 32; reg++) {
          used_[DOLIR_STATE_GPR0 + reg] = true;
          dirty_[DOLIR_STATE_GPR0 + reg] = true;
        }
      }
      if (inst.op == DOLIR_OP_GUEST_STORE) {
        used_[DOLIR_STATE_RESERVE_ADDR] = true;
        used_[DOLIR_STATE_RESERVE_VALID] = true;
        dirty_[DOLIR_STATE_RESERVE_VALID] = true;
      }
    }
  }
}

void FunctionEmitter::scanExactFloat(u64 descriptor) {
  auto op = static_cast<DolIRExactFloat>(descriptor & 0xFFu);
  u32 d = (descriptor >> 8) & 0xFFu;
  u32 a = (descriptor >> 16) & 0xFFu;
  u32 b = (descriptor >> 24) & 0xFFu;
  u32 c = (descriptor >> 32) & 0xFFu;
  used_[DOLIR_STATE_FPSCR] = true;
  dirty_[DOLIR_STATE_FPSCR] = true;
  if (op == DOLIR_EXACT_FCMPU || op == DOLIR_EXACT_FCMPO) {
    used_[DOLIR_STATE_CR] = true;
    dirty_[DOLIR_STATE_CR] = true;
    used_[DOLIR_STATE_FPR0 + a] = true;
    used_[DOLIR_STATE_FPR0 + b] = true;
    return;
  }
  used_[DOLIR_STATE_FPR0 + d] = true;
  dirty_[DOLIR_STATE_FPR0 + d] = true;
  if (op == DOLIR_EXACT_FRES ||
      (op >= DOLIR_EXACT_FADDS && op <= DOLIR_EXACT_FDIVS) ||
      op == DOLIR_EXACT_FRSP ||
      (op >= DOLIR_EXACT_FMADDS && op <= DOLIR_EXACT_FNMSUBS)) {
    used_[DOLIR_STATE_PS1_0 + d] = true;
    dirty_[DOLIR_STATE_PS1_0 + d] = true;
  }
  if (op == DOLIR_EXACT_FRES || op == DOLIR_EXACT_FRSQRTE ||
      op == DOLIR_EXACT_FCTIW || op == DOLIR_EXACT_FCTIWZ ||
      op == DOLIR_EXACT_FRSP) {
    used_[DOLIR_STATE_FPR0 + b] = true;
  } else if (op == DOLIR_EXACT_FMULS || op == DOLIR_EXACT_FMUL) {
    used_[DOLIR_STATE_FPR0 + a] = true;
    used_[DOLIR_STATE_FPR0 + c] = true;
  } else if ((op >= DOLIR_EXACT_FADDS && op <= DOLIR_EXACT_FDIVS) ||
             (op >= DOLIR_EXACT_FADD && op <= DOLIR_EXACT_FDIV)) {
    used_[DOLIR_STATE_FPR0 + a] = true;
    used_[DOLIR_STATE_FPR0 + b] = true;
  } else {
    used_[DOLIR_STATE_FPR0 + a] = true;
    used_[DOLIR_STATE_FPR0 + b] = true;
    used_[DOLIR_STATE_FPR0 + c] = true;
  }
}

void FunctionEmitter::scanExactPaired(u64 descriptor) {
  auto op = static_cast<DolIRExactPaired>(descriptor & 0xFFu);
  u32 d = (descriptor >> 8) & 0xFFu;
  u32 a = (descriptor >> 16) & 0xFFu;
  u32 b = (descriptor >> 24) & 0xFFu;
  u32 c = (descriptor >> 32) & 0xFFu;
  used_[DOLIR_STATE_FPSCR] = true;
  dirty_[DOLIR_STATE_FPSCR] = true;
  if (op >= DOLIR_EXACT_PS_CMPU0) {
    used_[DOLIR_STATE_CR] = true;
    dirty_[DOLIR_STATE_CR] = true;
    used_[DOLIR_STATE_FPR0 + a] = true;
    used_[DOLIR_STATE_PS1_0 + a] = true;
    used_[DOLIR_STATE_FPR0 + b] = true;
    used_[DOLIR_STATE_PS1_0 + b] = true;
    return;
  }
  used_[DOLIR_STATE_FPR0 + d] = true;
  dirty_[DOLIR_STATE_FPR0 + d] = true;
  used_[DOLIR_STATE_PS1_0 + d] = true;
  dirty_[DOLIR_STATE_PS1_0 + d] = true;
  auto usePair = [this](u32 reg) {
    used_[DOLIR_STATE_FPR0 + reg] = true;
    used_[DOLIR_STATE_PS1_0 + reg] = true;
  };
  if (op == DOLIR_EXACT_PS_RES || op == DOLIR_EXACT_PS_RSQRTE) {
    usePair(b);
    return;
  }
  usePair(a);
  if (op == DOLIR_EXACT_PS_MUL || op == DOLIR_EXACT_PS_MULS0 ||
      op == DOLIR_EXACT_PS_MULS1) {
    usePair(c);
    return;
  }
  usePair(b);
  if (op >= DOLIR_EXACT_PS_MADD && op <= DOLIR_EXACT_PS_SUM1)
    usePair(c);
}

void FunctionEmitter::scanContinuations() {
  for (u32 i = 0; i < source_.block_count; i++) {
    const DolIRTerminator &term = source_.blocks[i].terminator;
    if (!term.linked)
      continue;
    u32 continuation = term.guest_pc + 4u;
    u32 block = 0;
    if (continuation >= source_.guest_start &&
        continuation < source_.guest_end &&
        ((continuation - source_.guest_start) & 3u) == 0) {
      block = (continuation - source_.guest_start) / 4u;
      if (block < source_.block_count)
        continuations_.push_back(block);
    }
  }
}

void FunctionEmitter::scanLoopHeaders() {
  loop_headers_.assign(source_.block_count, false);
  for (u32 i = 0; i < source_.block_count; i++) {
    const DolIRTerminator &term = source_.blocks[i].terminator;
    u32 count = term.kind == DOLIR_TERM_COND_BRANCH ? 2u
                : term.kind == DOLIR_TERM_BRANCH    ? 1u
                                                    : 0u;
    for (u32 edge = 0; edge < count; edge++) {
      if (term.targets[edge] != DOLIR_NO_BLOCK && term.targets[edge] <= i)
        loop_headers_[term.targets[edge]] = true;
    }
  }
}

void FunctionEmitter::emitEntry() {
  builder_.SetInsertPoint(entry_);
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    if (!used_[slot])
      continue;
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    state_[slot] = builder_.CreateAlloca(type(dolir_state_type(stateSlot)),
                                         nullptr, "state");
    builder_.CreateStore(loadContext(stateSlot), state_[slot]);
  }
  cycles_ =
      builder_.CreateAlloca(Type::getInt64Ty(context_), nullptr, "cycles");
  builder_.CreateStore(ConstantInt::get(Type::getInt64Ty(context_), 0),
                       cycles_);
  Value *pc = loadOffset(Type::getInt32Ty(context_), offsetof(CPUState, pc));
  BasicBlock *bad = BasicBlock::Create(context_, "entry_miss", function_);
  auto *dispatch = builder_.CreateSwitch(pc, bad, source_.block_count);
  for (u32 i = 0; i < source_.block_count; i++)
    dispatch->addCase(ConstantInt::get(Type::getInt32Ty(context_),
                                       source_.blocks[i].guest_address),
                      blocks_[i]);
  builder_.SetInsertPoint(bad);
  builder_.CreateRetVoid();
}

void FunctionEmitter::chargeCycles(u32 cycles) {
  Value *old = builder_.CreateLoad(Type::getInt64Ty(context_), cycles_);
  Value *next = builder_.CreateAdd(
      old, ConstantInt::get(Type::getInt64Ty(context_), cycles));
  builder_.CreateStore(next, cycles_);
  // The shared guard survives helper and generated-function boundaries.
  Value *guard_old =
      builder_.CreateLoad(Type::getInt64Ty(context_), guard_cycles_);
  builder_.CreateStore(
      builder_.CreateAdd(guard_old,
                         ConstantInt::get(Type::getInt64Ty(context_), cycles)),
      guard_cycles_);
}

void FunctionEmitter::materialize(u32 pc) {
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    if (!dirty_[slot])
      continue;
    // Re-enabled after the third root cause: the predecessor model was missing
    // the indirect-switch edges that DOLIR_TERM_INDIRECT lowers to. A
    // continuation block normally has a targets-predecessor as well, so it never
    // fell into the no-predecessor case, and inherited a dirty set the indirect
    // path does not justify. Both dataflow passes now include those edges.
    //
    // Previously DISABLED because, even with helper writes handled, this hung
    // Mario Kart: the module loaded, reported running, and never advanced a
    // frame in 180 seconds across four attempts. The -12% module size and -23% build time
    // it produced are real and worthless, because the module does not run.
    //
    // 240 differential pairs across 5 seeds agree with the C backend, including
    // call-shaped sequences with LR save/restore. So whatever it breaks is not
    // reached by straight-line code, nor by one level of direct calls -- the
    // suite's remaining blind spots are branch-shaped control flow inside a
    // region, indirect transfers through the continuations switch, exception
    // paths, and re-entry from the dispatcher mid-region.
    //
    // The pattern across three attempts is consistent and worth stating: every
    // narrowing of a materialisation barrier has failed on a path the emitter
    // reaches by a route the analysis did not model. Barrier narrowing should
    // not be attempted again until the successor model provably matches the
    // edges the emitter actually generates -- and the way to establish that is
    // to derive both from one description rather than to keep patching the
    // analysis after each failure.
    if (narrowBarriers() &&
        !mayBeDirty(current_block_, static_cast<DolIRStateSlot>(slot)))
      continue;
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    storeContext(
        stateSlot,
        builder_.CreateLoad(type(dolir_state_type(stateSlot)), state_[slot]));
  }
  storeContext(DOLIR_STATE_PC,
               ConstantInt::get(Type::getInt32Ty(context_), pc));
  Value *downcount =
      loadOffset(Type::getInt64Ty(context_), offsetof(CPUState, downcount));
  // Only unmaterialized cycles are owed to downcount.
  Value *cycles = builder_.CreateLoad(Type::getInt64Ty(context_), cycles_);
  builder_.CreateStore(builder_.CreateSub(downcount, cycles),
                       bytePtr(offsetof(CPUState, downcount)));
}

void FunctionEmitter::sideExit(u32 pc) {
  materialize(pc);
  builder_.CreateRetVoid();
}

void FunctionEmitter::emitBudgetGuard(u32 pc) {
  // Guard the whole native call chain, not one generated function.
  Value *cycles =
      builder_.CreateLoad(Type::getInt64Ty(context_), guard_cycles_);
  Value *over_cycles = builder_.CreateICmpUGE(
      cycles, ConstantInt::get(Type::getInt64Ty(context_), 256));
  // Backstop for zero-cycle loops.
  Value *steps = builder_.CreateLoad(Type::getInt64Ty(context_), guard_steps_);
  Value *next_steps = builder_.CreateAdd(
      steps, ConstantInt::get(Type::getInt64Ty(context_), 1));
  builder_.CreateStore(next_steps, guard_steps_);
  Value *over_steps = builder_.CreateICmpUGE(
      next_steps, ConstantInt::get(Type::getInt64Ty(context_), 2048));
  Value *exhausted = builder_.CreateOr(over_cycles, over_steps);
  BasicBlock *run = BasicBlock::Create(context_, "budget_run", function_);
  BasicBlock *exit = BasicBlock::Create(context_, "budget_exit", function_);
  builder_.CreateCondBr(exhausted, exit, run);
  builder_.SetInsertPoint(exit);
  sideExit(pc);
  builder_.SetInsertPoint(run);
}

bool FunctionEmitter::emitBlock(u32 index, raw_ostream &diagnostics) {
  const DolIRBlock &block = source_.blocks[index];
  builder_.SetInsertPoint(blocks_[index]);
  // Every materialize() emitted while this block is being lowered consults the
  // reaching-writes set for it.
  current_block_ = index;
  if (loop_headers_[index])
    emitBudgetGuard(block.guest_address);
  chargeCycles(block.cycle_cost);
  values_.assign(source_.value_count, nullptr);
  for (u32 i = 0; i < block.instruction_count; i++) {
    if (!emitInstruction(block.instructions[i], diagnostics))
      return false;
  }
  return emitTerminator(block.terminator, diagnostics);
}

Value *FunctionEmitter::operand(const DolIRInstruction &inst, u32 index) {
  return values_[inst.operands[index]];
}

Value *FunctionEmitter::castValue(DolIROp op, Type *resultType, Value *value) {
  switch (op) {
  case DOLIR_OP_TRUNC:
    return builder_.CreateTrunc(value, resultType);
  case DOLIR_OP_ZEXT:
    return builder_.CreateZExt(value, resultType);
  case DOLIR_OP_SEXT:
    return builder_.CreateSExt(value, resultType);
  case DOLIR_OP_BITCAST:
    return builder_.CreateBitCast(value, resultType);
  case DOLIR_OP_FPTRUNC:
    return builder_.CreateFPTrunc(value, resultType);
  case DOLIR_OP_FPEXT:
    return builder_.CreateFPExt(value, resultType);
  default:
    return nullptr;
  }
}

Value *FunctionEmitter::bswap(Value *value) {
  auto *integer = cast<IntegerType>(value->getType());
  if (integer->getBitWidth() == 8)
    return value;
  Function *intrinsic =
      Intrinsic::getDeclaration(&module_, Intrinsic::bswap, {value->getType()});
  return builder_.CreateCall(intrinsic, {value});
}

bool FunctionEmitter::emitInstruction(const DolIRInstruction &inst,
                                      raw_ostream &diagnostics) {
  current_pc_ = inst.guest_pc;
  Value *result = nullptr;
  Type *resultType = type(inst.type);
  switch (inst.op) {
  case DOLIR_OP_CONSTANT:
    if (inst.type == DOLIR_TYPE_F32)
      result = ConstantFP::get(
          context_, APFloat(APFloat::IEEEsingle(), APInt(32, inst.immediate)));
    else if (inst.type == DOLIR_TYPE_F64)
      result = ConstantFP::get(
          context_, APFloat(APFloat::IEEEdouble(), APInt(64, inst.immediate)));
    else
      result = ConstantInt::get(resultType, inst.immediate);
    break;
  case DOLIR_OP_STATE_READ:
    result = builder_.CreateLoad(resultType, state_[inst.aux]);
    break;
  case DOLIR_OP_STATE_WRITE:
    builder_.CreateStore(operand(inst, 0), state_[inst.aux]);
    break;
  case DOLIR_OP_ADD:
    result = builder_.CreateAdd(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_SUB:
    result = builder_.CreateSub(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_MUL:
    result = builder_.CreateMul(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_UDIV:
    result = builder_.CreateUDiv(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_SDIV:
    result = builder_.CreateSDiv(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_AND:
    result = builder_.CreateAnd(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_OR:
    result = builder_.CreateOr(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_XOR:
    result = builder_.CreateXor(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_NOT:
    result = builder_.CreateNot(operand(inst, 0));
    break;
  case DOLIR_OP_SHL:
    result = builder_.CreateShl(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_LSHR:
    result = builder_.CreateLShr(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_ASHR:
    result = builder_.CreateAShr(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_ROTL: {
    Function *intrinsic =
        Intrinsic::getDeclaration(&module_, Intrinsic::fshl, {resultType});
    result = builder_.CreateCall(
        intrinsic, {operand(inst, 0), operand(inst, 0), operand(inst, 1)});
    break;
  }
  case DOLIR_OP_CLZ: {
    Function *intrinsic =
        Intrinsic::getDeclaration(&module_, Intrinsic::ctlz, {resultType});
    result = builder_.CreateCall(
        intrinsic, {operand(inst, 0), ConstantInt::getFalse(context_)});
    break;
  }
  case DOLIR_OP_BSWAP:
    result = bswap(operand(inst, 0));
    break;
  case DOLIR_OP_TRUNC:
  case DOLIR_OP_ZEXT:
  case DOLIR_OP_SEXT:
  case DOLIR_OP_BITCAST:
  case DOLIR_OP_FPTRUNC:
  case DOLIR_OP_FPEXT:
    result = castValue(inst.op, resultType, operand(inst, 0));
    break;
  case DOLIR_OP_ICMP_EQ:
    result = builder_.CreateICmpEQ(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_ICMP_NE:
    result = builder_.CreateICmpNE(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_ICMP_ULT:
    result = builder_.CreateICmpULT(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_ICMP_ULE:
    result = builder_.CreateICmpULE(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_ICMP_SLT:
    result = builder_.CreateICmpSLT(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_ICMP_SLE:
    result = builder_.CreateICmpSLE(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_FCMP_OEQ:
    result = builder_.CreateFCmpOEQ(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_FCMP_OLT:
    result = builder_.CreateFCmpOLT(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_FCMP_OGE:
    result = builder_.CreateFCmpOGE(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_SELECT:
    result = builder_.CreateSelect(operand(inst, 0), operand(inst, 1),
                                   operand(inst, 2));
    break;
  case DOLIR_OP_FADD:
    result = builder_.CreateFAdd(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_FSUB:
    result = builder_.CreateFSub(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_FMUL:
    result = builder_.CreateFMul(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_FDIV:
    result = builder_.CreateFDiv(operand(inst, 0), operand(inst, 1));
    break;
  case DOLIR_OP_FNEG:
    result = builder_.CreateFNeg(operand(inst, 0));
    break;
  case DOLIR_OP_FABS: {
    Function *intrinsic =
        Intrinsic::getDeclaration(&module_, Intrinsic::fabs, {resultType});
    result = builder_.CreateCall(intrinsic, {operand(inst, 0)});
    break;
  }
  case DOLIR_OP_VECTOR_BUILD: {
    result = PoisonValue::get(resultType);
    result =
        builder_.CreateInsertElement(result, operand(inst, 0), uint64_t{0});
    result = builder_.CreateInsertElement(result, operand(inst, 1), 1u);
    break;
  }
  case DOLIR_OP_VECTOR_EXTRACT:
    result = builder_.CreateExtractElement(operand(inst, 0), inst.aux);
    break;
  case DOLIR_OP_VECTOR_SHUFFLE:
    result = builder_.CreateShuffleVector(
        operand(inst, 0), operand(inst, 1),
        {static_cast<int>(inst.aux & 0xFFu),
         static_cast<int>((inst.aux >> 8) & 0xFFu)});
    break;
  case DOLIR_OP_GUEST_LOAD:
    result = emitGuestLoad(operand(inst, 0), resultType, inst.aux & 0xffu,
                           (inst.aux & 0x100u) != 0);
    break;
  case DOLIR_OP_GUEST_STORE:
    emitGuestStore(operand(inst, 0), operand(inst, 1), inst.aux & 0xffu);
    break;
  case DOLIR_OP_HELPER_CALL:
    if (inst.aux == DOLIR_HELPER_FP_AVAILABLE)
      result = emitFPAvailable(inst.guest_pc);
    else if (inst.aux == DOLIR_HELPER_MEMORY_FENCE)
      builder_.CreateFence(AtomicOrdering::SequentiallyConsistent);
    else if (inst.aux == DOLIR_HELPER_EXACT_FLOAT)
      emitExactFloat(inst.immediate);
    else if (inst.aux == DOLIR_HELPER_EXACT_PAIRED)
      emitExactPaired(inst.immediate);
    else if (inst.aux == DOLIR_HELPER_PSQ_LOAD ||
             inst.aux == DOLIR_HELPER_PSQ_STORE)
      result = emitPSQ(inst);
    else if (inst.aux == DOLIR_HELPER_STORE_CONDITIONAL)
      emitStoreConditional(inst);
    else if (inst.aux == DOLIR_HELPER_FPSCR_UPDATED)
      emitFPSCRUpdated();
    else if (inst.aux == DOLIR_HELPER_FPSCR_BIT)
      emitFPSCRBit(inst.immediate);
    else if (inst.aux == DOLIR_HELPER_PROGRAM_EXCEPTION)
      emitProgramException(inst);
    else if (inst.aux == DOLIR_HELPER_SPR_READ)
      result = emitSPRRead(inst);
    else if (inst.aux == DOLIR_HELPER_SPR_WRITE)
      emitSPRWrite(inst);
    else if (inst.aux == DOLIR_HELPER_LSWX)
      emitLSWX(inst);
    else if (inst.aux == DOLIR_HELPER_DCBZ_L ||
             inst.aux == DOLIR_HELPER_ECIWX || inst.aux == DOLIR_HELPER_ECOWX ||
             inst.aux == DOLIR_HELPER_TLBIE ||
             inst.aux == DOLIR_HELPER_CACHE_CONTROL)
      result = emitRuntimeBoundary(inst);
    else {
      diagnostics << "dolllvm: unsupported helper " << inst.aux << " at 0x"
                  << format_hex_no_prefix(inst.guest_pc, 8) << "\n";
      return false;
    }
    break;
  default:
    diagnostics << "dolllvm: unsupported DolIR op " << unsigned(inst.op)
                << " at 0x" << format_hex_no_prefix(inst.guest_pc, 8) << "\n";
    return false;
  }
  if (inst.result)
    values_[inst.result] = result;
  return inst.type == DOLIR_TYPE_VOID || result != nullptr;
}

} // namespace dolllvm
