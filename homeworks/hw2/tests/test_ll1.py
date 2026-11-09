"""FIRST/FOLLOW examples and regressions for the EBNF checker."""

from contextlib import redirect_stderr, redirect_stdout
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.ll1 import BEGIN, END, EOF, EPSILON, BYTE_CLASS, EBNF, Grammar, generated_table, main, read_grammar


ROOT = Path(__file__).resolve().parents[1]
DANGLING = '''
statement = conditional | "a" ;
conditional = "if" "(" condition ")" statement [ "else" statement ] ;
condition = "c" ;
'''


class GrammarTests(unittest.TestCase):
    def test_external_terminals_must_be_declared(self):
        with self.assertRaisesRegex(ValueError, "undefined nonterminal UNKNOWN"):
            Grammar("start = UNKNOWN ;")
        grammar = Grammar("start = TOKEN ;", terminals={"TOKEN"})
        self.assertEqual(grammar.first["start"], {"TOKEN"})

    def test_lexical_definitions_do_not_change_token_first_sets(self):
        document = ('```ebnf\nstart = IDENT INT ;\n```\n'
                    '```ebnf-lexical\nIDENT = letter { letter } ;\n'
                    'INT = digit { digit } ; letter = "a" ; digit = "1" ;\n```\n')
        grammar = read_grammar(document)
        self.assertEqual(grammar.first["start"], {"IDENT"})
        self.assertEqual(set(grammar.rules), {"start"})
        for definition in ("IDENT = letter { letter } ;", "INT = digit { digit } ;"):
            with self.subTest(definition=definition):
                with self.assertRaisesRegex(ValueError, "undefined nonterminal"):
                    read_grammar(document.replace(definition, ""))

    def test_lexical_references_must_be_defined(self):
        for reference in ("missing", "MISSING"):
            document = ('```ebnf\nstart = IDENT ;\n```\n'
                        f'```ebnf-lexical\nIDENT = {reference} ;\n```\n')
            with self.subTest(reference=reference):
                with self.assertRaisesRegex(ValueError, "undefined nonterminal"):
                    read_grammar(document)

    def test_unknown_syntax_token_is_not_implicitly_allowed(self):
        document = ('```ebnf\nstart = UNKNOWN ;\n```\n'
                    '```ebnf-lexical\nIDENT = "a" ;\n```\n')
        with self.assertRaisesRegex(ValueError, "undefined nonterminal UNKNOWN"):
            read_grammar(document)

    def test_comment_byte_primitive_is_explicit(self):
        parsed = EBNF("commentByte = " + BYTE_CLASS + " ;")
        self.assertEqual(parsed.rules["commentByte"], [(BYTE_CLASS,)])
        for primitive in (
            "? anything ?", "? byte 0x00-0xFF ?",
            BYTE_CLASS.replace("0x0E", "0x0D"), BYTE_CLASS[:-1],
        ):
            with self.subTest(primitive=primitive):
                with self.assertRaises(ValueError):
                    EBNF("commentByte = " + primitive + " ;")

    def test_duplicate_lexical_blocks_fail(self):
        block = '```ebnf-lexical\nIDENT = "a" ;\n```\n'
        with self.assertRaisesRegex(ValueError, "exactly one"):
            read_grammar('```ebnf\nstart = IDENT ;\n```\n' + block + block)

    def test_sequence(self):
        grammar = Grammar('start = "x" IDENT "end" ;', terminals={'IDENT'})
        self.assertEqual(grammar.first["start"], {'"x"'})
        self.assertEqual(grammar.follow["start"], {EOF})
        self.assertEqual(grammar.conflicts(), [])

    def test_nullable_prefix_and_suffix(self):
        grammar = Grammar('start = left right ; left = "a" | ; right = "b" | ;')
        self.assertEqual(grammar.first["start"], {'"a"', '"b"', EPSILON})
        self.assertEqual(grammar.follow["left"], {'"b"', EOF})
        self.assertEqual(grammar.follow["right"], {EOF})
        self.assertEqual(grammar.conflicts(), [])

    def test_propagation_through_rules(self):
        grammar = Grammar('start = left "z" ; left = right ; right = "x" | ;')
        self.assertEqual(grammar.first["start"], {'"x"', '"z"'})
        self.assertEqual(grammar.first["left"], {'"x"', EPSILON})
        self.assertEqual(grammar.follow["right"], {'"z"'})

    def test_right_recursion(self):
        grammar = Grammar('start = "x" start | ;')
        self.assertEqual(grammar.first["start"], {'"x"', EPSILON})
        self.assertEqual(grammar.follow["start"], {EOF})
        self.assertEqual(grammar.conflicts(), [])

    def test_ebnf_groups_options_and_repetitions(self):
        grammar = Grammar('start = "(" [ IDENT { ("," | ";") IDENT } ] ")" ;', terminals={'IDENT'})
        self.assertEqual(grammar.first["start"], {'"("'})
        self.assertEqual(grammar.follow["start"], {EOF})
        self.assertEqual(grammar.conflicts(), [])
        # The original rule, not its first helper, gets the end-of-input marker.
        for name in grammar.rules.keys() - {"start"}:
            self.assertNotIn(EOF, grammar.follow[name])

    def test_empty_rule(self):
        grammar = Grammar('start = ;')
        self.assertEqual(grammar.first["start"], {EPSILON})
        self.assertEqual(grammar.follow["start"], {EOF})

    def test_first_first_conflict(self):
        grammar = Grammar('start = "x" | "x" "y" ;')
        self.assertEqual(grammar.conflicts(), [("start", '"x"', 0, 1)])

    def test_first_follow_conflict(self):
        grammar = Grammar('start = item "x" ; item = "x" | ;')
        self.assertEqual(grammar.conflicts(), [("item", '"x"', 0, 1)])

    def test_two_nullable_alternatives_conflict_at_eof(self):
        grammar = Grammar('start = left | right ; left = ; right = ;')
        self.assertEqual(grammar.conflicts(), [("start", EOF, 0, 1)])

    def test_repetition_follow_conflict(self):
        grammar = Grammar('start = { "x" } "x" ;')
        self.assertEqual(grammar.conflicts(), [("start$1", '"x"', 0, 1)])

    def test_left_recursion(self):
        for source in ('start = start "x" | "x" ;',
                       'start = next ; next = start | "x" ;',
                       'start = [ "x" ] start | "y" ;',
                       'start = { [ "x" ] } ;'):
            with self.subTest(source=source):
                with self.assertRaisesRegex(ValueError, "left recursion"):
                    Grammar(source)

    def test_invalid_grammar(self):
        for source, message in (
            ('', "empty grammar"),
            ('start = missing ;', "undefined nonterminal missing"),
            ('start = "x" ; start = "y" ;', "duplicate rule start"),
            ('start = [ "x" ;', "expected"),
            ('start = "x"', "expected"),
            ('start = @ ;', "invalid EBNF"),
            ('start = "" ;', "empty terminal"),
        ):
            with self.subTest(source=source):
                with self.assertRaisesRegex(ValueError, message):
                    Grammar(source)

    def test_literal_punctuation_and_escapes(self):
        grammar = Grammar(r'start = "|" "[" "\"" "]" ;')
        self.assertEqual(grammar.first["start"], {'"|"'})
        self.assertEqual(grammar.conflicts(), [])
        self.assertIn(r'`"\|"`', grammar.table())
        # Equivalent spellings must not hide a FIRST/FIRST conflict.
        grammar = Grammar(r'start = "a" | "\u0061" ;')
        self.assertEqual(grammar.conflicts(), [("start", '"a"', 0, 1)])

    def test_quoted_nonterminal_name_is_a_terminal(self):
        grammar = Grammar('start = "next" next ; next = "x" ;')
        self.assertEqual(grammar.first["start"], {'"next"'})
        self.assertEqual(grammar.first["next"], {'"x"'})

    def test_only_documented_dangling_else_is_allowed(self):
        grammar = Grammar(DANGLING)
        conflicts = grammar.conflicts()
        self.assertEqual(len(conflicts), 1)
        name, lookahead, _, _ = conflicts[0]
        self.assertTrue(grammar.dangling_else(name, lookahead))
        self.assertFalse(grammar.dangling_else(name, '"other"'))

        unrelated = Grammar(DANGLING.replace("conditional", "branch"))
        self.assertTrue(unrelated.conflicts())
        self.assertFalse(any(unrelated.dangling_else(name, token)
                             for name, token, _, _ in unrelated.conflicts()))

    def test_extra_else_alternative_is_not_allowed(self):
        source = DANGLING.replace('[ "else" statement ]',
                                  '[ "else" statement | "else" "b" ]')
        grammar = Grammar(source)
        self.assertTrue(grammar.conflicts())
        self.assertFalse(any(grammar.dangling_else(name, token)
                             for name, token, _, _ in grammar.conflicts()))

    def test_markdown_requires_one_ebnf_block(self):
        block = '```ebnf\nstart = "x" ;\n```\n'
        self.assertEqual(read_grammar(block).first["start"], {'"x"'})
        for document in ('', '```c\nint x;\n```\n', block + block):
            with self.subTest(document=document):
                with self.assertRaisesRegex(ValueError, "exactly one"):
                    read_grammar(document)

    def test_marker_validation(self):
        for document in ('', BEGIN, END + BEGIN, BEGIN + BEGIN + END):
            with self.subTest(document=document):
                with self.assertRaises(ValueError):
                    generated_table(document, "table")

    def test_repository_grammar_and_table(self):
        document = (ROOT / "grammar.md").read_text(encoding="utf-8")
        grammar = read_grammar(document)
        self.assertEqual(len(grammar.conflicts()), 1)
        self.assertTrue(all(grammar.dangling_else(name, token)
                            for name, token, _, _ in grammar.conflicts()))
        self.assertEqual(document, generated_table(document, grammar.table()))


class CommandTests(unittest.TestCase):
    def run_command(self, path, *arguments):
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            status = main([str(path), *arguments])
        return status, output.getvalue(), errors.getvalue()

    def test_table_update_and_stale_check(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "grammar.md"
            path.write_text('before\n```ebnf\nstart = "x" ;\n```\n' +
                            BEGIN + '\noutdated\n' + END + '\nafter\n')
            status, _, errors = self.run_command(path)
            self.assertEqual(status, 1)
            self.assertIn("stale FIRST/FOLLOW", errors)
            self.assertEqual(self.run_command(path, "--update")[0], 0)
            updated = path.read_text()
            self.assertTrue(updated.startswith('before\n```ebnf'))
            self.assertTrue(updated.endswith('\nafter\n'))
            self.assertEqual(self.run_command(path)[0], 0)
            self.assertEqual(self.run_command(path, "--update")[0], 0)
            self.assertEqual(path.read_text(), updated)

    def test_conflict_never_updates_document(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "grammar.md"
            document = '```ebnf\nstart = "x" | "x" "y" ;\n```\n' + BEGIN + END
            path.write_text(document)
            status, _, errors = self.run_command(path, "--update")
            self.assertEqual(status, 1)
            self.assertIn('LL(1) conflict in start on "x"', errors)
            self.assertEqual(path.read_text(), document)

    def test_allowed_conflict_still_reports_other_conflicts(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "grammar.md"
            source = DANGLING.replace('"a" ;', '"a" | "a" "b" ;')
            path.write_text('```ebnf\n' + source + '```\n')
            status, output, errors = self.run_command(path)
            self.assertEqual(status, 1)
            self.assertIn("allowed: LL(1) conflict", output)
            self.assertIn("error: LL(1) conflict in statement", errors)

    def test_missing_file(self):
        with tempfile.TemporaryDirectory() as folder:
            status, _, errors = self.run_command(Path(folder) / "missing.md")
            self.assertEqual(status, 1)
            self.assertIn(": error:", errors)

    def test_script_entrypoint(self):
        result = subprocess.run([sys.executable, "-B", str(ROOT / "tools/ll1.py"),
                                 str(ROOT / "grammar.md")], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("| FIRST | FOLLOW |", result.stdout)
        self.assertIn("allowed: LL(1) conflict", result.stdout)
        self.assertEqual(result.stderr, "")


if __name__ == "__main__":
    unittest.main()
