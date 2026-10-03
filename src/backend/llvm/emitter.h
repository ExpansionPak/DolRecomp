#ifndef DOLRECOMP_LLVM_EMITTER_H
#define DOLRECOMP_LLVM_EMITTER_H
#include "backend/llvm/llvm_backend.h"
#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/CallingConv.h>
#include <llvm/IR/IRBuilder.h>
namespace llvm {
class AllocaInst;
class Argument;
class BasicBlock;
class ConstantInt;
class Function;
class FunctionType;
class LLVMContext;
class Module;
class PHINode;
class StructType;
class Type;
class Value;
class raw_ostream;
} // namespace llvm
namespace dolllvm {
class FunctionEmitter final {
public:
  FunctionEmitter(llvm::LLVMContext &context, llvm::Module &module,
                  const DolIRFunction &source, const DolLLVMOptions &options);
  bool emit(llvm::raw_ostream &diagnostics);
private:
  enum class FPRepresentation : u8 {
    Raw,
    ScalarF64,
    ScalarF32,
    PairF64,
    PairF32,
  };
  enum class FPValueClass : u8 {
    Unknown,
    NoNaN,
    Finite,
    NormalOrZero,
  };

  std::string blockName(u32 index) const;
  std::string symbolName(llvm::StringRef base) const;
  llvm::Type *type(DolIRType type);
  llvm::FunctionType *bodyFunctionType(const DolLLVMFunctionRange *range);
  llvm::CallingConv::ID bodyCallingConvention() const;
  llvm::Type *nativeResultType(const DolLLVMFunctionRange *range);
  llvm::StructType *chainType();
  llvm::StructType *runtimeType();
  llvm::StructType *stateInterfaceType();
  bool stateInput(const DolLLVMFunctionRange *range, DolIRStateSlot slot) const;
  bool stateOutput(const DolLLVMFunctionRange *range, DolIRStateSlot slot) const;
  std::size_t stateOffset(DolIRStateSlot slot) const;
  llvm::Value *bytePtr(std::size_t offset);
  llvm::Value *runtimeField(u32 field);
  llvm::Value *stateField(u32 field);
  llvm::Value *callStateRead(u32 field, llvm::ArrayRef<llvm::Value *> arguments, llvm::Type *result_type);
  void callStateWrite(u32 field, llvm::ArrayRef<llvm::Value *> arguments);
  llvm::Value *loadContext(DolIRStateSlot slot);
  void storeContext(DolIRStateSlot slot, llvm::Value *value);
  llvm::Value *loadOffset(llvm::Type *value_type, std::size_t offset);

  void scanState();
  void scanStableControls();
  void scanContinuations();
  void scanLoopHeaders();
  void scanRegionLeaders();
  void finalizeStateSSA();
  bool slotInMemory(DolIRStateSlot slot) const;

  void emitEntry();
  bool emitWrapper(llvm::raw_ostream &diagnostics);
  bool emitModernWrapper(llvm::raw_ostream &diagnostics);
  void chargeCycles(u32 cycles);
  void chargeCycles(llvm::Value *cycles);
  llvm::Value *emitTimebaseRead();
  void emitTimebaseWrite(const DolIRInstruction &inst);
  void stageStateMask(const u64 *mask); void syncDirtyState();
  void commitModernState(); void reloadModernState();
  bool modernStateCacheable(DolIRStateSlot slot) const;
  llvm::Value *modernStateValues(); llvm::Value *modernStateDirtyMask();
  llvm::Value *modernStateValidMask(); void invalidateModernStateCache();
  void retainModernPersistentDirty();
  void settleCycles();
  void flushCallCounters(bool force_cycles = false);
  void reloadCallCounters();
  void returnFromBody();
  void returnStructuredExit();
  llvm::Value *structuredExitPending();
  void returnNative(llvm::Value *pc);
  llvm::Value *nativeResult(llvm::Value *pc, bool native, bool from_context);
  llvm::Value *nativeOutputs(bool from_context);
  u32 nativeOutputLaneCount(const DolLLVMFunctionRange *range);
  bool nativeCyclesInResult(const DolLLVMFunctionRange *range);
  u32 nativeResultLaneCount(const DolLLVMFunctionRange *range);
  llvm::Value *nativeCycleValue(llvm::Value *result, const DolLLVMFunctionRange *range);
  llvm::Value *nativeOutputValue(llvm::Value *result, const DolLLVMFunctionRange *range,
                                 DolIRStateSlot slot);
  llvm::Value *nativeResultPC(llvm::Value *result);
  llvm::Value *nativeResultContinues(llvm::Value *result);
  void acceptNativeResult(llvm::Value *result, const DolLLVMFunctionRange *range);
  void materialize(u32 pc); void materialize(llvm::Value *pc);
  void materializeMemoryService(u32 pc);
  void materializeFifoService(u32 pc);
  void sideExit(u32 pc, u32 reason = 0); void emitSharedSideExit();
  void branchMemoryServiceFailure(llvm::Value *reason, llvm::Value *elapsed);
  void emitMemoryServiceFailure();
  void emitBudgetGuard(u32 pc);
  bool emitRegion(u32 index, llvm::raw_ostream &diagnostics);
  llvm::Value *operand(const DolIRInstruction &instruction, u32 index);
  llvm::Value *castValue(DolIROp op, llvm::Type *result_type, llvm::Value *value);
  llvm::Value *bswap(llvm::Value *value);
  bool emitInstruction(const DolIRInstruction &instruction,
                       llvm::raw_ostream &diagnostics);
  void emitStateWrite(const DolIRInstruction &instruction);

  llvm::AllocaInst *temporary(llvm::Type *value_type, llvm::StringRef name);
  llvm::Value *stateValue(DolIRStateSlot slot);
  void syncState(DolIRStateSlot slot);
  void reloadState(DolIRStateSlot slot);
  void reloadUsedState();
  void resetFPRepresentations();
  void invalidateFPRepresentations();
  void noteStateWrite(DolIRStateSlot slot, llvm::Value *value);
  void initializeEntryControls();
  llvm::Value *gqrValue(u32 index);
  llvm::Value *gqrType(u32 index, bool load);
  llvm::Value *gqrScale(u32 index, bool load);
  llvm::Value *psqEnabled(bool indexed);
  llvm::Value *niEnabled();
  llvm::Value *pairF32(u32 reg);
  llvm::Value *pairF64(u32 reg);
  llvm::Value *architecturalPairF64(u32 reg);
  void writePairF32(u32 reg, llvm::Value *pair, bool denormal_safe,
                    FPValueClass value_class = FPValueClass::Unknown);
  void writePairF64(u32 reg, llvm::Value *pair, bool exact_single = false,
                    FPValueClass value_class = FPValueClass::Unknown);
  void updateFPRF(llvm::Value *lane0);
  void materializeFPRF();
  llvm::Value *roundPairToSingle(llvm::Value *pair);
  llvm::Value *forcePairedMultiplierPrecision(llvm::Value *pair);
  llvm::Value *preferPairedNaN(llvm::Value *result, llvm::Value *left,
                               llvm::Value *right, bool operands_finite);
  llvm::Value *constrainedPairBinary(unsigned intrinsic, llvm::Value *left,
                                     llvm::Value *right);
  llvm::Value *constrainedPairFMA(llvm::Value *left, llvm::Value *multiplier,
                                  llvm::Value *addend);
  llvm::Value *correctPairedFMARounding(llvm::Value *result, llvm::Value *left,
                                        llvm::Value *multiplier,
                                        llvm::Value *addend);
  llvm::Value *preferPairedFMANaN(llvm::Value *result, llvm::Value *left,
                                  llvm::Value *addend, llvm::Value *multiplier,
                                  bool operands_finite);
  bool emitVectorPaired(DolIRExactPaired op, u32 d, u32 a, u32 b, u32 c);
  bool emitVectorPairedFMA(DolIRExactPaired op, u32 d, u32 a, u32 b, u32 c);
  void continueAfterRuntimeBoundary(llvm::StringRef prefix);
  void emitFPSCRUpdated();
  void emitFPSCRBit(u64 descriptor);
  void emitProgramException(const DolIRInstruction &instruction);
  llvm::Value *emitSPRRead(const DolIRInstruction &instruction);
  void emitSPRWrite(const DolIRInstruction &instruction);
  void emitLSWX(const DolIRInstruction &instruction);
  void emitCacheControl(const DolIRInstruction &instruction);
  void emitInstructionService(u32 pc, u32 fallback_cycle_cost = 0);
  llvm::Value *emitRuntimeBoundary(const DolIRInstruction &instruction);
  void emitExactFloat(u64 descriptor);
  void emitExactPaired(u64 descriptor);
  llvm::Value *emitPSQ(const DolIRInstruction &instruction);
  llvm::Value *emitPSQLoad(const DolIRInstruction &, llvm::Value *, llvm::Value *, llvm::Value *);
  llvm::Value *emitKnownPSQ(const DolIRInstruction &instruction, u32 type,
                            s32 scale);
  void emitStoreConditional(const DolIRInstruction &instruction);
  llvm::Value *emitFPAvailable(u32 pc);

  llvm::Value *normalizeAddress(llvm::Value *address);
  llvm::Value *provenMemoryPointer(const DolIRInstruction &instruction,
                                   llvm::Value *address, u32 width,
                                   llvm::Value **offset,
                                   llvm::Value **available);
  llvm::Value *rangeCheck(llvm::Value *normalized, u32 base, llvm::Value *size,
                          u32 width);
  llvm::Value *endianLoad(llvm::Value *pointer, llvm::Type *result_type,
                          u32 width);
  llvm::Value *externalRead(llvm::Value *address, u32 width, llvm::Value **yielded);
  llvm::Value *emitGuestLoad(const DolIRInstruction &instruction,
                             llvm::Value *address, llvm::Type *result_type,
                             u32 width, bool sign);
  llvm::Value *emitGuestLoad(llvm::Value *address, llvm::Type *result_type,
                             u32 width, bool sign);
  void clearReservation(llvm::Value *address);
  void journal(llvm::Value *offset, u32 width);
  void endianStore(llvm::Value *pointer, llvm::Value *value, u32 width);
  llvm::Value *externalWrite(llvm::Value *address, llvm::Value *value, u32 width);
  llvm::Value *externalFifoWrite(llvm::Value *address, llvm::Value *value,
                                 u32 width);
  void emitGuestStore(const DolIRInstruction &instruction, llvm::Value *address,
                      llvm::Value *value, u32 width);
  void emitGuestStore(llvm::Value *address, llvm::Value *value, u32 width);

  llvm::BasicBlock *directDestination(const DolIRTerminator &terminator,
                                      u32 slot);
  const DolLLVMFunctionRange *rangeFor(u32 address) const; const DolLLVMPatch *patchFor(u32 address) const;
  const DolLLVMCallEdge *callEdge(const DolIRTerminator &terminator, u32 slot) const;
  llvm::BasicBlock *externalDestination(const DolIRTerminator &terminator,
                                        u32 slot);
  llvm::BasicBlock *exitDestination(u32 pc);
  llvm::BasicBlock *fallbackDestination(const DolIRTerminator &terminator);
  llvm::BasicBlock *fallbackEdge(u32 pc);
  void emitFallbackHandler();
  void emitColdEntry(llvm::BasicBlock *entry_miss);
  bool emitTerminator(const DolIRTerminator &terminator,
                      llvm::raw_ostream &diagnostics);

  llvm::LLVMContext &context_;
  llvm::Module &module_;
  const DolIRFunction &source_;
  llvm::IRBuilder<> builder_;
  llvm::Function *function_ = nullptr;
  llvm::Argument *ctx_ = nullptr;
  llvm::Argument *state_interface_ = nullptr;
  llvm::Argument *chain_ = nullptr;
  llvm::BasicBlock *entry_ = nullptr;
  llvm::AllocaInst *cycles_ = nullptr;
  llvm::AllocaInst *guard_cycles_local_ = nullptr;
  llvm::Value *pending_cycles_ = nullptr;
  // Shared across generated calls until control returns to the dispatcher.
  llvm::Value *guard_cycles_ = nullptr;
  // Termination backstop for zero-cycle loops.
  llvm::Value *guard_steps_ = nullptr;
  llvm::Value *control_pc_ = nullptr;
  llvm::Value *entry_pc_ = nullptr;
  llvm::Value *return_pc_ = nullptr;
  llvm::Value *initial_cycles_ = nullptr;
  std::array<llvm::Argument *, DOLIR_STATE_COUNT> native_inputs_{};
  llvm::Value *ram_ = nullptr;
  llvm::Value *ram_size_ = nullptr;
  llvm::Value *mem2_ = nullptr;
  llvm::Value *mem2_size_ = nullptr;
  llvm::BasicBlock *fallback_block_ = nullptr;
  llvm::PHINode *fallback_pc_ = nullptr;
  llvm::BasicBlock *shared_side_exit_ = nullptr;
  llvm::PHINode *shared_side_exit_pc_ = nullptr, *shared_side_exit_reason_ = nullptr;
  llvm::BasicBlock *memory_service_failure_ = nullptr;
  llvm::PHINode *memory_service_failure_reason_ = nullptr, *memory_service_failure_elapsed_ = nullptr;
  std::array<llvm::Value *, DOLIR_STATE_COUNT> state_{};
  std::array<llvm::AllocaInst *, 32> pair_f32_{};
  std::array<llvm::AllocaInst *, 32> pair_f64_{};
  std::array<FPRepresentation, 32> fp_rep_{};
  std::array<bool, 32> fp_exact_single_{};
  std::array<bool, 32> fp_denormal_safe_{};
  std::array<FPValueClass, 32> fp_value_class_{};
  std::array<llvm::ConstantInt *, DOLIR_STATE_COUNT> known_state_{};
  std::array<bool, 8> stable_gqr_{};
  bool stable_hid2_ = false;
  bool stable_ni_ = false;
  std::array<llvm::Value *, 8> entry_gqr_{};
  std::array<llvm::Value *, 8> entry_gqr_load_type_{};
  std::array<llvm::Value *, 8> entry_gqr_store_type_{};
  std::array<llvm::Value *, 8> entry_gqr_load_scale_{};
  std::array<llvm::Value *, 8> entry_gqr_store_scale_{};
  llvm::Value *entry_psq_direct_enabled_ = nullptr;
  llvm::Value *entry_psq_indexed_enabled_ = nullptr;
  llvm::Value *entry_ni_enabled_ = nullptr;
  bool psq_direct_proven_ = false;
  bool psq_indexed_proven_ = false;
  llvm::Value *pending_fprf_ = nullptr;
  std::array<bool, DOLIR_STATE_COUNT> used_{};
  std::array<bool, DOLIR_STATE_COUNT> dirty_{};
  std::vector<llvm::BasicBlock *> blocks_;
  std::vector<bool> region_leaders_;
  std::vector<bool> loop_headers_;
  std::vector<llvm::Value *> values_;
  std::vector<u32> continuations_;
  const DolLLVMFunctionRange *ranges_ = nullptr; u32 range_count_ = 0;
  const DolLLVMCallEdge *call_edges_ = nullptr; u32 call_edge_count_ = 0; const DolLLVMPatch *patches_ = nullptr; u32 patch_count_ = 0;
  const DolLLVMFunctionRange *abi_range_ = nullptr;
  bool native_abi_ = false;
  bool cold_escapes_ = false;
  bool intrinsic_escapes_ = false;
  const u32 *entry_points_ = nullptr;
  u32 entry_point_count_ = 0;
  bool write_journal_ = false;
  DolLLVMSemantics semantics_ = DOLLLVM_SEMANTICS_EXACT;
  std::string symbol_suffix_;
  bool fixed_memory_layout_ = false;
  bool state_in_memory_ = false;
  bool modern_runtime_ = false;
  u32 expected_ram_size_ = 0, expected_mem2_size_ = 0;
  u32 current_pc_ = 0;
  llvm::Value *service_yield_ = nullptr;
  bool service_yield_used_ = false, fp_available_checked_ = false;
};
} // namespace dolllvm
#endif
