/* Public API: runs the pipeline regex -> NFA -> DFA -> minimal DFA, and
   inspects, exports and verifies the result. */

#include "hw1.h"

#include "automata/minimize.h"
#include "lexer/automaton.h" // IWYU pragma: keep (defines struct Automaton)
#include "spec/regex.h"
#include "spec/rules.h"

#include <stdlib.h>

/* All rules share one NFA (state 0 is the common start) so that a single DFA
   tracks every token candidate at once; conflicts are resolved in dfa_add. */
static int make_nfa(Nfa *nfa, char *error, size_t error_size) {
  int start = nfa_state(nfa);

  for (int rule_index = 0; rule_index < rule_count; ++rule_index) {
    Fragment fragment;

    if (!regex_compile(nfa, rules[rule_index].regex, &fragment, error,
                       error_size)) {
      return 0;
    }

    nfa_epsilon(nfa, start, fragment.start);
    nfa->states[fragment.end].accept = rule_index;
  }

  return 1;
}

Automaton *automaton_build(char *error, size_t error_size) {
  Nfa nfa = {0};

  if (!make_nfa(&nfa, error, error_size)) {
    nfa_free(&nfa);

    return NULL;
  }

  Automaton *automaton = xcalloc(1, sizeof *automaton);
  automaton->nfa_count = nfa.count;
  automaton->original = determinize(&nfa);
  automaton->minimal = minimize(&automaton->original);

  nfa_free(&nfa);

  return automaton;
}

void automaton_free(Automaton *automaton) {
  if (automaton) {
    dfa_free(&automaton->original);
    dfa_free(&automaton->minimal);
    free(automaton);
  }
}

size_t automaton_nfa_states(const Automaton *automaton) {
  return automaton->nfa_count;
}

size_t automaton_dfa_states(const Automaton *automaton) {
  return automaton->original.count;
}

size_t automaton_min_states(const Automaton *automaton) {
  return automaton->minimal.count;
}

size_t automaton_trap(const Automaton *automaton) {
  return (size_t)automaton->minimal.trap;
}

int automaton_export_csv(const Automaton *automaton, FILE *output) {
  if (fprintf(output, "state,symbol,next,token,action,start,trap\n") < 0) {
    return 0;
  }

  for (size_t state_index = 0; state_index < automaton->minimal.count;
       ++state_index) {
    const DfaState *state = &automaton->minimal.states[state_index];
    int rule_index = state->accept;
    const char *action = "";

    if (rule_index >= 0) {
      switch (rules[rule_index].action) {
      case RULE_EMIT:
        action = "emit";
        break;
      case RULE_SKIP:
        action = "skip";
        break;
      case RULE_ERROR:
        action = "error";
        break;
      }
    }

    for (int symbol = 0; symbol < ALPHABET; ++symbol) {
      if (fprintf(output, "%zu,%d,%d,%s,%s,%d,%d\n", state_index, symbol,
                  state->next[symbol],
                  rule_index < 0 ? "" : rules[rule_index].name, action,
                  (int)state_index == automaton->minimal.start,
                  (int)state_index == automaton->minimal.trap) < 0) {
        return 0;
      }
    }
  }

  return !ferror(output);
}

/* Product-automaton BFS checks equivalence for all input strings, not a sample.
 */
int automaton_verify(const Automaton *automaton) {
  const Dfa *original = &automaton->original, *minimal = &automaton->minimal;
  size_t combinations = original->count * minimal->count;

  unsigned char *visited = xcalloc(combinations, 1);
  size_t *queue = xrealloc(NULL, combinations * sizeof *queue);
  size_t head = 0, tail = 0;

  size_t start = (size_t)original->start * minimal->count + minimal->start;

  visited[start] = 1;
  queue[tail++] = start;

  int equivalent = 1;

  while (head < tail && equivalent) {
    size_t state_pair = queue[head++];
    size_t original_state = state_pair / minimal->count,
           minimal_state = state_pair % minimal->count;

    if (original->states[original_state].accept !=
        minimal->states[minimal_state].accept) {
      equivalent = 0;
      break;
    }

    for (int symbol = 0; symbol < ALPHABET; ++symbol) {
      size_t next_pair = (size_t)original->states[original_state].next[symbol] *
                             minimal->count +
                         minimal->states[minimal_state].next[symbol];

      if (!visited[next_pair]) {
        visited[next_pair] = 1;
        queue[tail++] = next_pair;
      }
    }
  }

  free(visited);
  free(queue);

  return equivalent;
}
