/* Regex parser: compiles one token regex into a fragment of a shared NFA. */
#ifndef SPEC_REGEX_H
#define SPEC_REGEX_H

#include "automata/nfa.h"

/* Returns 1 and stores the fragment in *out, or returns 0 with a message in
   error. Diagnostics are optional (NULL or size 0); status never depends on
   buffer size. The buffer is cleared on entry; only the first error is kept. */
int regex_compile(Nfa *nfa, const char *regex, Fragment *out, char *error,
                  size_t error_size);

#endif
