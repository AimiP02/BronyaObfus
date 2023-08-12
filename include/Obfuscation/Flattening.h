#ifndef LLVM_FLATTENING_H
#define LLVM_FLATTENING_H

#include "llvm/IR/PassManager.h"
#include <Obfuscation/PassRegistry.h>

namespace llvm {

class FlatteningPass : public PassInfoMixin<FlatteningPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

} // namespace llvm

#endif