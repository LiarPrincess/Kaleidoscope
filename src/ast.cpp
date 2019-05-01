#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"

#include "ast.h"

llvm::LLVMContext context;
std::unique_ptr<llvm::Module> module;
std::unique_ptr<llvm::legacy::FunctionPassManager> functionPassManager;

static llvm::IRBuilder<> builder(context);
static std::map<std::string, llvm::Value *> namedValues;

//===----------------------------------------------------------------------===//
// Errors
//===----------------------------------------------------------------------===//

// Helper function for error handling.
llvm::Value *LogErrorV(const char *str) {
  fprintf(stderr, "Codegen error: %s\n", str);
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Module
//===----------------------------------------------------------------------===//

void InitializeModuleAndPassManager() {
  module = llvm::make_unique<llvm::Module>("my cool jit", context);

  functionPassManager = llvm::make_unique<llvm::legacy::FunctionPassManager>(module.get());
  functionPassManager->add(llvm::createInstructionCombiningPass());
  functionPassManager->add(llvm::createReassociatePass());
  functionPassManager->add(llvm::createGVNPass());
  functionPassManager->add(llvm::createCFGSimplificationPass());
  functionPassManager->doInitialization();
}

//===----------------------------------------------------------------------===//
// Codegen
//===----------------------------------------------------------------------===//

llvm::Value *NumberExprAST::codegen() {
  return llvm::ConstantFP::get(context, llvm::APFloat(this->Value));
}

llvm::Value *VariableExprAST::codegen() {
  // look this variable up in the function.
  llvm::Value *value = namedValues[this->Name];
  if (!value)
    LogErrorV("Unknown variable name.");

  return value;
}

llvm::Value *BinaryExprAST::codegen() {
  llvm::Value *left = this->Left->codegen();
  llvm::Value *right = this->Right->codegen();

  if (!left || !right)
    return nullptr;

  switch (this->Op) {
    case '+':
      return builder.CreateFAdd(left, right, "addtmp");
    case '-':
      return builder.CreateFSub(left, right, "subtmp");
    case '*':
      return builder.CreateFMul(left, right, "multmp");
    case '<':
      // convert bool 0/1 to double 0.0 or 1.0
      left = builder.CreateFCmpULT(left, right, "cmptmp");
      return builder.CreateUIToFP(left, llvm::Type::getDoubleTy(context), "booltmp");
    default:
      return LogErrorV("invalid binary operator");
  }
}

llvm::Value *CallExprAST::codegen() {
  auto *callee = module->getFunction(this->Callee);
  if (!callee)
    return LogErrorV("Unknown function referenced");

  auto argumentCount = this->Args.size();
  if (callee->arg_size() != argumentCount)
    return LogErrorV("Incorrect number of arguments passed");

  std::vector<llvm::Value *> arguments;
  for (unsigned i = 0; i < argumentCount; i++) {
    auto value = this->Args[i]->codegen();
    arguments.push_back(value);

    if (!value)
      return nullptr;
  }

  return builder.CreateCall(callee, arguments, "calltmp");
}

llvm::Function *PrototypeAST::codegen() {
  // make function type: double(double, double)
  auto doubleType = llvm::Type::getDoubleTy(context);
  std::vector<llvm::Type *> doubles(this->Args.size(), doubleType);

  auto linkage = llvm::Function::ExternalLinkage;
  auto functionType = llvm::FunctionType::get(doubleType, doubles, false);
  auto function = llvm::Function::Create(functionType, linkage, this->Name, module.get());

  unsigned index = 0;
  for (auto &arg : function->args()) {
    arg.setName(this->Args[index++]);
  }

  return function;
}

llvm::Function *FunctionAST::codegen() {
  auto function = module->getFunction(this->Proto->getName());

  if (!function)
    function = this->Proto->codegen();

  if (!function)
    return nullptr;

  // it should not have an body (yet...)
  if (!function->empty())
    return (llvm::Function *)LogErrorV("Function cannot be redefined.");

  auto block = llvm::BasicBlock::Create(context, "entry", function);
  builder.SetInsertPoint(block);

  namedValues.clear();
  for (auto &arg : function->args()) {
    namedValues[arg.getName()] = &arg;
  }

  if (auto returnValue = this->Body->codegen()) {
    builder.CreateRet(returnValue);
    llvm::verifyFunction(*function);
    functionPassManager->run(*function);
    return function;
  }

  function->eraseFromParent();
  return nullptr;
}
