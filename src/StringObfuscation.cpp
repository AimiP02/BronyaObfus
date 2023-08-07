#include "Obfuscation/StringObfuscation.h"
#include "Obfuscation/CryptoUtils.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdint.h>
#include <vcruntime_string.h>
#include <vector>

using namespace llvm;

static cl::opt<bool>
    RunStringObfuscationPass("string-obfus", cl::init(false),
                             cl::desc("BronyaObfus - StringObfuscationPass"));

namespace {

class GlobalString {
public:
  GlobalString(GlobalVariable *Var, uint64_t Key, int32_t StringLength)
      : Var(Var), Key(Key), Index(-1), Type(SIMPLE_STRING_TYPE),
        StringLength(StringLength) {}

  GlobalString(GlobalVariable *Var, uint64_t Key, uint32_t Index,
               int32_t StringLength)
      : Var(Var), Key(Key), Index(Index), Type(STRUCT_STRING_TYPE),
        StringLength(StringLength) {}

public:
  GlobalVariable *Var;
  uint64_t Key;
  uint32_t Index;
  int32_t Type;
  int32_t StringLength;

  enum StringTypes { SIMPLE_STRING_TYPE, STRUCT_STRING_TYPE };
};

void OutputIR(Function *Func) {
  for (auto &BB : *Func) {
    for (auto &Inst : BB) {
      Inst.print(errs());
      errs() << "\n";
    }
  }
}

// std::string GenHashName(GlobalVariable *GV) {
//   Module &M = *GV->getParent();
//   std::string FuncName =
//       formatv("{0}_{1:x-}", M.getName(), M.getMDKindID(GV->getName()));
//   SHA1 sha1;
//   sha1.update(FuncName);

//   auto digest = sha1.final();

//   std::stringstream ss;
//   ss << std::hex;

//   for (size_t i = 0; i < digest.size(); i++) {
//     ss << std::setw(2) << std::setfill('0')
//        << static_cast<unsigned>(digest[i] & 0xFF);
//   }

//   return ss.str();
// }

// Encode string, return encoded string
std::tuple<char *, uint64_t> encodeString(const char *Data, uint32_t Length) {
  char *NewData = (char *)malloc(Length);

  memcpy(NewData, Data, Length);

  uint64_t Key = cryptoutils->get_uint64_t();

  for (uint32_t i = 0; i < Length; i++) {
    NewData[i] ^= ((char *)&Key)[i % 8];
  }

  return {NewData, Key};
}

Function *createDecodeFunc(Module &M) {
  auto &Ctx = M.getContext();
  FunctionType *FuncType = FunctionType::get(
      Type::getVoidTy(Ctx),
      {Type::getInt8PtrTy(Ctx), Type::getInt32Ty(Ctx), Type::getInt64Ty(Ctx)},
      false);
  // std::string FuncName = GenHashName(GS.Var);
  std::string FuncName = "docode";
  FunctionCallee Callee = M.getOrInsertFunction(FuncName, FuncType);
  Function *Func = cast<Function>(Callee.getCallee());
  Func->setCallingConv(CallingConv::C);

  Function::arg_iterator Args = Func->arg_begin();
  Value *param0 = Args++;
  param0->setName("Data");
  Value *param1 = Args++;
  param1->setName("Length");
  Value *param2 = Args++;
  param2->setName("Key");

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", Func);
  BasicBlock *ForCond = BasicBlock::Create(Ctx, "for.cond", Func);
  BasicBlock *ForBody = BasicBlock::Create(Ctx, "for.body", Func);
  BasicBlock *ForInc = BasicBlock::Create(Ctx, "for.inc", Func);
  BasicBlock *ForEnd = BasicBlock::Create(Ctx, "for.end", Func);

  IRBuilder<> Builder(Entry);

  Type *Int64Ty = Builder.getInt64Ty();
  Type *Int32Ty = Builder.getInt32Ty();
  Type *Int8PtrTy = Builder.getInt8PtrTy();

  Builder.SetInsertPoint(Entry);
  auto *KeyAddr = Builder.CreateAlloca(Int64Ty, nullptr, "key.addr");
  auto *LengthAddr = Builder.CreateAlloca(Int32Ty, nullptr, "length.addr");
  auto *DataAddr = Builder.CreateAlloca(Int8PtrTy, nullptr, "data.addr");
  // Set initial state
  AllocaInst *IndexPtr =
      Builder.CreateAlloca(Int32Ty, ConstantInt::get(Int32Ty, 1, false), "i");
  Builder.CreateStore(ConstantInt::get(Int32Ty, 0), IndexPtr);
  Builder.CreateStore(param2, KeyAddr);
  Builder.CreateStore(param1, LengthAddr);
  Builder.CreateStore(param0, DataAddr);
  // Jump to Condition
  Builder.CreateBr(ForCond);
  // i < Length
  Builder.SetInsertPoint(ForCond);
  LoadInst *Index = Builder.CreateLoad(Int32Ty, IndexPtr);
  LoadInst *DataLength = Builder.CreateLoad(Int32Ty, LengthAddr);
  ICmpInst *Cond = cast<ICmpInst>(Builder.CreateICmpSLT(Index, DataLength));
  // if i < Length is true, jump to body, or jump to end
  Builder.CreateCondBr(Cond, ForBody, ForEnd);
  // GS.Var[i] ^= Key[i]
  Builder.SetInsertPoint(ForBody);

  auto Var1_index = Builder.CreateLoad(Int32Ty, IndexPtr);
  // auto Var2_length = Builder.CreateLoad(Int32Ty, LengthAddr);
  auto Var3_rem = Builder.CreateURem(
      Var1_index, ConstantInt::get(Type::getInt32Ty(Ctx), 8));
  auto ZEXT_rem = Builder.CreateZExt(Var3_rem, Int64Ty);
  auto KeyIndex =
      Builder.CreateInBoundsGEP(Builder.getInt8Ty(), KeyAddr, ZEXT_rem);

  auto Var4_value = Builder.CreateLoad(Builder.getInt8Ty(), KeyIndex);
  auto KeyValue = Builder.CreateSExt(
      Var4_value, Int32Ty); // KeyValue = (char*)&Key[i % Length]
  auto Var5_data = Builder.CreateLoad(Int8PtrTy, DataAddr);
  auto Var6_index = Builder.CreateLoad(Int32Ty, IndexPtr);
  auto ZEXT_index = Builder.CreateZExt(Var6_index, Int64Ty);
  auto DataIndex =
      Builder.CreateInBoundsGEP(Builder.getInt8Ty(), Var5_data, ZEXT_index);
  auto Var7_value = Builder.CreateLoad(Builder.getInt8Ty(), DataIndex);
  auto DataValue =
      Builder.CreateSExt(Var7_value, Int32Ty); // DataValue = Data[i]
  auto XorResult = Builder.CreateXor(DataValue, KeyValue);
  auto TruncXorResult = Builder.CreateTrunc(XorResult, Builder.getInt8Ty());
  auto Result = Builder.CreateStore(TruncXorResult, DataIndex);

  Builder.CreateBr(ForInc);
  // i++
  Builder.SetInsertPoint(ForInc);
  Builder.CreateStore(Builder.CreateAdd(Index, ConstantInt::get(Int32Ty, 1)),
                      IndexPtr);
  Builder.CreateBr(ForCond);

  Builder.SetInsertPoint(ForEnd);
  Builder.CreateRetVoid();

  // Debug
  OutputIR(Func);

  return Func;
}

Function *createDecodeStubFunc(Module &M,
                               std::vector<GlobalString *> &GlobalStrings,
                               Function *DecodeFunc) {
  auto &Ctx = M.getContext();
  FunctionCallee DecodeStubCallee =
      M.getOrInsertFunction("decode_stub", Type::getVoidTy(Ctx));
  Function *DecodeStubFunc = cast<Function>(DecodeStubCallee.getCallee());
  DecodeStubFunc->setCallingConv(CallingConv::C);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", DecodeStubFunc);
  IRBuilder<> Builder(Entry);

  for (auto *GS : GlobalStrings) {
    Value *Key = ConstantInt::get(Type::getInt64Ty(Ctx), GS->Key);
    Value *Length = ConstantInt::get(Type::getInt32Ty(Ctx), GS->StringLength);
    switch (GS->Type) {
    case GS->SIMPLE_STRING_TYPE: {
      auto StrPtr = Builder.CreatePointerCast(GS->Var, Type::getInt8PtrTy(Ctx));
      Builder.CreateCall(DecodeFunc, {StrPtr, Length, Key});
      break;
    }
    case GS->STRUCT_STRING_TYPE: {
      auto String =
          Builder.CreateStructGEP(Type::getInt32Ty(Ctx), GS->Var, GS->Index);
      auto StrPtr = Builder.CreatePointerCast(String, Type::getInt8PtrTy(Ctx));
      Builder.CreateCall(DecodeFunc, {StrPtr, Length, Key});
      break;
    }
    default:
      break;
    }
  }

  Builder.CreateRetVoid();

  return DecodeStubFunc;
}

void insertDecodeStubBasicBlock(Function *F, Function *DecodeStubFunc) {
  auto &Ctx = F->getContext();
  BasicBlock &EntryBlock = F->getEntryBlock();

  BasicBlock *NewBB = BasicBlock::Create(Ctx, "DecodeStub",
                                         EntryBlock.getParent(), &EntryBlock);
  IRBuilder<> Builder(NewBB);

  Builder.CreateCall(DecodeStubFunc);
  Builder.CreateBr(&EntryBlock);

  // Output IR
  // for (auto &Inst : *NewBB) {
  //   Inst.print(errs());
  //   errs() << "\n";
  // }
}

void insertDecodeStubFuncCtor(Function *F) {
  auto *M = F->getParent();
  appendToGlobalCtors(*M, F, 0);
}

std::vector<GlobalString *> encodeGlobalStrings(Module &M) {
  std::vector<GlobalString *> Result;
  auto &Ctx = M.getContext();

  for (auto &GV : M.getGlobalList()) {
    if (!GV.hasInitializer() || GV.hasExternalLinkage())
      continue;
    // Find const global string
    // Const global string has two types: Direct string, Struct string
    Constant *Initializer = GV.getInitializer();
    // If constant global string is a array string, suck as "Hello, world"
    if (isa<ConstantDataArray>(Initializer)) {
      auto CDA = cast<ConstantDataArray>(Initializer);
      if (!CDA->isString())
        continue;

      // Extract raw string
      StringRef RawString = CDA->getAsString();
      const char *Data = RawString.begin();
      const int32_t Size = RawString.size();

      errs() << "String: " << Data << "Length: " << Size << "\n";

      auto [NewData, Key] = encodeString(Data, Size);
      Constant *NewConst =
          ConstantDataArray::getString(Ctx, StringRef(NewData, Size), false);

      // Overwrite origin value
      GV.setInitializer(NewConst);
      Result.push_back(new GlobalString(&GV, Key, Size));
      GV.setConstant(false);

    } else if (isa<ConstantStruct>(Initializer)) { // else, it's a struct
      auto CS = cast<ConstantStruct>(Initializer);

      for (uint32_t i = 0; i < CS->getNumOperands(); i++) {
        auto CDA = cast<ConstantDataArray>(CS->getOperand(i));
        if (!CDA->isString())
          continue;

        StringRef RawString = CDA->getAsString();
        const char *Data = RawString.begin();
        const int32_t Size = RawString.size();

        errs() << "Structure: " << CS->getName() << "String: " << Data
               << "Length: " << Size << "\n";

        auto [NewData, Key] = encodeString(Data, Size);
        Constant *NewConst =
            ConstantDataArray::getString(Ctx, StringRef(NewData, Size));

        CS->setOperand(i, NewConst);
        Result.push_back(new GlobalString(&GV, Key, i, Size));
        GV.setConstant(false);
      }
    }
  }

  return Result;
}

bool runStringObfuscation(Module &M) {
  outs() << "Start StringObfuscation Pass.\n";

  auto GlobalStrings = encodeGlobalStrings(M);

  auto *DecodeFunc = createDecodeFunc(M);

  auto *DecodeStub = createDecodeStubFunc(M, GlobalStrings, DecodeFunc);

  // Function *MainFunc = M.getFunction("main");

  // insertDecodeStubBasicBlock(MainFunc, DecodeStub);
  insertDecodeStubFuncCtor(DecodeStub);

  outs() << "Finished StringObfuscation Pass.\n";
  return false;
}

} // namespace

PreservedAnalyses StringObfuscationPass::run(Module &M,
                                             ModuleAnalysisManager &AM) {
  if (!runStringObfuscation(M))
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}

// New pass manager register