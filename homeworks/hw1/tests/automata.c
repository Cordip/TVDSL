/* Regex unit tests, a CSV export content check and an independent
   minimization oracle: table filling, Hopcroft/Motwani/Ullman (2008),
   sections 4.4.1 and 4.4.3.
   Different token labels, not just final/nonfinal, distinguish states. */
#include "automata/minimize.h"
#include "hw1.h"
#include "lexer/automaton.h" // IWYU pragma: keep (defines struct Automaton)
#include "spec/regex.h"
#include "spec/rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t passed, failed;

static void report(const char *name, int ok) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
  if (ok) {
    ++passed;
  } else {
    ++failed;
  }
}

/* Mark pairs with different outputs, then propagate distinguishability
   backwards until a fixed point. No partition/worklist code is shared. */
static size_t table_classes(const Dfa *dfa) {
  size_t state_count = dfa->count;
  unsigned char *different = xcalloc(state_count * state_count, 1);

  for (size_t first = 0; first < state_count; ++first) {
    for (size_t second = 0; second < state_count; ++second) {
      different[first * state_count + second] =
          dfa->states[first].accept != dfa->states[second].accept;
    }
  }

  int changed;

  do {
    changed = 0;

    for (size_t first = 0; first < state_count; ++first) {
      for (size_t second = first + 1; second < state_count; ++second) {
        if (different[first * state_count + second]) {
          continue;
        }

        for (int character = 0; character < ALPHABET; ++character) {
          size_t first_next = (size_t)dfa->states[first].next[character];
          size_t second_next = (size_t)dfa->states[second].next[character];

          if (different[first_next * state_count + second_next]) {
            different[first * state_count + second] =
                different[second * state_count + first] = 1;
            changed = 1;
            break;
          }
        }
      }
    }
  } while (changed);

  size_t classes = 0;

  for (size_t first = 0; first < state_count; ++first) {
    size_t second;

    for (second = 0; second < first; ++second) {
      if (!different[first * state_count + second]) {
        break;
      }
    }

    if (second == first) {
      ++classes;
    }
  }

  free(different);

  return classes;
}

static int check_minimum(const Dfa *source, const Dfa *minimal) {
  /* Borrow the DFA storage for the existing read-only equivalence check. */
  Automaton pair = {.original = *source, .minimal = *minimal};

  int ok = automaton_verify(&pair) && table_classes(source) == minimal->count;

  if (ok && source->count == minimal->count) {
    printf("INFO  DFA already minimal: size unchanged (%zu states)\n",
           source->count);
  }

  return ok;
}

static int compile(const char *regex, Dfa *out) {
  Nfa nfa = {0};
  int start = nfa_state(&nfa);
  Fragment fragment;
  char error[256] = "";
  int ok = regex_compile(&nfa, regex, &fragment, error, sizeof error);

  if (ok) {
    nfa_epsilon(&nfa, start, fragment.start);
    nfa.states[fragment.end].accept = 0;
    *out = determinize(&nfa);
  } else {
    fprintf(stderr, "regex %s: %s\n", regex, error);
  }

  nfa_free(&nfa);

  return ok;
}

static int accepts(const Dfa *dfa, const unsigned char *input, size_t length) {
  int state = dfa->start;

  for (size_t i = 0; i < length; ++i) {
    state = dfa->states[state].next[symbol_of(input[i])];
  }

  return dfa->states[state].accept >= 0;
}

static void check_regexes(void) {
  static const struct {
    const char *regex;
    const char *yes[7], *no[7];
  } cases[] = {
      {"abc", {"abc", NULL}, {"", "ab", "abcd", "xabc", NULL}},
      {"a|bc", {"a", "bc", NULL}, {"", "ac", "b", "abc", NULL}},
      {"(a|b)c", {"ac", "bc", NULL}, {"a", "b", "abc", NULL}},
      {"ab*", {"a", "ab", "abbb", NULL}, {"", "b", "aba", NULL}},
      {"(ab)+", {"ab", "abab", NULL}, {"", "a", "abb", NULL}},
      {"ab?c", {"ac", "abc", NULL}, {"abbc", "ab", "", NULL}},
      {"(ab|c)*d?",
       {"", "d", "ab", "c", "abcabd", NULL},
       {"a", "dd", "dc", NULL}},
      {"((a|b)+c)?", {"", "ac", "bbac", NULL}, {"c", "a", "acac", NULL}},
      {"(a?)*", {"", "a", "aaaa", NULL}, {"b", "ab", NULL}},
      {"[a-cX-Z_]+",
       {"a", "c", "X", "Z", "_", "abXYZ_", NULL},
       {"", "d", "W", "[", NULL}},
      {"[-a]+", {"-", "a-", NULL}, {"b", "", NULL}},
      {"[a-]+", {"-", "a-", NULL}, {"b", "", NULL}},
      {"[^a-c]+", {"d", "XYZ", "\n", NULL}, {"", "a", "c", "da", NULL}},
      {"\\(a\\|b\\)\\*\\+\\?", {"(a|b)*+?", NULL}, {"a", "(a|b)", NULL}},
      {"[\\]\\\\]+", {"]", "\\", "]\\]", NULL}, {"[", "a", "", NULL}},
  };

  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
    Dfa original = {0};
    int ok = compile(cases[i].regex, &original);

    if (ok) {
      Dfa minimal = minimize(&original);

      ok = check_minimum(&original, &minimal);

      for (size_t j = 0; cases[i].yes[j]; ++j) {
        ok &= accepts(&minimal, (const unsigned char *)cases[i].yes[j],
                      strlen(cases[i].yes[j]));
      }

      for (size_t j = 0; cases[i].no[j]; ++j) {
        ok &= !accepts(&minimal, (const unsigned char *)cases[i].no[j],
                       strlen(cases[i].no[j]));
      }

      dfa_free(&minimal);
      dfa_free(&original);
    }

    report(cases[i].regex, ok);
  }

  Dfa bytes = {0};
  int ok = compile("[^a]", &bytes);

  if (ok) {
    for (int byte = 0; byte < 256; ++byte) {
      unsigned char input = (unsigned char)byte;

      ok &= accepts(&bytes, &input, 1) == (byte != 'a');
    }

    dfa_free(&bytes);
  }

  report("negated_class_all_bytes_including_NUL", ok);
}

static void check_invalid_regexes(void) {
  static const struct {
    const char *name;
    const char *patterns[6];
  } cases[] = {
      {"invalid_alternatives", {"", "|a", "a|", NULL}},
      {"invalid_groups", {"()", "(a", "a)", "a()", "()*", NULL}},
      {"invalid_quantifiers", {"*a", "a**", NULL}},
      {"invalid_classes", {"[]", "[^]", "[a", "[z-a]", "[a-", NULL}},
      {"unfinished_escapes", {"\\", "(a\\", "[\\", "[a-\\", NULL}},
      {"non_ascii_regex", {"\xFF", "\\\xFF", "[\xFF]", NULL}},
  };

  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
    int ok = 1;

    for (size_t j = 0; cases[i].patterns[j]; ++j) {
      /* Exact allocation exposes reads beyond the terminating NUL to ASan. */
      const char *pattern = cases[i].patterns[j];
      char *regex = xcalloc(strlen(pattern) + 1, 1);
      Nfa nfa = {0};
      Fragment fragment;
      char error[256] = "";

      strcpy(regex, pattern);

      if (regex_compile(&nfa, regex, &fragment, error, sizeof error) ||
          !error[0]) {
        fprintf(stderr, "%s: pattern %zu was not rejected\n", cases[i].name,
                j + 1);
        ok = 0;
      }

      nfa_free(&nfa);
      free(regex);
    }

    report(cases[i].name, ok);
  }
}

static void check_regressions(void) {
  int ok = 1;

  for (size_t size = 0; size <= 2; ++size) {
    Nfa nfa = {0};
    Fragment fragment;
    char error[2] = "";

    ok &= !regex_compile(&nfa, "", &fragment, size ? error : NULL, size);
    ok &= fragment.start == -1 && fragment.end == -1;
    nfa_free(&nfa);
  }

  report("short_error_buffer_keeps_failure", ok);

  Dfa original = {0};
  ok = compile("(a|[^a])*", &original);

  if (ok) {
    Dfa minimal = minimize(&original);

    ok = original.trap == -1 && minimal.trap == -1 && minimal.count == 1 &&
         check_minimum(&original, &minimal);
    dfa_free(&minimal);
    dfa_free(&original);
  }

  report("universal_regex_has_no_trap", ok);
}

static void check_funny_minimality(void) {
  char error[256] = "";
  Automaton *automaton = automaton_build(error, sizeof error);
  int ok =
      automaton && check_minimum(&automaton->original, &automaton->minimal);

  report("Funny_minimality_by_table_filling", ok);
  automaton_free(automaton);
}

/* Rows must come in state-major order, one per (state, symbol), and every
   field must match the in-memory minimal DFA, not just the table shape. */
static int csv_matches_minimal(const Dfa *minimal, FILE *csv) {
  static const char *const action_names[] = {
      [RULE_EMIT] = "emit", [RULE_SKIP] = "skip", [RULE_ERROR] = "error"};
  char line[256];
  size_t rows = 0;

  if (!fgets(line, sizeof line, csv) ||
      strcmp(line, "state,symbol,next,token,action,start,trap\n") != 0) {
    return 0;
  }

  while (fgets(line, sizeof line, csv)) {
    size_t state_index;
    int symbol, next, labels = -1;

    if (sscanf(line, "%zu,%d,%d,%n", &state_index, &symbol, &next, &labels) !=
            3 ||
        labels < 0 || state_index != rows / ALPHABET ||
        symbol != (int)(rows % ALPHABET) || state_index >= minimal->count) {
      return 0;
    }

    const DfaState *state = &minimal->states[state_index];
    int rule_index = state->accept;
    char expected[128];

    snprintf(expected, sizeof expected, "%s,%s,%d,%d\n",
             rule_index < 0 ? "" : rules[rule_index].name,
             rule_index < 0 ? "" : action_names[rules[rule_index].action],
             (int)state_index == minimal->start,
             (int)state_index == minimal->trap);

    if (next != state->next[symbol] || strcmp(line + labels, expected) != 0) {
      return 0;
    }

    ++rows;
  }

  return !ferror(csv) && rows == minimal->count * ALPHABET;
}

static void check_csv_content(void) {
  char error[256] = "";
  Automaton *automaton = automaton_build(error, sizeof error);
  FILE *csv = tmpfile();
  int ok = automaton && csv && automaton_export_csv(automaton, csv);

  if (ok) {
    rewind(csv);
    ok = csv_matches_minimal(&automaton->minimal, csv);
  }

  if (csv) {
    fclose(csv);
  }

  automaton_free(automaton);

  report("csv_content_matches_minimal_dfa", ok);
}

int main(void) {
  check_regexes();
  check_invalid_regexes();
  check_funny_minimality();
  check_csv_content();
  check_regressions();
  printf("\nAutomata: %zu passed, %zu failed\n", passed, failed);

  return failed != 0;
}
