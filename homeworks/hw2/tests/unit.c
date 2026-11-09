/* Direct component checks; the CLI snapshots exercise complete programs. */
#include "hw2.h"
#include "parse/tokens.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t passed, failed;

static int check(const char *name, int success) {
  printf("%s  %s\n", success ? "PASS" : "FAIL", name);
  if (success) {
    ++passed;
  } else {
    ++failed;
  }
  return success;
}

static Node *parse(const Automaton *automaton, const char *source,
                   Arena *arena) {
  return parse_source(automaton, "<unit>", (const unsigned char *)source,
                      strlen(source), arena);
}

static Node *function(Node *module) {
  if (!module || module->kind != NODE_MODULE || !module->as.module.items ||
      module->as.module.items->kind != NODE_FUNCTION) {
    return NULL;
  }
  return module->as.module.items;
}

static char *dump(const Node *node, AstFormat format) {
  FILE *out = tmpfile();
  if (!out) {
    return NULL;
  }
  ast_print(out, node, format);
  long length = ftell(out);
  if (ferror(out) || length < 0 || length > 65536 || fflush(out) != 0) {
    fclose(out);
    return NULL;
  }
  rewind(out);
  char *text = calloc((size_t)length + 1, 1);
  if (!text) {
    fclose(out);
    return NULL;
  }
  size_t read = fread(text, 1, (size_t)length, out);
  if (fclose(out) != 0 || read != (size_t)length) {
    free(text);
    return NULL;
  }
  return text;
}

static size_t occurrences(const char *text, const char *needle) {
  size_t count = 0, length = strlen(needle);
  for (const char *found = text; (found = strstr(found, needle));
       found += length) {
    ++count;
  }
  return count;
}

static void tokens(const Automaton *automaton) {
  const unsigned char source[] = " \r\n length(x) + 42 // \xc3\xa9\n";
  const TokenKind kinds[] = {TOK_IDENT, TOK_LPAREN, TOK_IDENT, TOK_RPAREN,
                             TOK_PLUS,  TOK_INT,    TOK_EOF};
  const size_t offsets[] = {4, 10, 11, 12, 14, 16, 25};
  const size_t lengths[] = {6, 1, 1, 1, 1, 2, 0};
  const size_t columns[] = {2, 8, 9, 10, 12, 14, 1};
  Lexeme *items = NULL;
  LexError error = {0};
  int ok = tokens_collect(automaton, source, sizeof source - 1, &items, &error);
  if (check("tokens collected, including non-ASCII comment", ok)) {
    int kinds_ok = 1, positions_ok = 1;
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; ++i) {
      kinds_ok &= items[i].kind == kinds[i];
      positions_ok &=
          items[i].offset == offsets[i] && items[i].length == lengths[i] &&
          items[i].column == columns[i] && items[i].line == (i == 6 ? 3u : 2u);
    }
    check("length maps to IDENT and token kinds are preserved", kinds_ok);
    check("token spans and EOF position", positions_ok);
  }
  free(items);
  items = NULL;
  ok = tokens_collect(automaton, (const unsigned char *)"", 0, &items, &error);
  check("empty input has positioned EOF",
        ok && items && items[0].kind == TOK_EOF && items[0].offset == 0 &&
            items[0].length == 0 && items[0].line == 1 && items[0].column == 1);
  free(items);
  Lexeme marker = {0};
  items = &marker;
  ok = tokens_collect(automaton, (const unsigned char *)"007", 3, &items,
                      &error);
  check("lexical failure returns no token array",
        !ok && items == NULL && error.offset == 0 && error.line == 1 &&
            error.column == 1 && error.byte == '0');
  if (items != &marker) {
    free(items);
  }
}

static void arena(void) {
  Arena storage = {0};
  unsigned char *first = arena_alloc(&storage, 31);
  memset(first, 0xa5, 31);
  struct ArenaBlock *initial = storage.blocks;
  const size_t sizes[] = {1, 3, 17, 100003, 100003};
  int aligned = (uintptr_t)first % _Alignof(max_align_t) == 0;
  for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) {
    void *allocation = arena_alloc(&storage, sizes[i]);
    aligned &= (uintptr_t)allocation % _Alignof(max_align_t) == 0;
    memset(allocation, (int)i, sizes[i]);
  }
  check("arena allocation alignment", aligned);
  int preserved = 1;
  for (size_t i = 0; i < 31; ++i) {
    preserved &= first[i] == 0xa5;
  }
  check("arena growth preserves old data",
        storage.blocks != initial && preserved);
  Node *local = ast_node(&storage, NODE_LOCAL, 5, 7);
  check("node construction initializes position and optional data",
        local->kind == NODE_LOCAL && local->line == 5 && local->column == 7 &&
            !local->next && !local->as.var_def.name &&
            local->as.var_def.type == TYPE_NONE);
  arena_free(&storage);
  check("arena_free clears the arena", storage.blocks == NULL);
  Node *fresh = ast_node(&storage, NODE_BOOL, 1, 2);
  check("freed arena can be reused",
        fresh->kind == NODE_BOOL && !fresh->as.boolean);
  arena_free(&storage);
}

static void parser(const Automaton *automaton) {
  const char *source = "function f(x: int, y: int) returns r: int {\n"
                       "  r = 1 + 2 * 3 - 4;\n"
                       "  if (x > 0) r = x; else r = 0;\n"
                       "}\n"
                       "p(x: int) => x > 0 -> x < 2 -> x == 1;\n";
  Arena storage = {0};
  Node *module = parse(automaton, source, &storage);
  Node *f = function(module);
  if (!check("parser creates a module and function", f != NULL)) {
    arena_free(&storage);
    return;
  }
  check("function fields and source position",
        strcmp(f->as.function.name, "f") == 0 && f->line == 1 &&
            f->column == 1 && f->as.function.params &&
            f->as.function.params->next &&
            f->as.function.params->as.var_def.type == TYPE_INT &&
            f->as.function.returns &&
            strcmp(f->as.function.returns->as.var_def.name, "r") == 0 &&
            !f->as.function.precondition && !f->as.function.postcondition);
  Node *block = f->as.function.body;
  Node *assignment =
      block && block->kind == NODE_BLOCK ? block->as.block.statements : NULL;
  Node *value = assignment && assignment->kind == NODE_ASSIGN
                    ? assignment->as.assign.value
                    : NULL;
  Node *sum =
      value && value->kind == NODE_BINARY && value->as.binary.op == OP_SUB
          ? value->as.binary.left
          : NULL;
  Node *product = sum && sum->kind == NODE_BINARY && sum->as.binary.op == OP_ADD
                      ? sum->as.binary.right
                      : NULL;
  check("AST arithmetic precedence and left association",
        product && product->kind == NODE_BINARY &&
            product->as.binary.op == OP_MUL && sum->as.binary.left &&
            sum->as.binary.left->kind == NODE_INT && value->as.binary.right &&
            strcmp(value->as.binary.right->as.literal, "4") == 0);
  check("AST statement and expression positions",
        assignment && assignment->line == 2 && assignment->column == 3 &&
            value && value->line == 2 && value->column == 7);
  Node *conditional = assignment ? assignment->next : NULL;
  check("AST conditional has both branches",
        conditional && conditional->kind == NODE_IF &&
            conditional->as.if_stmt.condition &&
            conditional->as.if_stmt.condition->kind == NODE_COMPARE &&
            conditional->as.if_stmt.then_branch &&
            conditional->as.if_stmt.else_branch);
  Node *formula = f->next;
  Node *body = formula && formula->kind == NODE_FORMULA
                   ? formula->as.formula.body
                   : NULL;
  check("AST implication is right associative",
        body && body->kind == NODE_LOGICAL &&
            body->as.binary.op == OP_IMPLIES && body->as.binary.left &&
            body->as.binary.left->kind == NODE_COMPARE &&
            body->as.binary.right &&
            body->as.binary.right->kind == NODE_LOGICAL &&
            body->as.binary.right->as.binary.op == OP_IMPLIES);
  char *text = dump(module, AST_DOT);
  const char *first = text ? strstr(text, "label=\"params[0]\"") : NULL;
  const char *second = text ? strstr(text, "label=\"params[1]\"") : NULL;
  check("DOT retains ordered parameter edges",
        first && second && first < second);
  free(text);
  arena_free(&storage);
}

static void desugaring(const Automaton *automaton) {
  Arena storage = {0};
  Node *module = parse(automaton,
                       "f(a: int[], i: int, j: int, v: int) returns r: int {\n"
                       "  a[i][j] = v;\n  r = set(v);\n}\n",
                       &storage);
  Node *f = function(module);
  Node *raw = f && f->as.function.body
                  ? f->as.function.body->as.block.statements
                  : NULL;
  if (!check("raw array assignment has both indices",
             raw && raw->kind == NODE_ARRAY_ASSIGN &&
                 raw->as.array_assign.indices &&
                 raw->as.array_assign.indices->next)) {
    arena_free(&storage);
    return;
  }
  Node *i = raw->as.array_assign.indices, *j = i->next;
  Node *rhs = raw->as.array_assign.value;
  desugar_module(module, &storage);
  Node *outer = raw->kind == NODE_ASSIGN ? raw->as.assign.value : NULL;
  Node *inner =
      outer && outer->kind == NODE_ARRAY_UPDATE ? outer->as.update.value : NULL;
  Node *prefix =
      inner && inner->kind == NODE_ARRAY_UPDATE ? inner->as.update.array : NULL;
  check("nested array assignment becomes nested updates",
        outer && inner && inner->kind == NODE_ARRAY_UPDATE &&
            outer->as.update.index == i && inner->as.update.index == j &&
            inner->as.update.value == rhs && !i->next && !j->next);
  check("desugaring shares the original array and index prefix",
        prefix && prefix->kind == NODE_INDEX &&
            prefix->as.index.array == outer->as.update.array &&
            prefix->as.index.index == i);
  check("desugared nodes retain assignment position",
        outer && inner && prefix && outer->line == 2 && outer->column == 3 &&
            inner->line == 2 && inner->column == 3 && prefix->line == 2 &&
            prefix->column == 3);
  Node *call = raw->next && raw->next->kind == NODE_ASSIGN
                   ? raw->next->as.assign.value
                   : NULL;
  check("user function set remains a call",
        call && call->kind == NODE_CALL &&
            strcmp(call->as.call.name, "set") == 0);
  arena_free(&storage);

  module =
      parse(automaton, "f(a: int[]) returns r: int { a[0] = 7; }", &storage);
  f = function(module);
  raw = f && f->as.function.body ? f->as.function.body->as.block.statements
                                 : NULL;
  desugar_module(module, &storage);
  outer = raw && raw->kind == NODE_ASSIGN ? raw->as.assign.value : NULL;
  check(
      "one-dimensional update preserves its operands",
      outer && outer->kind == NODE_ARRAY_UPDATE && outer->as.update.array &&
          outer->as.update.array->kind == NODE_VAR &&
          strcmp(outer->as.update.array->as.var, "a") == 0 &&
          outer->as.update.index && outer->as.update.index->kind == NODE_INT &&
          strcmp(outer->as.update.index->as.literal, "0") == 0 &&
          outer->as.update.value && outer->as.update.value->kind == NODE_INT &&
          strcmp(outer->as.update.value->as.literal, "7") == 0);
  arena_free(&storage);
}

static void ownership(const Automaton *automaton) {
  const char *original = "function owned(a: int[]) returns result: int[] {\n"
                         " result = a; result[0] = 123;\n}\n";
  char *source = malloc(strlen(original) + 1);
  if (!source) {
    check("ownership source allocation", 0);
    return;
  }
  strcpy(source, original);
  Arena storage = {0};
  Node *module = parse(automaton, source, &storage);
  free(source);
  Node *f = function(module);
  Node *first = f && f->as.function.body
                    ? f->as.function.body->as.block.statements
                    : NULL;
  Node *update = first ? first->next : NULL;
  check("AST names and literals outlive source buffer",
        f && strcmp(f->as.function.name, "owned") == 0 &&
            f->as.function.returns &&
            strcmp(f->as.function.returns->as.var_def.name, "result") == 0 &&
            update && update->kind == NODE_ARRAY_ASSIGN &&
            update->as.array_assign.value &&
            strcmp(update->as.array_assign.value->as.literal, "123") == 0);
  desugar_module(module, &storage);
  char *text = module ? dump(module, AST_DOT) : NULL;
  check("AST can be desugared and printed after source release",
        text && strstr(text, "name=\"owned\"") &&
            strstr(text, "value=\"123\""));
  free(text);
  arena_free(&storage);
}

static void dot(void) {
  Arena storage = {0};
  Node *shared = ast_node(&storage, NODE_VAR, 1, 1);
  shared->as.var = "a\"b\\c\nnext";
  Node *first = ast_node(&storage, NODE_INT, 2, 4);
  Node *second = ast_node(&storage, NODE_INT, 3, 4);
  first->as.literal = second->as.literal = "1";
  Node *left = ast_node(&storage, NODE_INDEX, 2, 1);
  Node *right = ast_node(&storage, NODE_INDEX, 3, 1);
  left->as.index.array = right->as.index.array = shared;
  left->as.index.index = first;
  right->as.index.index = second;
  Node *sum = ast_node(&storage, NODE_BINARY, 1, 1);
  sum->as.binary.op = OP_ADD;
  sum->as.binary.left = left;
  sum->as.binary.right = right;
  char *text = dump(sum, AST_DOT);
  char *again = dump(sum, AST_DOT);
  if (check("DOT dump available", text && again)) {
    check("DOT describes unique pointers and preserves every edge",
          occurrences(text, " [kind=") == 6 && occurrences(text, " -> ") == 6 &&
              occurrences(text, " -> n3 [label=\"array\"]") == 2);
    check("DOT keeps distinct equal-valued nodes and positions",
          occurrences(text, "value=\"1\"") == 2 &&
              strstr(text, "line=2, col=4") && strstr(text, "line=3, col=4"));
    check("DOT escapes data strings",
          strstr(text, "name=\"a\\\"b\\\\c\\nnext\"") != NULL);
    check("DOT output is reproducible", strcmp(text, again) == 0);
    check("DOT roles and references have deterministic IDs",
          strstr(text, "n0 -> n1 [label=\"left\"]") &&
              strstr(text, "n0 -> n2 [label=\"right\"]") &&
              strstr(text, "n3 [kind=\"Var\"") &&
              strstr(text, "n4 [kind=\"Int\"") &&
              strstr(text, "n5 [kind=\"Int\""));
  }
  free(text);
  free(again);
  text = dump(sum, AST_TREE);
  if (check("tree dump available", text != NULL)) {
    check("tree branches retain roles and scalar values",
          strstr(text, "Binary op=\"+\"") && strstr(text, "├── left: Index") &&
              strstr(text, "└── right: Index") &&
              occurrences(text, "Int value=\"1\"") == 2);
    check("tree expands shared pointers once",
          occurrences(text, "Var name=") == 1 &&
              occurrences(text, "↪ #3 Var @1:1") == 1 &&
              occurrences(text, "[#") == 6);
    check("tree escapes data without breaking its branch lines",
          strstr(text, "name=\"a\\\"b\\\\c\\nnext\"") != NULL &&
              occurrences(text, "\n") == 7);
  }
  free(text);
  arena_free(&storage);
}

int main(void) {
  char error[256] = {0};
  Automaton *automaton = automaton_build(error, sizeof error);
  if (!automaton) {
    fprintf(stderr, "Build error: %s\n", error);
    return 1;
  }
  tokens(automaton);
  arena();
  parser(automaton);
  desugaring(automaton);
  ownership(automaton);
  dot();
  automaton_free(automaton);
  printf("%zu C checks passed, %zu failed\n", passed, failed);
  return failed != 0;
}
