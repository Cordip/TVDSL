/* Tree and DOT views share the same field traversal and pointer registry. */
#include "hw2.h"

#include "util/common.h"

#include <stdint.h>
#include <stdlib.h>

static const char *const kind_names[] = {
    [NODE_MODULE] = "Module",
    [NODE_FUNCTION] = "Function",
    [NODE_FORMULA] = "Formula",
    [NODE_PARAM] = "Param",
    [NODE_LOCAL] = "Local",
    [NODE_ASSIGN] = "Assign",
    [NODE_ARRAY_ASSIGN] = "ArrayAssign",
    [NODE_TUPLE_ASSIGN] = "TupleAssign",
    [NODE_IF] = "If",
    [NODE_WHILE] = "While",
    [NODE_BLOCK] = "Block",
    [NODE_ASSERT] = "Assert",
    [NODE_ASSUME] = "Assume",
    [NODE_INT] = "Int",
    [NODE_BOOL] = "Bool",
    [NODE_VAR] = "Var",
    [NODE_INDEX] = "Index",
    [NODE_CALL] = "Call",
    [NODE_UNARY] = "Unary",
    [NODE_BINARY] = "Binary",
    [NODE_COMPARE] = "Compare",
    [NODE_NOT] = "Not",
    [NODE_LOGICAL] = "Logical",
    [NODE_QUANTIFIER] = "Quantifier",
    [NODE_ARRAY_UPDATE] = "ArrayUpdate",
};

static const char *const op_names[] = {
    [OP_NEG] = "-",         [OP_ADD] = "+",      [OP_SUB] = "-",
    [OP_MUL] = "*",         [OP_DIV] = "/",      [OP_EQ] = "==",
    [OP_NE] = "!=",         [OP_LT] = "<",       [OP_LE] = "<=",
    [OP_GT] = ">",          [OP_GE] = ">=",      [OP_AND] = "and",
    [OP_OR] = "or",         [OP_IMPLIES] = "->", [OP_FORALL] = "forall",
    [OP_EXISTS] = "exists",
};

static const char *const type_names[] = {
    [TYPE_NONE] = NULL, [TYPE_INT] = "int", [TYPE_INT_ARRAY] = "int[]"};

typedef struct {
  const Node *node;
  int printed;
} NodeRef;

typedef struct {
  size_t id, depth, index;
  const char *field;
  int list_item, last;
} Branch;

typedef struct {
  FILE *out;
  AstFormat format;
  NodeRef *nodes;
  size_t count, capacity, current;
  size_t *slots;
  size_t slot_count;
  int header_open;
  Branch *children;
  size_t child_count, child_capacity;
} Printer;

static size_t pointer_hash(const Node *node) {
  uintptr_t value = (uintptr_t)node;

  return (size_t)((value >> 3) * (uintptr_t)2654435761u);
}

static void grow_slots(Printer *p) {
  size_t count = p->slot_count ? p->slot_count * 2 : 128;
  size_t *slots = xcalloc(count, sizeof *slots);

  for (size_t id = 0; id < p->count; ++id) {
    size_t slot = pointer_hash(p->nodes[id].node) & (count - 1);

    while (slots[slot]) {
      slot = (slot + 1) & (count - 1);
    }
    slots[slot] = id + 1;
  }
  free(p->slots);
  p->slots = slots;
  p->slot_count = count;
}

static size_t register_node(Printer *p, const Node *node) {
  if (p->count + 1 > p->slot_count / 2) {
    grow_slots(p);
  }

  size_t slot = pointer_hash(node) & (p->slot_count - 1);

  while (p->slots[slot]) {
    size_t id = p->slots[slot] - 1;

    if (p->nodes[id].node == node) {
      return id;
    }
    slot = (slot + 1) & (p->slot_count - 1);
  }
  if (p->count == p->capacity) {
    p->capacity = p->capacity ? p->capacity * 2 : 128;
    p->nodes = xrealloc(p->nodes, p->capacity * sizeof *p->nodes);
  }

  size_t id = p->count++;

  p->nodes[id] = (NodeRef){.node = node};
  p->slots[slot] = id + 1;
  return id;
}

static void quoted(FILE *out, const char *text) {
  fputc('"', out);
  for (const unsigned char *byte = (const unsigned char *)text; *byte; ++byte) {
    switch (*byte) {
    case '"':
    case '\\':
      fputc('\\', out);
      fputc(*byte, out);
      break;
    case '\n':
      fputs("\\n", out);
      break;
    case '\r':
      fputs("\\r", out);
      break;
    case '\t':
      fputc('\t', out);
      break;
    default:
      fputc(*byte, out);
      break;
    }
  }
  fputc('"', out);
}

static void finish_header(Printer *p) {
  if (p->header_open) {
    if (p->format == AST_DOT) {
      fputs("];\n", p->out);
    } else {
      const Node *node = p->nodes[p->current].node;

      fprintf(p->out, " @%zu:%zu [#%zu]\n", node->line, node->column,
              p->current);
    }
    p->header_open = 0;
  }
}

static void begin(Printer *p, const Node *node) {
  const char *kind = kind_names[node->kind];

  if (p->format == AST_DOT) {
    fprintf(p->out,
            "  n%zu [kind=\"%s\", line=%zu, col=%zu, label=\"%s @%zu:%zu\"",
            p->current, kind, node->line, node->column, kind, node->line,
            node->column);
  } else {
    fputs(kind, p->out);
  }
  p->header_open = 1;
}

static void attr(Printer *p, const char *name, const char *value) {
  if (value) {
    fprintf(p->out, "%s%s=", p->format == AST_DOT ? ", " : " ", name);
    quoted(p->out, value);
  }
}

static void edge(Printer *p, const char *field, const Node *node, int list_item,
                 size_t index) {
  if (!node) {
    return;
  }
  finish_header(p);
  size_t id = register_node(p, node);

  if (p->format == AST_TREE) {
    if (p->child_count == p->child_capacity) {
      p->child_capacity = p->child_capacity ? p->child_capacity * 2 : 16;
      p->children =
          xrealloc(p->children, p->child_capacity * sizeof *p->children);
    }
    p->children[p->child_count++] = (Branch){
        .id = id, .field = field, .list_item = list_item, .index = index};
    return;
  }

  fprintf(p->out, "  n%zu -> n%zu [label=\"%s", p->current, id, field);
  if (list_item) {
    fprintf(p->out, "[%zu]", index);
  }
  fputs("\"];\n", p->out);
}

static void child(Printer *p, const char *field, const Node *node) {
  edge(p, field, node, 0, 0);
}

static void list(Printer *p, const char *field, const Node *item) {
  for (size_t index = 0; item; item = item->next, ++index) {
    edge(p, field, item, 1, index);
  }
}

static void print_node(Printer *p, const Node *node) {
  begin(p, node);

  switch (node->kind) {
  case NODE_MODULE:
    list(p, "items", node->as.module.items);
    break;
  case NODE_FUNCTION:
    attr(p, "name", node->as.function.name);
    list(p, "params", node->as.function.params);
    child(p, "requires", node->as.function.precondition);
    list(p, "returns", node->as.function.returns);
    child(p, "ensures", node->as.function.postcondition);
    list(p, "locals", node->as.function.locals);
    child(p, "body", node->as.function.body);
    break;
  case NODE_FORMULA:
    attr(p, "name", node->as.formula.name);
    list(p, "params", node->as.formula.params);
    child(p, "body", node->as.formula.body);
    break;
  case NODE_PARAM:
  case NODE_LOCAL:
    attr(p, "name", node->as.var_def.name);
    attr(p, "type", type_names[node->as.var_def.type]);
    break;
  case NODE_ASSIGN:
    attr(p, "name", node->as.assign.name);
    child(p, "value", node->as.assign.value);
    break;
  case NODE_ARRAY_ASSIGN:
    attr(p, "name", node->as.array_assign.name);
    list(p, "indices", node->as.array_assign.indices);
    child(p, "value", node->as.array_assign.value);
    break;
  case NODE_TUPLE_ASSIGN:
    list(p, "targets", node->as.tuple_assign.targets);
    child(p, "value", node->as.tuple_assign.call);
    break;
  case NODE_IF:
    child(p, "condition", node->as.if_stmt.condition);
    child(p, "then", node->as.if_stmt.then_branch);
    child(p, "else", node->as.if_stmt.else_branch);
    break;
  case NODE_WHILE:
    child(p, "condition", node->as.while_stmt.condition);
    child(p, "invariant", node->as.while_stmt.invariant);
    child(p, "body", node->as.while_stmt.body);
    break;
  case NODE_BLOCK:
    list(p, "statements", node->as.block.statements);
    break;
  case NODE_ASSERT:
  case NODE_ASSUME:
    child(p, "predicate", node->as.check.predicate);
    break;
  case NODE_INT:
    attr(p, "value", node->as.literal);
    break;
  case NODE_BOOL:
    attr(p, "value", node->as.boolean ? "true" : "false");
    break;
  case NODE_VAR:
    attr(p, "name", node->as.var);
    break;
  case NODE_INDEX:
    child(p, "array", node->as.index.array);
    child(p, "index", node->as.index.index);
    break;
  case NODE_CALL:
    attr(p, "name", node->as.call.name);
    list(p, "args", node->as.call.args);
    break;
  case NODE_UNARY:
    attr(p, "op", op_names[node->as.unary.op]);
    child(p, "operand", node->as.unary.operand);
    break;
  case NODE_NOT:
    child(p, "operand", node->as.unary.operand);
    break;
  case NODE_BINARY:
  case NODE_COMPARE:
  case NODE_LOGICAL:
    attr(p, "op", op_names[node->as.binary.op]);
    child(p, "left", node->as.binary.left);
    child(p, "right", node->as.binary.right);
    break;
  case NODE_QUANTIFIER:
    attr(p, "op", op_names[node->as.quantifier.op]);
    child(p, "var", node->as.quantifier.var);
    child(p, "body", node->as.quantifier.body);
    break;
  case NODE_ARRAY_UPDATE:
    child(p, "array", node->as.update.array);
    child(p, "index", node->as.update.index);
    child(p, "value", node->as.update.value);
    break;
  }

  finish_header(p);
}

static void tree_prefix(Printer *p, const Branch *branch,
                        const unsigned char *more) {
  if (!branch->depth) {
    return;
  }
  /* Keep deep graphs compact without omitting nodes or their actual depth. */
  for (size_t level = 1; level < branch->depth && level <= 12; ++level) {
    fputs(more[level] ? "│   " : "    ", p->out);
  }
  if (branch->depth > 12) {
    fprintf(p->out, "…[depth=%zu] ", branch->depth);
  }
  fputs(branch->last ? "└── " : "├── ", p->out);
  fputs(branch->field, p->out);
  if (branch->list_item) {
    fprintf(p->out, "[%zu]", branch->index);
  }
  fputs(": ", p->out);
}

static void print_tree(Printer *p) {
  size_t count = 1, capacity = 16, depth_capacity = 16;
  Branch *stack = xcalloc(capacity, sizeof *stack);
  unsigned char *more = xcalloc(depth_capacity, sizeof *more);

  while (count && !ferror(p->out)) {
    Branch branch = stack[--count];
    const Node *node = p->nodes[branch.id].node;

    if (branch.depth >= depth_capacity) {
      while (branch.depth >= depth_capacity) {
        depth_capacity *= 2;
      }
      more = xrealloc(more, depth_capacity * sizeof *more);
    }
    more[branch.depth] = !branch.last;
    tree_prefix(p, &branch, more);
    if (p->nodes[branch.id].printed) {
      fprintf(p->out, "↪ #%zu %s @%zu:%zu\n", branch.id, kind_names[node->kind],
              node->line, node->column);
      continue;
    }
    p->nodes[branch.id].printed = 1;
    p->current = branch.id;
    p->child_count = 0;
    print_node(p, node);

    if (count + p->child_count > capacity) {
      while (count + p->child_count > capacity) {
        capacity *= 2;
      }
      stack = xrealloc(stack, capacity * sizeof *stack);
    }
    for (size_t index = p->child_count; index > 0; --index) {
      Branch child = p->children[index - 1];

      child.depth = branch.depth + 1;
      child.last = index == p->child_count;
      stack[count++] = child;
    }
  }
  free(more);
  free(stack);
}

void ast_print(FILE *output, const Node *node, AstFormat format) {
  Printer printer = {.out = output, .format = format};

  if (node) {
    register_node(&printer, node);
  }
  if (format == AST_TREE) {
    if (node) {
      print_tree(&printer);
    }
  } else {
    fputs("digraph AST {\n", output);
    for (printer.current = 0; printer.current < printer.count;
         ++printer.current) {
      const Node *current = printer.nodes[printer.current].node;

      print_node(&printer, current);
      if (ferror(output)) {
        break;
      }
    }
    fputs("}\n", output);
  }
  free(printer.nodes);
  free(printer.slots);
  free(printer.children);
}
