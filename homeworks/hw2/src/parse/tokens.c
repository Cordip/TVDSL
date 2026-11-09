#include "parse/tokens.h"

#include "util/common.h"

#include <stdlib.h>
#include <string.h>

/* hw1 rule names (src/spec/rules.c), indexed by TokenKind. */
static const char *const rule_names[TOK_COUNT] = {
    [TOK_FUNCTION] = "FUNCTION",
    [TOK_RETURNS] = "RETURNS",
    [TOK_REQUIRES] = "REQUIRES",
    [TOK_ENSURES] = "ENSURES",
    [TOK_USES] = "USES",
    [TOK_WHILE] = "WHILE",
    [TOK_IF] = "IF",
    [TOK_ELSE] = "ELSE",
    [TOK_ASSERT] = "ASSERT",
    [TOK_ASSUME] = "ASSUME",
    [TOK_INVARIANT] = "INVARIANT",
    [TOK_TRUE] = "TRUE",
    [TOK_FALSE] = "FALSE",
    [TOK_NOT] = "NOT",
    [TOK_AND] = "AND",
    [TOK_OR] = "OR",
    [TOK_FORALL] = "FORALL",
    [TOK_EXISTS] = "EXISTS",
    [TOK_TYPE_INT] = "TYPE_INT",
    [TOK_INT] = "INT",
    [TOK_IDENT] = "IDENT",
    [TOK_LPAREN] = "LPAREN",
    [TOK_RPAREN] = "RPAREN",
    [TOK_LBRACE] = "LBRACE",
    [TOK_RBRACE] = "RBRACE",
    [TOK_LBRACKET] = "LBRACKET",
    [TOK_RBRACKET] = "RBRACKET",
    [TOK_COMMA] = "COMMA",
    [TOK_SEMICOLON] = "SEMICOLON",
    [TOK_COLON] = "COLON",
    [TOK_PIPE] = "PIPE",
    [TOK_PLUS] = "PLUS",
    [TOK_MINUS] = "MINUS",
    [TOK_STAR] = "STAR",
    [TOK_SLASH] = "SLASH",
    [TOK_EQEQ] = "EQEQ",
    [TOK_NEQ] = "NEQ",
    [TOK_LTE] = "LTE",
    [TOK_GTE] = "GTE",
    [TOK_LT] = "LT",
    [TOK_GT] = "GT",
    [TOK_EQ] = "EQ",
    [TOK_ARROW] = "ARROW",
    [TOK_IMPLIES] = "IMPLIES",
};

const char *const token_spellings[TOK_COUNT] = {
    [TOK_EOF] = "end of file",
    [TOK_FUNCTION] = "'function'",
    [TOK_RETURNS] = "'returns'",
    [TOK_REQUIRES] = "'requires'",
    [TOK_ENSURES] = "'ensures'",
    [TOK_USES] = "'uses'",
    [TOK_WHILE] = "'while'",
    [TOK_IF] = "'if'",
    [TOK_ELSE] = "'else'",
    [TOK_ASSERT] = "'assert'",
    [TOK_ASSUME] = "'assume'",
    [TOK_INVARIANT] = "'invariant'",
    [TOK_TRUE] = "'true'",
    [TOK_FALSE] = "'false'",
    [TOK_NOT] = "'not'",
    [TOK_AND] = "'and'",
    [TOK_OR] = "'or'",
    [TOK_FORALL] = "'forall'",
    [TOK_EXISTS] = "'exists'",
    [TOK_TYPE_INT] = "'int'",
    [TOK_INT] = "number",
    [TOK_IDENT] = "identifier",
    [TOK_LPAREN] = "'('",
    [TOK_RPAREN] = "')'",
    [TOK_LBRACE] = "'{'",
    [TOK_RBRACE] = "'}'",
    [TOK_LBRACKET] = "'['",
    [TOK_RBRACKET] = "']'",
    [TOK_COMMA] = "','",
    [TOK_SEMICOLON] = "';'",
    [TOK_COLON] = "':'",
    [TOK_PIPE] = "'|'",
    [TOK_PLUS] = "'+'",
    [TOK_MINUS] = "'-'",
    [TOK_STAR] = "'*'",
    [TOK_SLASH] = "'/'",
    [TOK_EQEQ] = "'=='",
    [TOK_NEQ] = "'!='",
    [TOK_LTE] = "'<='",
    [TOK_GTE] = "'>='",
    [TOK_LT] = "'<'",
    [TOK_GT] = "'>'",
    [TOK_EQ] = "'='",
    [TOK_ARROW] = "'->'",
    [TOK_IMPLIES] = "'=>'",
};

typedef struct {
  Lexeme *items;
  size_t count, capacity;
} Lexemes;

static TokenKind kind_of(const char *rule_name) {
  if (strcmp(rule_name, "LENGTH") == 0) {
    return TOK_IDENT;
  }

  for (int kind = TOK_EOF + 1; kind < TOK_COUNT; ++kind) {
    if (strcmp(rule_names[kind], rule_name) == 0) {
      return (TokenKind)kind;
    }
  }

  /* A rule added to hw1 without a parser token is a build mistake. */
  fprintf(stderr, "Unknown token kind %s\n", rule_name);
  exit(1);
}

static void push(Lexemes *lexemes, Lexeme lexeme) {
  if (lexemes->count == lexemes->capacity) {
    lexemes->capacity = lexemes->capacity ? lexemes->capacity * 2 : 256;
    lexemes->items =
        xrealloc(lexemes->items, lexemes->capacity * sizeof *lexemes->items);
  }

  lexemes->items[lexemes->count++] = lexeme;
}

static int collect(const Token *token, void *context) {
  push(context, (Lexeme){kind_of(token->kind), token->offset, token->length,
                         token->line, token->column});

  return 1;
}

int tokens_collect(const Automaton *automaton, const unsigned char *input,
                   size_t length, Lexeme **tokens, LexError *error) {
  Lexemes lexemes = {0};

  if (lexer_scan(automaton, input, length, collect, &lexemes, error) != 1) {
    free(lexemes.items);
    *tokens = NULL;

    return 0;
  }

  /* hw1 reports no end position, so count it here for the EOF token. */
  Lexeme end = {TOK_EOF, length, 0, 1, 1};

  for (size_t byte_index = 0; byte_index < length; ++byte_index) {
    if (input[byte_index] == '\n') {
      ++end.line;
      end.column = 1;
    } else {
      ++end.column;
    }
  }

  push(&lexemes, end);
  *tokens = lexemes.items;

  return 1;
}
