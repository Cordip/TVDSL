/* Reference lexer built on POSIX <regex.h> from libc. It shares only the rule
   table with the real lexer: regex parsing, matching and maximal munch are
   independent, so a bug in the NFA/DFA pipeline shows up as a mismatch. */
#ifndef TESTS_ORACLE_H
#define TESTS_ORACLE_H

#include "hw1.h"

#include <stddef.h>

/* Compiles every rule with regcomp. Returns 0 and fills error on failure. */
int oracle_init(char *error, size_t error_size);
void oracle_free(void);

/* Oracle input is limited to bytes 1..255: regexec works on C strings. In the
   default "C" locale it matches byte by byte, so a negated class takes any
   non-ASCII byte, the same way the lexer's NON_ASCII symbol does. */
int oracle_accepts_input(const unsigned char *input, size_t length);

/* Runs the real lexer and the oracle on input. Returns 1 if tokens and errors
   agree; otherwise returns 0 and describes the first difference in report. */
int oracle_compare(const Automaton *automaton, const unsigned char *input,
                   size_t length, char *report, size_t report_size);

#endif
