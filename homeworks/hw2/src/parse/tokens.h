/* hw1 tokens collected into one array, kinds mapped to an enum. */
#ifndef PARSE_TOKENS_H
#define PARSE_TOKENS_H

#include "hw1.h"

typedef enum {
  TOK_EOF,
  TOK_FUNCTION,
  TOK_RETURNS,
  TOK_REQUIRES,
  TOK_ENSURES,
  TOK_USES,
  TOK_WHILE,
  TOK_IF,
  TOK_ELSE,
  TOK_ASSERT,
  TOK_ASSUME,
  TOK_INVARIANT,
  TOK_TRUE,
  TOK_FALSE,
  TOK_NOT,
  TOK_AND,
  TOK_OR,
  TOK_FORALL,
  TOK_EXISTS,
  TOK_TYPE_INT,
  TOK_INT,
  TOK_IDENT,
  TOK_LPAREN,
  TOK_RPAREN,
  TOK_LBRACE,
  TOK_RBRACE,
  TOK_LBRACKET,
  TOK_RBRACKET,
  TOK_COMMA,
  TOK_SEMICOLON,
  TOK_COLON,
  TOK_PIPE,
  TOK_PLUS,
  TOK_MINUS,
  TOK_STAR,
  TOK_SLASH,
  TOK_EQEQ,
  TOK_NEQ,
  TOK_LTE,
  TOK_GTE,
  TOK_LT,
  TOK_GT,
  TOK_EQ,
  TOK_ARROW,
  TOK_IMPLIES,
  TOK_COUNT
} TokenKind;

/* The text is input[offset .. offset + length). */
typedef struct {
  TokenKind kind;
  size_t offset, length, line, column;
} Lexeme;

/* How messages name a token kind, e.g. "';'" or "'while'". */
extern const char *const token_spellings[TOK_COUNT];

/* Scans input into *tokens, the last one TOK_EOF at the end of input; the
   caller frees *tokens. Returns 1 on success and 0 on a lexical
   error (see error), in which case *tokens is NULL. */
int tokens_collect(const Automaton *automaton, const unsigned char *input,
                   size_t length, Lexeme **tokens, LexError *error);

#endif
