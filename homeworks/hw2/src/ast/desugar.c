/* Array update sugar: a[i] = v becomes a = set(a, i, v), and a chain nests
   without temporaries: a[i][j] = v becomes a = set(a, i, set(a[i], j, v)). */
#include "hw2.h"

/* set(base, index, value), where the rest of the chain after index goes into
   value. New nodes take the position of the assignment at. */
static Node *update(Arena *arena, const Node *at, Node *base, Node *index,
                    Node *value) {
  Node *rest = index->next;

  index->next = NULL;

  if (rest) {
    Node *element = ast_node(arena, NODE_INDEX, at->line, at->column);

    element->as.index.array = base;
    element->as.index.index = index;
    value = update(arena, at, element, rest, value);
  }

  Node *node = ast_node(arena, NODE_ARRAY_UPDATE, at->line, at->column);

  node->as.update.array = base;
  node->as.update.index = index;
  node->as.update.value = value;

  return node;
}

void desugar_module(Node *node, Arena *arena) {
  if (!node) {
    return;
  }

  switch (node->kind) {
  case NODE_MODULE:
    for (Node *item = node->as.module.items; item; item = item->next) {
      desugar_module(item, arena);
    }
    break;
  case NODE_FUNCTION:
    desugar_module(node->as.function.body, arena);
    break;
  case NODE_BLOCK:
    for (Node *statement = node->as.block.statements; statement;
         statement = statement->next) {
      desugar_module(statement, arena);
    }
    break;
  case NODE_IF:
    desugar_module(node->as.if_stmt.then_branch, arena);
    desugar_module(node->as.if_stmt.else_branch, arena);
    break;
  case NODE_WHILE:
    desugar_module(node->as.while_stmt.body, arena);
    break;
  case NODE_ARRAY_ASSIGN: {
    const char *name = node->as.array_assign.name;
    Node *indices = node->as.array_assign.indices;
    Node *value = node->as.array_assign.value;
    Node *array = ast_node(arena, NODE_VAR, node->line, node->column);

    array->as.var = name;
    node->kind = NODE_ASSIGN;
    node->as.assign.name = name;
    node->as.assign.value = update(arena, node, array, indices, value);
    break;
  }
  default:
    break;
  }
}
