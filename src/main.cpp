#include "ast.h"
#include "common.h"
#include "parser.h"

int main(int argc, char const *argv[]) {
  // Install standard binary operators. 1 is lowest precedence.
  AddBinaryOpPrecedence('<', 10);
  AddBinaryOpPrecedence('+', 20);
  AddBinaryOpPrecedence('-', 20);
  AddBinaryOpPrecedence('*', 40); // highest.

  InitializeModuleAndPassManager();
  MainLoop();

  return 0;
}
