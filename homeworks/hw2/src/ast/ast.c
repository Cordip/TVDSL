/* Bump allocator for the AST: nodes are never freed one by one. */
#include "hw2.h"

#include "util/common.h"

#include <stdlib.h>

enum { BLOCK_UNITS = 4096 };

struct ArenaBlock {
  struct ArenaBlock *next;
  size_t used, size; /* in units of max_align_t */
  max_align_t data[];
};

void *arena_alloc(Arena *arena, size_t size) {
  size_t units = (size + sizeof(max_align_t) - 1) / sizeof(max_align_t);
  struct ArenaBlock *block = arena->blocks;

  if (!block || block->size - block->used < units) {
    size_t capacity = units > BLOCK_UNITS ? units : BLOCK_UNITS;

    block = xcalloc(1, sizeof *block + capacity * sizeof(max_align_t));
    block->size = capacity;
    block->next = arena->blocks;
    arena->blocks = block;
  }

  void *result = block->data + block->used;

  block->used += units;

  return result;
}

void arena_free(Arena *arena) {
  while (arena->blocks) {
    struct ArenaBlock *next = arena->blocks->next;

    free(arena->blocks);
    arena->blocks = next;
  }
}

Node *ast_node(Arena *arena, NodeKind kind, size_t line, size_t column) {
  Node *node = arena_alloc(arena, sizeof *node);

  node->kind = kind;
  node->line = line;
  node->column = column;

  return node;
}
