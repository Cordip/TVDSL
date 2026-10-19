/* Lexer scenarios: name, input bytes, expected tokens or first error. */
#ifndef TESTS_CASES_H
#define TESTS_CASES_H

static const struct {
  const char *name, *input, *expected;
} cases[] = {
    {"empty", "", "(none)"},
    {"mixed_whitespace", " \t\r\n\t ", "(none)"},
    {"integers", "0 9 123 0x", "INT:0 INT:9 INT:123 INT:0 IDENT:x"},
    {"int_double_zero", "00", "ERROR 1:1"},
    {"int_leading_zero", "01", "ERROR 1:1"},
    {"int_leading_zero_mid", "x 007", "ERROR 1:3"},
    {"int_leading_zero_before_operator", "01+2", "ERROR 1:1"},
    {"int_leading_zero_before_identifier", "007name", "ERROR 1:1"},
    {"int_leading_zero_after_newline", "x\n01", "ERROR 2:1"},
    {"zero_boundaries", "0 0+1 0name",
     "INT:0 INT:0 PLUS:+ INT:1 INT:0 IDENT:name"},
    {"leading_zero_inside_identifier", "name007", "IDENT:name007"},
    {"leading_zero_inside_comment", "// 007\n0", "INT:0"},
    {"identifiers", "a_b _x _ foo42 functionx xif If",
     "IDENT:a_b IDENT:_x IDENT:_ IDENT:foo42 IDENT:functionx IDENT:xif "
     "IDENT:If"},
    {"keywords",
     "function returns requires ensures uses while if else assert assume "
     "invariant length true false not "
     "and or forall exists int",
     "FUNCTION:function RETURNS:returns REQUIRES:requires ENSURES:ensures "
     "USES:uses WHILE:while IF:if "
     "ELSE:else ASSERT:assert ASSUME:assume INVARIANT:invariant LENGTH:length "
     "TRUE:true FALSE:false NOT:not "
     "AND:and OR:or FORALL:forall EXISTS:exists TYPE_INT:int"},
    {"keyword_call", "function(x)",
     "FUNCTION:function LPAREN:( IDENT:x RPAREN:)"},
    {"delimiters", "()[]{},;:|",
     "LPAREN:( RPAREN:) LBRACKET:[ RBRACKET:] LBRACE:{ RBRACE:} COMMA:, "
     "SEMICOLON:; COLON:: PIPE:|"},
    {"operators", "+ - * / == != <= >= < > = -> => === a<=b",
     "PLUS:+ MINUS:- STAR:* SLASH:/ EQEQ:== NEQ:!= LTE:<= GTE:>= LT:< GT:> "
     "EQ:= ARROW:-> IMPLIES:=> EQEQ:== "
     "EQ:= IDENT:a LTE:<= IDENT:b"},
    {"comment_empty", "//", "(none)"},
    {"comment_to_eol", "x// a comment\r\ny", "IDENT:x IDENT:y"},
    {"comment_after_slash", "y / 2//tail", "IDENT:y SLASH:/ INT:2"},
    {"comment_hides_ascii", "// @ ! # $ ~", "(none)"},
    {"comment_non_ascii", "x // \xD0\xB6 \xC3\xA9\ny", "IDENT:x IDENT:y"},
    {"trap_non_ascii_after_comment", "// \xD0\xB6\n\xD0\xB6", "ERROR 2:1"},
    {"trap_after_newline", "x\n@", "ERROR 2:1"},
    {"trap_non_ascii", "a\xC3\xA9", "ERROR 1:2"},
    {"trap_hash", "a # b", "ERROR 1:3"},
};

#endif
