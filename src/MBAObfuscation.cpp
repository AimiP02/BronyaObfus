#include "Obfuscation/MBAObfuscation.h"
#include "Eigen/Dense"
#include "Eigen/src/Core/Matrix.h"
#include "Obfuscation/CryptoUtils.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include <algorithm>
#include <stdint.h>
#include <vector>

using namespace llvm;
using namespace Eigen;

static cl::opt<bool>
    RunMBAObfuscationPass("mba-substitute", cl::init(false),
                          cl::desc("BronyaObfus - MBA Substitute"));

static cl::opt<int>
    ObfuTimes("mba-times", cl::init(1),
              cl::desc("Run MBASubstitute pass <mba-times> time(s)"));

static cl::opt<int> ObfuProbRate(
    "mba-prob", cl::init(100),
    cl::desc("Choose the probability <mba-prob> for each basic blocks will "
             "be obfuscated by MBA Pass"));

static cl::opt<int>
    TermsNumber("linear-mba-terms", cl::init(10),
                cl::desc("Choose <linear-mba-terms> boolean exprs to construct "
                         "the linear MBA expr."));

namespace {

int8_t TruthTable[15][4] = {
    {0, 0, 0, 1}, // x & y
    {0, 0, 1, 0}, // x & ~y
    {0, 0, 1, 1}, // x
    {0, 1, 0, 0}, // ~x & y
    {0, 1, 0, 1}, // y
    {0, 1, 1, 0}, // x ^ y
    {0, 1, 1, 1}, // x | y
    {1, 0, 0, 0}, // ~(x | y)
    {1, 0, 0, 1}, // ~(x ^ y)
    {1, 0, 1, 0}, // ~y
    {1, 0, 1, 1}, // x | ~y
    {1, 1, 0, 0}, // ~x
    {1, 1, 0, 1}, // ~x | y
    {1, 1, 1, 0}, // ~(x & y)
    {1, 1, 1, 1}, // -1
};

int64_t *generateLinearMBA(int exprNumber) {
  int *exprSelector = new int[exprNumber];
  int64_t *coeffs = new int64_t[15];
  while (true) {
    std::fill_n(coeffs, 15, 0);
    for (int i = 0; i < exprNumber; i++) {
      exprSelector[i] = rand() % 15;
    }
    MatrixXd A(4, exprNumber);
    VectorXd b(4);
    VectorXd X;
    b << 0, 0, 0, 0;
    for (int i = 0; i < exprNumber; i++) {
      for (int j = 0; j < 4; j++) {
        A(j, i) = TruthTable[exprSelector[i]][j];
      }
    }
    X = A.fullPivLu().kernel().col(0);
    // reject if coeffs contain non-integer or are all zero
    bool reject = false;
    for (int i = 0; i < exprNumber; i++) {
      coeffs[exprSelector[i]] += X[i];
      if (std::abs(X[i] - (int64_t)X[i]) > 1e-5) {
        reject = true;
        break;
      }
    }
    if (reject)
      continue;
    reject = true;
    for (int i = 0; i < 15; i++) {
      if (coeffs[i] != 0)
        reject = false;
    }
    if (reject)
      continue;
    delete[] exprSelector;
    return coeffs;
  }
}

Value *insertLinearMBA(int64_t *params, Instruction *insertBefore) {
  auto &Ctx = insertBefore->getContext();
  IRBuilder<> builder(Ctx);
  builder.SetInsertPoint(insertBefore);
  Value *x, *y;
  if (isa<BinaryOperator>(insertBefore) &&
      insertBefore->getNumOperands() == 2) {
    x = insertBefore->getOperand(0);
    y = insertBefore->getOperand(1);
  } else {
    Module &M = *insertBefore->getModule();
    Type *type = insertBefore->getOperand(0)->getType();
    uint64_t randX = cryptoutils->get_uint64_t(),
             randY = cryptoutils->get_uint64_t();
    GlobalVariable *xPtr =
        new GlobalVariable(M, type, false, GlobalValue::PrivateLinkage,
                           ConstantInt::get(type, randX), "x");
    GlobalVariable *yPtr =
        new GlobalVariable(M, type, false, GlobalValue::PrivateLinkage,
                           ConstantInt::get(type, randY), "y");
    x = builder.CreateLoad(type, xPtr, "x");
    y = builder.CreateLoad(type, yPtr, "y");
  }
  Value *mbaExpr = builder.CreateAlloca(x->getType());
  builder.CreateStore(ConstantInt::get(x->getType(), 0), mbaExpr);
  mbaExpr = builder.CreateLoad(x->getType(), mbaExpr);
  Value *boolExpr, *term;
  for (int i = 0; i < 15; i++) {
    if (!params[i])
      continue;
    // x & y
    if (i == 0)
      boolExpr = builder.CreateAnd(x, y);
    // x & ~y
    else if (i == 1)
      boolExpr = builder.CreateAnd(x, builder.CreateNot(y));
    // x
    else if (i == 2)
      boolExpr = x;
    // ~x & y
    else if (i == 3)
      boolExpr = builder.CreateAnd(builder.CreateNot(x), y);
    // y
    else if (i == 4)
      boolExpr = y;
    // x ^ y
    else if (i == 5)
      boolExpr = builder.CreateXor(x, y);
    // x | y
    else if (i == 6)
      boolExpr = builder.CreateOr(x, y);
    // ~(x | y)
    else if (i == 7)
      boolExpr = builder.CreateNot(builder.CreateOr(x, y));
    // ~(x ^ y)
    else if (i == 8)
      boolExpr = builder.CreateNot(builder.CreateXor(x, y));
    // ~y
    else if (i == 9)
      boolExpr = builder.CreateNot(y);
    // x | ~y
    else if (i == 10)
      boolExpr = builder.CreateOr(x, builder.CreateNot(y));
    // ~x
    else if (i == 11)
      boolExpr = builder.CreateNot(x);
    // ~x | y
    else if (i == 12)
      boolExpr = builder.CreateOr(builder.CreateNot(x), y);
    // ~(x & y)
    else if (i == 13)
      boolExpr = builder.CreateNot(builder.CreateAnd(x, y));
    // -1
    else if (i == 14)
      boolExpr = ConstantInt::get(x->getType(), -1);
    term =
        builder.CreateMul(ConstantInt::get(x->getType(), params[i]), boolExpr);
    mbaExpr = builder.CreateAdd(mbaExpr, term);
  }
  return mbaExpr;
}

uint64_t exgcd(uint64_t a, uint64_t b, uint64_t &x, uint64_t &y) {
  if (b == 0) {
    x = 1, y = 0;
    return a;
  }
  uint64_t g = exgcd(b, a % b, y, x);
  y -= a / b * x;
  return g;
}

uint64_t inv(uint64_t a, uint64_t p) {
  uint64_t x, y;
  exgcd(a, p, x, y);
  // get the inverse element
  return (x % p + p) % p;
}

uint64_t inverse(uint64_t n, uint32_t bitWidth) {
  assert(bitWidth <= 32);
  return inv(n, 1LL << bitWidth);
}

void generateUnivariatePoly(uint64_t *a, uint64_t *b, uint32_t bitWidth) {
  uint64_t a0, a1, b0, b1, a1_inv;
  a0 = cryptoutils->get_uint64_t(), a1 = cryptoutils->get_uint64_t() | 1;

  // Calculate a1_inv
  a1_inv = inverse(a1, bitWidth);

  // Calculate b1
  b1 = a1_inv;

  // Calculate b0
  b0 = -(b1 * a0);

  a[0] = a0, a[1] = a1, b[0] = b0, b[1] = b1;
}

Value *insertPolynomialMBA(Value *linearMBAExpr, Instruction *insertBefore) {
  IRBuilder<> builder(insertBefore->getContext());
  builder.SetInsertPoint(insertBefore);
  Type *operandType = insertBefore->getOperand(0)->getType();
  uint32_t bitWidth = operandType->getIntegerBitWidth();
  uint64_t a[2], b[2];
  generateUnivariatePoly(a, b, bitWidth);

  Value *expr;
  expr = builder.CreateMul(ConstantInt::get(operandType, b[1]), linearMBAExpr);
  expr = builder.CreateAdd(expr, ConstantInt::get(operandType, b[0]));
  expr = builder.CreateMul(ConstantInt::get(operandType, a[1]), expr);
  expr = builder.CreateAdd(expr, ConstantInt::get(operandType, a[0]));
  return expr;
}

Value *substituteAdd(BinaryOperator *BI) {
  int64_t *terms = generateLinearMBA(TermsNumber);
  terms[2] += 1;
  terms[4] += 1;
  return insertLinearMBA(terms, BI);
}

Value *substituteSub(BinaryOperator *BI) {
  int64_t *terms = generateLinearMBA(TermsNumber);
  terms[2] += 1;
  terms[4] -= 1;
  return insertLinearMBA(terms, BI);
}

Value *substituteXor(BinaryOperator *BI) {
  int64_t *terms = generateLinearMBA(TermsNumber);
  terms[5] += 1;
  return insertLinearMBA(terms, BI);
}

Value *substituteAnd(BinaryOperator *BI) {
  int64_t *terms = generateLinearMBA(TermsNumber);
  terms[0] += 1;
  return insertLinearMBA(terms, BI);
}

Value *substituteOr(BinaryOperator *BI) {
  int64_t *terms = generateLinearMBA(TermsNumber);
  terms[6] += 1;
  return insertLinearMBA(terms, BI);
}

void substitute(BinaryOperator *BI) {
  Value *mbaExpr = nullptr;
  switch (BI->getOpcode()) {
  case BinaryOperator::Add:
    mbaExpr = substituteAdd(BI);
    break;
  case BinaryOperator::Sub:
    mbaExpr = substituteSub(BI);
    break;
  case BinaryOperator::And:
    mbaExpr = substituteAnd(BI);
    break;
  case BinaryOperator::Or:
    mbaExpr = substituteOr(BI);
    break;
  case BinaryOperator::Xor:
    mbaExpr = substituteXor(BI);
    break;
  default:
    break;
  }
  if (mbaExpr) {
    if (BI->getOperand(0)->getType()->getIntegerBitWidth() <= 32) {
      mbaExpr = insertPolynomialMBA(mbaExpr, BI);
    }
    BI->replaceAllUsesWith(mbaExpr);
  }
}

void substituteConstant(Instruction *Inst, int i) {
  ConstantInt *val = dyn_cast<ConstantInt>(Inst->getOperand(i));
  if (val && val->getBitWidth() <= 64) {
    int64_t *terms = generateLinearMBA(TermsNumber);
    terms[14] -= val->getValue().getZExtValue();
    Value *mbaExpr = insertLinearMBA(terms, Inst);
    if (val->getBitWidth() <= 32) {
      mbaExpr = insertPolynomialMBA(mbaExpr, Inst);
    }
    Inst->setOperand(i, mbaExpr);
  }
}

void OutputIR(Function *Func) {
  for (auto &BB : *Func) {
    for (auto &Inst : BB) {
      Inst.print(errs());
      errs() << "\n";
    }
  }
}

bool runMBAObfuscation(Function &F) {
  if (std::string FuncName = F.getName().str();
      FuncName.find("TLS") != std::string::npos) {
    return false;
  }

  if (ObfuTimes <= 0) {
    errs() << "MBAObfuscation application number -mba_times=x must be x >"
              " 0";
    return false;
  }

  if (!((ObfuProbRate > 0) && (ObfuProbRate <= 100))) {
    errs() << "MBAObfuscation application basic blocks percentage "
              "-mba_prob=x must be 0 < x <= 100";
    return false;
  }

  outs() << "Start MBASubstitute Pass.\n";

  for (int i = 0; i < ObfuTimes; i++) {
    // outs() << "Times: " << i << "\n";
    for (auto &BB : F) {
      std::vector<Instruction *> OrigInst;
      for (auto &Inst : BB) {
        OrigInst.push_back(&Inst);
        // outs() << "Push back instruction to OrigInst.\n";
      }
      // outs() << "Get OrigInst.\n";
      for (auto *Inst : OrigInst) {
        // outs() << "Instruction: ";
        // Inst->print(outs());
        // outs() << "\n";
        if (isa<BinaryOperator>(Inst)) {
          // outs() << "Into Branch1\n";
          BinaryOperator *BI = cast<BinaryOperator>(Inst);
          // outs() << "Get BinaryOperator.\n";
          if (BI->getOperand(0)->getType()->isIntegerTy() &&
              BI->getOperand(0)->getType()->getIntegerBitWidth() <= 64) {
            if (static_cast<int32_t>(cryptoutils->get_range(100)) <=
                ObfuProbRate) {
              // outs() << "Begin to substitute\n";
              substitute(BI);
              // outs() << "End to substitute\n";
            }
          }
        } else {
          // outs() << "Into Branch2\n";
          for (int i = 0; i < Inst->getNumOperands(); i++) {
            if (Inst->getOperand(0)->getType()->isIntegerTy() &&
                static_cast<int32_t>(cryptoutils->get_range(100)) <=
                    ObfuProbRate) {
              if (isa<StoreInst>(Inst) || isa<CmpInst>(Inst)) {
                substituteConstant(Inst, i);
              }
            }
          }
        }
      }
    }
  }

  // OutputIR(&F);

  outs() << "Finished MBASubstitute Pass.\n";

  return false;
}

} // namespace

PreservedAnalyses MBAObfuscationPass::run(Function &F,
                                          FunctionAnalysisManager &AM) {
  if (!runMBAObfuscation(F))
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}