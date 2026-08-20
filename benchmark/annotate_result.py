#!/usr/bin/env python3
"""Add FieldPack build and host metadata to a Google Benchmark JSON result."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from typing import Final


CONTEXT_KEYS: Final = (
    "preset",
    "build_type",
    "compiler_id",
    "compiler_version",
    "compiler_path",
    "compiler_flags",
    "host_system_processor",
    "chunk_extent",
    "field_types",
    "result_name_schema",
)


def parse_arguments() -> argparse.Namespace:
    """Parse paths and run-specific metadata supplied by the shell runner."""
    parser = argparse.ArgumentParser()
    parser.add_argument("result", type=Path)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("run_label")
    parser.add_argument("cpu_model")
    return parser.parse_args()


def read_metadata(path: Path) -> dict[str, str]:
    """Read trusted configure-time key/value data without evaluating shell."""
    metadata: dict[str, str] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        key, separator, value = line.partition("=")
        if not separator or not key:
            raise ValueError(f"{path}:{line_number}: expected key=value metadata")
        metadata[key] = value

    missing = [key for key in CONTEXT_KEYS if key not in metadata]
    if missing:
        raise ValueError(f"{path}: missing metadata keys: {', '.join(missing)}")
    return metadata


def main() -> None:
    """Atomically augment one valid Google Benchmark JSON result."""
    arguments = parse_arguments()
    metadata = read_metadata(arguments.metadata)
    document = json.loads(arguments.result.read_text(encoding="utf-8"))
    context = document.get("context")
    if not isinstance(context, dict):
        raise ValueError(f"{arguments.result}: missing JSON context object")

    context.update({key: metadata[key] for key in CONTEXT_KEYS})
    context["run_label"] = arguments.run_label
    context["cpu_model"] = arguments.cpu_model

    temporary = arguments.result.with_suffix(arguments.result.suffix + ".tmp")
    temporary.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, arguments.result)


if __name__ == "__main__":
    main()
