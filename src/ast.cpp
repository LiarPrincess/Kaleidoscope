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
std::unique_ptr<llvm::orc::KaleidoscopeJIT> jit;
std::map<std::string, std::unique_ptr<PrototypeAST>> functionPrototypes;
std::map<char, int> binaryOpPrecedence;

static llvm::IRBuilder<> builder(context);
static std::map<std::string, llvm::Value *> namedValues;
static std::unique_ptr<llvm::legacy::FunctionPassManager> functionPassManager;

//===----------------------------------------------------------------------===//
// Errors
//===----------------------------------------------------------------------===//

// Helper function for error handling.
llvm::Value *LogErrorV(const char *str) {
  fprintf(stderr, "Codegen error: %s\n", str);
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Initialize
//===----------------------------------------------------------------------===//

void InitializeJIT() {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  jit = llvm::make_unique<llvm::orc::KaleidoscopeJIT>();
}

void InitializeModuleAndPassManager() {
  module = llvm::make_unique<llvm::Module>("my cool jit", context);
  module->setDataLayout(jit->getTargetMachine().createDataLayout());

  functionPassManager = llvm::make_unique<llvm::legacy::FunctionPassManager>(module.get());
  functionPassManager->add(llvm::createInstructionCombiningPass());
  functionPassManager->add(llvm::createReassociatePass());
  functionPassManager->add(llvm::createGVNPass());
  functionPassManager->add(llvm::createCFGSimplificationPass());
  functionPassManager->doInitialization();
}

void AddBinaryOp(char op, int precedence) {
  binaryOpPrecedence[op] = precedence;
}

//===----------------------------------------------------------------------===//
// Codegen - primary
//===----------------------------------------------------------------------===//

llvm::Value *NumberExprAST::codegen() {
  return llvm::ConstantFP::get(context, llvm::APFloat(this->Value));
}

llvm::Value *VariableExprAST::codegen() {
  // look this variable up in the function.
  auto value = namedValues[this->Name];
  if (!value)
    LogErrorV("Unknown variable name.");

  return value;
}

static llvm::Function *getFunction(const std::string &name) {
  if (auto function = module->getFunction(name))
    return function;

  auto prototype = functionPrototypes.find(name);
  if (prototype != functionPrototypes.end())
    return prototype->second->codegen();

  return nullptr;
}

llvm::Value *BinaryExprAST::codegen() {
  auto left = this->Left->codegen();
  auto right = this->Right->codegen();

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
      break;
  }

  // If it wasn't a builtin binary operator, it must be a user defined one.
  auto function = getFunction(std::string("binary") + this->Op);
  assert(function && "binary operator not found!");

  llvm::Value *Operands[2] = {left, right};
  return builder.CreateCall(function, Operands, "binop");
}

//===----------------------------------------------------------------------===//
// Codegen - control
//===----------------------------------------------------------------------===//

llvm::Value *IfExprAST::codegen() {
  auto condInput = this->Cond->codegen();
  if (!condInput)
    return nullptr;

  auto zero = llvm::ConstantFP::get(context, llvm::APFloat(0.0));
  auto cond = builder.CreateFCmpONE(condInput, zero, "ifcond");

  // current object that is being built
  auto parent = builder.GetInsertBlock()->getParent();

  auto thenBlock = llvm::BasicBlock::Create(context, "then", parent);
  auto elseBlock = llvm::BasicBlock::Create(context, "else");
  auto mergeBlock = llvm::BasicBlock::Create(context, "ifcont");

  builder.CreateCondBr(cond, thenBlock, elseBlock);

  // 'then' codegen
  builder.SetInsertPoint(thenBlock);
  auto thenBlockContent = this->Then->codegen();
  if (!thenBlockContent)
    return nullptr;

  builder.CreateBr(mergeBlock); // br label %ifcont
  thenBlock = builder.GetInsertBlock(); // we could nest block, ant 'then' may no longer be last one

  // 'else' codegen
  parent->getBasicBlockList().push_back(elseBlock);
  builder.SetInsertPoint(elseBlock);

  auto elseBlockContent = this->Else->codegen();
  if (!elseBlockContent)
    return nullptr;

  builder.CreateBr(mergeBlock); // br label %ifcont
  elseBlock = builder.GetInsertBlock(); // same as before

  // 'merge' codegen
  parent->getBasicBlockList().push_back(mergeBlock);
  builder.SetInsertPoint(mergeBlock);

  auto doubleType = llvm::Type::getDoubleTy(context);
  auto phiNode = builder.CreatePHI(doubleType, 2, "iftmp");
  phiNode->addIncoming(thenBlockContent, thenBlock);
  phiNode->addIncoming(elseBlockContent, elseBlock);

  return phiNode;
}

llvm::Value *ForExprAST::codegen() {
  auto initialVal = this->Start->codegen();
  if (!initialVal)
    return nullptr;

  // current object that is being built
  auto parent = builder.GetInsertBlock()->getParent();

  auto preheaderBlock = builder.GetInsertBlock();
  auto loopBlock = llvm::BasicBlock::Create(context, "loop", parent);

  builder.CreateBr(loopBlock);

  // loop
  builder.SetInsertPoint(loopBlock);

  auto doubleType = llvm::Type::getDoubleTy(context);
  auto loopPhiNode = builder.CreatePHI(doubleType, 2, this->VarName.c_str());
  loopPhiNode->addIncoming(initialVal, preheaderBlock);

  auto oldValue = namedValues[this->VarName];
  namedValues[this->VarName] = loopPhiNode;

  if (!this->Body->codegen())
    return nullptr;

  llvm::Value *increment = nullptr;
  if (this->Step) {
    increment = this->Step->codegen();
    if (!increment)
      return nullptr;
  } else {
    increment = llvm::ConstantFP::get(context, llvm::APFloat(1.0));
  }

  auto nextVal = builder.CreateFAdd(loopPhiNode, increment, "nextVal");

  auto endCond = this->End->codegen();
  if (!endCond)
    return nullptr;

  auto zero = llvm::ConstantFP::get(context, llvm::APFloat(0.0));
  endCond = builder.CreateFCmpONE(endCond, zero, "loopcond");

  // Create the "after loop" block and insert it.
  auto loopEndBlock = builder.GetInsertBlock();
  auto afterBlock = llvm::BasicBlock::Create(context, "afterloop", parent);
  builder.CreateCondBr(endCond, loopBlock, afterBlock);

  builder.SetInsertPoint(afterBlock);

  loopPhiNode->addIncoming(nextVal, loopEndBlock);
  if (oldValue)
    namedValues[VarName] = oldValue;
  else
    namedValues.erase(this->VarName);

  return llvm::Constant::getNullValue(llvm::Type::getDoubleTy(context));
}

llvm::Value *CallExprAST::codegen() {
  auto *callee = getFunction(this->Callee);
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

//===----------------------------------------------------------------------===//
// Codegen - functions
//===----------------------------------------------------------------------===//

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
  // Transfer ownership of the prototype to the FunctionProtos map,
  // but keep a reference to it for use below.
  PrototypeAST &prototype = *this->Proto;
  functionPrototypes[this->Proto->getName()] = std::move(this->Proto);
  llvm::Function *function = getFunction(prototype.getName());

  if (!function)
    return nullptr;

  // If this is an operator, install it.
  if (prototype.isBinaryOp())
    binaryOpPrecedence[prototype.getOperatorName()] = prototype.getBinaryPrecedence();

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
