/* Internal layout of the public Automaton handle (see hw1.h), shared by
   automaton.c, which builds it, and lexer.c, which runs it. */
#ifndef LEXER_AUTOMATON_H
#define LEXER_AUTOMATON_H

#include "automata/dfa.h"

struct Automaton {
  Dfa original, minimal;
  size_t nfa_count;
};

#endif
