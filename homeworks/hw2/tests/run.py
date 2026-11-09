"""CLI snapshots, course examples and bounded depth regressions for HW2."""

import argparse
import difflib
import os
from pathlib import Path
import re
import selectors
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
OUTPUT_LIMIT = 1024 * 1024
DIAGNOSTIC = re.compile(rb".+:\d+:\d+: error: .+")
LEXICAL = re.compile(
    rb"error: (unexpected (byte|character)|integer literal with a leading zero)"
)


def bounded_run(arguments, cwd, limit=OUTPUT_LIMIT, timeout=10, stdin=None):
    """Bound both pipes while reading; always reap a timed-out/oversized child."""
    process = subprocess.Popen(
        arguments, cwd=cwd, stdin=stdin, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    buffers = {"stdout": bytearray(), "stderr": bytearray()}
    deadline = time.monotonic() + timeout
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ, "stdout")
            selector.register(process.stderr, selectors.EVENT_READ, "stderr")
            while selector.get_map():
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f"timeout after {timeout} seconds")
                for key, _ in selector.select(remaining):
                    buffer = buffers[key.data]
                    chunk = os.read(key.fd, min(65536, limit + 1 - len(buffer)))
                    if not chunk:
                        selector.unregister(key.fileobj)
                        continue
                    buffer.extend(chunk)
                    if len(buffer) > limit:
                        raise ValueError(f"{key.data} exceeds {limit} bytes")
            status = process.wait(timeout=max(0, deadline - time.monotonic()))
        return subprocess.CompletedProcess(
            arguments, status, bytes(buffers["stdout"]), bytes(buffers["stderr"])
        )
    finally:
        if process.poll() is None:
            process.kill()
        process.wait()
        process.stdout.close()
        process.stderr.close()


def load_examples(root, examples):
    manifest = root / "tests/examples.expected"
    try:
        lines = manifest.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ValueError(f"cannot read {manifest}: {error}") from error
    if not examples.is_dir():
        raise ValueError(f"no examples directory {examples}")
    rows = {}
    for number, line in enumerate(lines, 1):
        words = line.partition("#")[0].split()
        if not words:
            continue
        if len(words) != 2 or words[1] not in {"parses", "syntax", "lexical"}:
            raise ValueError(f"{manifest}:{number}: invalid example row")
        name, expected = words
        if name in rows:
            raise ValueError(f"{manifest}:{number}: duplicate example {name}")
        rows[name] = expected
    if not rows:
        raise ValueError(f"{manifest}: empty example list")
    actual = {file.relative_to(examples).as_posix() for file in examples.rglob("*.funny")}
    if rows.keys() - actual:
        raise ValueError("missing examples: " + ", ".join(sorted(rows.keys() - actual)))
    if actual - rows.keys():
        raise ValueError("unclassified examples: " + ", ".join(sorted(actual - rows.keys())))
    return rows


def parser_error(result):
    lines = result.stderr.splitlines()
    return (
        result.returncode == 1
        and not result.stdout
        and bool(lines)
        and all(DIAGNOSTIC.fullmatch(line) for line in lines)
    )


def successful_dot(result):
    return (
        result.returncode == 0
        and not result.stderr
        and result.stdout.startswith(b"digraph AST {\n")
        and result.stdout.endswith(b"}\n")
    )


def successful_tree(result):
    return (result.returncode == 0 and not result.stderr
            and result.stdout.startswith(b"Module @"))


class Runner:
    def __init__(self, binary, root, update=False):
        self.binary = str(binary)
        self.root = root
        self.update = update
        self.passed = 0
        self.failed = 0

    def report(self, name, success):
        if success:
            self.passed += 1
        else:
            self.failed += 1
        print(f"{'PASS' if success else 'FAIL'}  {name}")

    def invoke(self, arguments, name, discard=False):
        try:
            command = [self.binary, *map(str, arguments)]
            if discard:
                return subprocess.run(
                    command, cwd=self.root, stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE, timeout=10,
                )
            return bounded_run(command, self.root)
        except (OSError, ValueError, TimeoutError, subprocess.TimeoutExpired) as error:
            self.report(f"{name}: {error}", False)
            return None

    def snapshot(self, name, expected, actual):
        if self.update:
            expected.write_bytes(actual)
        if not expected.is_file():
            self.report(f"{name}: no expected output", False)
            return
        old = expected.read_bytes()
        if old != actual:
            print("".join(difflib.unified_diff(
                old.decode("utf-8", errors="replace").splitlines(keepends=True),
                actual.decode("utf-8", errors="replace").splitlines(keepends=True),
                fromfile=str(expected), tofile="actual",
            )), end="")
        self.report(name, old == actual)

    def case(self, source):
        name = source.stem
        relative = source.relative_to(self.root)
        result = self.invoke(["--dot", relative], name)
        if result is None:
            return
        errors, dot = source.with_suffix(".err"), source.with_suffix(".dot")
        new = not errors.exists() and not dot.exists()
        if errors.exists() or (new and self.update and parser_error(result)):
            if parser_error(result):
                self.snapshot(name + " (errors)", errors, result.stderr)
            else:
                self.report(f"{name}: expected parser error and no AST", False)
            return
        if not successful_dot(result):
            print(result.stderr.decode("utf-8", errors="replace"), end="")
            self.report(f"{name}: expected exit 0, DOT and no errors", False)
            return
        self.snapshot(name, dot, result.stdout)
        raw = source.with_suffix(".raw.dot")
        if raw.exists():
            result = self.invoke(["--dot", "--raw", relative], name + " (--raw)")
            if result is not None:
                if successful_dot(result):
                    self.snapshot(name + " (--raw)", raw, result.stdout)
                else:
                    self.report(f"{name} (--raw): expected successful DOT", False)

        for suffix, flags in ((".tree", []), (".raw.tree", ["--raw"])):
            expected = source.with_suffix(suffix)
            if expected.exists():
                label = name + " (" + suffix + ")"
                result = self.invoke([*flags, relative], label)
                if result is not None:
                    if successful_tree(result):
                        self.snapshot(label, expected, result.stdout)
                    else:
                        self.report(label + ": expected successful tree", False)

    def cli(self, missing):
        for arguments in (
            [], ["--format=xml", "tests/cases/basic.funny"],
            ["--format=text", "tests/cases/basic.funny"],
            ["--format=json", "tests/cases/basic.funny"],
            ["a.funny", "b.funny"],
        ):
            result = self.invoke(arguments, "arguments")
            if result is not None:
                self.report(f"arguments {arguments} -> 2",
                            result.returncode == 2 and not result.stdout
                            and result.stderr.startswith(b"Usage:"))
        result = self.invoke([missing], "missing input")
        if result is not None:
            self.report("missing input -> 1", result.returncode == 1
                        and not result.stdout
                        and result.stderr == f"hw2: error: cannot read {missing}\n".encode())

    def examples(self, examples, rows):
        for name, expected in rows.items():
            result = self.invoke([examples / name], f"example {name}")
            if result is None:
                continue
            if successful_tree(result):
                actual = "parses"
            elif parser_error(result):
                actual = "lexical" if LEXICAL.search(result.stderr) else "syntax"
            else:
                actual = f"unexpected exit/output ({result.returncode})"
            self.report(f"example {name}: {actual}, expected {expected}",
                        actual == expected)

    def api_example(self, binary):
        valid = (b"p(x: int) => x > 0;\n"
                 b"f(a: int[]) returns r: int[] uses tmp {\n"
                 b" tmp = a; tmp[0] = 7; r = tmp; }\n")
        for name, source in (
            ("success", valid),
            ("syntax", b"f() returns r: int { r = ; }"),
            ("lexical", b"f() returns r: int { r = 007; }"),
        ):
            try:
                with tempfile.TemporaryFile() as stream:
                    stream.write(source)
                    stream.seek(0)
                    result = bounded_run([str(binary)], self.root, stdin=stream)
                if name == "success":
                    success = (result.returncode == 0 and not result.stderr
                               and result.stdout.startswith(b"Function f\n  Local tmp: unspecified\nModule @")
                               and b"ArrayUpdate" in result.stdout
                               and b"ArrayAssign" not in result.stdout)
                else:
                    success = (parser_error(result)
                               and result.stderr.startswith(b"<stdin>:1:")
                               and bool(LEXICAL.search(result.stderr)) == (name == "lexical"))
                self.report("API example: " + name, success)
            except (OSError, ValueError, TimeoutError, subprocess.TimeoutExpired) as error:
                self.report(f"API example {name}: {error}", False)

    def depths(self, source):
        cases = (
            ("sum", 998, 0), ("sum", 999, 1), ("sum", 1001, 1),
            ("mixed", 600, 1), ("mixed", 20, 0),
            ("index", 997, 0), ("index", 998, 1),
            ("update", 16, 0), ("update", 500, 1),
            ("update_index", 997, 0), ("update_index", 998, 1),
            ("update_value", 998, 1), ("calls", 1001, 1),
            ("implies", 1001, 1), ("blocks", 1001, 1),
            ("block_sum", 600, 1), ("wide", 2000, 0),
        )
        for kind, size, expected in cases:
            source.write_text(depth_source(kind, size), encoding="utf-8")
            for flags in ([], ["--raw"], ["--dot"], ["--raw", "--dot"]):
                name = f"depth {kind} {size} {flags} -> {expected}"
                result = self.invoke([*flags, source], name, discard=expected == 0)
                if result is None:
                    continue
                if expected == 0:
                    success = result.returncode == 0 and not result.stderr
                else:
                    success = (parser_error(result) and
                               b"error: nesting deeper than 1000 levels\n" in result.stderr)
                self.report(name, success)

        source.write_text(depth_source("update", 499), encoding="utf-8")
        result = self.invoke(["--dot", source], "shared chain 499")
        if result is not None:
            nodes = re.findall(rb"^  n\d+ \[kind=", result.stdout, re.MULTILINE)
            edges = re.findall(rb"^  n\d+ -> n\d+ ", result.stdout, re.MULTILINE)
            self.report("shared chain 499: bounded DOT, 1504 nodes, 2499 edges",
                        successful_dot(result) and len(nodes) == 1504
                        and len(edges) == 2499)

        result = self.invoke([source], "shared chain 499 (tree)")
        if result is not None:
            ids = re.findall(rb"\[#(\d+)\]$", result.stdout, re.MULTILINE)
            references = re.findall("↪ #".encode() + rb"(\d+) ", result.stdout)
            self.report("shared chain 499: bounded tree, unique nodes and shared references",
                        successful_tree(result) and len(ids) == len(set(ids)) == 1504
                        and len(references) == 996 and set(references) <= set(ids)
                        and b"depth=" in result.stdout)


def depth_source(kind, size):
    total = "+".join(["1"] * size)
    if kind == "index":
        statement = "r = a" + "[0]" * size
    elif kind == "update":
        statement = "a" + "[0]" * size + " = 1"
    elif kind == "update_index":
        statement = f"a[{total}] = 1"
    elif kind == "update_value":
        statement = "a[0] = " + total
    elif kind == "calls":
        statement = "r = " + "g(" * size + "1" + ")" * size
    elif kind == "mixed":
        statement = f"r = ({total})" + "+1" * size
    elif kind == "wide":
        statement = "r = g(" + ",".join(["1"] * size) + ")"
    elif kind == "implies":
        statement = "assert " + " -> ".join(["true"] * size)
    else:
        statement = "r = " + total
    if kind in {"blocks", "block_sum"}:
        statement = "{" * size + statement + ";" + "}" * size
    else:
        statement += ";"
    return "f(a: int[]) returns r: int { " + statement + " }"


def main(argv=None, root=ROOT):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("examples", nargs="?", type=Path, default=Path("tests/examples"))
    parser.add_argument("--api-example", type=Path)
    args = parser.parse_args(argv)
    try:
        examples = args.examples if args.examples.is_absolute() else root / args.examples
        rows = load_examples(root, examples)
        runner = Runner(args.binary.resolve(), root, os.environ.get("UPDATE") == "1")
        cases = sorted((root / "tests/cases").glob("*.funny"))
        if not cases:
            raise ValueError("no snapshot cases")
        for source in cases:
            runner.case(source)
        with tempfile.TemporaryDirectory(prefix="hw2-tests-") as folder:
            generated = Path(folder) / "input.funny"
            runner.cli(Path(folder) / "missing.funny")
            runner.examples(examples, rows)
            runner.depths(generated)
        if args.api_example is not None:
            runner.api_example(args.api_example.resolve())
        print(f"{runner.passed} passed, {runner.failed} failed")
        return int(runner.failed != 0)
    except (OSError, ValueError) as error:
        print(f"runner: error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
