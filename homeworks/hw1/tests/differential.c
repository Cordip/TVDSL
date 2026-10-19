/* Differential test: the real lexer against the libc <regex.h> oracle.
 *
 *   ./build/differential [random_strings] [seed]
 *
 * Phase 1 checks every string of length 1 and 2 over bytes 1..255.
 * Phase 2 checks random strings glued from token fragments, so that
 * keyword/identifier, operator and comment boundaries are hit often. */

#include "oracle.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
static const char *fragments[] = {
    "function", "returns", "requires", "ensures", "uses", "while", "if", "else", "assert", "assume",
    "invariant", "length", "true", "false", "not", "and", "or", "forall", "exists", "int",

    "f", "fun", "i", "in", "x", "_", "a1",
    "0", "1", "9", "00", "10",

    "=", "==", "!", "!=", "<", "<=", ">", ">=", "-", "->", "=>", "+", "*", "/", "//",
    "(", ")", "[", "]", "{", "}", ",", ";", ":", "|",

    " ", "\t", "\r", "\n", "\r\n",
    "@", "#", "$", "~", "\x7F", "\x01",
    "\xD0\xB6", "\xC3\xA9", "\xFF",
};
// clang-format on

enum { FRAGMENT_COUNT = sizeof fragments / sizeof fragments[0] };

static uint64_t rng_state;

static uint64_t next_random(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;

  return rng_state;
}

static size_t mismatches;

static void check(const Automaton *automaton, const unsigned char *input,
                  size_t length) {
  char report[4096];

  if (oracle_compare(automaton, input, length, report, sizeof report)) {
    return;
  }

  if (mismatches < 10) {
    printf("MISMATCH %s\n", report);
  }

  ++mismatches;
}

static size_t check_short_strings(const Automaton *automaton) {
  unsigned char input[2];
  size_t checked = 0;

  for (int first = 1; first < 256; ++first) {
    input[0] = (unsigned char)first;
    check(automaton, input, 1);
    ++checked;

    for (int second = 1; second < 256; ++second) {
      input[1] = (unsigned char)second;
      check(automaton, input, 2);
      ++checked;
    }
  }

  return checked;
}

static void check_random_strings(const Automaton *automaton, size_t count) {
  unsigned char input[256];

  for (size_t i = 0; i < count; ++i) {
    size_t length = 0;
    size_t pieces = 1 + next_random() % 8;

    for (size_t piece = 0; piece < pieces; ++piece) {
      if (next_random() % 5 == 0) {
        input[length++] = (unsigned char)(1 + next_random() % 255);
        continue;
      }

      const char *fragment = fragments[next_random() % FRAGMENT_COUNT];
      size_t size = strlen(fragment);

      memcpy(input + length, fragment, size);
      length += size;
    }

    check(automaton, input, length);
  }
}

int main(int argc, char **argv) {
  size_t random_count = argc > 1 ? strtoul(argv[1], NULL, 10) : 20000;

  rng_state = argc > 2 ? strtoull(argv[2], NULL, 10) : 1;
  if (rng_state == 0) {
    rng_state = 1;
  }

  char error[256] = "";
  Automaton *automaton = automaton_build(error, sizeof error);

  if (!automaton) {
    fprintf(stderr, "automaton: %s\n", error);

    return 1;
  }

  if (!oracle_init(error, sizeof error)) {
    fprintf(stderr, "oracle: %s\n", error);

    return 1;
  }

  size_t exhaustive = check_short_strings(automaton);

  check_random_strings(automaton, random_count);

  printf("differential: %zu exhaustive strings (length 1-2) + %zu random "
         "strings, %zu mismatches\n",
         exhaustive, random_count, mismatches);

  oracle_free();

  automaton_free(automaton);

  return mismatches == 0 ? 0 : 1;
}
