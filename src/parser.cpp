#include "llvm/ADT/STLExtras.h"

#include "ast.h"
#include "lexer.h"
#include "parser.h"

//===----------------------------------------------------------------------===//
// Tokens
//===----------------------------------------------------------------------===//

// Current token the parser is looking at.
int currentToken;

// Reads another token from the lexer and puts it in currentToken.
int GetNextToken() {
  return currentToken = AdvanceToken();
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

// numberexpr ::= number
std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto result = llvm::make_unique<NumberExprAST>(tokenNumericValue);
  GetNextToken(); // consume the number
  return std::move(result);
}

// parenexpr ::= '(' expression ')'
std::unique_ptr<ExprAST> ParseParenExpr() {
  GetNextToken();

  auto innerExpr = ParseExpr();
  if (!innerExpr)
    return nullptr;

  if (currentToken != ')')
    return LogError("Expected ')'.");

  GetNextToken();
  return innerExpr;
}

// identifierexpr
//   ::= identifier
//   ::= identifier '(' expression* ')'
std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string name = tokenIdentifier;
  GetNextToken(); // eat identifier

  // variable ref.
  if (currentToken != '(')
    return llvm::make_unique<VariableExprAST>(name);

  // function call
  GetNextToken(); // eat (
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

      GetNextToken();
    }
  }

  GetNextToken(); // eat )
  return llvm::make_unique<CallExprAST>(name, std::move(args));
}

/// varexpr ::= 'var' identifier ('=' expression)?
//                    (',' identifier ('=' expression)?)* 'in' expression
static std::unique_ptr<ExprAST> ParseVarExpr() {
  GetNextToken(); // eat the var.

  std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;

  // At least one variable name is required.
  if (currentToken != tok_identifier)
    return LogError("expected identifier after var");

  while (1) {
    std::string Name = tokenIdentifier;
    GetNextToken(); // eat identifier

    // Read the optional initializer.
    std::unique_ptr<ExprAST> Init;
    if (currentToken == '=') {
      GetNextToken(); // eat the '='.

      Init = ParseExpr();
      if (!Init)
        return nullptr;
    }

    VarNames.push_back(std::make_pair(Name, std::move(Init)));

    // End of var list, exit loop.
    if (currentToken != ',')
      break;

    GetNextToken(); // eat ','

    if (currentToken != tok_identifier)
      return LogError("expected identifier list after var");
  }

  // At this point, we have to have 'in'.
  if (currentToken != tok_in)
    return LogError("expected 'in' keyword after 'var'");

  GetNextToken(); // eat 'in'.

  auto Body = ParseExpr();
  if (!Body)
    return nullptr;

  return llvm::make_unique<VarExprAST>(std::move(VarNames), std::move(Body));
}

std::unique_ptr<ExprAST> ParseIfExpr();
std::unique_ptr<ExprAST> ParseForExpr();

/// primary
///   ::= identifierexpr
///   ::= numberexpr
///   ::= parenexpr
///   ::= ifexpr
///   ::= forexpr
///   ::= varexpr
std::unique_ptr<ExprAST> ParsePrimary() {
  switch (currentToken) {
    case tok_identifier:
      return ParseIdentifierExpr();
    case tok_number:
      return ParseNumberExpr();
    case tok_if:
      return ParseIfExpr();
    case tok_for:
      return ParseForExpr();
    case tok_var:
      return ParseVarExpr();
    case '(':
      return ParseParenExpr();
    default:
      return LogError("Unknown token when expecting an expression.");
  }
}

//===----------------------------------------------------------------------===//
// Operators
//===----------------------------------------------------------------------===//

// unary
//   ::= primary
//   ::= '!' unary
std::unique_ptr<ExprAST> ParseUnary() {
  // If the current token is not an operator, it must be a primary expr.
  if (!isascii(currentToken) || currentToken == '(' || currentToken == ',')
    return ParsePrimary();

  // If this is a unary operator, read it.
  int opCode = currentToken;
  GetNextToken();

  if (auto operand = ParseUnary())
    return llvm::make_unique<UnaryExprAST>(opCode, std::move(operand));

  return nullptr;
}

// Get the precedence of the pending binary operator token
static int GetTokenPrecedence() {
  if (!isascii(currentToken))
    return -1;

  int precedence = binaryOpPrecedence[currentToken];
  return precedence <= 0 ? -1 : precedence;
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
    GetNextToken();

    auto rhs = ParseUnary();
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
std::unique_ptr<ExprAST> ParseExpr() {
  auto lhs = ParseUnary();
  if (!lhs)
    return nullptr;

  return ParseBinaryOperationRhs(0, std::move(lhs));
}

//===----------------------------------------------------------------------===//
// Control expressions
//===----------------------------------------------------------------------===//

std::unique_ptr<ExprAST> ParseIfExpr() {
  GetNextToken(); // eat if

  auto cond = ParseExpr();
  if (!cond)
    return nullptr;

  if (currentToken != tok_then)
    return LogError("expected then");
  GetNextToken(); // eat then

  auto thenExpr = ParseExpr();
  if (!thenExpr)
    return nullptr;

  if (currentToken != tok_else)
    return LogError("expected else");

  GetNextToken();

  auto elseExpr = ParseExpr();
  if (!elseExpr)
    return nullptr;

  return llvm::make_unique<IfExprAST>(std::move(cond), std::move(thenExpr), std::move(elseExpr));
}

// forexpr ::= 'for' identifier '=' expr ',' expr (',' expr)? 'in' expression
std::unique_ptr<ExprAST> ParseForExpr() {
  GetNextToken(); // eat for

  if (currentToken != tok_identifier)
    return LogError("expected identifier after for");

  auto varName = tokenIdentifier;
  GetNextToken(); // eat identifier.

  if (currentToken != '=')
    return LogError("expected '=' after for");
  GetNextToken(); // eat '='

  auto initialVal = ParseExpr();
  if (!initialVal)
    return nullptr;

  if (currentToken != ',')
    return LogError("expected ',' after for initial value");
  GetNextToken(); // eat ','

  auto endValue = ParseExpr();
  if (!endValue)
    return nullptr;

  // The step value is optional.
  std::unique_ptr<ExprAST> increment;
  if (currentToken == ',') {
    GetNextToken();
    increment = ParseExpr();
    if (!increment)
      return nullptr;
  }

  if (currentToken != tok_in)
    return LogError("expected 'in' after for");
  GetNextToken(); // eat 'in'

  auto body = ParseExpr();
  if (!body)
    return nullptr;

  return llvm::make_unique<ForExprAST>(varName, std::move(initialVal), std::move(endValue),
                                       std::move(increment), std::move(body));
}

//===----------------------------------------------------------------------===//
// Functions
//===----------------------------------------------------------------------===//

// prototype
//   ::= id '(' id* ')'
//   ::= binary LETTER number? (id, id)
//   ::= unary LETTER (id)
std::unique_ptr<PrototypeAST> ParsePrototype() {
  std::string name;

  unsigned kind = 0; // 0 = identifier, 1 = unary, 2 = binary.
  unsigned binaryPrecedence = 30;

  switch (currentToken) {
    case tok_identifier:
      name = tokenIdentifier;
      kind = 0;
      GetNextToken();
      break;
    case tok_unary:
      GetNextToken();
      if (!isascii(currentToken))
        return LogErrorP("Expected unary operator");

      name = "unary";
      name += (char)currentToken;
      kind = 1;
      GetNextToken();
      break;
    case tok_binary:
      GetNextToken();
      if (!isascii(currentToken))
        return LogErrorP("Expected binary operator");

      name = "binary";
      name += (char)currentToken;
      kind = 2;
      GetNextToken();

      // Read the precedence if present.
      if (currentToken == tok_number) {
        if (tokenNumericValue < 1 || tokenNumericValue > 100)
          return LogErrorP("Invalid precedence: must be 1..100");

        binaryPrecedence = (unsigned)tokenNumericValue;
        GetNextToken();
      }
      break;
    default:
      return LogErrorP("Expected function name in prototype");
  }

  if (currentToken != '(')
    return LogErrorP("Expected '(' in prototype");

  std::vector<std::string> argNames;
  while (GetNextToken() == tok_identifier)
    argNames.push_back(tokenIdentifier);

  if (currentToken != ')')
    return LogErrorP("Expected ')' in prototype");

  // success.
  GetNextToken(); // eat ')'.

  // Verify right number of names for operator.
  if (kind && argNames.size() != kind)
    return LogErrorP("Invalid number of operands for operator");

  return llvm::make_unique<PrototypeAST>(name, std::move(argNames), kind != 0, binaryPrecedence);
}

// definition ::= 'def' prototype expression
std::unique_ptr<FunctionAST> ParseFunctionDefinition() {
  GetNextToken(); // eat def.

  auto prototype = ParsePrototype();
  if (!prototype)
    return nullptr;

  if (auto expr = ParseExpr())
    return llvm::make_unique<FunctionAST>(std::move(prototype), std::move(expr));

  return nullptr;
}

// external ::= 'extern' prototype
std::unique_ptr<PrototypeAST> ParseExtern() {
  GetNextToken(); // eat extern
  return ParsePrototype();
}

// toplevelexpr ::= expression
std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto expr = ParseExpr()) {
    // Make an anonymous proto.
    auto Proto = llvm::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
    return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(expr));
  }
  return nullptr;
}
