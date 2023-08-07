#include "Obfuscation/BogusControlFlow.h"
#include "Obfuscation/CryptoUtils.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

#include <cstring>
#include <random>
#include <stdint.h>
#include <vector>

using namespace llvm;

static cl::opt<bool>
    RunBogusControlFlowPass("bogus-control-flow", cl::init(false),
                            cl::desc("BronyaObfus - BogusControlFlow"));

static cl::opt<int>
    ObfuTimes("bcf-times", cl::init(1),
              cl::desc("Run BogusControlFlow pass <bcf-times> time(s)"));

static cl::opt<int> ObfuProbRate(
    "bcf-prob", cl::init(30),
    cl::desc("Choose the probability <bcf-prob> for each basic blocks will "
             "be obfuscated by BCF Pass"));

namespace {

#define OVERFLOW_MASK (0x3FF)

std::vector<BasicBlock *> UsefulBB;

static const uint32_t Primes[] = {
    127,  131,  137,  139,  149,  151,  157,  163,  167,  173,  179,  181,
    191,  193,  197,  199,  211,  223,  227,  229,  233,  239,  241,  251,
    257,  263,  269,  271,  277,  281,  283,  293,  307,  311,  313,  317,
    331,  337,  347,  349,  353,  359,  367,  373,  379,  383,  389,  397,
    401,  409,  419,  421,  431,  433,  439,  443,  449,  457,  461,  463,
    467,  479,  487,  491,  499,  503,  509,  521,  523,  541,  547,  557,
    563,  569,  571,  577,  587,  593,  599,  601,  607,  613,  617,  619,
    631,  641,  643,  647,  653,  659,  661,  673,  677,  683,  691,  701,
    709,  719,  727,  733,  739,  743,  751,  757,  761,  769,  773,  787,
    797,  809,  811,  821,  823,  827,  829,  839,  853,  857,  859,  863,
    877,  881,  883,  887,  907,  911,  919,  929,  937,  941,  947,  953,
    967,  971,  977,  983,  991,  997,  1009, 1013, 1019, 1021, 1031, 1033,
    1039, 1049, 1051, 1061, 1063, 1069, 1087, 1091, 1093, 1097, 1103, 1109,
    1117, 1123, 1129, 1151, 1153, 1163, 1171, 1181, 1187, 1193, 1201, 1213,
    1217, 1223, 1229, 1231, 1237, 1249, 1259, 1277, 1279, 1283, 1289, 1291,
    1297, 1301, 1303, 1307, 1319, 1321, 1327, 1361, 1367, 1373, 1381, 1399,
    1409, 1423, 1427, 1429, 1433, 1439, 1447, 1451, 1453, 1459, 1471, 1481,
    1483, 1487, 1489, 1493, 1499, 1511, 1523, 1531, 1543, 1549, 1553, 1559,
    1567, 1571, 1579, 1583, 1597, 1601, 1607, 1609, 1613, 1619, 1621};

bool checkContainInlineASM(BasicBlock &BB) {
  for (auto &Inst : BB) {
    for (uint32_t i = 0; i < Inst.getNumOperands(); i++) {
      if (isa<InlineAsm>(Inst.getOperand(i)))
        return true;
    }
  }
  return false;
}

BasicBlock *createJunkBB(Function &F) {
  auto &Ctx = F.getContext();
  Module *M = F.getParent();
  auto RNG = M->createRNG("BronyaObfus");
  BasicBlock *Junk = BasicBlock::Create(Ctx, "junk", &F);

  bool isX64 =
      Triple(F.getParent()->getTargetTriple()).getArch() == Triple::x86_64;
  // only support x64
  if (isX64) {
    auto *FuncType = FunctionType::get(Type::getVoidTy(Ctx), false);
    IRBuilder<> Builder(Junk);

    uint8_t jmp_op[] = {0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
                        0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f};

    for (int i = 0; i < 5; i++) {
      std::string junk_asm = "";

      if (i != 4) {
        junk_asm +=
            ".byte " + std::to_string(jmp_op[(*RNG)() % sizeof(jmp_op)]) + "\n";
        junk_asm += ".byte " + std::to_string((*RNG)() % 0x100) + "\n";
      } else {
        junk_asm +=
            ".byte 143\n.byte " + std::to_string((*RNG)() % 0x100) + "\n";
      }
      InlineAsm *IA = InlineAsm::get(FuncType, junk_asm, "", true, false);
      Builder.CreateCall(IA);
    }

    Builder.CreateUnreachable();
    Junk->moveAfter(&*F.begin());
    return Junk;
  }

  return nullptr;
}

BasicBlock *createAlteredBasicBlock(BasicBlock *BB) {
  std::vector<Instruction *> OrigReg;
  auto &EntryBB = BB->getParent()->getEntryBlock();

  for (auto &Inst : *BB) {
    if (!(isa<AllocaInst>(&Inst) && Inst.getParent() == &EntryBB) &&
        Inst.isUsedOutsideOfBlock(BB)) {
      OrigReg.push_back(&Inst);
    }
  }

  for (auto *Inst : OrigReg) {
    DemoteRegToStack(*Inst, EntryBB.getTerminator());
  }

  // repair connection of variable between altered and body
  ValueToValueMapTy VMap;
  BasicBlock *AlteredBB =
      CloneBasicBlock(BB, VMap, "AlteredBB", BB->getParent());
  // auto OrigIter = BB->begin();

  for (auto &Inst : *AlteredBB) {
    for (unsigned int i = 0; i < Inst.getNumOperands(); i++) {
      Value *V = MapValue(Inst.getOperand(i), VMap);
      if (V) {
        Inst.setOperand(i, V);
      }
    }
    // OrigIter++;
  }

  return AlteredBB;
}

void OutputBasicBlock(BasicBlock *BB) {
  outs() << "BasicBlock: " << BB->getName() << "\n";
  for (auto &Inst : *BB) {
    Inst.print(outs());
    outs() << "\n";
  }
}

Value *createSimpleBogusCMP(BasicBlock *BB) {
  // y < 10 || x * (x + 1) % 2 == 0
  Module *M = BB->getModule();
  auto &Ctx = M->getContext();

  Type *Int32Ty = Type::getInt32Ty(Ctx);

  GlobalVariable *PtrX =
      new GlobalVariable(*M, Int32Ty, false, GlobalValue::CommonLinkage,
                         ConstantInt::get(Int32Ty, 0, false), "x");
  GlobalVariable *PtrY =
      new GlobalVariable(*M, Int32Ty, false, GlobalValue::CommonLinkage,
                         ConstantInt::get(Int32Ty, 0, false), "y");

  auto *X = new LoadInst(Int32Ty, PtrX, "", BB);
  auto *Y = new LoadInst(Int32Ty, PtrY, "", BB);

  // y < 10
  auto *Cond1 = new ICmpInst(*BB, CmpInst::ICMP_SLT, Y,
                             ConstantInt::get(Int32Ty, 10, false));

  // x * (x + 1) % 2
  auto *op1 =
      BinaryOperator::CreateAdd(X, ConstantInt::get(Int32Ty, 1, false), "", BB);
  auto *op2 = BinaryOperator::CreateMul(op1, X, "", BB);
  auto *op3 = BinaryOperator::CreateURem(
      op2, ConstantInt::get(Int32Ty, 2, false), "", BB);

  auto *Cond2 = new ICmpInst(*BB, CmpInst::ICMP_EQ, op3,
                             ConstantInt::get(Int32Ty, 0, false));

  return BinaryOperator::CreateOr(Cond1, Cond2, "", BB);
}

Value *createComplexBogusCMP(BasicBlock *BB) {
  // prime1 * (((x & 0x3FF) | any1)^2) != prime2 * (((y & 0x3FF) | any2)^2)
  Module *M = BB->getModule();
  Type *PrimeLLVMTy =
      IntegerType::getIntNTy(M->getContext(), sizeof(uint32_t) * 8);

  // get random generator
  auto RNG = M->createRNG("BronyaObfus");

  // get prime
  uint32_t _prime1 = 0, _prime2 = 0;
  size_t PrimeCount = sizeof(Primes) / sizeof(*Primes);
  do {
    _prime1 = Primes[(*RNG)() % PrimeCount];
    _prime2 = Primes[(*RNG)() % PrimeCount];
  } while (_prime1 == _prime2);

  // get any
  uint32_t _any1 = 0, _any2 = 0;
  do {
    _any1 = (*RNG)() & OVERFLOW_MASK;
    _any2 = (*RNG)() & OVERFLOW_MASK;
  } while ((_any1 == _any2) || !_any1 || !_any2);

  // get variable
  // outs() << "Collect useful variables: \n";
  // for (auto &Inst : UsefulVar) {
  //   Inst->print(outs());
  //   outs() << "\n";
  // }

  IRBuilder<> Builder(BB, BB->end());

  // select two variable from program
  // TODO: when selecting variable, it will select a variable
  //       that is used in other basic blocks, which will cause
  //       the variable can't dominate the basic block.
  auto *V1 = Builder.CreateAlloca(PrimeLLVMTy);
  auto *V2 = Builder.CreateAlloca(PrimeLLVMTy);

  // get llvm type variable
  auto *Prime1 = ConstantInt::get(PrimeLLVMTy, _prime1);
  auto *Prime2 = ConstantInt::get(PrimeLLVMTy, _prime2);
  auto *Any1 = ConstantInt::get(PrimeLLVMTy, _any1);
  auto *Any2 = ConstantInt::get(PrimeLLVMTy, _any2);
  auto *X = Builder.CreateLoad(PrimeLLVMTy, V1);
  auto *Y = Builder.CreateLoad(PrimeLLVMTy, V2);
  auto *OverflowMask = ConstantInt::get(PrimeLLVMTy, OVERFLOW_MASK);

  Value *LHSCast = Builder.CreateZExtOrTrunc(X, PrimeLLVMTy);
  Value *LHSAnd = Builder.CreateAnd(LHSCast, OverflowMask);
  Value *LHSOr = Builder.CreateOr(LHSAnd, Any1);
  Value *LHSSquare = Builder.CreateMul(LHSOr, LHSOr);
  Value *LHSTot = Builder.CreateMul(LHSSquare, Prime1);

  Value *RHSCast = Builder.CreateZExtOrTrunc(Y, PrimeLLVMTy);
  Value *RHSAnd = Builder.CreateAnd(RHSCast, OverflowMask);
  Value *RHSOr = Builder.CreateOr(RHSAnd, Any2);
  Value *RHSSquare = Builder.CreateMul(RHSOr, RHSOr);
  Value *RHSTot = Builder.CreateMul(RHSSquare, Prime2);

  // OutputBasicBlock(BB);

  return Builder.CreateICmp(CmpInst::Predicate::ICMP_NE, LHSTot, RHSTot);
}

void bogus(BasicBlock *Entry) {
  // split basic blocks
  BasicBlock *Body =
      Entry->splitBasicBlock(Entry->getFirstNonPHIOrDbgOrLifetime(), "body");

  BasicBlock *End = Body->splitBasicBlock(Body->getTerminator(), "end");

  BasicBlock *Altered = createAlteredBasicBlock(Body);

  // outs() << "Compeletly create altered basic block.\n";

  // erase terminator branch instruction
  Entry->getTerminator()->eraseFromParent();
  Body->getTerminator()->eraseFromParent();
  Altered->getTerminator()->eraseFromParent();

  // outs() << "All splited basic block has erased terminator.\n";

  // insert invariant opaque predicates
  Value *Cond1, *Cond2;
  if (cryptoutils->get_range(100) >= 50) {
    Cond1 = createSimpleBogusCMP(Entry);
    Cond2 = createSimpleBogusCMP(Body);
  } else {
    Cond1 = createComplexBogusCMP(Entry);
    Cond2 = createComplexBogusCMP(Body);
  }

  BranchInst::Create(Body, Altered, Cond1, Entry);
  BranchInst::Create(End, Altered, Cond2, Body);

  auto *F = Entry->getParent();
  auto *Junk = createJunkBB(*F);

  if (cryptoutils->get_range(100) >= 50) {
    BranchInst::Create(Body, Altered);
  } else {
    BranchInst::Create(Junk, Altered);
  }

  // outs() << "Bogus this basic block succeessfully.\n";
}

bool runBogusControlFlow(Function &F) {
  outs() << "Start BogusControlFlow Pass.\n";

  // check optional settings
  if (ObfuTimes <= 0) {
    errs() << "BogusControlFlow application number -bcf_times=x must be x >"
              " 0";
    return false;
  }

  if (!((ObfuProbRate > 0) && (ObfuProbRate <= 100))) {
    errs() << "BogusControlFlow application basic blocks percentage "
              "-bcf_prob=x must be 0 < x <= 100";
    return false;
  }

  // start bogus control flow obfuscation
  for (int times = 0; times < ObfuTimes; times++) {
    // outs() << "Times: " << times << "\n";
    std::vector<BasicBlock *> OrigBB;
    for (auto &BB : F) {
      if (!checkContainInlineASM(BB)) {
        OrigBB.push_back(&BB);
      }
    }

    // outs() << "Compeletly check if function has inline asm.\n";
    UsefulBB.clear();
    for (auto *BB : OrigBB) {
      if (BB->getTerminator()->getNumSuccessors() >= 1) {
        if (static_cast<int32_t>(cryptoutils->get_range(100)) <= ObfuProbRate) {
          UsefulBB.emplace_back(BB);
        }
      }
    }

    for (auto *BB : UsefulBB) {
      bogus(BB);
    }
  }

  outs() << "Compeletly bogus function: \033[42m" << F.getName() << "\033[0m\n";

  outs() << "Finished BogusControlFlow Pass.\n";

  return false;
}

} // namespace

PreservedAnalyses BogusControlFlowPass::run(Function &F,
                                            FunctionAnalysisManager &AM) {
  if (!runBogusControlFlow(F))
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}