#!/usr/bin/env python3
"""Normalize compiler optimization records and render a local HTML report."""

from __future__ import annotations

import argparse
import ast
import gzip
import html
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Iterable, Sequence


class ReportError(RuntimeError):
    """An optimization record could not be interpreted safely."""


@dataclass(frozen=True, slots=True)
class Remark:
    """Compiler-independent representation of one optimization remark."""

    file: str
    line: int
    column: int
    function: str
    compiler: str
    optimization_pass: str
    message: str
    result: str
    occurrences: int = 1


@dataclass(frozen=True, slots=True)
class ReportFilters:
    """Optional exact/substring filters applied before rendering."""

    compiler: str = ""
    file: str = ""
    function: str = ""
    optimization_pass: str = ""
    result: str = ""


_CLANG_DOCUMENT = re.compile(r"(?m)^---(?:\s+!(?P<tag>[A-Za-z]+))?\s*$")
_SCALAR = r"(?P<value>[^\n]+)"


def _yaml_scalar(value: str) -> str:
    value = value.strip()
    if not value:
        return ""
    if value[0] in "'\"" and value[-1:] == value[0]:
        if value[0] == "'":
            return value[1:-1].replace("''", "'")
        try:
            parsed = ast.literal_eval(value)
            return str(parsed)
        except (SyntaxError, ValueError):
            return value[1:-1]
    return value


def _clang_field(document: str, name: str) -> str:
    match = re.search(rf"(?m)^\s*{re.escape(name)}:\s*{_SCALAR}$", document)
    return _yaml_scalar(match.group("value")) if match else ""


def _positive_int(value: object) -> int:
    try:
        return max(0, int(value))
    except (TypeError, ValueError):
        return 0


def _clang_debug_location(document: str) -> tuple[str, int, int]:
    inline = re.search(r"(?ms)^\s*DebugLoc:\s*\{(?P<body>[^}]*)\}", document)
    if inline:
        body = inline.group("body")
        values = {
            match.group("key"): _yaml_scalar(match.group("value"))
            for match in re.finditer(
                r"(?P<key>File|Line|Column):\s*(?P<value>.*?)(?:,|$)", body
            )
        }
        return (
            values.get("File", ""),
            _positive_int(values.get("Line", 0)),
            _positive_int(values.get("Column", 0)),
        )

    block = re.search(
        r"(?ms)^\s*DebugLoc:\s*\n(?P<body>(?:\s{2,}[^\n]*\n?)*)", document
    )
    body = block.group("body") if block else ""
    return (
        _clang_field(body, "File"),
        _positive_int(_clang_field(body, "Line")),
        _positive_int(_clang_field(body, "Column")),
    )


def _clang_message(document: str, fallback: str) -> str:
    args = re.search(r"(?ms)^Args:\s*\n(?P<body>.*?)(?=^[A-Za-z][A-Za-z0-9_]*:|\Z)", document)
    if not args:
        return _clang_field(document, "Message") or fallback
    values = []
    for match in re.finditer(r"(?m)^\s*-\s+[A-Za-z][A-Za-z0-9_]*:\s*(?P<value>.*)$", args.group("body")):
        values.append(_yaml_scalar(match.group("value")))
    message = "".join(values).strip()
    return message or fallback


def parse_clang_yaml(text: str, compiler: str = "clang") -> list[Remark]:
    """Parse the stable subset shared by serialized Clang YAML remarks."""

    if not text.strip():
        return []
    starts = list(_CLANG_DOCUMENT.finditer(text))
    if not starts:
        raise ReportError("Clang optimization record has no YAML documents")

    remarks: list[Remark] = []
    for index, start in enumerate(starts):
        stop = starts[index + 1].start() if index + 1 < len(starts) else len(text)
        document = text[start.end() : stop].replace("\n...", "\n")
        tag = (start.group("tag") or "Analysis").lower()
        result = {"passed": "passed", "missed": "missed", "analysis": "analysis"}.get(tag)
        if result is None:
            continue
        file_name, line, column = _clang_debug_location(document)
        optimization_pass = _clang_field(document, "Pass")
        function = _clang_field(document, "Function")
        name = _clang_field(document, "Name")
        message = _clang_message(document, name)
        if file_name or optimization_pass or function or message:
            remarks.append(
                Remark(
                    file=file_name,
                    line=line,
                    column=column,
                    function=function,
                    compiler=compiler,
                    optimization_pass=optimization_pass,
                    message=message,
                    result=result,
                )
            )
    return remarks


def _flatten_gcc_message(value: object) -> str:
    if isinstance(value, str):
        return value
    if isinstance(value, (int, float)):
        return str(value)
    if isinstance(value, list):
        return "".join(_flatten_gcc_message(item) for item in value)
    if isinstance(value, dict):
        preferred = ("text", "expr", "value", "message", "stmt")
        selected = [value[key] for key in preferred if key in value]
        return "".join(_flatten_gcc_message(item) for item in selected)
    return ""


def _gcc_result(kind: object) -> str:
    normalized = str(kind).lower()
    if normalized in {"success", "optimized", "passed"}:
        return "passed"
    if normalized in {"failure", "missed"}:
        return "missed"
    return "analysis"


def parse_gcc_json(data: object, compiler: str = "gcc") -> list[Remark]:
    """Parse GCC's experimental JSON records while ignoring unknown fields."""

    if not isinstance(data, list):
        raise ReportError(
            "GCC optimization record changed: expected a top-level JSON array; "
            f"found {type(data).__name__}"
        )
    def records(value: object) -> Iterable[dict[str, object]]:
        if isinstance(value, list):
            for item in value:
                yield from records(item)
        elif isinstance(value, dict) and any(
            key in value for key in ("kind", "message", "location", "pass", "function")
        ):
            yield value

    remarks: list[Remark] = []
    for record in records(data):
        if not isinstance(record, dict):
            continue
        location = record.get("location", {})
        if not isinstance(location, dict):
            location = {}
        message = _flatten_gcc_message(record.get("message", "")).strip()
        remarks.append(
            Remark(
                file=str(location.get("file", record.get("file", ""))),
                line=_positive_int(location.get("line", record.get("line", 0))),
                column=_positive_int(location.get("column", record.get("column", 0))),
                function=str(record.get("function", record.get("function_name", ""))),
                compiler=compiler,
                optimization_pass=str(record.get("pass", record.get("opt_pass", ""))),
                message=message,
                result=_gcc_result(record.get("kind", record.get("result", "analysis"))),
            )
        )
    return remarks


def read_record(path: Path, compiler: str = "") -> list[Remark]:
    """Read one supported record, reporting malformed/truncated input clearly."""

    inferred = compiler.lower()
    if not inferred:
        inferred = "gcc" if path.name.endswith((".json.gz", ".json")) else "clang"
    try:
        if inferred == "gcc":
            if path.name.endswith(".gz"):
                with gzip.open(path, "rt", encoding="utf-8") as stream:
                    data = json.load(stream)
            else:
                data = json.loads(path.read_text(encoding="utf-8"))
            return parse_gcc_json(data)
        return parse_clang_yaml(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ReportError(f"cannot parse optimization record {path}: {error}") from error


def _inside_root(path: Path, root: Path) -> bool:
    try:
        path.resolve(strict=False).relative_to(root.resolve(strict=False))
        return True
    except ValueError:
        return False


def retain_project_remarks(remarks: Iterable[Remark], source_root: Path) -> list[Remark]:
    """Keep project records and rewrite their paths relative to the source root."""

    kept = []
    root = source_root.resolve(strict=False)
    for remark in remarks:
        if not remark.file:
            continue
        candidate = Path(remark.file)
        if not candidate.is_absolute():
            candidate = root / candidate
        if not _inside_root(candidate, root):
            continue
        kept.append(replace(remark, file=candidate.resolve(strict=False).relative_to(root).as_posix()))
    return kept


def group_duplicates(remarks: Iterable[Remark]) -> list[Remark]:
    """Collapse equal instantiation remarks while preserving occurrence counts."""

    counts = Counter(replace(remark, occurrences=1) for remark in remarks)
    return [replace(remark, occurrences=count) for remark, count in sorted(counts.items(), key=lambda item: _sort_key(item[0]))]


def _sort_key(remark: Remark) -> tuple[object, ...]:
    return (remark.file, remark.line, remark.column, remark.function, remark.optimization_pass, remark.result, remark.message)


def filter_remarks(remarks: Iterable[Remark], filters: ReportFilters) -> list[Remark]:
    """Apply report filters; text fields use case-insensitive substrings."""

    def contains(value: str, query: str) -> bool:
        return not query or query.casefold() in value.casefold()

    return [
        remark
        for remark in remarks
        if contains(remark.compiler, filters.compiler)
        and contains(remark.file, filters.file)
        and contains(remark.function, filters.function)
        and contains(remark.optimization_pass, filters.optimization_pass)
        and (not filters.result or remark.result == filters.result)
    ]


def _source_group(file_name: str) -> str:
    if file_name.startswith("include/"):
        return "library"
    if file_name.startswith("benchmark/"):
        return "benchmark"
    return "project"


def _safe_page_name(file_name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", file_name) + ".html"


def _page(title: str, body: str) -> str:
    style = """
body{font-family:Inter,ui-sans-serif,system-ui,sans-serif;margin:0;color:#173f3a;background:#f7faf9}
main{max-width:112rem;margin:auto;padding:2rem}a{color:#087f78}code{font-family:ui-monospace,monospace}
table{border-collapse:collapse;width:100%;background:white}th,td{padding:.55rem;border:1px solid #d5e2df;text-align:left;vertical-align:top}
th{background:#e9f3f1}.passed{color:#16734a}.missed{color:#a13d2d}.analysis{color:#745c15}
.summary{display:flex;gap:1rem;flex-wrap:wrap}.card{background:white;border:1px solid #d5e2df;padding:1rem;min-width:10rem}
label{display:inline-flex;flex-direction:column;margin:0 .6rem .8rem 0}input,select{padding:.35rem;min-width:10rem}
.source{font-family:ui-monospace,monospace;white-space:pre}.line{display:grid;grid-template-columns:5rem 1fr}.number{color:#71817e;text-align:right;padding-right:1rem}
.remark{white-space:normal;background:#fff7db;border-left:.25rem solid #c18b00;padding:.35rem .7rem;margin:.2rem 0 .6rem 5rem}
"""
    return f"<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>{html.escape(title)}</title><style>{style}</style></head><body><main>{body}</main></body></html>\n"


def _render_source(source_root: Path, file_name: str, remarks: Sequence[Remark]) -> str:
    path = source_root / file_name
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        lines = []
    by_line: dict[int, list[Remark]] = {}
    for remark in remarks:
        by_line.setdefault(remark.line, []).append(remark)
    content = [f"<h1>{html.escape(file_name)}</h1><p><a href=\"index.html\">Back to summary</a></p><div class=\"source\">"]
    for remark in by_line.get(0, []):
        content.append(
            f'<div class="remark {remark.result}"><strong>{html.escape(remark.result)}</strong> '
            f'[{html.escape(remark.compiler)} / {html.escape(remark.optimization_pass)}] '
            f'{html.escape(remark.message)} <small>({remark.occurrences} occurrence(s), no source line)</small></div>'
        )
    if not lines:
        content.append("<p>Source text was unavailable.</p>")
    for number, line in enumerate(lines, 1):
        content.append(f'<div class="line" id="L{number}"><span class="number">{number}</span><span>{html.escape(line)}</span></div>')
        for remark in by_line.get(number, []):
            content.append(
                f'<div class="remark {remark.result}"><strong>{html.escape(remark.result)}</strong> '
                f'[{html.escape(remark.compiler)} / {html.escape(remark.optimization_pass)}] '
                f'{html.escape(remark.message)} <small>({remark.occurrences} occurrence(s))</small></div>'
            )
    content.append("</div>")
    return _page(file_name, "".join(content))


def render_report(
    remarks: Sequence[Remark],
    source_root: Path,
    output_dir: Path,
    *,
    compiler_version: str = "",
    command_line: str = "",
) -> Path:
    """Write an index and annotated source pages, including a valid empty report."""

    output_dir.mkdir(parents=True, exist_ok=True)
    for file_name in sorted({remark.file for remark in remarks}):
        file_remarks = [remark for remark in remarks if remark.file == file_name]
        (output_dir / _safe_page_name(file_name)).write_text(
            _render_source(source_root, file_name, file_remarks), encoding="utf-8"
        )

    counts = Counter(remark.result for remark in remarks)
    cards = "".join(
        f'<div class="card {category}"><strong>{category.title()}</strong><br>{counts[category]}</div>'
        for category in ("passed", "missed", "analysis")
    )
    metadata = (
        f"<p><strong>Compiler:</strong> <code>{html.escape(compiler_version)}</code><br>"
        f"<strong>Command:</strong> <code>{html.escape(command_line)}</code></p>"
    )
    controls = """
<div id="filters"><label>File<input data-column="file"></label><label>Kernel/function<input data-column="function"></label>
<label>Compiler<input data-column="compiler"></label><label>Pass<input data-column="pass"></label>
<label>Result<select data-column="result"><option value="">all</option><option>passed</option><option>missed</option><option>analysis</option></select></label></div>
"""
    rows = []
    for remark in remarks:
        source_link = f'{_safe_page_name(remark.file)}#L{remark.line}'
        values = {
            "file": remark.file,
            "function": remark.function,
            "compiler": remark.compiler,
            "pass": remark.optimization_pass,
            "result": remark.result,
        }
        attrs = " ".join(f'data-{key}="{html.escape(value, quote=True)}"' for key, value in values.items())
        rows.append(
            f'<tr {attrs}><td>{html.escape(_source_group(remark.file))}</td>'
            f'<td><a href="{html.escape(source_link, quote=True)}">{html.escape(remark.file)}:{remark.line}:{remark.column}</a></td>'
            f'<td>{html.escape(remark.function)}</td><td>{html.escape(remark.compiler)}</td>'
            f'<td>{html.escape(remark.optimization_pass)}</td><td class="{remark.result}">{html.escape(remark.result)}</td>'
            f'<td>{html.escape(remark.message)}</td><td>{remark.occurrences}</td></tr>'
        )
    empty = '<p id="empty">No relevant optimization remarks were found.</p>' if not rows else ""
    table = "<table><thead><tr><th>Area</th><th>Location</th><th>Function</th><th>Compiler</th><th>Pass</th><th>Result</th><th>Message</th><th>Count</th></tr></thead><tbody>" + "".join(rows) + "</tbody></table>"
    script = """
<script>const controls=[...document.querySelectorAll('[data-column]')];const rows=[...document.querySelectorAll('tbody tr')];
function apply(){for(const row of rows){row.hidden=controls.some(c=>c.value&&!row.dataset[c.dataset.column].toLowerCase().includes(c.value.toLowerCase()));}}
for(const control of controls){control.addEventListener('input',apply);}</script>
"""
    index = output_dir / "index.html"
    index.write_text(
        _page("FieldPack optimization report", f"<h1>FieldPack optimization report</h1><div class=\"summary\">{cards}</div>{metadata}{controls}{empty}{table}{script}"),
        encoding="utf-8",
    )
    return index


def discover_records(paths: Sequence[Path], directories: Sequence[Path]) -> list[Path]:
    discovered = list(paths)
    suffixes = (".opt.yaml", ".opt.yml", ".opt-record.json.gz", ".opt-record.json")
    for directory in directories:
        if directory.exists():
            discovered.extend(path for path in directory.rglob("*") if path.is_file() and path.name.endswith(suffixes))
    return sorted(set(path.resolve() for path in discovered))


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", action="append", type=Path, default=[])
    parser.add_argument("--input-dir", action="append", type=Path, default=[])
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--compiler-version", default="")
    parser.add_argument("--command", default="")
    parser.add_argument("--filter-compiler", default="")
    parser.add_argument("--filter-file", default="")
    parser.add_argument("--filter-function", default="")
    parser.add_argument("--filter-pass", default="")
    parser.add_argument("--filter-result", choices=("", "passed", "missed", "analysis"), default="")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        records = discover_records(args.input, args.input_dir)
        remarks = [remark for path in records for remark in read_record(path)]
        remarks = retain_project_remarks(remarks, args.source_root)
        remarks = filter_remarks(
            remarks,
            ReportFilters(
                compiler=args.filter_compiler,
                file=args.filter_file,
                function=args.filter_function,
                optimization_pass=args.filter_pass,
                result=args.filter_result,
            ),
        )
        remarks = group_duplicates(remarks)
        report = render_report(
            remarks,
            args.source_root,
            args.output,
            compiler_version=args.compiler_version,
            command_line=args.command,
        )
    except ReportError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    print(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
