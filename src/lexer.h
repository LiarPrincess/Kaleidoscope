#pragma once

#include "common.h"

// The lexer returns tokens [0-255] if it is an unknown character,
// otherwise one of these for known things.
enum Token {
  tok_eof = -1,
  // commands
  tok_def = -2,
  tok_extern = -3,
  // primary
  tok_identifier = -4,
  tok_number = -5,
};

// Filled in if tok_identifier
extern std::string IdentifierStr; // TODO: tokenIdentifier

// Filled in if tok_number
extern double NumVal; // TODO: tokenNumericValue

// Returns the next token from standard input.
int getToken();
