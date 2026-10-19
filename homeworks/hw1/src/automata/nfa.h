/* Thompson NFA: storage and the fragment combinators used by the regex parser.
 */
#ifndef AUTOMATA_NFA_H
#define AUTOMATA_NFA_H

#include "util/common.h"

#include <stddef.h>
#include <stdint.h>

/* NFA edge: either an epsilon edge, or a character edge whose label is a set
   of alphabet symbols stored as a bit mask (bit c set = symbol c). */
typedef struct {
  int to, epsilon;
  uint64_t mask[MASK_WORDS];
} Edge;
/* accept = index of the rule this state accepts, or -1. */
typedef struct {
  Edge *edges;
  size_t count, capacity;
  int accept;
} NfaState;
typedef struct {
  NfaState *states;
  size_t count, capacity;
} Nfa;
/* Partial Thompson automaton with one entry and one exit state. */
typedef struct {
  int start, end;
} Fragment;

int nfa_state(Nfa *nfa);
void nfa_epsilon(Nfa *nfa, int from, int to);
/* Fragment matching one character from mask. */
Fragment nfa_atom(Nfa *nfa, const uint64_t mask[MASK_WORDS]);
/* left followed by right. */
Fragment nfa_concat(Nfa *nfa, Fragment left, Fragment right);
/* left or right. */
Fragment nfa_alternate(Nfa *nfa, Fragment left, Fragment right);
/* mode is the quantifier character: '*' (0 or more), '+' (1 or more), '?' (0 or
 * 1). */
Fragment nfa_repeat(Nfa *nfa, Fragment inner, int mode);
void nfa_free(Nfa *nfa);

static inline void set_char(uint64_t mask[MASK_WORDS], int character) {
  mask[character / 64] |= UINT64_C(1) << (character % 64);
}

static inline int has_char(const uint64_t mask[MASK_WORDS], int character) {
  return (mask[character / 64] >> (character % 64)) & 1;
}

#endif
