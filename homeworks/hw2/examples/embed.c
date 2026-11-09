/* A consumer of the public parser API; reads Funny from standard input. */
#include "hw2.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
  unsigned char *input = NULL;
  size_t length = 0;
  int loaded = lexer_read_input(stdin, HW1_MAX_INPUT_SIZE, &input, &length);

  if (loaded != 1) {
    fputs(loaded == 0 ? "api-example: input too large\n"
                      : "api-example: cannot read standard input\n",
          stderr);
    return 1;
  }

  char error[256] = {0};
  Automaton *automaton = automaton_build(error, sizeof error);

  if (!automaton) {
    fprintf(stderr, "api-example: cannot build lexer: %s\n", error);
    free(input);
    return 1;
  }

  Arena arena = {0};
  Node *module = parse_source(automaton, "<stdin>", input, length, &arena);
  free(input);
  int status = module ? 0 : 1;

  if (module) {
    desugar_module(module, &arena);
    /* A T0 semantic pass can consume module here, while arena is alive. */
    for (const Node *item = module->as.module.items; item; item = item->next) {
      if (item->kind != NODE_FUNCTION) {
        continue;
      }
      printf("Function %s\n", item->as.function.name);
      for (const Node *local = item->as.function.locals; local;
           local = local->next) {
        const char *types[] = {"unspecified", "int", "int[]"};
        printf("  Local %s: %s\n", local->as.var_def.name,
               types[local->as.var_def.type]);
      }
    }
    ast_print(stdout, module, AST_TREE);
  }

  if (fflush(stdout) == EOF || ferror(stdout)) {
    fputs("api-example: cannot write standard output\n", stderr);
    status = 1;
  }
  arena_free(&arena);
  automaton_free(automaton);
  return status;
}
