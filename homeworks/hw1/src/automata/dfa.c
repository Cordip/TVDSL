#include "automata/dfa.h"

#include <stdlib.h>
#include <string.h>

static void bit_add(uint64_t *set, int state) {
  set[state / 64] |= UINT64_C(1) << (state % 64);
}

static int bit_has(const uint64_t *set, int state) {
  return (set[state / 64] >> (state % 64)) & 1;
}

static void closure(const Nfa *nfa, uint64_t *set, int *stack) {
  size_t top = 0;

  for (size_t i = 0; i < nfa->count; ++i) {
    if (bit_has(set, (int)i)) {
      stack[top++] = (int)i;
    }
  }

  while (top) {
    NfaState *state = &nfa->states[stack[--top]];

    for (size_t i = 0; i < state->count; ++i) {
      Edge *edge = &state->edges[i];

      if (edge->epsilon && !bit_has(set, edge->to)) {
        bit_add(set, edge->to);
        stack[top++] = edge->to;
      }
    }
  }
}

/* Returns the DFA state for this NFA-state set, creating it if new. Picking the
   lowest rule index is what makes "if" a keyword rather than an IDENT. */
static int dfa_add(Dfa *dfa, const uint64_t *set, const Nfa *nfa) {
  for (size_t i = 0; i < dfa->count; ++i) {
    if (memcmp(set, dfa->states[i].set, dfa->words * sizeof *set) == 0) {
      return (int)i;
    }
  }

  if (dfa->count == dfa->capacity) {
    dfa->capacity = dfa->capacity ? dfa->capacity * 2 : 64;
    dfa->states = xrealloc(dfa->states, dfa->capacity * sizeof *dfa->states);
  }

  int id = (int)dfa->count++;
  DfaState *state = &dfa->states[id];
  state->set = xrealloc(NULL, dfa->words * sizeof *set);
  memcpy(state->set, set, dfa->words * sizeof *set);

  state->accept = -1;

  for (size_t i = 0; i < nfa->count; ++i) {
    if (bit_has(set, (int)i) && nfa->states[i].accept >= 0 &&
        (state->accept < 0 || nfa->states[i].accept < state->accept)) {
      state->accept = nfa->states[i].accept;
    }
  }

  return id;
}

Dfa determinize(const Nfa *nfa) {
  Dfa dfa = {.trap = -1};
  dfa.words = (nfa->count + 63) / 64;

  uint64_t *set = xcalloc(dfa.words, sizeof *set);
  int *stack = xrealloc(NULL, nfa->count * sizeof *stack);

  bit_add(set, 0);
  closure(nfa, set,
          stack); /* NFA state 0 is the start, see make_nfa in automaton.c. */
  dfa.start = dfa_add(&dfa, set, nfa);

  /* The bound grows on purpose: dfa.states doubles as the worklist. */
  for (size_t dfa_index = 0; dfa_index < dfa.count; ++dfa_index) {
    for (int character = 0; character < ALPHABET; ++character) {
      memset(set, 0, dfa.words * sizeof *set);

      for (size_t nfa_index = 0; nfa_index < nfa->count; ++nfa_index) {
        if (!bit_has(dfa.states[dfa_index].set, (int)nfa_index)) {
          continue;
        }

        NfaState *state = &nfa->states[nfa_index];

        for (size_t edge_index = 0; edge_index < state->count; ++edge_index) {
          Edge *edge = &state->edges[edge_index];

          if (!edge->epsilon && has_char(edge->mask, character)) {
            bit_add(set, edge->to);
          }
        }
      }

      closure(nfa, set, stack);

      /* dfa_add can reallocate states, so index into the array afterwards. */
      int next = dfa_add(&dfa, set, nfa);

      dfa.states[dfa_index].next[character] = next;
    }
  }

  memset(set, 0, dfa.words * sizeof *set);

  for (size_t i = 0; i < dfa.count; ++i) {
    if (memcmp(dfa.states[i].set, set, dfa.words * sizeof *set) == 0) {
      dfa.trap = (int)i;
    }
  }

  free(stack);
  free(set);

  return dfa;
}

void dfa_free(Dfa *dfa) {
  for (size_t i = 0; i < dfa->count; ++i) {
    free(dfa->states[i].set);
  }

  free(dfa->states);
}
