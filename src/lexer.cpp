#include "lexer.h"

std::string tokenIdentifier;
double tokenNumericValue;

int AdvanceToken() {
  static int lastChar = ' ';

  // skip whitespaces
  while (isspace(lastChar))
    lastChar = getchar();

  // identifier: [a-zA-Z][a-zA-Z0-9]*
  if (isalpha(lastChar)) {
    tokenIdentifier = lastChar;
    while (isalnum((lastChar = getchar())))
      tokenIdentifier += lastChar;

    if (tokenIdentifier == "def")
      return tok_def;
    if (tokenIdentifier == "extern")
      return tok_extern;
    if (tokenIdentifier == "if")
      return tok_if;
    if (tokenIdentifier == "then")
      return tok_then;
    if (tokenIdentifier == "else")
      return tok_else;
    if (tokenIdentifier == "for")
      return tok_for;
    if (tokenIdentifier == "in")
      return tok_in;
    if (tokenIdentifier == "binary")
      return tok_binary;
    if (tokenIdentifier == "unary")
      return tok_unary;

    return tok_identifier;
  }

  // number: [0-9.]+
  if (isdigit(lastChar) || lastChar == '.') {
    std::string numString;
    do {
      numString += lastChar;
      lastChar = getchar();
    } while (isdigit(lastChar) || lastChar == '.');

    tokenNumericValue = strtod(numString.c_str(), 0);
    return tok_number;
  }

  if (lastChar == '#') {
    // Comment until end of line.
    do
      lastChar = getchar();
    while (lastChar != EOF && lastChar != '\n' && lastChar != '\r');

    if (lastChar != EOF)
      return AdvanceToken();
  }

  // Check for end of file.  Don't eat the EOF.
  if (lastChar == EOF)
    return tok_eof;

  // Otherwise, just return the character as its ascii value.
  int result = lastChar;
  lastChar = getchar();
  return result;
}
