/* LL(1) recursive descent over the grammar in grammar.md: one function per
   nonterminal, except that the expression levels share one precedence
   climbing function. A syntax error longjmps to the nearest guard (a block
   statement or a top-level item), which skips to a sync token and goes on. */
#include "hw2.h"

#include "parse/tokens.h"

#include <ctype.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* What the current expression stands for; decides how calls and booleans
   are checked. */
typedef enum { MODE_EXPRESSION, MODE_CONDITION, MODE_PREDICATE } Mode;

/* A call in a predicate is a formula reference where a boolean is needed and
   a function call where an integer is. */
typedef enum { VALUE_INT, VALUE_BOOL, VALUE_ANY } Value;

typedef struct {
  Node *node;
  int height;
} Expression;

typedef struct {
  const char *path;
  const unsigned char *input;
  const Lexeme *tokens; /* ends with TOK_EOF */
  size_t pos;
  Arena *arena;
  jmp_buf *recover;
  Mode mode;
  int failed;
  int depth; /* of parse_expr and parse_statement calls */
  int statement_depth;
} Parser;

/* Deeper nesting is refused before it overflows the C stack. */
enum { MAX_DEPTH = 1000 };

/* Binding power, weakest first; 0 in the table means "not a binary
   operator". */
enum {
  PREC_IMPLIES = 1,
  PREC_OR,
  PREC_AND,
  PREC_NOT,
  PREC_COMPARE,
  PREC_SUM,
  PREC_PRODUCT,
  PREC_UNARY
};

static const struct {
  int precedence;
  NodeKind kind;
  Op op;
} binary_ops[TOK_COUNT] = {
    [TOK_ARROW] = {PREC_IMPLIES, NODE_LOGICAL, OP_IMPLIES},
    [TOK_OR] = {PREC_OR, NODE_LOGICAL, OP_OR},
    [TOK_AND] = {PREC_AND, NODE_LOGICAL, OP_AND},
    [TOK_EQEQ] = {PREC_COMPARE, NODE_COMPARE, OP_EQ},
    [TOK_NEQ] = {PREC_COMPARE, NODE_COMPARE, OP_NE},
    [TOK_LT] = {PREC_COMPARE, NODE_COMPARE, OP_LT},
    [TOK_LTE] = {PREC_COMPARE, NODE_COMPARE, OP_LE},
    [TOK_GT] = {PREC_COMPARE, NODE_COMPARE, OP_GT},
    [TOK_GTE] = {PREC_COMPARE, NODE_COMPARE, OP_GE},
    [TOK_PLUS] = {PREC_SUM, NODE_BINARY, OP_ADD},
    [TOK_MINUS] = {PREC_SUM, NODE_BINARY, OP_SUB},
    [TOK_STAR] = {PREC_PRODUCT, NODE_BINARY, OP_MUL},
    [TOK_SLASH] = {PREC_PRODUCT, NODE_BINARY, OP_DIV},
};

static Node *parse_statement(Parser *p);
static Expression parse_expr(Parser *p, int min_precedence);

static TokenKind peek(const Parser *p) { return p->tokens[p->pos].kind; }

/* Never moves past TOK_EOF. */
static const Lexeme *advance(Parser *p) {
  const Lexeme *token = &p->tokens[p->pos];

  if (token->kind != TOK_EOF) {
    ++p->pos;
  }

  return token;
}

static int accept(Parser *p, TokenKind kind) {
  if (peek(p) != kind) {
    return 0;
  }

  advance(p);

  return 1;
}

static _Noreturn void fail_at(Parser *p, size_t line, size_t column,
                              const char *format, ...) {
  va_list args;

  fprintf(stderr, "%s:%zu:%zu: error: ", p->path, line, column);
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputc('\n', stderr);
  p->failed = 1;
  longjmp(*p->recover, 1);
}

static _Noreturn void fail_expected(Parser *p, const char *expected) {
  const Lexeme *token = &p->tokens[p->pos];

  if (token->kind == TOK_IDENT || token->kind == TOK_INT) {
    int length = token->length > 32 ? 32 : (int)token->length;

    fail_at(p, token->line, token->column, "expected %s, found %s '%.*s%s'",
            expected, token_spellings[token->kind], length,
            (const char *)p->input + token->offset,
            token->length > 32 ? "..." : "");
  }

  fail_at(p, token->line, token->column, "expected %s, found %s", expected,
          token_spellings[token->kind]);
}

static void check_depth(Parser *p, int depth) {
  if (depth > MAX_DEPTH) {
    const Lexeme *token = &p->tokens[p->pos];

    fail_at(p, token->line, token->column, "nesting deeper than %d levels",
            MAX_DEPTH);
  }
}

static void enter(Parser *p) { check_depth(p, ++p->depth); }

static int maximum(int left, int right) { return left > right ? left : right; }

static Expression expression(Parser *p, Node *node, int height) {
  check_depth(p, p->statement_depth + height);

  return (Expression){node, height};
}

static const Lexeme *expect(Parser *p, TokenKind kind) {
  if (peek(p) != kind) {
    fail_expected(p, token_spellings[kind]);
  }

  return advance(p);
}

static void expect_semicolon(Parser *p) {
  if (!accept(p, TOK_SEMICOLON)) {
    const Lexeme *previous = &p->tokens[p->pos - 1];

    fail_at(p, previous->line, previous->column + previous->length,
            "expected ';' after expression");
  }
}

static const char *text_of(Parser *p, const Lexeme *token) {
  char *text = arena_alloc(p->arena, token->length + 1);

  memcpy(text, p->input + token->offset, token->length);
  text[token->length] = '\0';

  return text;
}

static Node *node_at(Parser *p, NodeKind kind, const Lexeme *token) {
  return ast_node(p->arena, kind, token->line, token->column);
}

/* A function or formula header starts here: "function", or a name whose
   parenthesized list is followed by "requires", "returns" or "=>". No
   statement starts like that, so it only ends error recovery and a block
   that misses its '}'. */
static int at_item_start(const Parser *p) {
  size_t index = p->pos;

  if (p->tokens[index].kind == TOK_FUNCTION) {
    return 1;
  }

  if (p->tokens[index].kind != TOK_IDENT ||
      p->tokens[index + 1].kind != TOK_LPAREN) {
    return 0;
  }

  for (index += 2; p->tokens[index].kind != TOK_RPAREN; ++index) {
    switch (p->tokens[index].kind) {
    case TOK_EOF:
    case TOK_LPAREN:
    case TOK_SEMICOLON:
    case TOK_LBRACE:
    case TOK_RBRACE:
      return 0;
    default:
      break;
    }
  }

  TokenKind next = p->tokens[index + 1].kind;

  return next == TOK_REQUIRES || next == TOK_RETURNS || next == TOK_IMPLIES;
}

/* Panic mode: skips to the next item; inside a block also past the next ';'
   or a balanced '{...}', or up to the '}' that closes the block. If the
   failed rule consumed nothing since start, one token goes first, which keeps
   every loop over guards finite. */
static void synchronize(Parser *p, int statement, size_t start) {
  size_t depth = 0;

  while (peek(p) != TOK_EOF) {
    TokenKind kind = peek(p);

    if (p->pos != start &&
        (at_item_start(p) || (statement && depth == 0 && kind == TOK_RBRACE))) {
      return;
    }

    advance(p);

    if (kind == TOK_LBRACE) {
      ++depth;
    } else if (kind == TOK_RBRACE && depth > 0) {
      if (--depth == 0 && statement && peek(p) != TOK_ELSE) {
        return;
      }
    } else if (kind == TOK_SEMICOLON && depth == 0 && statement &&
               peek(p) != TOK_ELSE) {
      return;
    }
  }
}

/* Runs parse; after a syntax error synchronizes and returns NULL. */
static Node *parse_guarded(Parser *p, Node *(*parse)(Parser *), int statement) {
  jmp_buf here, *outer = p->recover;
  size_t start = p->pos;
  int depth = p->depth;
  int statement_depth = p->statement_depth;

  p->recover = &here;

  if (setjmp(here) != 0) {
    p->recover = outer;
    p->depth = depth;
    p->statement_depth = statement_depth;
    synchronize(p, statement, start);

    return NULL;
  }

  Node *node = parse(p);

  p->recover = outer;

  return node;
}

static Value value_of(const Parser *p, const Node *node) {
  switch (node->kind) {
  case NODE_BOOL:
  case NODE_COMPARE:
  case NODE_NOT:
  case NODE_LOGICAL:
  case NODE_QUANTIFIER:
    return VALUE_BOOL;
  case NODE_CALL:
    return p->mode == MODE_PREDICATE ? VALUE_ANY : VALUE_INT;
  default:
    return VALUE_INT;
  }
}

/* Reports "expected condition, found expression" and the like. */
static Expression need(Parser *p, Expression parsed, Value value) {
  Node *node = parsed.node;
  Value actual = value_of(p, node);

  if (actual != VALUE_ANY && actual != value) {
    const char *boolean = p->mode == MODE_PREDICATE ? "predicate" : "condition";

    fail_at(p, node->line, node->column, "expected %s, found %s",
            value == VALUE_BOOL ? boolean : "expression",
            value == VALUE_BOOL ? "expression" : boolean);
  }

  return parsed;
}

static Expression parse_integer(Parser *p) {
  return need(p, parse_expr(p, PREC_IMPLIES), VALUE_INT);
}

static Expression parse_condition(Parser *p) {
  p->mode = MODE_CONDITION;

  Expression parsed = parse_expr(p, PREC_IMPLIES);

  if (peek(p) == TOK_EQ) {
    fail_expected(p, "')' or comparison");
  }

  return need(p, parsed, VALUE_BOOL);
}

static Expression parse_predicate(Parser *p) {
  p->mode = MODE_PREDICATE;

  return need(p, parse_expr(p, PREC_IMPLIES), VALUE_BOOL);
}

static VarType parse_variable_type(Parser *p) {
  expect(p, TOK_TYPE_INT);

  if (accept(p, TOK_LBRACKET)) {
    expect(p, TOK_RBRACKET);

    return TYPE_INT_ARRAY;
  }

  return TYPE_INT;
}

static Node *parse_variable_def(Parser *p) {
  const Lexeme *name = expect(p, TOK_IDENT);
  Node *node = node_at(p, NODE_PARAM, name);

  node->as.var_def.name = text_of(p, name);
  expect(p, TOK_COLON);
  node->as.var_def.type = parse_variable_type(p);

  return node;
}

static Node *parse_local_var_def(Parser *p) {
  const Lexeme *name = expect(p, TOK_IDENT);
  Node *node = node_at(p, NODE_LOCAL, name);

  node->as.var_def.name = text_of(p, name);

  if (accept(p, TOK_COLON)) {
    node->as.var_def.type = parse_variable_type(p);
  }

  return node;
}

/* name was just consumed. */
static Expression parse_arguments(Parser *p, const Lexeme *name) {
  Node *node = node_at(p, NODE_CALL, name);
  Node **tail = &node->as.call.args;
  int height = 1;

  node->as.call.name = text_of(p, name);
  expect(p, TOK_LPAREN);

  if (peek(p) != TOK_RPAREN) {
    do {
      Expression argument = parse_integer(p);

      *tail = argument.node;
      height = maximum(height, 1 + argument.height);
      tail = &(*tail)->next;
    } while (accept(p, TOK_COMMA));
  }

  expect(p, TOK_RPAREN);

  return expression(p, node, height);
}

static Expression parse_function_call(Parser *p) {
  if (peek(p) != TOK_IDENT) {
    fail_expected(p, "function call");
  }

  return parse_arguments(p, advance(p));
}

static Expression parse_index(Parser *p) {
  expect(p, TOK_LBRACKET);

  Expression index = parse_integer(p);

  expect(p, TOK_RBRACKET);

  return index;
}

static Expression parse_quantifier(Parser *p) {
  const Lexeme *token = advance(p);

  if (p->mode != MODE_PREDICATE) {
    fail_at(p, token->line, token->column, "quantifier outside a predicate");
  }

  Node *node = node_at(p, NODE_QUANTIFIER, token);

  node->as.quantifier.op = token->kind == TOK_FORALL ? OP_FORALL : OP_EXISTS;
  expect(p, TOK_LPAREN);
  node->as.quantifier.var = parse_variable_def(p);
  expect(p, TOK_PIPE);
  Expression body = parse_predicate(p);

  node->as.quantifier.body = body.node;
  expect(p, TOK_RPAREN);

  return expression(p, node, 1 + body.height);
}

static Expression parse_primary(Parser *p) {
  const Lexeme *token = &p->tokens[p->pos];
  Node *node;

  switch (token->kind) {
  case TOK_INT:
    node = node_at(p, NODE_INT, advance(p));
    node->as.literal = text_of(p, token);

    return expression(p, node, 1);
  case TOK_TRUE:
  case TOK_FALSE:
    node = node_at(p, NODE_BOOL, advance(p));
    node->as.boolean = token->kind == TOK_TRUE;

    return expression(p, node, 1);
  case TOK_LPAREN: {
    advance(p);
    Expression parsed = parse_expr(p, PREC_IMPLIES);

    node = parsed.node;
    expect(p, TOK_RPAREN);
    /* The node starts at its '(' now. */
    node->line = token->line;
    node->column = token->column;

    return parsed;
  }
  case TOK_FORALL:
  case TOK_EXISTS:
    return parse_quantifier(p);
  case TOK_IDENT: {
    advance(p);

    if (peek(p) == TOK_LPAREN) {
      return parse_arguments(p, token);
    }

    node = node_at(p, NODE_VAR, token);
    node->as.var = text_of(p, token);
    Expression parsed = expression(p, node, 1);

    while (peek(p) == TOK_LBRACKET) {
      Node *array = node;

      node = node_at(p, NODE_INDEX, token);
      node->as.index.array = array;
      Expression index = parse_index(p);

      node->as.index.index = index.node;
      parsed = expression(p, node, 1 + maximum(parsed.height, index.height));
    }

    return parsed;
  }
  default:
    fail_expected(p, p->mode == MODE_EXPRESSION  ? "expression"
                     : p->mode == MODE_CONDITION ? "condition"
                                                 : "predicate");
  }
}

/* Precedence climbing over binary_ops; prefix "not" and "-" bind their
   operand at their own level. Operand kinds are checked as soon as known. */
static Expression parse_expr(Parser *p, int min_precedence) {
  const Lexeme *token = &p->tokens[p->pos];
  Expression left;
  int depth = p->depth;

  enter(p);

  if (accept(p, TOK_NOT)) {
    if (p->mode == MODE_EXPRESSION) {
      fail_at(p, token->line, token->column,
              "expected expression, found condition");
    }

    Node *node = node_at(p, NODE_NOT, token);
    Expression operand = need(p, parse_expr(p, PREC_NOT), VALUE_BOOL);

    node->as.unary.operand = operand.node;
    left = expression(p, node, 1 + operand.height);
  } else if (accept(p, TOK_MINUS)) {
    Node *node = node_at(p, NODE_UNARY, token);
    Expression operand = need(p, parse_expr(p, PREC_UNARY), VALUE_INT);

    node->as.unary.op = OP_NEG;
    node->as.unary.operand = operand.node;
    left = expression(p, node, 1 + operand.height);
  } else {
    left = parse_primary(p);
  }

  for (;;) {
    TokenKind op = peek(p);
    int precedence = binary_ops[op].precedence;

    if (precedence == 0 || precedence < min_precedence) {
      p->depth = depth;

      return left;
    }

    Value operand =
        binary_ops[op].kind == NODE_LOGICAL ? VALUE_BOOL : VALUE_INT;
    if (operand == VALUE_BOOL && p->mode == MODE_EXPRESSION) {
      const Lexeme *operator = &p->tokens[p->pos];

      fail_at(p, operator->line, operator->column,
              "expected expression, found condition");
    }

    Node *node = ast_node(p->arena, binary_ops[op].kind, left.node->line,
                          left.node->column);

    node->as.binary.op = binary_ops[op].op;
    node->as.binary.left = need(p, left, operand).node;
    advance(p);
    /* "->" is right-associative, the others left-associative. */
    Expression right =
        need(p, parse_expr(p, op == TOK_ARROW ? precedence : precedence + 1),
             operand);

    node->as.binary.right = right.node;
    left = expression(p, node, 1 + maximum(left.height, right.height));

    if (precedence == PREC_COMPARE &&
        binary_ops[peek(p)].precedence == PREC_COMPARE) {
      const Lexeme *extra = &p->tokens[p->pos];

      fail_at(p, extra->line, extra->column,
              "comparison operators cannot be chained");
    }
  }
}

static Node *parse_assignment(Parser *p) {
  const Lexeme *name = advance(p);
  Node *node;
  Node **tail;

  p->mode = MODE_EXPRESSION;

  switch (peek(p)) {
  case TOK_EQ:
    advance(p);
    node = node_at(p, NODE_ASSIGN, name);
    node->as.assign.name = text_of(p, name);
    node->as.assign.value = parse_integer(p).node;
    break;
  case TOK_LBRACKET: {
    node = node_at(p, NODE_ARRAY_ASSIGN, name);
    node->as.array_assign.name = text_of(p, name);
    tail = &node->as.array_assign.indices;
    int count = 0, height = 1;

    do {
      Expression index = parse_index(p);

      *tail = index.node;
      tail = &(*tail)->next;
      ++count;
      /* Each set adds a level; its array prefix grows with the index count. */
      height = maximum(height, maximum(2 * count, count + index.height));
      check_depth(p, p->statement_depth + height);
    } while (peek(p) == TOK_LBRACKET);

    expect(p, TOK_EQ);
    Expression value = parse_integer(p);

    node->as.array_assign.value = value.node;
    check_depth(p, p->statement_depth + maximum(height, count + value.height));
    break;
  }
  case TOK_COMMA:
    node = node_at(p, NODE_TUPLE_ASSIGN, name);
    tail = &node->as.tuple_assign.targets;
    *tail = node_at(p, NODE_VAR, name);
    (*tail)->as.var = text_of(p, name);

    while (accept(p, TOK_COMMA)) {
      const Lexeme *target = expect(p, TOK_IDENT);

      tail = &(*tail)->next;
      *tail = node_at(p, NODE_VAR, target);
      (*tail)->as.var = text_of(p, target);
    }

    expect(p, TOK_EQ);
    node->as.tuple_assign.call = parse_function_call(p).node;
    break;
  default:
    fail_expected(p, "'=', '[' or ','");
  }

  expect_semicolon(p);

  return node;
}

static Node *parse_conditional(Parser *p) {
  Node *node = node_at(p, NODE_IF, advance(p));

  expect(p, TOK_LPAREN);
  node->as.if_stmt.condition = parse_condition(p).node;
  expect(p, TOK_RPAREN);
  node->as.if_stmt.then_branch = parse_statement(p);

  /* A dangling else binds to the nearest if. */
  if (accept(p, TOK_ELSE)) {
    node->as.if_stmt.else_branch = parse_statement(p);
  }

  return node;
}

static Node *parse_loop(Parser *p) {
  Node *node = node_at(p, NODE_WHILE, advance(p));

  expect(p, TOK_LPAREN);
  node->as.while_stmt.condition = parse_condition(p).node;
  expect(p, TOK_RPAREN);

  if (accept(p, TOK_INVARIANT)) {
    node->as.while_stmt.invariant = parse_predicate(p).node;
  }

  node->as.while_stmt.body = parse_statement(p);

  return node;
}

static Node *parse_block(Parser *p) {
  const Lexeme *open = advance(p);
  Node *node = node_at(p, NODE_BLOCK, open);
  Node **tail = &node->as.block.statements;

  while (peek(p) != TOK_RBRACE && peek(p) != TOK_EOF && !at_item_start(p)) {
    Node *statement = parse_guarded(p, parse_statement, 1);

    if (statement) {
      *tail = statement;
      tail = &statement->next;
    }
  }

  if (peek(p) != TOK_RBRACE) {
    char expected[64];

    snprintf(expected, sizeof expected, "'}' to close '{' at %zu:%zu",
             open->line, open->column);
    fail_expected(p, expected);
  }

  advance(p);

  return node;
}

static Node *parse_assertion(Parser *p) {
  const Lexeme *token = advance(p);
  Node *node =
      node_at(p, token->kind == TOK_ASSERT ? NODE_ASSERT : NODE_ASSUME, token);

  node->as.check.predicate = parse_predicate(p).node;
  expect_semicolon(p);

  return node;
}

static Node *parse_statement(Parser *p) {
  Node *node;
  int depth = p->depth;

  if (at_item_start(p)) {
    fail_expected(p, "statement");
  }

  enter(p);
  ++p->statement_depth;

  switch (peek(p)) {
  case TOK_IDENT:
    node = parse_assignment(p);
    break;
  case TOK_IF:
    node = parse_conditional(p);
    break;
  case TOK_WHILE:
    node = parse_loop(p);
    break;
  case TOK_LBRACE:
    node = parse_block(p);
    break;
  case TOK_ASSERT:
  case TOK_ASSUME:
    node = parse_assertion(p);
    break;
  default:
    fail_expected(p, "statement");
  }

  --p->statement_depth;
  p->depth = depth;

  return node;
}

static Node *parse_parameters(Parser *p) {
  Node *params = NULL, **tail = &params;

  expect(p, TOK_LPAREN);

  if (peek(p) != TOK_RPAREN) {
    do {
      *tail = parse_variable_def(p);
      tail = &(*tail)->next;
    } while (accept(p, TOK_COMMA));
  }

  expect(p, TOK_RPAREN);

  return params;
}

/* The header up to the parameters is already in node. */
static Node *parse_function_tail(Parser *p, Node *node) {
  Node **tail = &node->as.function.returns;

  if (accept(p, TOK_REQUIRES)) {
    node->as.function.precondition = parse_predicate(p).node;
  }

  expect(p, TOK_RETURNS);

  do {
    *tail = parse_variable_def(p);
    tail = &(*tail)->next;
  } while (accept(p, TOK_COMMA));

  if (accept(p, TOK_ENSURES)) {
    node->as.function.postcondition = parse_predicate(p).node;
  }

  if (accept(p, TOK_USES)) {
    tail = &node->as.function.locals;

    do {
      *tail = parse_local_var_def(p);
      tail = &(*tail)->next;
    } while (accept(p, TOK_COMMA));
  }

  node->as.function.body = parse_statement(p);

  return node;
}

static Node *parse_item(Parser *p) {
  const Lexeme *start = &p->tokens[p->pos];
  int keyword = accept(p, TOK_FUNCTION);

  if (peek(p) != TOK_IDENT) {
    fail_expected(p, keyword ? "function name" : "function or formula");
  }

  const Lexeme *name = advance(p);
  Node *params = parse_parameters(p);

  /* The "function" keyword is for functions only. */
  if (!keyword && accept(p, TOK_IMPLIES)) {
    Node *node = node_at(p, NODE_FORMULA, start);

    node->as.formula.name = text_of(p, name);
    node->as.formula.params = params;
    node->as.formula.body = parse_predicate(p).node;
    accept(p, TOK_SEMICOLON); /* optional: see grammar.md */

    return node;
  }

  if (!keyword && peek(p) != TOK_REQUIRES && peek(p) != TOK_RETURNS) {
    fail_expected(p, "'=>', 'requires' or 'returns'");
  }

  Node *node = node_at(p, NODE_FUNCTION, start);

  node->as.function.name = text_of(p, name);
  node->as.function.params = params;

  return parse_function_tail(p, node);
}

static Node *parse_module(Parser *p) {
  Node *node = node_at(p, NODE_MODULE, &p->tokens[0]);
  Node **tail = &node->as.module.items;

  /* At least one item: empty input is an error. */
  do {
    Node *item = parse_guarded(p, parse_item, 0);

    if (item) {
      *tail = item;
      tail = &item->next;
    }
  } while (peek(p) != TOK_EOF);

  return node;
}

static void report_lexical(const char *path, const unsigned char *input,
                           size_t length, const LexError *error) {
  fprintf(stderr, "%s:%zu:%zu: error: ", path, error->line, error->column);

  if (error->byte == '0' && error->offset + 1 < length &&
      isdigit(input[error->offset + 1])) {
    fputs("integer literal with a leading zero\n", stderr);
  } else if (error->byte >= 32 && error->byte < 127) {
    fprintf(stderr, "unexpected character '%c'\n", error->byte);
  } else {
    fprintf(stderr, "unexpected byte 0x%02X\n", error->byte);
  }
}

Node *parse_source(const Automaton *automaton, const char *path,
                   const unsigned char *input, size_t length, Arena *arena) {
  Lexeme *tokens;
  LexError error;

  if (!tokens_collect(automaton, input, length, &tokens, &error)) {
    report_lexical(path, input, length, &error);

    return NULL;
  }

  Parser parser = {
      .path = path, .input = input, .tokens = tokens, .arena = arena};
  Node *module = parse_module(&parser);

  free(tokens);

  return parser.failed ? NULL : module;
}
