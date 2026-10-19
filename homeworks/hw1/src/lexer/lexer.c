/* Maximal-munch scanner over the minimal DFA, and the whole-input reader that
   feeds it. The scanner works on one array, so a token may be any length. */
#include "hw1.h"

#include "lexer/automaton.h" // IWYU pragma: keep (defines struct Automaton)
#include "spec/rules.h"
#include "util/common.h"

#include <stdlib.h>

enum { READ_BLOCK = 4096 };

typedef struct {
  size_t offset, line, column;
} Position;

/* Doubles the buffer but never past ceiling, so an oversized input costs at
   most limit + 1 bytes before it is refused. */
static size_t grow_capacity(size_t capacity, size_t ceiling) {
  if (capacity == 0) {
    return READ_BLOCK < ceiling ? READ_BLOCK : ceiling;
  }

  return capacity <= ceiling / 2 ? capacity * 2 : ceiling;
}

int lexer_read_input(FILE *input, size_t limit, unsigned char **data,
                     size_t *length) {
  size_t ceiling = limit + 1;
  unsigned char *buffer = NULL;
  size_t used = 0, capacity = 0;

  for (;;) {
    if (used == capacity) {
      capacity = grow_capacity(capacity, ceiling);
      buffer = xrealloc(buffer, capacity);
    }

    size_t requested = capacity - used;
    size_t count = fread(buffer + used, 1, requested, input);

    used += count;

    if (used > limit) {
      free(buffer);

      return 0;
    }

    if (count < requested) {
      break;
    }
  }

  if (ferror(input)) {
    free(buffer);

    return -1;
  }

  *data = buffer;
  *length = used;

  return 1;
}

static void advance(Position *position, const unsigned char *text,
                    size_t length) {
  position->offset += length;

  for (size_t byte_index = 0; byte_index < length; ++byte_index) {
    if (text[byte_index] == '\n') {
      ++position->line;
      position->column = 1;
    } else {
      ++position->column;
    }
  }
}

static int reject(LexError *error, Position position, unsigned char byte) {
  if (error) {
    *error = (LexError){position.offset, position.line, position.column, byte};
  }

  return 0;
}

int lexer_scan(const Automaton *automaton, const unsigned char *input,
               size_t length, TokenSink sink, void *context, LexError *error) {
  const Dfa *dfa = &automaton->minimal;
  Position position = {0, 1, 1};

  while (position.offset < length) {
    const unsigned char *text = input + position.offset;
    size_t remaining = length - position.offset;
    int state = dfa->start, rule_index = -1;
    size_t scanned_length = 0, matched_length = 0;

    /* Run until the trap, remembering the last accepting prefix: the
       longest match wins, and the bytes after it are scanned again. */
    while (scanned_length < remaining) {
      int next = dfa->states[state].next[symbol_of(text[scanned_length])];

      if (next == dfa->trap) {
        break;
      }

      state = next;
      ++scanned_length;

      if (dfa->states[state].accept >= 0) {
        rule_index = dfa->states[state].accept;
        matched_length = scanned_length;
      }
    }

    if (rule_index < 0 || rules[rule_index].action == RULE_ERROR) {
      return reject(error, position, text[0]);
    }

    if (rules[rule_index].action == RULE_EMIT && sink) {
      Token token = {rules[rule_index].name, text,          matched_length,
                     position.offset,        position.line, position.column};

      if (!sink(&token, context)) {
        return -1;
      }
    }

    advance(&position, text, matched_length);
  }

  return 1;
}
