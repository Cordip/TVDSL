/* Recursive-descent regex parser that emits NFA fragments directly.
 *
 *   alt    := concat ('|' concat)*
 *   concat := (base quantifier?)+          quantifier := '*' | '+' | '?'
 *   base   := '(' alt ')' | '[' '^'? class-item+ ']' | '\' char | char
 *
 * Supported escapes: \n \r \t; any other escaped character is itself.
 * Only the first error is kept: later ones are usually its consequences. */

#include "spec/regex.h"

#include "util/common.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  const char *source;
  size_t pos;
  Nfa *nfa;
  char *error;
  size_t error_size;
  int failed;
} Parser;

static void parse_error(Parser *parser, const char *message) {
  if (parser->failed) {
    return;
  }

  parser->failed = 1;
  if (parser->error && parser->error_size) {
    snprintf(parser->error, parser->error_size, "%s at offset %zu in %s",
             message, parser->pos, parser->source);
  }
}

static int escaped(Parser *parser) {
  unsigned char character = (unsigned char)parser->source[parser->pos++];

  if (!character) {
    /* Callers may inspect the delimiter after a failed escape. Keep the
       cursor at the terminating NUL, never one byte beyond the string. */
    --parser->pos;
    parse_error(parser, "Trailing backslash");

    return -1;
  }

  if (character == 'n') {
    return '\n';
  }

  if (character == 'r') {
    return '\r';
  }

  if (character == 't') {
    return '\t';
  }

  return character;
}

static int class_char(Parser *parser) {
  unsigned char character = (unsigned char)parser->source[parser->pos++];

  if (character == '\\') {
    return escaped(parser);
  }

  if (!character) {
    --parser->pos;
    parse_error(parser, "Unclosed character class");

    return -1;
  }

  return character;
}

static Fragment parse_alt(Parser *parser);

static Fragment parse_base(Parser *parser) {
  unsigned char character = (unsigned char)parser->source[parser->pos++];

  if (character == '(') {
    Fragment fragment = parse_alt(parser);

    if (parser->source[parser->pos] != ')') {
      parse_error(parser, "Expected ')'");
    } else {
      ++parser->pos;
    }

    return fragment;
  }

  uint64_t mask[MASK_WORDS] = {0};

  if (character == '[') {
    int negated = parser->source[parser->pos] == '^';

    if (negated) {
      ++parser->pos;
    }

    int any = 0;

    while (parser->source[parser->pos] && parser->source[parser->pos] != ']') {
      int first = class_char(parser);

      if (first < 0 || first >= ASCII) {
        parse_error(parser, "Non-ASCII class character");
        break;
      }

      int last = first;

      if (parser->source[parser->pos] == '-' &&
          parser->source[parser->pos + 1] != ']') {
        ++parser->pos;
        last = class_char(parser);
        if (last < first || last >= ASCII) {
          parse_error(parser, "Invalid character range");
          break;
        }
      }

      for (int i = first; i <= last; ++i) {
        set_char(mask, i);
      }

      any = 1;
    }

    if (parser->source[parser->pos] != ']') {
      parse_error(parser, "Unclosed character class");
    } else {
      ++parser->pos;
    }

    if (!any) {
      parse_error(parser, "Empty character class");
    }

    /* A negated class also matches NON_ASCII; bits past ALPHABET are never
     * read. */
    if (negated) {
      for (size_t word = 0; word < MASK_WORDS; ++word) {
        mask[word] = ~mask[word];
      }
    }
  } else {
    if (character == '\\') {
      character = (unsigned char)escaped(parser);
    }

    if (character >= ASCII || character == 0) {
      parse_error(parser, "Invalid literal");
    } else {
      set_char(mask, character);
    }
  }

  return nfa_atom(parser->nfa, mask);
}

static Fragment parse_concat(Parser *parser) {
  Fragment result = {-1, -1};

  while (parser->source[parser->pos] && parser->source[parser->pos] != '|' &&
         parser->source[parser->pos] != ')') {
    if (strchr("*+?", parser->source[parser->pos])) {
      parse_error(parser, "Quantifier without operand");
      break;
    }

    Fragment fragment = parse_base(parser);

    /* Stop before using a fragment from a failed parse: it may be {-1, -1}. */
    if (parser->failed) {
      break;
    }

    if (parser->source[parser->pos] &&
        strchr("*+?", parser->source[parser->pos])) {
      fragment =
          nfa_repeat(parser->nfa, fragment, parser->source[parser->pos++]);
    }

    result =
        result.start < 0 ? fragment : nfa_concat(parser->nfa, result, fragment);
  }

  if (result.start < 0) {
    parse_error(parser, "Empty alternative");
  }

  return result;
}

static Fragment parse_alt(Parser *parser) {
  Fragment fragment = parse_concat(parser);

  while (!parser->failed && parser->source[parser->pos] == '|') {
    ++parser->pos;

    Fragment other = parse_concat(parser);

    if (!parser->failed) {
      fragment = nfa_alternate(parser->nfa, fragment, other);
    }
  }

  return fragment;
}

int regex_compile(Nfa *nfa, const char *regex, Fragment *out, char *error,
                  size_t error_size) {
  Parser parser = {
      .source = regex, .nfa = nfa, .error = error, .error_size = error_size};

  if (error && error_size) {
    error[0] = 0;
  }

  *out = parse_alt(&parser);
  if (!parser.failed && regex[parser.pos]) {
    parse_error(&parser, "Unexpected character");
  }

  if (parser.failed) {
    *out = (Fragment){-1, -1};
  }

  return !parser.failed;
}
