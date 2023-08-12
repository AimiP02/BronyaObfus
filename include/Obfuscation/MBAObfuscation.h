#ifndef LLVM_MBA_OBFUSCATION_H
#define LLVM_MBA_OBFUSCATION_H

#include "llvm/IR/PassManager.h"
#include <Obfuscation/PassRegistry.h>

namespace llvm {

class MBAObfuscationPass : public PassInfoMixin<MBAObfuscationPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

} // namespace llvm

#endif