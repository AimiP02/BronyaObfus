#ifndef LLVM_INDIRECT_CALL_H
#define LLVM_INDIRECT_CALL_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include <Obfuscation/PassRegistry.h>
#include <map>
#include <stdint.h>

namespace llvm {

class IndirectCallPass : public PassInfoMixin<IndirectCallPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  bool runIndirectCall(Function &F);
  GlobalVariable *getIndirectCallees(Function &F, ConstantInt *EncKey);
  void numberCallees(Function &F);

  static bool isRequired() { return true; }

private:
  std::map<Function *, uint32_t> CalleeNumbering;
  std::vector<CallInst *> CallSites;
  std::vector<Function *> CalleeList;
};

} // namespace llvm

#endif