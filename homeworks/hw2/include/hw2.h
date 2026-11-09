/* Public API of the Funny parser: the AST, the parser over the hw1 lexer,
 * desugaring and the printers.
 *
 * Modules (src/<group>/), in pipeline order:
 *   parse/tokens.c   hw1 tokens collected into an array with enum kinds
 *   parse/parser.c   LL(1) recursive descent, Pratt for expressions, panic mode
 *   ast/desugar.c    a[i] = v  ->  a = set(a, i, v)
 *   ast/print.c      tree and DOT views with shared nodes
 *   ast/ast.c        arena and node constructor
 *
 * The grammar is in grammar.md. */

#ifndef HW2_H
#define HW2_H

#include "hw1.h"

#include <stdio.h>

/* Every node and string lives in the arena; arena_free releases them all.
   Zero-initialize before use: Arena arena = {0}. */
typedef struct {
  struct ArenaBlock *blocks;
} Arena;

void *arena_alloc(Arena *arena, size_t size);
void arena_free(Arena *arena);

typedef enum {
  NODE_MODULE,
  NODE_FUNCTION,
  NODE_FORMULA,
  NODE_PARAM,
  NODE_LOCAL,
  /* Statements. */
  NODE_ASSIGN,
  NODE_ARRAY_ASSIGN, /* raw tree only */
  NODE_TUPLE_ASSIGN,
  NODE_IF,
  NODE_WHILE,
  NODE_BLOCK,
  NODE_ASSERT,
  NODE_ASSUME,
  /* Expressions, conditions and predicates. */
  NODE_INT,
  NODE_BOOL,
  NODE_VAR,
  NODE_INDEX,
  NODE_CALL,
  NODE_UNARY,
  NODE_BINARY,
  NODE_COMPARE,
  NODE_NOT,
  NODE_LOGICAL,
  NODE_QUANTIFIER,
  NODE_ARRAY_UPDATE /* desugared tree only */
} NodeKind;

typedef enum { TYPE_NONE, TYPE_INT, TYPE_INT_ARRAY } VarType;

typedef enum {
  OP_NEG, /* Unary */
  OP_ADD, /* Binary */
  OP_SUB,
  OP_MUL,
  OP_DIV,
  OP_EQ, /* Compare */
  OP_NE,
  OP_LT,
  OP_LE,
  OP_GT,
  OP_GE,
  OP_AND, /* Logical */
  OP_OR,
  OP_IMPLIES,
  OP_FORALL, /* Quantifier */
  OP_EXISTS
} Op;

typedef struct Node Node;

/* Lists are chained through next. Optional children (requires, ensures,
   invariant, else, a Local's type) are NULL or TYPE_NONE when absent.
   After desugar_module a tree may share subtrees: an array chain reuses its
   prefix nodes, so treat nodes as immutable. NULL annotations preserve source
   absence; semantic defaults are requires=true, ensures=false, invariant=true
   (funny.ru.md). A later pass applies them; the parser inserts no Bool nodes.
 */
struct Node {
  NodeKind kind;
  size_t line, column; /* first token */
  Node *next;
  union {
    struct {
      Node *items; /* Function and Formula */
    } module;
    struct {
      const char *name;
      Node *params, *precondition, *returns, *postcondition, *locals, *body;
    } function;
    struct {
      const char *name;
      Node *params, *body;
    } formula;
    struct {
      const char *name;
      VarType type;
    } var_def; /* Param, Local */
    struct {
      const char *name;
      Node *value;
    } assign;
    struct {
      const char *name;
      Node *indices, *value;
    } array_assign;
    struct {
      Node *targets; /* Var */
      Node *call;
    } tuple_assign;
    struct {
      Node *condition, *then_branch, *else_branch;
    } if_stmt;
    struct {
      Node *condition, *invariant, *body;
    } while_stmt;
    struct {
      Node *statements;
    } block;
    struct {
      Node *predicate;
    } check;             /* Assert, Assume */
    const char *literal; /* Int: decimal text, no range check */
    int boolean;
    const char *var; /* Var */
    struct {
      Node *array, *index;
    } index;
    struct {
      const char *name; /* "length" for the builtin */
      Node *args;
    } call;
    struct {
      Op op;
      Node *operand;
    } unary; /* Unary, Not */
    struct {
      Op op;
      Node *left, *right;
    } binary; /* Binary, Compare, Logical */
    struct {
      Op op;
      Node *var; /* Param */
      Node *body;
    } quantifier;
    struct {
      Node *array, *index, *value;
    } update; /* ArrayUpdate: set(array, index, value) */
  } as;
};

/* Zeroed node from the arena. */
Node *ast_node(Arena *arena, NodeKind kind, size_t line, size_t column);

/* Lexes and parses input. The first lexical error stops before parsing; after
   successful tokenization, panic recovery can report multiple syntax errors.
   Diagnostics go to stderr as "path:line:col: error: message". Returns Module
   or NULL after any error. Nodes/strings belong to arena, including partial
   results on failure: the caller always frees arena. Input may be released
   after this call. TYPE_NONE on a Local means its type was not specified. */
Node *parse_source(const Automaton *automaton, const char *path,
                   const unsigned char *input, size_t length, Arena *arena);

/* Rewrites every ArrayAssign into Assign(a, ArrayUpdate(...)), in place. */
void desugar_module(Node *module, Arena *arena);

typedef enum { AST_TREE, AST_DOT } AstFormat;

/* TREE shows child fields as branches and shared nodes as references to #ID.
   Indentation is capped at 12 levels; deeper lines show their actual depth.
   DOT: digraph AST, root n0. Each node pointer has one definition with kind,
   line, col, label and its scalar fields. Edge labels name child fields;
   list edges include a zero-based index (e.g. params[0]). Missing optional
   children and empty lists have no edges. IDs follow breadth-first discovery
   order and are stable across runs. Strings escape quotes, backslashes and
   line breaks; the caller owns output and checks its write status. */
void ast_print(FILE *output, const Node *node, AstFormat format);

#endif
