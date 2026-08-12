#ifndef DOLRECOMP_LLVM_FUNCTION_EMITTER_H
#define DOLRECOMP_LLVM_FUNCTION_EMITTER_H

#include "backend/llvm/llvm_backend.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/IRBuilder.h>

namespace llvm {
class AllocaInst;
class Argument;
class BasicBlock;
class Function;
class LLVMContext;
class Module;
class Type;
class Value;
class raw_ostream;
} // namespace llvm

namespace dolllvm {

class FunctionEmitter final {
public:
  FunctionEmitter(llvm::LLVMContext &context, llvm::Module &module,
                  const DolIRFunction &source,
                  const DolLLVMFunctionRange *ranges, u32 range_count);

  bool emit(llvm::raw_ostream &diagnostics);

private:
  std::string blockName(u32 index) const;
  llvm::Type *type(DolIRType type);
  std::size_t stateOffset(DolIRStateSlot slot) const;
  llvm::Value *bytePtr(std::size_t offset);
  llvm::Value *loadContext(DolIRStateSlot slot);
  void storeContext(DolIRStateSlot slot, llvm::Value *value);
  llvm::Value *loadOffset(llvm::Type *value_type, std::size_t offset);

  void scanState();
  // Backward dataflow over the region's blocks: which guest state slots are
  // live on entry to each one. Used to reload only what the continuation
  // actually needs after a call, instead of everything the function touches
  // anywhere.
  void computeLiveness();
  bool liveAt(u32 block, DolIRStateSlot slot) const;
  void reloadLiveState(u32 block);
  // Forward dataflow: may a slot have been written on some path reaching this
  // block? A slot never written still holds its entry value in CPUState, so
  // storing it back at a barrier is pure waste.
  void computeReachingWrites();
  bool mayBeDirty(u32 block, DolIRStateSlot slot) const;
  void scanExactFloat(u64 descriptor);
  void scanExactPaired(u64 descriptor);
  void scanContinuations();
  void scanLoopHeaders();

  void emitEntry();
  bool emitWrapper(llvm::raw_ostream &diagnostics);
  void chargeCycles(u32 cycles);
  void materialize(u32 pc);
  void sideExit(u32 pc);
  void emitBudgetGuard(u32 pc);
  bool emitBlock(u32 index, llvm::raw_ostream &diagnostics);
  llvm::Value *operand(const DolIRInstruction &instruction, u32 index);
  llvm::Value *castValue(DolIROp op, llvm::Type *result_type,
                         llvm::Value *value);
  llvm::Value *bswap(llvm::Value *value);
  bool emitInstruction(const DolIRInstruction &instruction,
                       llvm::raw_ostream &diagnostics);

  llvm::AllocaInst *temporary(llvm::Type *value_type, llvm::StringRef name);
  llvm::Value *stateValue(DolIRStateSlot slot);
  void syncState(DolIRStateSlot slot);
  void reloadState(DolIRStateSlot slot);
  void reloadUsedState();
  void continueAfterRuntimeBoundary(llvm::StringRef prefix);
  void emitFPSCRUpdated();
  void emitFPSCRBit(u64 descriptor);
  void emitProgramException(const DolIRInstruction &instruction);
  llvm::Value *emitSPRRead(const DolIRInstruction &instruction);
  void emitSPRWrite(const DolIRInstruction &instruction);
  void emitLSWX(const DolIRInstruction &instruction);
  llvm::Value *emitRuntimeBoundary(const DolIRInstruction &instruction);
  void emitExactFloat(u64 descriptor);
  void emitExactPaired(u64 descriptor);
  llvm::Value *emitPSQ(const DolIRInstruction &instruction);
  void emitStoreConditional(const DolIRInstruction &instruction);
  llvm::Value *emitFPAvailable(u32 pc);

  llvm::Value *normalizeAddress(llvm::Value *address);
  llvm::Value *rangeCheck(llvm::Value *normalized, u32 base, llvm::Value *size,
                          u32 width);
  llvm::Value *endianLoad(llvm::Value *pointer, llvm::Type *result_type,
                          u32 width);
  llvm::Value *externalRead(llvm::Value *address, u32 width);
  llvm::Value *emitGuestLoad(llvm::Value *address, llvm::Type *result_type,
                             u32 width, bool sign);
  void clearReservation(llvm::Value *address);
  void journal(llvm::Value *offset, u32 width);
  void endianStore(llvm::Value *pointer, llvm::Value *value, u32 width);
  void externalWrite(llvm::Value *address, llvm::Value *value, u32 width);
  void emitGuestStore(llvm::Value *address, llvm::Value *value, u32 width);

  llvm::BasicBlock *directDestination(const DolIRTerminator &terminator,
                                      u32 slot);
  const DolLLVMFunctionRange *rangeFor(u32 address) const;
  llvm::BasicBlock *externalDestination(const DolIRTerminator &terminator,
                                        u32 slot);
  llvm::BasicBlock *exitDestination(u32 pc);
  bool emitTerminator(const DolIRTerminator &terminator,
                      llvm::raw_ostream &diagnostics);

  llvm::LLVMContext &context_;
  llvm::Module &module_;
  const DolIRFunction &source_;
  llvm::IRBuilder<> builder_;
  llvm::Function *function_ = nullptr;
  llvm::Argument *ctx_ = nullptr;
  llvm::BasicBlock *entry_ = nullptr;
  llvm::AllocaInst *cycles_ = nullptr;
  // Shared across generated calls until control returns to the dispatcher.
  llvm::Value *guard_cycles_ = nullptr;
  // Termination backstop for zero-cycle loops.
  llvm::Value *guard_steps_ = nullptr;
  std::array<llvm::AllocaInst *, DOLIR_STATE_COUNT> state_{};
  std::array<bool, DOLIR_STATE_COUNT> used_{};
  std::array<bool, DOLIR_STATE_COUNT> dirty_{};
  // live_in_[block * DOLIR_STATE_COUNT + slot]. Flat rather than nested so the
  // fixpoint loop touches one contiguous buffer.
  std::vector<unsigned char> live_in_;
  // dirty_in_[block * DOLIR_STATE_COUNT + slot]: written on some path to here.
  std::vector<unsigned char> dirty_in_;
  // Slots written anywhere inside a block, folded in so a barrier partway
  // through the block still stores what the block itself has written.
  std::vector<unsigned char> writes_in_block_;
  u32 current_block_ = 0;
  std::vector<llvm::BasicBlock *> blocks_;
  std::vector<bool> loop_headers_;
  std::vector<llvm::Value *> values_;
  std::vector<u32> continuations_;
  const DolLLVMFunctionRange *ranges_ = nullptr;
  u32 range_count_ = 0;
  u32 current_pc_ = 0;
};

} // namespace dolllvm

#endif
