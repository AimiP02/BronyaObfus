#ifndef LLVM_BOGUS_CONTROL_FLOW_H
#define LLVM_BOGUS_CONTROL_FLOW_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include <llvm/Transforms/Obfuscation/PassRegistry.h>

namespace llvm {

class BogusControlFlowPass : public PassInfoMixin<BogusControlFlowPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

} // namespace llvm

#endif