"""Check the EBNF block in grammar.md using only the Python standard library."""

import argparse
import itertools
import json
from pathlib import Path
import re
import sys


TOKEN = re.compile(r'\s+|"(?:[^"\\]|\\.)*"|\?[^?\r\n]*\?|[A-Za-z_][A-Za-z_0-9]*|[=;|()[\]{}]')
NAME = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")
BEGIN = "<!-- ll1:begin -->"
END = "<!-- ll1:end -->"
EPSILON = None
EOF = "$"
BYTE_CLASS = "? byte 0x00-0x09, 0x0B-0x0C, 0x0E-0xFF ?"


class EBNF:
    def __init__(self, source, terminals=()):
        self.tokens = []
        offset = 0
        while offset < len(source):
            match = TOKEN.match(source, offset)
            if match is None:
                raise ValueError(f"invalid EBNF at {source[offset:offset + 20]!r}")
            token = match.group()
            if token.startswith("?") and token != BYTE_CLASS:
                raise ValueError(f"invalid byte class {token!r}")
            if not token.isspace():
                if token.startswith('"'):
                    literal = json.loads(token)
                    if not literal:
                        raise ValueError("empty terminal; use an empty alternative")
                    token = json.dumps(literal, ensure_ascii=False)
                self.tokens.append(token)
            offset = match.end()

        self.position = 0
        self.rules = {}
        self.originals = []
        self.owner = ""
        self.helper_count = 0
        while self.peek() is not None:
            name = self.take()
            if NAME.fullmatch(name) is None:
                raise ValueError(f"expected rule name, found {name!r}")
            if name in self.rules:
                raise ValueError(f"duplicate rule {name}")
            self.owner = name
            self.originals.append(name)
            self.take("=")
            self.rules[name] = self.choice()
            self.take(";")
        if not self.originals:
            raise ValueError("empty grammar")

        for alternatives in self.rules.values():
            for sequence in alternatives:
                for symbol in sequence:
                    if (symbol not in self.rules and not symbol.startswith('"')
                            and symbol != BYTE_CLASS and symbol not in terminals):
                        raise ValueError(f"undefined nonterminal {symbol}")

    def peek(self):
        return self.tokens[self.position] if self.position < len(self.tokens) else None

    def take(self, expected=None):
        token = self.peek()
        if token is None or (expected is not None and token != expected):
            raise ValueError(f"expected {expected or 'symbol'!r}, found {token!r}")
        self.position += 1
        return token

    def choice(self):
        alternatives = [self.sequence()]
        while self.peek() == "|":
            self.take("|")
            alternatives.append(self.sequence())
        return alternatives

    def sequence(self):
        symbols = []
        while self.peek() not in (None, "|", ";", ")", "]", "}"):
            token = self.take()
            if token in ("(", "[", "{"):
                alternatives = self.choice()
                self.take({"(": ")", "[": "]", "{": "}"}[token])
                self.helper_count += 1
                helper = f"{self.owner}${self.helper_count}"
                if token == "{":
                    alternatives = [sequence + (helper,) for sequence in alternatives]
                if token in ("[", "{"):
                    alternatives.append(())
                self.rules[helper] = alternatives
                symbols.append(helper)
            elif token.startswith('"') or token == BYTE_CLASS or NAME.fullmatch(token):
                symbols.append(token)
            else:
                raise ValueError(f"expected symbol, found {token!r}")
        return tuple(symbols)


class Grammar:
    def __init__(self, source, terminals=()):
        parsed = EBNF(source, terminals)
        self.rules = parsed.rules
        self.originals = parsed.originals
        self.first = {name: set() for name in self.rules}
        changed = True
        while changed:
            changed = False
            for name, alternatives in self.rules.items():
                for sequence in alternatives:
                    before = len(self.first[name])
                    self.first[name].update(self.first_of(sequence))
                    changed |= len(self.first[name]) != before

        self.check_left_recursion()
        self.follow = {name: set() for name in self.rules}
        self.follow[self.originals[0]].add(EOF)
        changed = True
        while changed:
            changed = False
            for name, alternatives in self.rules.items():
                for sequence in alternatives:
                    trailer = self.follow[name].copy()
                    for symbol in reversed(sequence):
                        if symbol not in self.rules:
                            trailer = {symbol}
                            continue
                        before = len(self.follow[symbol])
                        self.follow[symbol].update(trailer)
                        changed |= len(self.follow[symbol]) != before
                        starts = self.first[symbol]
                        if EPSILON in starts:
                            trailer = trailer | (starts - {EPSILON})
                        else:
                            trailer = starts.copy()

    def first_of(self, sequence):
        result = set()
        for symbol in sequence:
            if symbol not in self.rules:
                result.add(symbol)
                break
            result.update(self.first[symbol] - {EPSILON})
            if EPSILON not in self.first[symbol]:
                break
        else:
            result.add(EPSILON)
        return result

    def check_left_recursion(self):
        edges = {}
        for name, alternatives in self.rules.items():
            edges[name] = set()
            for sequence in alternatives:
                for symbol in sequence:
                    if symbol not in self.rules:
                        break
                    edges[name].add(symbol)
                    if EPSILON not in self.first[symbol]:
                        break
        done = set()
        path = []

        def visit(name):
            if name in path:
                cycle = path[path.index(name):] + [name]
                raise ValueError("left recursion: " + " -> ".join(cycle))
            if name in done:
                return
            path.append(name)
            for child in sorted(edges[name]):
                visit(child)
            path.pop()
            done.add(name)

        for name in self.rules:
            visit(name)

    def conflicts(self):
        result = []
        for name, alternatives in self.rules.items():
            selections = []
            for sequence in alternatives:
                starts = self.first_of(sequence)
                selections.append((starts - {EPSILON}) |
                                  (self.follow[name] if EPSILON in starts else set()))
            for left, right in itertools.combinations(range(len(alternatives)), 2):
                for lookahead in sorted(selections[left] & selections[right]):
                    result.append((name, lookahead, left, right))
        return result

    def dangling_else(self, name, lookahead):
        return (
            lookahead == '"else"'
            and self.rules[name] == [('"else"', "statement"), ()]
            and self.rules.get("conditional") == [
                ('"if"', '"("', "condition", '")"', "statement", name)
            ]
        )

    def table(self):
        def display(symbols):
            values = [f"`{symbol}`".replace("|", r"\|")
                      for symbol in sorted(symbols - {EPSILON})]
            if EPSILON in symbols:
                values.append("ε")
            return ", ".join(values) or "∅"

        lines = ["| Нетерминал | FIRST | FOLLOW |", "| --- | --- | --- |"]
        for name in self.originals:
            lines.append(f"| `{name}` | {display(self.first[name])} | "
                         f"{display(self.follow[name])} |")
        return "\n".join(lines)


def read_grammar(document):
    def block(language, required):
        pattern = r"^```" + re.escape(language) + r"[ \t]*\n(.*?)^```[ \t]*$"
        blocks = re.findall(pattern, document, flags=re.MULTILINE | re.DOTALL)
        if len(blocks) > 1 or (required and len(blocks) != 1):
            raise ValueError(f"expected exactly one fenced {language} block")
        return blocks[0] if blocks else None

    syntax = block("ebnf", True)
    source = block("ebnf-lexical", False)
    lexical = EBNF(source) if source is not None else None
    # Only defined uppercase lexical rules export tokens to the syntax layer.
    terminals = {name for name in lexical.originals if name.isupper()} if lexical else set()
    return Grammar(syntax, terminals)


def generated_table(document, table):
    if document.count(BEGIN) != 1 or document.count(END) != 1:
        raise ValueError("expected one ll1:begin / ll1:end marker pair")
    before, rest = document.split(BEGIN)
    if END not in rest:
        raise ValueError("ll1:end must follow ll1:begin")
    _, after = rest.split(END)
    return before + BEGIN + "\n" + table + "\n" + END + after


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", type=Path, default=Path("grammar.md"))
    parser.add_argument("--update", action="store_true",
                        help="replace the marked FIRST/FOLLOW table after a successful check")
    args = parser.parse_args(argv)
    try:
        document = args.path.read_text(encoding="utf-8")
        grammar = read_grammar(document)
        print(grammar.table())
        failed = False
        for name, lookahead, left, right in grammar.conflicts():
            allowed = grammar.dangling_else(name, lookahead)
            message = (f"{args.path}: {'allowed' if allowed else 'error'}: "
                       f"LL(1) conflict in {name} on {lookahead}, "
                       f"alternatives {left + 1} and {right + 1}")
            print(message, file=sys.stdout if allowed else sys.stderr)
            failed |= not allowed
        if failed:
            return 1
        if args.update or BEGIN in document or END in document:
            updated = generated_table(document, grammar.table())
            if args.update:
                if updated != document:
                    args.path.write_text(updated, encoding="utf-8")
            elif updated != document:
                raise ValueError("stale FIRST/FOLLOW table; run with --update")
        return 0
    except (OSError, ValueError, RecursionError) as error:
        print(f"{args.path}: error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
