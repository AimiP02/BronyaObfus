#ifndef LLVM_STRING_OBFUSCATION_H
#define LLVM_STRING_OBFUSCATION_H

#include "llvm/IR/LLVMContext.h"
#include <llvm/Transforms/Obfuscation/PassRegistry.h>

namespace llvm {

class StringObfuscationPass : public PassInfoMixin<StringObfuscationPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  static bool isRequired() { return true; }
};

} // namespace llvm

#endif