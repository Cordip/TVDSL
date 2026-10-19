/* DFA storage and subset construction from an NFA. */
#ifndef AUTOMATA_DFA_H
#define AUTOMATA_DFA_H

#include "automata/nfa.h"
#include "util/common.h"

/* set = NFA states this DFA state stands for (bitset of `words` 64-bit words);
   next = complete transition table, accept = winning rule index or -1. */
typedef struct {
  uint64_t *set;
  int next[ALPHABET], accept;
} DfaState;
/* trap = the empty-subset dead state, or -1 if that subset is unreachable. */
typedef struct {
  DfaState *states;
  size_t count, capacity, words;
  int start, trap;
} Dfa;

/* The table is filled for every alphabet symbol so the lexer never meets a
   missing transition: an uncovered symbol leads to the empty NFA-state set,
   which is the trap. If no transition reaches the empty subset, trap is -1. */
Dfa determinize(const Nfa *nfa);
void dfa_free(Dfa *dfa);

#endif
