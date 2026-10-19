/* libFuzzer target for lexer_scan (make fuzz). Any byte sequence must be
   scanned without crashes or sanitizer reports, tokens must stay inside the
   input in increasing order, and input without NUL must agree with the oracle.
 */

#include "oracle.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  size_t length, end;
} Bounds;

static Automaton *automaton;

static int check_token(const Token *token, void *context) {
  Bounds *bounds = context;

  if (token->length == 0 || token->offset < bounds->end ||
      token->offset + token->length > bounds->length) {
    abort();
  }

  bounds->end = token->offset + token->length;

  return 1;
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  (void)argc;
  (void)argv;

  char error[256] = "";

  automaton = automaton_build(error, sizeof error);
  if (!automaton || !oracle_init(error, sizeof error)) {
    fprintf(stderr, "fuzz setup: %s\n", error);
    abort();
  }

  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  Bounds bounds = {size, 0};
  LexError error;

  if (!lexer_scan(automaton, data, size, check_token, &bounds, &error) &&
      error.offset >= size) {
    abort();
  }

  if (oracle_accepts_input(data, size)) {
    char report[4096];

    if (!oracle_compare(automaton, data, size, report, sizeof report)) {
      fprintf(stderr, "MISMATCH %s\n", report);
      abort();
    }
  }

  return 0;
}
