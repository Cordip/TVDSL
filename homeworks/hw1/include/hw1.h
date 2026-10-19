/* Public API of the Funny lexer generator.
 *
 * Pipeline: token regexes -> Thompson NFA -> DFA (subset construction)
 *           -> minimal DFA (Hopcroft) -> maximal-munch scanner.
 *
 * Modules (src/<group>/), in pipeline order:
 *   spec/rules.c         token regexes and their priority
 *   spec/regex.c         regex parser, emits NFA fragments
 *   automata/nfa.c       Thompson NFA
 *   automata/dfa.c       subset construction
 *   automata/minimize.c  Hopcroft minimization
 *   lexer/automaton.c    runs the pipeline; report, CSV export, equivalence
 * check lexer/lexer.c        input reader and scanner over the minimal DFA
 *   util/common.c        alphabet size and allocation helpers */

#ifndef HW1_H
#define HW1_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct Automaton Automaton;

/* Same limit as rustc (SourceFile::MAX_FILE_SIZE): offsets fit in 32 bits. */
#define HW1_MAX_INPUT_SIZE ((size_t)UINT32_MAX - 1)

typedef struct {
  const char *kind;
  const unsigned char *text;
  size_t length;
  size_t offset;
  size_t line;
  size_t column;
} Token;

typedef struct {
  size_t offset;
  size_t line;
  size_t column;
  unsigned char byte;
} LexError;

/* Receives each emitted token. Return 0 to stop the scan, for example on an
   output error. text is valid only during this call. */
typedef int (*TokenSink)(const Token *token, void *context);

/* Rule order determines priority when several rules accept the same lexeme. */
Automaton *automaton_build(char *error, size_t error_size);
void automaton_free(Automaton *automaton);
size_t automaton_nfa_states(const Automaton *automaton);
size_t automaton_dfa_states(const Automaton *automaton);
size_t automaton_min_states(const Automaton *automaton);
/* Returns (size_t)-1 when no empty-subset trap is reachable. */
size_t automaton_trap(const Automaton *automaton);
int automaton_export_csv(const Automaton *automaton, FILE *output);
int automaton_verify(const Automaton *automaton);
/* Reads the whole stream, pipes included, into *data, which the caller frees.
   Returns 1 on success, 0 when the input exceeds limit bytes (pass
   HW1_MAX_INPUT_SIZE; limit must be below SIZE_MAX), and -1 on a read error. */
int lexer_read_input(FILE *input, size_t limit, unsigned char **data,
                     size_t *length);
/* Returns 1 on success, 0 on a lexical error (see error), and -1 when the
   sink stops the scan. */
int lexer_scan(const Automaton *automaton, const unsigned char *input,
               size_t length, TokenSink sink, void *context, LexError *error);

#endif
