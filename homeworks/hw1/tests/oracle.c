#include "oracle.h"

#include "spec/rules.h"

#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 4096

typedef struct {
  const char *kind;
  size_t offset, length;
} Lexeme;

typedef struct {
  Lexeme tokens[MAX_TOKENS];
  size_t count;
  int success;
  size_t error_offset;
} Scan;

static regex_t *compiled;

/* Our escapes \t \r \n become the raw characters, because POSIX bracket
   expressions do not interpret backslashes. Other escapes outside a class
   stay as they are: in ERE "\(" is a literal parenthesis too. */
static int to_ere(const char *regex, char *output, size_t capacity) {
  size_t written = 0;
  int in_class = 0;

  for (const char *cursor = regex; *cursor; ++cursor) {
    char character = *cursor;
    char literal = 0;

    if (character == '\\') {
      char next = *++cursor;

      if (next == 't') {
        literal = '\t';
      } else if (next == 'r') {
        literal = '\r';
      } else if (next == 'n') {
        literal = '\n';
      } else if (in_class) {
        literal = next;
      } else if (written + 2 < capacity) {
        output[written++] = '\\';
        output[written++] = next;
        continue;
      } else {
        return 0;
      }
    } else if (character == '[') {
      in_class = 1;
    } else if (character == ']') {
      in_class = 0;
    }

    if (written + 1 >= capacity) {
      return 0;
    }

    output[written++] = literal ? literal : character;
  }

  output[written] = '\0';

  return 1;
}

int oracle_init(char *error, size_t error_size) {
  compiled = calloc((size_t)rule_count, sizeof *compiled);
  if (!compiled) {
    snprintf(error, error_size, "out of memory");

    return 0;
  }

  for (int rule_index = 0; rule_index < rule_count; ++rule_index) {
    char expression[256], anchored[300];

    if (!to_ere(rules[rule_index].regex, expression, sizeof expression)) {
      snprintf(error, error_size, "cannot translate rule %s",
               rules[rule_index].name);

      return 0;
    }

    snprintf(anchored, sizeof anchored, "^(%s)", expression);

    int status = regcomp(&compiled[rule_index], anchored, REG_EXTENDED);

    if (status != 0) {
      char message[128];

      regerror(status, &compiled[rule_index], message, sizeof message);
      snprintf(error, error_size, "regcomp %s: %s", rules[rule_index].name,
               message);

      return 0;
    }
  }

  return 1;
}

void oracle_free(void) {
  if (!compiled) {
    return;
  }

  for (int rule_index = 0; rule_index < rule_count; ++rule_index) {
    regfree(&compiled[rule_index]);
  }

  free(compiled);
  compiled = NULL;
}

int oracle_accepts_input(const unsigned char *input, size_t length) {
  for (size_t byte_index = 0; byte_index < length; ++byte_index) {
    if (input[byte_index] == 0) {
      return 0;
    }
  }

  return 1;
}

static void add_token(Scan *scan, const char *kind, size_t offset,
                      size_t length) {
  if (scan->count < MAX_TOKENS) {
    scan->tokens[scan->count++] = (Lexeme){kind, offset, length};
  }
}

/* POSIX regexec returns the leftmost-longest match, so for each rule the match
   at the current offset is that rule's longest lexeme. The longest over all
   rules wins; on a tie the rule listed first wins. */
static void oracle_scan(const char *text, size_t length, Scan *scan) {
  size_t offset = 0;

  scan->success = 1;

  while (offset < length) {
    int best_rule = -1;
    size_t best_length = 0;

    for (int rule_index = 0; rule_index < rule_count; ++rule_index) {
      regmatch_t match;

      if (regexec(&compiled[rule_index], text + offset, 1, &match, 0) != 0) {
        continue;
      }

      size_t match_length = (size_t)(match.rm_eo - match.rm_so);

      if (match_length > best_length) {
        best_rule = rule_index;
        best_length = match_length;
      }
    }

    if (best_rule < 0 || rules[best_rule].action == RULE_ERROR) {
      scan->success = 0;
      scan->error_offset = offset;

      return;
    }

    if (rules[best_rule].action == RULE_EMIT) {
      add_token(scan, rules[best_rule].name, offset, best_length);
    }

    offset += best_length;
  }
}

static int collect(const Token *token, void *context) {
  add_token(context, token->kind, token->offset, token->length);

  return 1;
}

static void describe(const unsigned char *input, size_t length, char *output,
                     size_t size) {
  size_t written = 0;

  for (size_t byte_index = 0; byte_index < length && written + 5 < size;
       ++byte_index) {
    unsigned char byte = input[byte_index];

    if (byte >= 32 && byte < 127 && byte != '"' && byte != '\\') {
      output[written++] = (char)byte;
    } else {
      written +=
          (size_t)snprintf(output + written, size - written, "\\x%02X", byte);
    }
  }

  output[written] = '\0';
}

static void describe_scan(const Scan *scan, char *output, size_t size) {
  size_t written = 0;

  for (size_t token_index = 0; token_index < scan->count && written + 64 < size;
       ++token_index) {
    const Lexeme *token = &scan->tokens[token_index];

    written += (size_t)snprintf(output + written, size - written, "%s@%zu+%zu ",
                                token->kind, token->offset, token->length);
  }

  if (!scan->success) {
    snprintf(output + written, size - written, "ERROR@%zu", scan->error_offset);
  } else if (written == 0) {
    snprintf(output, size, "(none)");
  }
}

static int same_scan(const Scan *expected, const Scan *actual) {
  if (expected->success != actual->success ||
      expected->count != actual->count) {
    return 0;
  }

  if (!expected->success && expected->error_offset != actual->error_offset) {
    return 0;
  }

  for (size_t token_index = 0; token_index < expected->count; ++token_index) {
    const Lexeme *expected_token = &expected->tokens[token_index],
                 *actual_token = &actual->tokens[token_index];

    if (strcmp(expected_token->kind, actual_token->kind) != 0 ||
        expected_token->offset != actual_token->offset ||
        expected_token->length != actual_token->length) {
      return 0;
    }
  }

  return 1;
}

int oracle_compare(const Automaton *automaton, const unsigned char *input,
                   size_t length, char *report, size_t report_size) {
  static Scan expected, actual;

  memset(&expected, 0, sizeof expected);
  memset(&actual, 0, sizeof actual);

  char *text = malloc(length + 1);

  if (!text) {
    snprintf(report, report_size, "out of memory");

    return 0;
  }

  memcpy(text, input, length);
  text[length] = '\0';

  oracle_scan(text, length, &expected);

  free(text);

  LexError error;

  actual.success =
      lexer_scan(automaton, input, length, collect, &actual, &error);
  if (!actual.success) {
    actual.error_offset = error.offset;
  }

  if (same_scan(&expected, &actual)) {
    return 1;
  }

  char shown[512], want[1024], got[1024];

  describe(input, length, shown, sizeof shown);
  describe_scan(&expected, want, sizeof want);
  describe_scan(&actual, got, sizeof got);
  snprintf(report, report_size, "input:  \"%s\"\n  oracle: %s\n  lexer:  %s",
           shown, want, got);

  return 0;
}
