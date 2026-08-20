#!/usr/bin/env python3
"""Unit and integration tests for the optimization-report converter."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "tools"))

from optimization_report import (  # noqa: E402
    Remark,
    ReportError,
    ReportFilters,
    filter_remarks,
    group_duplicates,
    parse_clang_yaml,
    parse_gcc_json,
    read_record,
    render_report,
    retain_project_remarks,
)


class OptimizationReportTests(unittest.TestCase):
    fixtures = Path(__file__).parent / "fixtures"

    def test_clang_preserves_fields_categories_and_unknown_keys(self) -> None:
        remarks = parse_clang_yaml((self.fixtures / "clang.opt.yaml").read_text())
        self.assertEqual([remark.result for remark in remarks], ["passed", "missed", "analysis", "passed"])
        self.assertEqual(remarks[0].file, "include/fieldpack/execution.hpp")
        self.assertEqual((remarks[0].line, remarks[0].column), (217, 9))
        self.assertEqual(remarks[0].function, "_ZN9fieldpack14for_each_chunkEv")
        self.assertEqual(remarks[0].optimization_pass, "loop-vectorize")
        self.assertIn("vectorization width: 4", remarks[0].message)

    def test_gcc_compressed_fixture_and_unknown_keys(self) -> None:
        remarks = read_record(self.fixtures / "gcc.opt-record.json.gz")
        self.assertEqual([remark.result for remark in remarks], ["passed", "missed", "analysis"])
        self.assertEqual(remarks[0].compiler, "gcc")
        self.assertEqual(remarks[0].message, "loop vectorized using 16 byte vectors")

    def test_empty_and_malformed_records(self) -> None:
        self.assertEqual(parse_clang_yaml("  \n"), [])
        self.assertEqual(parse_gcc_json([]), [])
        with self.assertRaisesRegex(ReportError, "top-level JSON array"):
            parse_gcc_json({"format": "new"})
        with self.assertRaisesRegex(ReportError, "no YAML documents"):
            parse_clang_yaml("Pass: loop-vectorize")
        with tempfile.TemporaryDirectory() as directory:
            truncated = Path(directory) / "broken.opt-record.json.gz"
            truncated.write_bytes(b"not gzip")
            with self.assertRaisesRegex(ReportError, "cannot parse"):
                read_record(truncated)

    def test_root_filter_deduplication_and_filters(self) -> None:
        clang = parse_clang_yaml((self.fixtures / "clang.opt.yaml").read_text())
        kept = retain_project_remarks(clang, REPOSITORY)
        self.assertEqual(len(kept), 3)
        grouped = group_duplicates(kept)
        self.assertEqual(len(grouped), 2)
        self.assertEqual(grouped[1].occurrences, 2)
        self.assertEqual(len(filter_remarks(grouped, ReportFilters(result="missed"))), 1)
        self.assertEqual(len(filter_remarks(grouped, ReportFilters(file="EXECUTION", compiler="CLANG"))), 1)
        self.assertEqual(len(filter_remarks(grouped, ReportFilters(function="drift", optimization_pass="vector"))), 1)

    def test_renderer_escapes_text_and_generates_empty_report(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source_root = Path(directory) / "source"
            source_root.mkdir()
            (source_root / "escaped.cpp").write_text('const char* value = "<source>&";\n', encoding="utf-8")
            malicious = Remark(
                file="escaped.cpp",
                line=1,
                column=2,
                function="function<script>",
                compiler="clang",
                optimization_pass="loop<vectorize>",
                message="missed <script>alert('x')</script> & reason",
                result="missed",
            )
            output = Path(directory) / "report"
            index = render_report(
                [malicious], source_root, output, compiler_version="clang <22>", command_line="c++ < source"
            )
            content = index.read_text()
            self.assertIn("&lt;script&gt;", content)
            self.assertNotIn("<script>alert", content)
            source = (output / "escaped.cpp.html").read_text()
            self.assertIn("&lt;source&gt;&amp;", source)
            self.assertNotIn("<source>", source)
            empty = render_report([], REPOSITORY, Path(directory) / "empty")
            self.assertIn("No relevant optimization remarks", empty.read_text())
            self.assertIn("<!doctype html>", empty.read_text())

    def test_command_line_end_to_end(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "html"
            completed = subprocess.run(
                [
                    sys.executable,
                    str(REPOSITORY / "tools/optimization_report.py"),
                    "--input",
                    str(self.fixtures / "clang.opt.yaml"),
                    "--source-root",
                    str(REPOSITORY),
                    "--output",
                    str(output),
                    "--filter-result",
                    "passed",
                ],
                check=True,
                text=True,
                capture_output=True,
            )
            self.assertEqual(Path(completed.stdout.strip()), output / "index.html")
            self.assertTrue((output / "index.html").is_file())


if __name__ == "__main__":
    unittest.main()
