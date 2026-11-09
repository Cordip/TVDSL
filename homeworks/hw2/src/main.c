#include "hw2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program) {
  fprintf(stderr, "Usage: %s [--raw] [--dot] FILE\n", program);
}

/* Returns 1 with the whole file in *input, which the caller frees. */
static int read_file(const char *path, unsigned char **input, size_t *length) {
  FILE *file = fopen(path, "rb");

  if (!file) {
    fprintf(stderr, "hw2: error: cannot read %s\n", path);

    return 0;
  }

  int loaded = lexer_read_input(file, HW1_MAX_INPUT_SIZE, input, length);

  if (fclose(file) != 0 && loaded > 0) {
    free(*input);
    loaded = -1;
  }

  if (loaded == 0) {
    fprintf(stderr,
            "hw2: error: input larger than %zu bytes is unsupported: %s\n",
            HW1_MAX_INPUT_SIZE, path);
  } else if (loaded < 0) {
    fprintf(stderr, "hw2: error: cannot read %s\n", path);
  }

  return loaded > 0;
}

int main(int argc, char **argv) {
  const char *path = NULL;
  int raw = 0;
  AstFormat format = AST_TREE;

  for (int arg_index = 1; arg_index < argc; ++arg_index) {
    const char *arg = argv[arg_index];

    if (strcmp(arg, "--raw") == 0) {
      raw = 1;
    } else if (strcmp(arg, "--dot") == 0) {
      format = AST_DOT;
    } else if (arg[0] != '-' && !path) {
      path = arg;
    } else {
      usage(argv[0]);

      return 2;
    }
  }

  if (!path) {
    usage(argv[0]);

    return 2;
  }

  unsigned char *input = NULL;
  size_t length = 0;

  if (!read_file(path, &input, &length)) {
    return 1;
  }

  char error[256] = {0};
  Automaton *automaton = automaton_build(error, sizeof error);

  if (!automaton) {
    fprintf(stderr, "Build error: %s\n", error);
    free(input);

    return 1;
  }

  Arena arena = {0};
  Node *module = parse_source(automaton, path, input, length, &arena);
  int status = module ? 0 : 1;

  if (module) {
    if (!raw) {
      desugar_module(module, &arena);
    }

    ast_print(stdout, module, format);
  }

  if (fflush(stdout) == EOF || ferror(stdout)) {
    fputs("Cannot write standard output\n", stderr);
    status = 1;
  }

  arena_free(&arena);
  automaton_free(automaton);
  free(input);

  return status;
}
