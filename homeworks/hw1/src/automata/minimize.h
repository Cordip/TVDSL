/* Hopcroft DFA minimization. */
#ifndef AUTOMATA_MINIMIZE_H
#define AUTOMATA_MINIMIZE_H

#include "automata/dfa.h"

/* source must be a complete DFA with all states reachable (as determinize
   produces). Returns the minimal DFA. States accepting different rules are
   never merged, so token kinds are preserved. */
Dfa minimize(const Dfa *source);

#endif
