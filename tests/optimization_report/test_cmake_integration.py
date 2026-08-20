#!/usr/bin/env python3
"""CMake smoke test for opt-in compiler optimization-report capture."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path


def configure(
    cmake: str,
    repository: Path,
    build: Path,
    generator: str,
    compiler: str,
    build_type: str,
    enabled: bool,
) -> list[dict[str, str]]:
    command = [
        cmake,
        "-S",
        str(repository),
        "-B",
        str(build),
        "-G",
        generator,
        f"-DCMAKE_CXX_COMPILER={compiler}",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        "-DBUILD_TESTING=OFF",
        f"-DBUILD_EXAMPLES={'OFF' if enabled else 'ON'}",
        "-DBUILD_BENCHMARKS=OFF",
        f"-DENABLE_OPTIMIZATION_REPORTS={'ON' if enabled else 'OFF'}",
    ]
    subprocess.run(command, check=True, text=True, capture_output=True)
    return json.loads((build / "compile_commands.json").read_text())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", required=True, type=Path)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--generator", required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="fieldpack-report-cmake-") as temporary:
        root = Path(temporary)
        for name, build_type in (("debug", "Debug"), ("release", "Release")):
            commands = configure(
                args.cmake,
                args.repository,
                root / name,
                args.generator,
                args.compiler,
                build_type,
                False,
            )
            joined = "\n".join(entry["command"] for entry in commands)
            assert "fsave-optimization-record" not in joined
            assert "Rpass" not in joined

        report_build = root / "report"
        commands = configure(
            args.cmake,
            args.repository,
            report_build,
            args.generator,
            args.compiler,
            "Release",
            True,
        )
        joined = "\n".join(entry["command"] for entry in commands)
        assert "fsave-optimization-record" in joined
        subprocess.run(
            [args.cmake, "--build", str(report_build), "--target", "optimization-report", "--parallel", "2"],
            check=True,
            text=True,
            capture_output=True,
        )
        records = list(report_build.rglob("*.opt.yaml")) + list(report_build.rglob("*.opt-record.json.gz"))
        assert records, "the compiler advertised report support but produced no record"
        index = report_build / "report/index.html"
        content = index.read_text()
        assert "benchmark/benchmark_support.hpp" in content
        compiler_name = Path(args.compiler).name.lower()
        if "clang" in compiler_name:
            assert "loop-vectorize" in content
        else:
            assert "gcc" in content
        assert "passed" in content and "missed" in content and "analysis" in content
        assert "polynomial" in content and "drift" in content and "field_subset" in content
        assert "deliberately_inhibited" in content
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
