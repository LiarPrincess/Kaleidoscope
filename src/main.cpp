#include "llvm/IR/ValueSymbolTable.h"

#include "ast.h"
#include "common.h"
#include "lexer.h"
#include "parser.h"

static void HandleDefinition() {
  if (auto ast = ParseFunctionDefinition()) {
    if (auto ir = ast->codegen()) {
      fprintf(stderr, "Parsed a function definition.\n");
      ir->print(llvm::errs());
      fprintf(stderr, "\n");
    }
  } else {
    // Skip token for error recovery.
    GetNextToken();
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
    GetNextToken();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (auto ast = ParseTopLevelExpr()) {
    if (auto ir = ast->codegen()) {
      fprintf(stderr, "Parsed a top-level expr\n");
      ir->print(llvm::errs());
      fprintf(stderr, "\n");

      auto &symbols = module->getValueSymbolTable();
      fprintf(stderr, "Symbols (%d):\n", symbols.size());
      for (auto it = symbols.begin(); it != symbols.end(); ++it) {
        auto key = it->getKey();
        fprintf(stderr, "- %s\n", key.str().c_str());
      }

      auto moduleHandle = jit->addModule(std::move(module));
      InitializeModuleAndPassManager();

      auto exprSymbol = jit->findSymbol("__anon_expr");
      assert(exprSymbol && "Function not found");

      // Get the symbol's address and cast it to the right type (takes no
      // arguments, returns a double) so we can call it as a native function.
      double (*functionPtr)() = (double (*)())(intptr_t)exprSymbol.getAddress().get();
      fprintf(stderr, "Evaluated to %f\n", functionPtr());

      jit->removeModule(moduleHandle);
    }
  } else {
    // Skip token for error recovery.
    GetNextToken();
  }
}

int main(int argc, char const *argv[]) {
  // Install standard binary operators. 1 is lowest precedence.
  AddBinaryOp('<', 10);
  AddBinaryOp('+', 20);
  AddBinaryOp('-', 20);
  AddBinaryOp('*', 40); // highest.

  InitializeJIT();
  InitializeModuleAndPassManager();

  while (1) {
    fprintf(stderr, "ready> ");
    int token = GetNextToken();
    switch (token) {
      case tok_eof:
      case ';': // ignore top-level semicolons.
        return 0;
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

  return 0;
}
