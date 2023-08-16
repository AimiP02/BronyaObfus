#include "Obfuscation/Flattening.h"
#include "Obfuscation/CryptoUtils.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <stdint.h>

using namespace llvm;

static cl::opt<bool>
    RunFlatteningPass("flattening", cl::init(false),
                      cl::desc("BronyaObfus - Flattening Control Flow"));

namespace {

void fixStack(Function &F) {
  std::vector<PHINode *> OrigPHI;
  std::vector<Instruction *> OrigReg;
  BasicBlock &EntryBB = F.getEntryBlock();

  for (auto &BB : F) {
    for (auto &Inst : BB) {
      if (PHINode *PN = dyn_cast<PHINode>(&Inst)) {
        OrigPHI.push_back(PN);
      } else if (!(isa<AllocaInst>(&Inst) && Inst.getParent() == &EntryBB) &&
                 Inst.isUsedOutsideOfBlock(&BB)) {
        OrigReg.push_back(&Inst);
      }
    }
  }

  for (auto *PN : OrigPHI) {
    DemotePHIToStack(PN, EntryBB.getTerminator());
  }

  for (auto *Inst : OrigReg) {
    DemoteRegToStack(*Inst, EntryBB.getTerminator());
  }
}

bool runFlattening(Function &F) {
  outs() << "Start Flattening Pass.\n";

  outs() << "FunctionName: " << F.getName() << "\n";

  auto &Ctx = F.getContext();

  if (F.size() <= 1) {
    outs() << "This function has only one basicblock.\n";
    return false;
  }

  // outs() << "Count of BasicBlocks: " << F.size() << "\n";

  std::vector<BasicBlock *> OrigBB;

  // int index = 0;
  for (auto &BB : F) {
    // outs() << index++ << "\n";
    // OutputBasicBlock(&BB);
    if (isa<InvokeInst>(BB.getTerminator())) {
	  outs() << "This function has invoke instruction. Not support now!\n";
	  return false;
	}

    OrigBB.emplace_back(&BB);
  }

  OrigBB.erase(OrigBB.begin());
  BasicBlock &EntryBB = F.getEntryBlock();

  if (BranchInst *Br = dyn_cast<BranchInst>(EntryBB.getTerminator())) {
    if (Br->isConditional()) {
      BasicBlock *NewBB = EntryBB.splitBasicBlock(Br, "newBB");
      OrigBB.insert(OrigBB.begin(), NewBB);
    }
  }

  // Find all invoke instructions and delete unwind basicblock
  //std::vector<BasicBlock *> RemoveBB;
  //for (auto *BB : OrigBB) {
  //  Value *Terminate = BB->getTerminator();
  //  if (isa<InvokeInst>(*Terminate)) {
  //    InvokeInst *Invoke = dyn_cast<InvokeInst>(Terminate);
  //    RemoveBB.push_back(Invoke->getUnwindDest());
  //  }
  //}

  //for (auto *BB : RemoveBB) {
  //  if (auto Found = std::find(OrigBB.begin(), OrigBB.end(), BB);
  //      Found != OrigBB.end()) {
  //    OrigBB.erase(Found);
  //  }
  //}

  // create dispatch basicblock and ret basicblock
  BasicBlock *DispatchBB = BasicBlock::Create(Ctx, "dispatchBB", &F, &EntryBB);
  BasicBlock *ReturnBB = BasicBlock::Create(Ctx, "returnBB", &F, &EntryBB);
  BranchInst::Create(DispatchBB, ReturnBB);
  EntryBB.moveBefore(DispatchBB);

  EntryBB.getTerminator()->eraseFromParent();
  BranchInst *BrDispatchBB = BranchInst::Create(DispatchBB, &EntryBB);

  uint32_t RandNum = cryptoutils->get_uint32_t();
  AllocaInst *SwVarPtr =
      new AllocaInst(Type::getInt32Ty(Ctx), 0, "swVar.ptr", BrDispatchBB);
  new StoreInst(ConstantInt::get(Type::getInt32Ty(Ctx), RandNum), SwVarPtr,
                BrDispatchBB);
  LoadInst *SwVar =
      new LoadInst(Type::getInt32Ty(Ctx), SwVarPtr, "swVar", DispatchBB);
  BasicBlock *SwDefault = BasicBlock::Create(Ctx, "swDefault", &F, ReturnBB);
  BranchInst::Create(ReturnBB, SwDefault);
  SwitchInst *SwInst = SwitchInst::Create(SwVar, SwDefault, 0, DispatchBB);

  // Set switch case value
  for (auto *BB : OrigBB) {
    BB->moveBefore(ReturnBB);
    SwInst->addCase(ConstantInt::get(Type::getInt32Ty(Ctx), RandNum), BB);
    RandNum = cryptoutils->get_uint32_t();
  }

  // outs() << "Get SwitchVariable\n";

  for (auto *BB : OrigBB) {
    if (BB->getTerminator()->getNumSuccessors() == 0)
      continue;
    if (BB->getTerminator()->getNumSuccessors() == 1) {
      // direct jump
      auto *NumCase =
          SwInst->findCaseDest(BB->getTerminator()->getSuccessor(0));
      BB->getTerminator()->eraseFromParent();

      if (NumCase == nullptr) {
          NumCase = ConstantInt::get(Type::getInt32Ty(Ctx), cryptoutils->get_uint32_t());
      }

      new StoreInst(NumCase, SwVarPtr, BB);
      BranchInst::Create(ReturnBB, BB);
    } else if (BB->getTerminator()->getNumSuccessors() == 2) {
      // conditional jump
      auto *NumCaseTrue = SwInst->findCaseDest(BB->getTerminator()->getSuccessor(0));
      auto *NumCaseFalse = SwInst->findCaseDest(BB->getTerminator()->getSuccessor(1));

      if (NumCaseTrue == nullptr) {
          NumCaseTrue = ConstantInt::get(Type::getInt32Ty(Ctx), cryptoutils->get_uint32_t());
      }

      if (NumCaseFalse == nullptr) {
          NumCaseFalse = ConstantInt::get(Type::getInt32Ty(Ctx), cryptoutils->get_uint32_t());
      }

      if (BranchInst* BrInst = dyn_cast<BranchInst>(BB->getTerminator())) {
          SelectInst* Sel = SelectInst::Create(BrInst->getCondition(), NumCaseTrue, NumCaseFalse, "", BB->getTerminator());
          BB->getTerminator()->eraseFromParent();

          new StoreInst(Sel, SwVarPtr, BB);
          BranchInst::Create(ReturnBB, BB);
      }
    }
  }

  fixStack(F);

  //outs() << "Now Function has been modified: \n";
  //for (auto& BB : F) {
  //    for (auto& Inst : BB) {
  //        Inst.print(outs());
  //        outs() << "\n";
  //    }
  //}

  outs() << "Finished Flattening Pass.\n";
  return false;
}

} // namespace

PreservedAnalyses FlatteningPass::run(Function &F,
                                      FunctionAnalysisManager &AM) {
  if (!runFlattening(F))
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}