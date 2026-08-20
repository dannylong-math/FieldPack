#!/usr/bin/env python3
"""Install FieldPack and build a clean external find_package consumer."""

from __future__ import annotations

import argparse
import os
import subprocess
import tempfile
from pathlib import Path


def run(command: list[str]) -> None:
    subprocess.run(command, check=True, text=True, capture_output=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", required=True, type=Path)
    parser.add_argument("--project-build", required=True, type=Path)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--generator", required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="fieldpack-consumer-") as temporary:
        root = Path(temporary)
        prefix = root / "install"
        build = root / "build"
        run([args.cmake, "--install", str(args.project_build), "--prefix", str(prefix)])

        expected_headers = (
            "execution.hpp",
            "fieldpack.hpp",
            "layout.hpp",
            "schema.hpp",
            "table.hpp",
        )
        include = prefix / "include/fieldpack"
        for header in expected_headers:
            assert (include / header).is_file(), f"installed header missing: {header}"

        run(
            [
                args.cmake,
                "-S",
                str(args.repository / "tests/consumer"),
                "-B",
                str(build),
                "-G",
                args.generator,
                f"-DCMAKE_CXX_COMPILER={args.compiler}",
                f"-DCMAKE_PREFIX_PATH={prefix}",
            ]
        )
        run([args.cmake, "--build", str(build), "--parallel", "2"])
        executable = build / "fieldpack_installed_consumer"
        if os.name == "nt":
            executable = build / "Debug/fieldpack_installed_consumer.exe"
        run([str(executable)])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
