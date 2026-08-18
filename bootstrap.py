#!/usr/bin/env python3
"""Replace the template identity after creating a repository from this template."""

import argparse
import os
import re
import stat
import sys
import tempfile
from pathlib import Path
from typing import Dict, Iterable


ROOT = Path(__file__).resolve().parent
TEMPLATE_PROJECT = "FieldPack"
TEMPLATE_NAMESPACE = "fieldpack"
TEMPLATE_REPOSITORY = "dannylong-math/FieldPack"
TEMPLATE_DOCS_PACKAGE = "fieldpack-docs"
TEMPLATE_DESCRIPTION = "Utilities to manage efficient data layout of parameter packs"


def default_namespace(project_name: str) -> str:
    """Convert a CMake-style project name to a reasonable C++ namespace."""
    value = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", "_", project_name)
    value = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", value)
    value = re.sub(r"[^A-Za-z0-9_]+", "_", value).strip("_").lower()
    return value


def docs_package_name(repository: str) -> str:
    repository_name = repository.split("/", maxsplit=1)[1].lower()
    repository_name = re.sub(r"[^a-z0-9._-]+", "-", repository_name)
    repository_name = repository_name.lstrip("._") or "project"
    return f"{repository_name}-docs"


def validate_arguments(args: argparse.Namespace) -> None:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.+-]*", args.project_name):
        raise ValueError(
            "--project-name must start with a letter or digit and contain only "
            "letters, digits, underscores, dots, plus signs, or hyphens"
        )
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", args.namespace):
        raise ValueError("--namespace must be a valid C++ identifier")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repository):
        raise ValueError("--repository must have the form OWNER/REPOSITORY")
    if not args.description or any(ord(character) < 32 for character in args.description):
        raise ValueError("--description must be non-empty and fit on one line")


def escaped_description(description: str) -> str:
    return description.replace("\\", "\\\\").replace('"', '\\"')


def replace_tokens(contents: str, replacements: Dict[str, str]) -> str:
    for old, new in replacements.items():
        contents = contents.replace(old, new)
    return contents


def write_atomic(path: Path, contents: str) -> None:
    original_mode = stat.S_IMODE(path.stat().st_mode)
    temporary_name = ""
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".bootstrap.tmp",
            delete=False,
        ) as temporary:
            temporary.write(contents)
            temporary_name = temporary.name
        os.chmod(temporary_name, original_mode)
        os.replace(temporary_name, path)
    finally:
        if temporary_name and os.path.exists(temporary_name):
            os.unlink(temporary_name)


def candidate_files(header_path: Path) -> Iterable[Path]:
    return (
        ROOT / "CMakeLists.txt",
        ROOT / "Doxyfile",
        ROOT / "README.md",
        ROOT / "docs" / "package.json",
        ROOT / "docs" / "package-lock.json",
        ROOT / "docs" / "sourcey.config.ts",
        ROOT / "docs" / "introduction.md",
        ROOT / "benchmark" / "example_benchmark.cpp",
        ROOT / "tests" / "test_library.cpp",
        header_path,
        ROOT / "bootstrap.py",
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Rename the C++ template project, namespace, documentation, and badges."
    )
    parser.add_argument("--project-name", required=True, help="CMake project name, for example VectorMath")
    parser.add_argument(
        "--namespace",
        help="C++ namespace and include directory (defaults to snake_case project name)",
    )
    parser.add_argument("--repository", required=True, help="GitHub repository as OWNER/REPOSITORY")
    parser.add_argument(
        "--description",
        default=TEMPLATE_DESCRIPTION,
        help=f'one-line project description (default: "{TEMPLATE_DESCRIPTION}")',
    )
    parser.add_argument("--dry-run", action="store_true", help="show changes without writing them")
    args = parser.parse_args()
    if args.namespace is None:
        args.namespace = default_namespace(args.project_name)
    return args


def main() -> int:
    args = parse_arguments()
    try:
        validate_arguments(args)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    source_header_directory = ROOT / "include" / TEMPLATE_NAMESPACE
    destination_header_directory = ROOT / "include" / args.namespace
    renamed_header_directory = False

    if source_header_directory != destination_header_directory:
        if source_header_directory.exists() and destination_header_directory.exists():
            print(
                f"error: refusing to overwrite existing directory: {destination_header_directory}",
                file=sys.stderr,
            )
            return 2
        if source_header_directory.exists():
            print(
                f"rename: {source_header_directory.relative_to(ROOT)} -> "
                f"{destination_header_directory.relative_to(ROOT)}"
            )
            renamed_header_directory = True
            if not args.dry_run:
                source_header_directory.rename(destination_header_directory)

    header_path = (
        destination_header_directory / "example.hpp"
        if destination_header_directory.exists() and not args.dry_run
        else source_header_directory / "example.hpp"
    )

    replacements = {
        TEMPLATE_REPOSITORY: args.repository,
        TEMPLATE_DOCS_PACKAGE: docs_package_name(args.repository),
        TEMPLATE_PROJECT: args.project_name,
        TEMPLATE_NAMESPACE: args.namespace,
        TEMPLATE_DESCRIPTION: escaped_description(args.description),
    }

    changed_files = []
    for path in candidate_files(header_path):
        if not path.is_file():
            continue
        with path.open("r", encoding="utf-8", newline="") as source:
            original = source.read()
        updated = replace_tokens(original, replacements)
        if updated == original:
            continue
        changed_files.append(path.relative_to(ROOT))
        print(f"update: {path.relative_to(ROOT)}")
        if not args.dry_run:
            write_atomic(path, updated)

    if not changed_files and not renamed_header_directory:
        print("No template placeholders remain; nothing to change.")
    elif args.dry_run:
        print("Dry run complete; no files were changed.")
    else:
        print("Bootstrap complete. Review the changes and run the build, tests, benchmarks, and docs.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
