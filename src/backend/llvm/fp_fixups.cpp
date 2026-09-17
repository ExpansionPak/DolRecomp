#include "backend/llvm/fp_fixups.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Triple.h>

#include "common/types.h"

namespace dolllvm {

using namespace llvm;

static void markColdFixup(Function *function) {
  function->setCallingConv(CallingConv::PreserveAll);
  function->addFnAttr(Attribute::Cold);
  function->addFnAttr(Attribute::NoInline);
  function->addFnAttr(Attribute::NoUnwind);
  function->addFnAttr(Attribute::WillReturn);
  // Mach-O section specifiers are "__SEGMENT,__section" and its writer rejects
  // anything else outright, so a bare ".text.unlikely.*" name aborts emission on
  // Darwin targets ("invalid section specifier"). ELF and COFF both accept the
  // name, and the Cold plus NoInline attributes already carry the placement hint
  // on every target, so dropping only the name on Mach-O costs nothing.
  if (!Triple(function->getParent()->getTargetTriple()).isOSBinFormatMachO())
    function->setSection(".text.unlikely." + function->getName().str());
}

Function *getPairNaNFixup(Module &module, Type *pairType) {
  bool single = pairType->getScalarType()->isFloatTy();
  const char *name = single ? "fix_pair_nan_f32" : "fix_pair_nan_f64";
  if (Function *existing = module.getFunction(name))
    return existing;

  LLVMContext &context = module.getContext();
  Function *fixup = Function::Create(
      FunctionType::get(pairType, {pairType, pairType, pairType}, false),
      GlobalValue::InternalLinkage, name, module);
  markColdFixup(fixup);

  auto argument = fixup->arg_begin();
  Value *result = &*argument++;
  Value *left = &*argument++;
  Value *right = &*argument;
  BasicBlock *entry = BasicBlock::Create(context, "entry", fixup);
  IRBuilder<> builder(entry);
  unsigned width = pairType->getScalarSizeInBits();
  Type *integerPair = FixedVectorType::get(IntegerType::get(context, width), 2);
  auto splat = [integerPair](u64 value) {
    return ConstantVector::getSplat(
        ElementCount::getFixed(2),
        ConstantInt::get(integerPair->getScalarType(), value));
  };
  u64 quietBit = single ? 0x00400000u : 0x0008000000000000ull;
  u64 defaultNaN = single ? 0x7FC00000u : 0x7FF8000000000000ull;
  Value *leftNaN = builder.CreateFCmpUNO(left, left);
  Value *rightNaN = builder.CreateFCmpUNO(right, right);
  Value *resultNaN = builder.CreateFCmpUNO(result, result);
  Value *quietLeft = builder.CreateBitCast(
      builder.CreateOr(builder.CreateBitCast(left, integerPair),
                       splat(quietBit)),
      pairType);
  Value *quietRight = builder.CreateBitCast(
      builder.CreateOr(builder.CreateBitCast(right, integerPair),
                       splat(quietBit)),
      pairType);
  Value *fallback = builder.CreateSelect(
      resultNaN, builder.CreateBitCast(splat(defaultNaN), pairType), result);
  builder.CreateRet(builder.CreateSelect(
      leftNaN, quietLeft,
      builder.CreateSelect(rightNaN, quietRight, fallback)));
  return fixup;
}

Function *getFPRFUnusualFixup(Module &module) {
  constexpr const char *name = "classify_fprf_unusual";
  if (Function *existing = module.getFunction(name))
    return existing;

  LLVMContext &context = module.getContext();
  Type *i32 = Type::getInt32Ty(context);
  Function *fixup =
      Function::Create(FunctionType::get(i32, {i32}, false),
                       GlobalValue::InternalLinkage, name, module);
  markColdFixup(fixup);

  Value *bits = fixup->getArg(0);
  BasicBlock *entry = BasicBlock::Create(context, "entry", fixup);
  IRBuilder<> builder(entry);
  Value *sign = builder.CreateICmpNE(
      builder.CreateAnd(bits, builder.getInt32(0x80000000u)),
      builder.getInt32(0));
  Value *exponent = builder.CreateAnd(bits, builder.getInt32(0x7F800000u));
  Value *hasFraction = builder.CreateICmpNE(
      builder.CreateAnd(bits, builder.getInt32(0x007FFFFFu)),
      builder.getInt32(0));
  Value *infinity = builder.CreateSelect(sign, builder.getInt32(0x09u),
                                         builder.getInt32(0x05u));
  Value *specialClass =
      builder.CreateSelect(hasFraction, builder.getInt32(0x11u), infinity);
  Value *zero = builder.CreateSelect(sign, builder.getInt32(0x12u),
                                     builder.getInt32(0x02u));
  Value *subnormal = builder.CreateSelect(sign, builder.getInt32(0x18u),
                                          builder.getInt32(0x14u));
  Value *zeroClass = builder.CreateSelect(hasFraction, subnormal, zero);
  builder.CreateRet(builder.CreateSelect(
      builder.CreateICmpEQ(exponent, builder.getInt32(0x7F800000u)),
      specialClass, zeroClass));
  return fixup;
}

Function *getPairFMANaNFixup(Module &module, Type *pairType) {
  bool single = pairType->getScalarType()->isFloatTy();
  const char *name = single ? "fix_pair_fma_nan_f32" : "fix_pair_fma_nan_f64";
  if (Function *existing = module.getFunction(name))
    return existing;

  LLVMContext &context = module.getContext();
  Function *fixup = Function::Create(
      FunctionType::get(pairType, {pairType, pairType, pairType, pairType},
                        false),
      GlobalValue::InternalLinkage, name, module);
  markColdFixup(fixup);
  auto argument = fixup->arg_begin();
  Value *result = &*argument++;
  Value *left = &*argument++;
  Value *addend = &*argument++;
  Value *multiplier = &*argument;
  BasicBlock *entry = BasicBlock::Create(context, "entry", fixup);
  IRBuilder<> builder(entry);
  unsigned width = pairType->getScalarSizeInBits();
  Type *integerPair = FixedVectorType::get(IntegerType::get(context, width), 2);
  auto splat = [integerPair](u64 value) {
    return ConstantVector::getSplat(
        ElementCount::getFixed(2),
        ConstantInt::get(integerPair->getScalarType(), value));
  };
  u64 quietBit = single ? 0x00400000u : 0x0008000000000000ull;
  u64 defaultNaN = single ? 0x7FC00000u : 0x7FF8000000000000ull;
  auto quiet = [&](Value *value) {
    return builder.CreateBitCast(
        builder.CreateOr(builder.CreateBitCast(value, integerPair),
                         splat(quietBit)),
        pairType);
  };
  Value *fallback = builder.CreateSelect(
      builder.CreateFCmpUNO(result, result),
      builder.CreateBitCast(splat(defaultNaN), pairType), result);
  Value *selected =
      builder.CreateSelect(builder.CreateFCmpUNO(multiplier, multiplier),
                           quiet(multiplier), fallback);
  selected = builder.CreateSelect(builder.CreateFCmpUNO(addend, addend),
                                  quiet(addend), selected);
  selected = builder.CreateSelect(builder.CreateFCmpUNO(left, left),
                                  quiet(left), selected);
  builder.CreateRet(selected);
  return fixup;
}

Function *getPairFMARoundingFixup(Module &module) {
  constexpr const char *name = "fix_pair_fma_rounding";
  if (Function *existing = module.getFunction(name))
    return existing;

  LLVMContext &context = module.getContext();
  Type *pairType = FixedVectorType::get(Type::getDoubleTy(context), 2);
  Type *integerPair = FixedVectorType::get(Type::getInt64Ty(context), 2);
  Function *fixup = Function::Create(
      FunctionType::get(pairType, {pairType, pairType, pairType, pairType},
                        false),
      GlobalValue::InternalLinkage, name, module);
  markColdFixup(fixup);
  auto argument = fixup->arg_begin();
  Value *result = &*argument++;
  Value *left = &*argument++;
  Value *multiplier = &*argument++;
  Value *addend = &*argument;
  BasicBlock *entry = BasicBlock::Create(context, "entry", fixup);
  IRBuilder<> builder(entry);
  Function *fma = Intrinsic::getDeclaration(
      &module, Intrinsic::experimental_constrained_fma, {pairType});
  auto binary = [&](Intrinsic::ID id, Value *a, Value *b) {
    return builder.CreateConstrainedFPBinOp(
        id, a, b, nullptr, "", nullptr, RoundingMode::Dynamic, fp::ebIgnore);
  };
  Value *aPrime =
      binary(Intrinsic::experimental_constrained_fsub, addend, result);
  Value *bPrime =
      binary(Intrinsic::experimental_constrained_fadd, result, aPrime);
  Value *deltaA = builder.CreateConstrainedFPCall(
      fma, {left, multiplier, aPrime}, "", RoundingMode::Dynamic, fp::ebIgnore);
  Value *deltaB =
      binary(Intrinsic::experimental_constrained_fsub, addend, bPrime);
  Value *error =
      binary(Intrinsic::experimental_constrained_fadd, deltaA, deltaB);
  Value *bits = builder.CreateBitCast(result, integerPair);
  Value *tie = builder.CreateICmpEQ(
      builder.CreateAnd(bits, ConstantVector::getSplat(
                                  ElementCount::getFixed(2),
                                  ConstantInt::get(integerPair->getScalarType(),
                                                   0x000000001FFFFFFFull))),
      ConstantVector::getSplat(ElementCount::getFixed(2),
                               ConstantInt::get(integerPair->getScalarType(),
                                                0x0000000010000000ull)));
  Value *zero = ConstantAggregateZero::get(pairType);
  Value *nonzero = builder.CreateFCmpONE(error, zero);
  Value *sameDirection = builder.CreateICmpEQ(
      builder.CreateFCmpOGT(error, zero), builder.CreateFCmpOGT(result, zero));
  Value *one = ConstantVector::getSplat(
      ElementCount::getFixed(2),
      ConstantInt::get(integerPair->getScalarType(), 1));
  Value *delta = builder.CreateSelect(
      sameDirection, one, ConstantExpr::getNeg(cast<Constant>(one)));
  Value *corrected = builder.CreateAdd(bits, delta);
  builder.CreateRet(builder.CreateBitCast(
      builder.CreateSelect(builder.CreateAnd(tie, nonzero), corrected, bits),
      pairType));
  return fixup;
}

} // namespace dolllvm
