#include "cases.h"
#include "hw1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Runs lexer scenarios and structural checks; exits with 1 on any failure. */

/* Rows per state in the exported table: 128 ASCII characters and one symbol
   that stands for every non-ASCII byte. Kept apart from the library constant.
 */
#define CSV_SYMBOLS 129

static size_t passed, failed;

static void report(const char *name, int success, const char *detail) {
  if (success) {
    ++passed;
  } else {
    ++failed;
  }

  printf("%s  %s", success ? "PASS" : "FAIL", name);
  if (!success && detail && *detail) {
    printf("\n      %s", detail);
  }

  putchar('\n');
}

typedef struct {
  char data[8192];
  size_t used, count, line, column;
  int overflow;
} Capture;

static int collect(const Token *token, void *context) {
  Capture *capture = context;
  size_t room = sizeof capture->data - capture->used;
  int written = snprintf(capture->data + capture->used, room, "%s%s:%.*s",
                         capture->count ? " " : "", token->kind,
                         (int)token->length, (const char *)token->text);

  if (written < 0 || (size_t)written >= room) {
    capture->overflow = 1;

    return 0;
  }

  capture->used += (size_t)written;
  capture->count++;
  capture->line = token->line;
  capture->column = token->column;

  return 1;
}

/* Round-trips input through a temporary file. The caller frees *data, which
   stays NULL unless the read succeeded. */
static int read_back(const unsigned char *input, size_t length, size_t limit,
                     unsigned char **data, size_t *read_length) {
  FILE *file = tmpfile();
  int result = -1;

  if (file && fwrite(input, 1, length, file) == length) {
    rewind(file);
    result = lexer_read_input(file, limit, data, read_length);
  }

  if (file && fclose(file) != 0) {
    result = -1;
  }

  return result;
}

static void run_case(const Automaton *automaton, const char *name,
                     const unsigned char *input, size_t length,
                     const char *expected) {
  Capture capture = {0};
  LexError error = {0};
  char message[96];
  const char *actual;

  int scanned = lexer_scan(automaton, input, length, collect, &capture, &error);

  if (scanned) {
    actual = capture.count ? capture.data : "(none)";
  } else {
    snprintf(message, sizeof message, "ERROR %zu:%zu", error.line,
             error.column);
    actual = message;
  }

  int success = !capture.overflow && strcmp(actual, expected) == 0;

  report(name, success, NULL);
  if (!success) {
    printf("      expected: %s\n      actual:   %s%s\n", expected, actual,
           capture.overflow ? " (capture buffer overflow)" : "");
  }
}

/* Independent expectation for a one-byte input, derived from the language
   specification rather than from the automaton. */
static const char *single_byte_kind(int character) {
  static const struct {
    char character;
    const char *kind;
  } punctuation[] = {
      {'(', "LPAREN"}, {')', "RPAREN"}, {'[', "LBRACKET"}, {']', "RBRACKET"},
      {'{', "LBRACE"}, {'}', "RBRACE"}, {',', "COMMA"},    {';', "SEMICOLON"},
      {':', "COLON"},  {'|', "PIPE"},   {'+', "PLUS"},     {'-', "MINUS"},
      {'*', "STAR"},   {'/', "SLASH"},  {'<', "LT"},       {'>', "GT"},
      {'=', "EQ"},
  };

  if (character == ' ' || character == '\t' || character == '\r' ||
      character == '\n') {
    return "";
  }

  if (character >= '0' && character <= '9') {
    return "INT";
  }

  if ((character >= 'a' && character <= 'z') ||
      (character >= 'A' && character <= 'Z') || character == '_') {
    return "IDENT";
  }

  for (size_t index = 0; index < sizeof punctuation / sizeof punctuation[0];
       ++index) {
    if (punctuation[index].character == character) {
      return punctuation[index].kind;
    }
  }

  return NULL;
}

static void check_single_bytes(const Automaton *automaton) {
  char detail[256] = "";
  int success = 1;

  for (int character = 0; character < 256 && success; ++character) {
    unsigned char byte = (unsigned char)character;
    const char *kind = character < 128 ? single_byte_kind(character) : NULL;

    Capture capture = {0};
    LexError error;
    int scanned = lexer_scan(automaton, &byte, 1, collect, &capture, &error);

    if (!kind) {
      success = !scanned && error.offset == 0;
    } else if (!*kind) {
      success = scanned && capture.count == 0;
    } else {
      char expected[64];

      snprintf(expected, sizeof expected, "%s:%c", kind, character);
      success = scanned && strcmp(capture.data, expected) == 0;
    }

    if (!success) {
      snprintf(detail, sizeof detail, "byte 0x%02X: expected %s, got %.64s",
               character, kind ? (*kind ? kind : "skip") : "trap",
               scanned ? (capture.count ? capture.data : "skip") : "error");
    }
  }

  report("all_256_single_bytes", success, detail);
}

static void check_positions(const Automaton *automaton) {
  Capture capture = {0};
  LexError error;
  int success = lexer_scan(automaton, (const unsigned char *)" \r\n  y", 6,
                           collect, &capture, &error) &&
                capture.count == 1 && capture.line == 2 && capture.column == 3;

  report("token_position_after_crlf", success, "expected IDENT y at 2:3");
}

static void check_csv(const Automaton *automaton) {
  FILE *csv = tmpfile();

  if (!csv || !automaton_export_csv(automaton, csv)) {
    report("csv_export", 0, "export failed");
    if (csv) {
      fclose(csv);
    }

    return;
  }

  rewind(csv);

  char line[256], detail[256] = "";
  int success =
      fgets(line, sizeof line, csv) &&
      strcmp(line, "state,symbol,next,token,action,start,trap\n") == 0;

  if (!success) {
    snprintf(detail, sizeof detail, "bad header");
  }

  size_t rows = 0, trap_rows = 0, start_rows = 0,
         states = automaton_min_states(automaton);
  size_t emit_rows = 0, skip_rows = 0, error_rows = 0;
  int actions_valid = 1;

  while (success && fgets(line, sizeof line, csv)) {
    size_t state;
    int symbol, next;

    if (sscanf(line, "%zu,%d,%d,", &state, &symbol, &next) != 3 || symbol < 0 ||
        symbol >= CSV_SYMBOLS || next < 0 || (size_t)next >= states) {
      success = 0;
      snprintf(detail, sizeof detail, "bad row: %.200s", line);
      break;
    }

    if (state == automaton_trap(automaton)) {
      if ((size_t)next != state || !strstr(line, ",,,0,1\n")) {
        success = 0;
        snprintf(detail, sizeof detail, "trap is not a sink: %.200s", line);
      }

      ++trap_rows;
    }

    if (strstr(line, ",1,0\n")) {
      ++start_rows;
    }

    if (strstr(line, ",INVALID_INT,")) {
      actions_valid &= strstr(line, ",INVALID_INT,error,") != NULL;
      ++error_rows;
    } else if (strstr(line, ",WS,") || strstr(line, ",COMMENT,")) {
      actions_valid &= strstr(line, ",skip,") != NULL;
      ++skip_rows;
    } else if (strstr(line, ",emit,")) {
      ++emit_rows;
    } else {
      actions_valid &= strstr(line, ",,,") != NULL;
    }

    ++rows;
  }

  fclose(csv);
  report("csv_export", success, detail);

  snprintf(detail, sizeof detail,
           "rows=%zu (want %zu), trap rows=%zu, start rows=%zu (want %d)", rows,
           states * CSV_SYMBOLS, trap_rows, start_rows, CSV_SYMBOLS);
  report("csv_complete_table",
         success && rows == states * CSV_SYMBOLS && trap_rows == CSV_SYMBOLS &&
             start_rows == CSV_SYMBOLS,
         detail);
  report("csv_rule_actions",
         success && actions_valid && emit_rows && skip_rows && error_rows,
         "CSV must preserve emit, skip and error actions");
}

static void check_read_input(const Automaton *automaton) {
  /* Both are longer than one read block; the IDENT also fits the capture. */
  enum { IDENT_LENGTH = 5000, COMMENT_LENGTH = 20000 };
  static unsigned char input[IDENT_LENGTH + 1 + COMMENT_LENGTH];
  unsigned char *data = NULL;
  size_t length = 0;
  Capture capture = {0};
  LexError error;

  memset(input, 'a', IDENT_LENGTH);
  input[IDENT_LENGTH] = '\n';
  memset(input + IDENT_LENGTH + 1, 'x', COMMENT_LENGTH);
  input[IDENT_LENGTH + 1] = input[IDENT_LENGTH + 2] = '/';

  int success =
      read_back(input, sizeof input, HW1_MAX_INPUT_SIZE, &data, &length) == 1 &&
      length == sizeof input && memcmp(data, input, length) == 0 &&
      lexer_scan(automaton, data, length, collect, &capture, &error) == 1 &&
      capture.count == 1 && capture.used == strlen("IDENT:") + IDENT_LENGTH;

  free(data);
  report("read_input_long_token_and_comment", success,
         "5000-byte IDENT and 20000-byte comment must lex");

  data = NULL;
  success = read_back(input, 0, HW1_MAX_INPUT_SIZE, &data, &length) == 1 &&
            length == 0 &&
            lexer_scan(automaton, data, length, NULL, NULL, &error) == 1;
  free(data);
  report("read_input_empty_file", success,
         "an empty file must read and scan with no tokens");

  data = NULL;
  success = read_back(input, sizeof input, sizeof input, &data, &length) == 1 &&
            length == sizeof input;
  free(data);

  data = NULL;
  success &=
      read_back(input, sizeof input, sizeof input - 1, &data, &length) == 0 &&
      !data;
  free(data);
  report("read_input_size_limit", success,
         "accept exactly limit bytes, refuse limit + 1");
}

int main(void) {
  char error[256];
  Automaton *automaton = automaton_build(error, sizeof error);

  if (!automaton) {
    fprintf(stderr, "%s\n", error);

    return 1;
  }

  puts("== Automaton ==");
  report("minimal_dfa_equivalent_to_dfa", automaton_verify(automaton),
         "language mismatch");
  check_csv(automaton);

  puts("\n== Lexer cases ==");

  for (size_t index = 0; index < sizeof cases / sizeof cases[0]; ++index) {
    run_case(automaton, cases[index].name,
             (const unsigned char *)cases[index].input,
             strlen(cases[index].input), cases[index].expected);
  }

  puts("\n== Built-in checks ==");
  check_positions(automaton);
  check_single_bytes(automaton);
  check_read_input(automaton);

  automaton_free(automaton);
  printf("\n%zu passed, %zu failed\n", passed, failed);

  return failed != 0;
}
