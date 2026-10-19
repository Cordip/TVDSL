#include "automata/nfa.h"

#include "util/common.h"

#include <stdlib.h>
#include <string.h>

int nfa_state(Nfa *nfa) {
  if (nfa->count == nfa->capacity) {
    nfa->capacity = nfa->capacity ? nfa->capacity * 2 : 64;
    nfa->states = xrealloc(nfa->states, nfa->capacity * sizeof *nfa->states);
  }

  int id = (int)nfa->count++;

  nfa->states[id] = (NfaState){.accept = -1};

  return id;
}

static void nfa_edge(Nfa *nfa, int from, int to, int epsilon,
                     const uint64_t mask[MASK_WORDS]) {
  NfaState *state = &nfa->states[from];

  if (state->count == state->capacity) {
    state->capacity = state->capacity ? state->capacity * 2 : 4;
    state->edges =
        xrealloc(state->edges, state->capacity * sizeof *state->edges);
  }

  Edge edge = {.to = to, .epsilon = epsilon, .mask = {0}};

  if (mask) {
    memcpy(edge.mask, mask, sizeof edge.mask);
  }

  state->edges[state->count++] = edge;
}

void nfa_epsilon(Nfa *nfa, int from, int to) {
  nfa_edge(nfa, from, to, 1, NULL);
}

Fragment nfa_atom(Nfa *nfa, const uint64_t mask[MASK_WORDS]) {
  Fragment fragment = {nfa_state(nfa), nfa_state(nfa)};

  nfa_edge(nfa, fragment.start, fragment.end, 0, mask);

  return fragment;
}

Fragment nfa_concat(Nfa *nfa, Fragment left, Fragment right) {
  nfa_epsilon(nfa, left.end, right.start);

  return (Fragment){left.start, right.end};
}

Fragment nfa_alternate(Nfa *nfa, Fragment left, Fragment right) {
  Fragment fragment = {nfa_state(nfa), nfa_state(nfa)};

  nfa_epsilon(nfa, fragment.start, left.start);
  nfa_epsilon(nfa, fragment.start, right.start);

  nfa_epsilon(nfa, left.end, fragment.end);
  nfa_epsilon(nfa, right.end, fragment.end);

  return fragment;
}

Fragment nfa_repeat(Nfa *nfa, Fragment inner, int mode) {
  Fragment fragment = {nfa_state(nfa), nfa_state(nfa)};

  nfa_epsilon(nfa, fragment.start, inner.start);
  nfa_epsilon(nfa, inner.end, fragment.end);

  if (mode == '*' || mode == '?') {
    nfa_epsilon(nfa, fragment.start, fragment.end);
  }

  if (mode == '*' || mode == '+') {
    nfa_epsilon(nfa, inner.end, inner.start);
  }

  return fragment;
}

void nfa_free(Nfa *nfa) {
  for (size_t i = 0; i < nfa->count; ++i) {
    free(nfa->states[i].edges);
  }

  free(nfa->states);
}
