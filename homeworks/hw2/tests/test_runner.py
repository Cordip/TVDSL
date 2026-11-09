"""Runner failures must fail the suite without blessing broken snapshots."""

from contextlib import redirect_stderr, redirect_stdout
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import run as runner


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        (self.root / "tests/examples").mkdir(parents=True)
        (self.root / "tests/cases").mkdir()
        self.manifest = self.root / "tests/examples.expected"

    def entrypoint(self):
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            status = runner.main([sys.executable], root=self.root)
        return status, output.getvalue(), errors.getvalue()

    def test_missing_manifest_fails_before_tests(self):
        status, output, errors = self.entrypoint()
        self.assertEqual(status, 1)
        self.assertIn("cannot read", errors)
        self.assertIn("examples.expected", errors)
        self.assertNotIn("passed", output)

    def test_unreadable_manifest_fails_before_tests(self):
        self.manifest.write_text("example.funny parses\n")
        with mock.patch.object(Path, "read_text", side_effect=PermissionError("denied")):
            status, output, errors = self.entrypoint()
        self.assertEqual(status, 1)
        self.assertIn("examples.expected", errors)
        self.assertIn("denied", errors)
        self.assertNotIn("passed", output)

    def test_missing_example_fails(self):
        self.manifest.write_text("example.funny parses\n")
        status, _, errors = self.entrypoint()
        self.assertEqual(status, 1)
        self.assertIn("missing examples: example.funny", errors)

    def test_unclassified_example_fails(self):
        examples = self.root / "tests/examples"
        (examples / "example.funny").write_text("")
        (examples / "extra.funny").write_text("")
        self.manifest.write_text("example.funny parses\n")
        status, _, errors = self.entrypoint()
        self.assertEqual(status, 1)
        self.assertIn("unclassified examples: extra.funny", errors)

    def test_invalid_manifest_rows_fail(self):
        (self.root / "tests/examples/example.funny").write_text("")
        for content, message in (
            ("", "empty example list"),
            ("example.funny other\n", "invalid example row"),
            ("example.funny\n", "invalid example row"),
            ("example.funny parses\nexample.funny parses\n", "duplicate example"),
        ):
            with self.subTest(content=content):
                self.manifest.write_text(content)
                status, _, errors = self.entrypoint()
                self.assertEqual(status, 1)
                self.assertIn(message, errors)

    def fake_binary(self, status, errors):
        path = self.root / "fake.py"
        path.write_text(
            f"#!{sys.executable}\nimport sys\n"
            f"sys.stderr.write({errors!r})\nsys.exit({status})\n"
        )
        path.chmod(0o755)
        return path

    def test_update_never_blesses_crash_or_sanitizer_failure(self):
        source = self.root / "tests/cases/new.funny"
        source.write_text("")
        for status, errors in (
            (139, "crashed\n"),
            (1, "tests/cases/new.funny:1:1: error: expected expression\n"
                "ERROR: AddressSanitizer: heap-use-after-free\n"),
        ):
            with self.subTest(status=status, errors=errors):
                binary = self.fake_binary(status, errors)
                suite = runner.Runner(binary, self.root, update=True)
                with redirect_stdout(io.StringIO()):
                    suite.case(source)
                self.assertEqual(suite.failed, 1)
                self.assertFalse(source.with_suffix(".dot").exists())
                self.assertFalse(source.with_suffix(".err").exists())

    def test_update_keeps_a_positive_case_positive(self):
        source = self.root / "tests/cases/positive.funny"
        source.write_text("")
        expected = source.with_suffix(".dot")
        expected.write_bytes(b"original\n")
        binary = self.fake_binary(1, "tests/cases/positive.funny:1:1: error: bad input\n")
        suite = runner.Runner(binary, self.root, update=True)
        with redirect_stdout(io.StringIO()):
            suite.case(source)
        self.assertEqual(suite.failed, 1)
        self.assertEqual(expected.read_bytes(), b"original\n")
        self.assertFalse(source.with_suffix(".err").exists())

    def test_bounded_capture_reads_both_streams(self):
        result = runner.bounded_run(
            [sys.executable, "-c", "import sys; print('out'); print('err', file=sys.stderr)"],
            self.root,
        )
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"out\n")
        self.assertEqual(result.stderr, b"err\n")

    def check_child_cleanup(self, code, exception, **options):
        processes = []
        popen = subprocess.Popen

        def start(*args, **kwargs):
            child = popen(*args, **kwargs)
            processes.append(child)
            return child

        with mock.patch.object(runner.subprocess, "Popen", side_effect=start):
            with self.assertRaises(exception):
                runner.bounded_run([sys.executable, "-c", code], self.root, **options)
        child = processes[0]
        self.assertIsNotNone(child.poll())
        self.assertTrue(child.stdout.closed)
        self.assertTrue(child.stderr.closed)

    def test_output_limit_kills_and_reaps_child(self):
        for stream in ("stdout", "stderr"):
            with self.subTest(stream=stream):
                self.check_child_cleanup(
                    f"import sys,time; sys.{stream}.write('x'*10000); "
                    f"sys.{stream}.flush(); time.sleep(2)",
                    ValueError, limit=1024,
                )

    def test_timeout_kills_and_reaps_child(self):
        self.check_child_cleanup("import time; time.sleep(2)", TimeoutError, timeout=0.05)


if __name__ == "__main__":
    unittest.main()
