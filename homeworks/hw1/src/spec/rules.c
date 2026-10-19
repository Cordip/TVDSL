#include "spec/rules.h"

// clang-format off
const Rule rules[] = {
    /* Skipped: consumed without producing a token. */
    { "WS",          "[ \\t\\r\\n]+",          RULE_SKIP  },
    { "COMMENT",     "//[^\\r\\n]*",           RULE_SKIP  },

    /* Keywords. They must stay above IDENT, which matches the same text. */
    { "FUNCTION",    "function",               RULE_EMIT  },
    { "RETURNS",     "returns",                RULE_EMIT  },
    { "REQUIRES",    "requires",               RULE_EMIT  },
    { "ENSURES",     "ensures",                RULE_EMIT  },
    { "USES",        "uses",                   RULE_EMIT  },
    { "WHILE",       "while",                  RULE_EMIT  },
    { "IF",          "if",                     RULE_EMIT  },
    { "ELSE",        "else",                   RULE_EMIT  },
    { "ASSERT",      "assert",                 RULE_EMIT  },
    { "ASSUME",      "assume",                 RULE_EMIT  },
    { "INVARIANT",   "invariant",              RULE_EMIT  },
    { "LENGTH",      "length",                 RULE_EMIT  },
    { "TRUE",        "true",                   RULE_EMIT  },
    { "FALSE",       "false",                  RULE_EMIT  },
    { "NOT",         "not",                    RULE_EMIT  },
    { "AND",         "and",                    RULE_EMIT  },
    { "OR",          "or",                     RULE_EMIT  },
    { "FORALL",      "forall",                 RULE_EMIT  },
    { "EXISTS",      "exists",                 RULE_EMIT  },
    { "TYPE_INT",    "int",                    RULE_EMIT  },

    /* Numbers and identifiers. On "007" INVALID_INT matches more than INT,
       so maximal munch reports the leading zero as an error. */
    { "INVALID_INT", "0[0-9]+",                RULE_ERROR },
    { "INT",         "0|[1-9][0-9]*",          RULE_EMIT  },
    { "IDENT",       "[A-Za-z_][A-Za-z0-9_]*", RULE_EMIT  },

    /* Delimiters. */
    { "LPAREN",      "\\(",                    RULE_EMIT  },
    { "RPAREN",      "\\)",                    RULE_EMIT  },
    { "LBRACE",      "\\{",                    RULE_EMIT  },
    { "RBRACE",      "\\}",                    RULE_EMIT  },
    { "LBRACKET",    "\\[",                    RULE_EMIT  },
    { "RBRACKET",    "\\]",                    RULE_EMIT  },
    { "COMMA",       ",",                      RULE_EMIT  },
    { "SEMICOLON",   ";",                      RULE_EMIT  },
    { "COLON",       ":",                      RULE_EMIT  },
    { "PIPE",        "\\|",                    RULE_EMIT  },

    /* Operators. */
    { "PLUS",        "\\+",                    RULE_EMIT  },
    { "MINUS",       "-",                      RULE_EMIT  },
    { "STAR",        "\\*",                    RULE_EMIT  },
    { "SLASH",       "/",                      RULE_EMIT  },
    { "EQEQ",        "==",                     RULE_EMIT  },
    { "NEQ",         "!=",                     RULE_EMIT  },
    { "LTE",         "<=",                     RULE_EMIT  },
    { "GTE",         ">=",                     RULE_EMIT  },
    { "LT",          "<",                      RULE_EMIT  },
    { "GT",          ">",                      RULE_EMIT  },
    { "EQ",          "=",                      RULE_EMIT  },
    { "ARROW",       "->",                     RULE_EMIT  },
    { "IMPLIES",     "=>",                     RULE_EMIT  },
};
// clang-format on

const int rule_count = sizeof rules / sizeof rules[0];
