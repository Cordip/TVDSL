/* Error recovery must terminate: parses every prefix of each file and every
   copy with one byte removed. A hang is caught by the caller's timeout, a
   crash by the sanitizers. Diagnostics go to stderr; redirect it. */
#include "hw2.h"

#include "util/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t runs;

static void parse(const Automaton *automaton, FILE *output, const char *path,
                  const unsigned char *input, size_t length) {
  Arena arena = {0};
  Node *module = parse_source(automaton, path, input, length, &arena);

  if (module) {
    ast_print(output, module, AST_TREE);
    ast_print(output, module, AST_DOT);
    desugar_module(module, &arena);
    ast_print(output, module, AST_TREE);
    ast_print(output, module, AST_DOT);
  }

  arena_free(&arena);
  ++runs;
}

int main(int argc, char **argv) {
  char error[256] = {0};
  Automaton *automaton = automaton_build(error, sizeof error);

  if (!automaton) {
    fprintf(stderr, "Build error: %s\n", error);

    return 1;
  }

  FILE *output = fopen("/dev/null", "w");

  if (!output) {
    automaton_free(automaton);
    return 1;
  }

  for (int arg_index = 1; arg_index < argc; ++arg_index) {
    FILE *file = fopen(argv[arg_index], "rb");
    unsigned char *input = NULL;
    size_t length = 0;

    if (!file ||
        lexer_read_input(file, HW1_MAX_INPUT_SIZE, &input, &length) < 1) {
      printf("FAIL  cannot read %s\n", argv[arg_index]);
      if (file) {
        fclose(file);
      }
      fclose(output);
      automaton_free(automaton);

      return 1;
    }

    fclose(file);

    unsigned char *copy = xcalloc(length + 1, 1);

    for (size_t cut = 0; cut <= length; ++cut) {
      parse(automaton, output, argv[arg_index], input, cut);
    }

    for (size_t removed = 0; removed < length; ++removed) {
      memcpy(copy, input, removed);
      memcpy(copy + removed, input + removed + 1, length - removed - 1);
      parse(automaton, output, argv[arg_index], copy, length - 1);
    }

    free(copy);
    free(input);
  }

  automaton_free(automaton);
  if (fclose(output) != 0) {
    return 1;
  }
  printf("PASS  %zu truncated or garbled inputs terminated\n", runs);

  return 0;
}
