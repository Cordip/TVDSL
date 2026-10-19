/* Token rules of the Funny language. */
#ifndef SPEC_RULES_H
#define SPEC_RULES_H

typedef enum { RULE_EMIT, RULE_SKIP, RULE_ERROR } RuleAction;

/* When several rules accept the same lexeme, the one listed first wins,
   so keywords must stay above IDENT. Error rules reject the lexeme;
   skip rules consume it without emitting a token. */
typedef struct {
  const char *name, *regex;
  RuleAction action;
} Rule;

extern const Rule rules[];
extern const int rule_count;

#endif
