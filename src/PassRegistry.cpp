#include "Obfuscation/PassRegistry.h"
#include "Obfuscation/BogusControlFlow.h"
#include "Obfuscation/Flattening.h"
#include "Obfuscation/MBAObfuscation.h"
#include "Obfuscation/StringObfuscation.h"
#include "Obfuscation/IndirectCall.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/OptimizationLevel.h"

using namespace llvm;

llvm::PassPluginLibraryInfo getBronyaObfusPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "BronyaObfus", "v0.1", [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef PassName, FunctionPassManager &FPM, ...) {
                  if (PassName == "bogus-control-flow") {
                    FPM.addPass(BogusControlFlowPass());
                    return true;
                  }

                  if (PassName == "flattening") {
                    FPM.addPass(FlatteningPass());
                    return true;
                  }

                  if (PassName == "mba-substitute") {
                    FPM.addPass(MBAObfuscationPass());
                    return true;
                  }

                  if (PassName == "indirect-call") {
                    FPM.addPass(IndirectCallPass());
                    return true;
                  }

                  return false;
                });

            PB.registerPipelineParsingCallback(
                [](StringRef PassName, ModulePassManager &MPM, ...) {
                  if (PassName == "string-obfus") {
                    MPM.addPass(StringObfuscationPass());
                    return true;
                  }
                  return false;
                });

            PB.registerPipelineStartEPCallback([](ModulePassManager &MPM,
                                                  OptimizationLevel Level) {
              FunctionPassManager FPM;
              //FPM.addPass(BogusControlFlowPass());
              //FPM.addPass(FlatteningPass());
              //FPM.addPass(MBAObfuscationPass());
              FPM.addPass(IndirectCallPass());

              MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));

              //MPM.addPass(StringObfuscationPass());
            });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getBronyaObfusPluginInfo();
}