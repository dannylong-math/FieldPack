#!/usr/bin/env python3
"""Ensure the README quickstart is exactly the example compiled by CMake."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", required=True, type=Path)
    args = parser.parse_args()

    readme = (args.repository / "README.md").read_text()
    section = readme.split("## Using FieldPack", 1)[1].split("## Tiles, chunks, and tails", 1)[0]
    match = re.search(r"```cpp\n(?P<source>.*?)\n```", section, re.DOTALL)
    assert match, "README Using FieldPack section has no C++ block"
    documented = match.group("source").strip()
    compiled = (args.repository / "examples/documented_quickstart.cpp").read_text().strip()
    assert documented == compiled, "README quickstart and compiled documented_quickstart.cpp differ"
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
