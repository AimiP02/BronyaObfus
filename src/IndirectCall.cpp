#include "Obfuscation/IndirectCall.h"
#include "Obfuscation/CryptoUtils.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <string>
#include <vector>

using namespace llvm;

static cl::opt<bool>
    RunIndirectCallPass("indirect-call", cl::init(false),
                        cl::desc("BronyaObfus - IndirectCallPass"));

void IndirectCallPass::numberCallees(Function &F) {
  for (auto &BB : F) {
    for (auto &Inst : BB) {
      if (auto *CastInst = dyn_cast<CallInst>(&Inst); CastInst != nullptr) {
        auto *CB = dyn_cast<CallBase>(&Inst);
        auto *Callee = CB->getCalledFunction();

        if (Callee == nullptr) {
          continue;
        }

        if (Callee->isIntrinsic()) {
          continue;
        }

        auto *CallSite = dyn_cast<CallInst>(&Inst);

        CallSites.push_back(CallSite);

        if (!CalleeNumbering.count(Callee)) {
          CalleeNumbering[Callee] = CalleeList.size();
          CalleeList.push_back(Callee);
        }
      }
    }
  }
}

GlobalVariable *IndirectCallPass::getIndirectCallees(Function &F,
                                                     ConstantInt *EncKey) {
  auto &Ctx = F.getContext();
  std::string GVName(F.getName().str() + "_IndirectCallees");
  GlobalVariable *GV = F.getParent()->getNamedGlobal(GVName);

  if (GV) {
    return GV;
  }

  std::vector<Constant *> Elements;

  for (auto *Callee : CalleeList) {
    Constant *CE = ConstantExpr::getBitCast(Callee, Type::getInt8PtrTy(Ctx));
    CE = ConstantExpr::getGetElementPtr(Type::getInt8Ty(Ctx), CE, EncKey);
    Elements.push_back(CE);
  }

  ArrayType *ArrTy = ArrayType::get(Type::getInt8PtrTy(Ctx), Elements.size());
  Constant *CA = ConstantArray::get(ArrTy, ArrayRef<Constant *>(Elements));
  GV =
      new GlobalVariable(*F.getParent(), ArrTy, false,
                         GlobalValue::LinkageTypes::PrivateLinkage, CA, GVName);

  appendToCompilerUsed(*F.getParent(), {GV});

  return GV;
}

bool IndirectCallPass::runIndirectCall(Function &F) {
  CalleeNumbering.clear();
  CalleeList.clear();
  CallSites.clear();

  auto &Ctx = F.getContext();

  numberCallees(F);

  if (CalleeList.empty()) {
    return true;
  }

  uint64_t Key = cryptoutils->get_uint64_t();

  IntegerType *Int64Ty = Type::getInt32Ty(Ctx);

  auto *PosEncKey = ConstantInt::get(Int64Ty, Key, false);
  auto *NegEncKey = ConstantInt::get(Int64Ty, -Key, false);

  auto *MySecret = ConstantInt::get(Int64Ty, 0, true);

  auto *Zero = ConstantInt::get(Int64Ty, 0);

  GlobalVariable *Targets = getIndirectCallees(F, NegEncKey);

  for (auto *CS : CallSites) {
    SmallVector<Value *, 8> Args;
    SmallVector<AttributeSet, 8> ArgAttrVec;

    CallBase *CB = dyn_cast<CallBase>(CS);

    Function *Callee = CB->getCalledFunction();
    FunctionType *FuncType = CB->getFunctionType();

    IRBuilder<> Builder(CB);

    Args.clear();
    ArgAttrVec.clear();

    Value *Idx = ConstantInt::get(Int64Ty, CalleeNumbering[Callee]);
    Value *GEP = Builder.CreateGEP(Targets->getType(), Targets, {Zero, Idx});
    LoadInst *EncDestAddr =
        Builder.CreateLoad(GEP->getType(), GEP, CS->getName());

    const AttributeList &CallPAL = CB->getAttributes();
    auto *Arg = CB->arg_begin();
    uint32_t i = 0;

    for (uint32_t e = FuncType->getNumParams(); i != e; i++, Arg++) {
      Args.push_back(*Arg);
      ArgAttrVec.push_back(CallPAL.getParamAttrs(i));
    }

    for (auto *E = CB->arg_end(); Arg != E; i++, Arg++) {
      Args.push_back(*Arg);
      ArgAttrVec.push_back(CallPAL.getParamAttrs(i));
    }

    Value *Secret = Builder.CreateAdd(PosEncKey, MySecret);
    Value *DestAddr =
        Builder.CreateGEP(Type::getInt8Ty(Ctx), EncDestAddr, Secret);

    Value *FuncPtr = Builder.CreateBitCast(DestAddr, FuncType->getPointerTo());
    FuncPtr->setName("Call_" + Callee->getName());
    CB->setCalledOperand(FuncPtr);
  }
  return false;
}

PreservedAnalyses IndirectCallPass::run(Function &F,
                                        FunctionAnalysisManager &AM) {
  if (!runIndirectCall(F))
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}