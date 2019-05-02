#pragma once

#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/TargetSelect.h"

#include "KaleidoscopeJIT.h"
#include "common.h"

class PrototypeAST;

extern llvm::LLVMContext context;
extern std::unique_ptr<llvm::Module> module;
extern std::unique_ptr<llvm::orc::KaleidoscopeJIT> jit;
extern std::map<std::string, std::unique_ptr<PrototypeAST>> functionPrototypes;

void InitializeJIT();
void InitializeModuleAndPassManager();

// Base class for all expression nodes.
class ExprAST {
 public:
  virtual ~ExprAST() {}
  virtual llvm::Value *codegen() = 0;
};

// Expression class for numeric literals like "1.0".
class NumberExprAST : public ExprAST {
  double Value;

 public:
  NumberExprAST(double value) : Value(value) {}
  llvm::Value *codegen() override;
};

// Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
  std::string Name;

 public:
  VariableExprAST(const std::string &name) : Name(name) {}
  llvm::Value *codegen() override;
};

// Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
  char Op;
  std::unique_ptr<ExprAST> Left, Right;

 public:
  BinaryExprAST(char op, std::unique_ptr<ExprAST> left, std::unique_ptr<ExprAST> right)
      : Op(op), Left(std::move(left)), Right(std::move(right)) {}

  llvm::Value *codegen() override;
};

// IfExprAST - Expression class for if/then/else.
class IfExprAST : public ExprAST {
  std::unique_ptr<ExprAST> Cond, Then, Else;

 public:
  IfExprAST(std::unique_ptr<ExprAST> Cond, std::unique_ptr<ExprAST> Then,
            std::unique_ptr<ExprAST> Else)
      : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}

  llvm::Value *codegen() override;
};

// Expression class for function calls.
class CallExprAST : public ExprAST {
  std::string Callee;
  std::vector<std::unique_ptr<ExprAST>> Args;

 public:
  CallExprAST(const std::string &callee, std::vector<std::unique_ptr<ExprAST>> args)
      : Callee(callee), Args(std::move(args)) {}

  llvm::Value *codegen() override;
};

// Represents the "prototype" for a function, which captures its name,
// and its argument names (thus implicitly the number of arguments the function takes).
class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;

 public:
  PrototypeAST(const std::string &name, std::vector<std::string> args)
      : Name(name), Args(std::move(args)) {}

  const std::string &getName() const {
    return Name;
  }

  llvm::Function *codegen();
};

// Function definition.
class FunctionAST {
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body;

 public:
  FunctionAST(std::unique_ptr<PrototypeAST> proto, std::unique_ptr<ExprAST> body)
      : Proto(std::move(proto)), Body(std::move(body)) {}

  llvm::Function *codegen();
};
