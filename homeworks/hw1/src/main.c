#include "hw1.h"

#include "spec/rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int print_token(const Token *token, void *context) {
  (void)context;

  printf("%zu:%zu %s \"", token->line, token->column, token->kind);

  for (size_t byte_index = 0; byte_index < token->length; ++byte_index) {
    unsigned char byte = token->text[byte_index];

    if (byte == '\n') {
      fputs("\\n", stdout);
    } else if (byte == '\r') {
      fputs("\\r", stdout);
    } else if (byte == '\t') {
      fputs("\\t", stdout);
    } else if (byte == '"' || byte == '\\') {
      printf("\\%c", byte);
    } else if (byte < 32 || byte > 126) {
      printf("\\x%02X", byte);
    } else {
      putchar(byte);
    }
  }

  puts("\"");

  return !ferror(stdout);
}

/* Returns 0 when the minimal DFA is not equivalent to the subset-construction
 * DFA. */
static int print_report(const Automaton *automaton) {
  static const char *const action_names[] = {
      [RULE_EMIT] = "emit", [RULE_SKIP] = "skip", [RULE_ERROR] = "error"};

  puts("Rules (earlier wins on equal length):");

  for (int rule_index = 0; rule_index < rule_count; ++rule_index) {
    printf("  %-12s %-24s %s\n", rules[rule_index].name,
           rules[rule_index].regex, action_names[rules[rule_index].action]);
  }

  printf("NFA states: %zu\nDFA states: %zu\nMinimal DFA states: %zu\n",
         automaton_nfa_states(automaton), automaton_dfa_states(automaton),
         automaton_min_states(automaton));

  size_t trap = automaton_trap(automaton);

  if (trap == (size_t)-1) {
    puts("Trap state: none");
  } else {
    printf("Trap state: %zu (handles invalid transitions)\n", trap);
  }

  int equivalent = automaton_verify(automaton);

  printf("Minimal DFA equivalent to DFA: %s\n", equivalent ? "yes" : "no");

  return equivalent;
}

/* Returns the exit status of --scan. */
static int scan_file(const Automaton *automaton, const char *path) {
  FILE *file = fopen(path, "rb");

  if (!file) {
    fprintf(stderr, "Cannot read %s\n", path);

    return 1;
  }

  unsigned char *input = NULL;
  size_t length = 0;
  int loaded = lexer_read_input(file, HW1_MAX_INPUT_SIZE, &input, &length);
  int status = 0;

  if (fclose(file) != 0) {
    fprintf(stderr, "Cannot close %s\n", path);
    status = 1;
  }

  if (loaded == 0) {
    fprintf(stderr, "Input larger than %zu bytes is unsupported: %s\n",
            HW1_MAX_INPUT_SIZE, path);

    return 1;
  }

  if (loaded < 0) {
    fprintf(stderr, "Cannot read %s\n", path);

    return 1;
  }

  LexError lex_error;
  int scanned =
      lexer_scan(automaton, input, length, print_token, NULL, &lex_error);

  free(input);

  /* -1 means print_token failed; the final stdout check in main reports it. */
  if (scanned < 0) {
    return 1;
  }

  if (scanned == 0) {
    /* Tokens go out first so a pipe keeps their order; a failed
       flush leaves ferror(stdout) set for the final check. */
    if (fflush(stdout) == EOF) {
      status = 1;
    }

    fprintf(stderr, "Lexical error at %zu:%zu (offset %zu, byte 0x%02X)\n",
            lex_error.line, lex_error.column, lex_error.offset, lex_error.byte);
    status = 1;
  }

  return status;
}

static void usage(const char *program) {
  fprintf(stderr, "Usage: %s [--report | --export FILE | --scan FILE]\n",
          program);
}

int main(int argc, char **argv) {
  char error[256] = {0};
  Automaton *automaton = automaton_build(error, sizeof error);

  if (!automaton) {
    fprintf(stderr, "Build error: %s\n", error);

    return 1;
  }

  int status = 0;

  if (argc == 1 || (argc == 2 && strcmp(argv[1], "--report") == 0)) {
    if (!print_report(automaton)) {
      status = 1;
    }
  } else if (argc == 3 && strcmp(argv[1], "--export") == 0) {
    FILE *file = fopen(argv[2], "w");
    int written = file && automaton_export_csv(automaton, file);

    if (file && fclose(file) != 0) {
      written = 0;
    }

    if (!written) {
      fprintf(stderr, "Cannot write %s\n", argv[2]);
      status = 1;
    }
  } else if (argc == 3 && strcmp(argv[1], "--scan") == 0) {
    status = scan_file(automaton, argv[2]);
  } else {
    usage(argv[0]);
    status = 2;
  }

  if (fflush(stdout) == EOF || ferror(stdout)) {
    fputs("Cannot write standard output\n", stderr);
    status = 1;
  }

  automaton_free(automaton);

  return status;
}
