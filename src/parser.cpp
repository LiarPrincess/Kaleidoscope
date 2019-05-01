#include "llvm/ADT/STLExtras.h"

#include "ast.h"
#include "lexer.h"
#include "parser.h"

//===----------------------------------------------------------------------===//
// Tokens
//===----------------------------------------------------------------------===//

// Current token the parser is looking at.
static int currentToken;

// Reads another token from the lexer and puts it in currentToken.
static int getNextToken() {
  currentToken = getToken();
  return currentToken;
}

//===----------------------------------------------------------------------===//
// Errors
//===----------------------------------------------------------------------===//

// Helper function for error handling.
static std::unique_ptr<ExprAST> LogError(const char *str) {
  fprintf(stderr, "Parser error: %s\n", str);
  return nullptr;
}

// Helper function for error handling.
static std::unique_ptr<PrototypeAST> LogErrorP(const char *str) {
  LogError(str);
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Primary expressions
//===----------------------------------------------------------------------===//

static std::unique_ptr<ExprAST> ParseExpr();

// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto result = llvm::make_unique<NumberExprAST>(NumVal);
  getNextToken(); // consume the number
  return std::move(result);
}

// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
  getNextToken();

  auto innerExpr = ParseExpr();
  if (!innerExpr)
    return nullptr;

  if (currentToken != ')')
    return LogError("Expected ')'.");

  getNextToken();
  return innerExpr;
}

// identifierexpr
//   ::= identifier
//   ::= identifier '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string name = IdentifierStr;
  getNextToken(); // eat identifier

  // variable ref.
  if (currentToken != '(')
    return llvm::make_unique<VariableExprAST>(name);

  // function call
  getNextToken(); // eat (
  std::vector<std::unique_ptr<ExprAST>> args;
  if (currentToken != ')') {
    while (1) {
      if (auto arg = ParseExpr())
        args.push_back(std::move(arg));
      else
        return nullptr;

      if (currentToken == ')')
        break;

      if (currentToken != ',')
        return LogError("Expected ')' or ',' in argument list.");

      getNextToken();
    }
  }

  getNextToken(); // eat )
  return llvm::make_unique<CallExprAST>(name, std::move(args));
}

// primary
//   ::= identifierexpr
//   ::= numberexpr
//   ::= parenexpr
static std::unique_ptr<ExprAST> ParsePrimary() {
  switch (currentToken) {
    case tok_identifier:
      return ParseIdentifierExpr();
    case tok_number:
      return ParseNumberExpr();
    case '(':
      return ParseParenExpr();
    default:
      return LogError("Unknown token when expecting an expression.");
  }
}

//===----------------------------------------------------------------------===//
// Binary expressions
//===----------------------------------------------------------------------===//

// Precedence for each binary operator that is defined
static std::map<char, int> binaryOpPrecedence;

// Get the precedence of the pending binary operator token
static int GetTokenPrecedence() {
  if (!isascii(currentToken))
    return -1;

  int precedence = binaryOpPrecedence[currentToken];
  return precedence <= 0 ? -1 : precedence;
}

void AddBinaryOpPrecedence(char op, int precedence) {
  binaryOpPrecedence[op] = precedence;
}

// binary op rhs
//   ::= ('+' primary)*
static std::unique_ptr<ExprAST> ParseBinaryOperationRhs(int minPrecedence,
                                                        std::unique_ptr<ExprAST> lhs) {
  while (1) {
    int currentPrecedence = GetTokenPrecedence();
    if (currentPrecedence < minPrecedence)
      return lhs;

    int binOp = currentToken;
    getNextToken();

    auto rhs = ParsePrimary();
    if (!rhs)
      return nullptr;

    int nextPrecedence = GetTokenPrecedence();
    if (currentPrecedence < nextPrecedence) {
      rhs = ParseBinaryOperationRhs(minPrecedence + 1, std::move(rhs));
      if (!rhs)
        return nullptr;
    }

    lhs = llvm::make_unique<BinaryExprAST>(binOp, std::move(lhs), std::move(rhs));
  }
}

// expression
//   ::= primary binOpRhs
static std::unique_ptr<ExprAST> ParseExpr() {
  auto lhs = ParsePrimary();
  if (!lhs)
    return nullptr;

  return ParseBinaryOperationRhs(0, std::move(lhs));
}

//===----------------------------------------------------------------------===//
// Functions
//===----------------------------------------------------------------------===//

// prototype
//   ::= id '(' id* ')'
static std::unique_ptr<PrototypeAST> ParsePrototype() {
  if (currentToken != tok_identifier)
    return LogErrorP("Unexpected function name in prototype.");

  std::string name = IdentifierStr;
  getNextToken();

  if (currentToken != '(')
    return LogErrorP("Expected '(' in prototype");

  std::vector<std::string> argNames;
  while (getNextToken() == tok_identifier) {
    argNames.push_back(IdentifierStr);
  }

  if (currentToken != ')')
    return LogErrorP("Expected ')' in prototype");

  getNextToken(); // eat ')'
  return llvm::make_unique<PrototypeAST>(name, std::move(argNames));
}

// definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseFunctionDefinition() {
  getNextToken(); // eat def.

  auto prototype = ParsePrototype();
  if (!prototype)
    return nullptr;

  if (auto expr = ParseExpr())
    return llvm::make_unique<FunctionAST>(std::move(prototype), std::move(expr));

  return nullptr;
}

// external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat extern
  return ParsePrototype();
}

// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto expr = ParseExpr()) {
    // Make an anonymous proto.
    auto Proto = llvm::make_unique<PrototypeAST>("", std::vector<std::string>());
    return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(expr));
  }
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Main
//===----------------------------------------------------------------------===//

static void HandleDefinition() {
  if (auto ast = ParseFunctionDefinition()) {
    if (auto ir = ast->codegen()) {
      fprintf(stderr, "Parsed a function definition.\n");
      ir->print(llvm::errs());
      fprintf(stderr, "\n");
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern() {
  if (auto ast = ParseExtern()) {
    if (auto ir = ast->codegen()) {
      fprintf(stderr, "Parsed an extern\n");
      ir->print(llvm::errs());
      fprintf(stderr, "\n");
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (auto ast = ParseTopLevelExpr()) {
    if (auto ir = ast->codegen()) {
      fprintf(stderr, "Parsed a top-level expr\n");
      ir->print(llvm::errs());
      fprintf(stderr, "\n");
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

// top ::= definition | external | expression | ';'
void MainLoop() {
  fprintf(stderr, "ready> ");
  getNextToken();

  while (1) {
    fprintf(stderr, "ready> ");
    switch (currentToken) {
      case tok_eof:
        return;
      case ';': // ignore top-level semicolons.
        getNextToken();
        break;
      case tok_def:
        HandleDefinition();
        break;
      case tok_extern:
        HandleExtern();
        break;
      default:
        HandleTopLevelExpression();
        break;
    }
  }
}
