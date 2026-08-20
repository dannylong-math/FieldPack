#!/usr/bin/env bash
set -euo pipefail

usage()
{
    echo "Usage: $0 <preset> <baseline-label> <contender-label> <kernel>" >&2
    echo "       $0 layouts <preset> <run-label> <kernel> <baseline-regex> <contender-regex>" >&2
    echo "Kernels: polynomial, drift, field_subset, read_only_reduction" >&2
}

mode=runs
baseline_filter=""
contender_filter=""
if [[ ${1:-} == layouts ]]; then
    if [[ $# -ne 6 ]]; then
        usage
        exit 2
    fi
    mode=layouts
    preset=$2
    baseline_label=$3
    contender_label=$3
    kernel=$4
    baseline_filter=$5
    contender_filter=$6
else
    if [[ $# -ne 4 ]]; then
        usage
        exit 2
    fi
    preset=$1
    baseline_label=$2
    contender_label=$3
    kernel=$4
fi

case "$preset" in
    benchmark-portable|benchmark-x86-64-native|benchmark) ;;
    *)
        echo "error: unsupported benchmark preset: $preset" >&2
        usage
        exit 2
        ;;
esac

case "$kernel" in
    polynomial|drift|field_subset|read_only_reduction) ;;
    *)
        echo "error: unsupported benchmark kernel: $kernel" >&2
        usage
        exit 2
        ;;
esac

for label in "$baseline_label" "$contender_label"; do
    if [[ ! "$label" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
        echo "error: result labels must use only letters, digits, dots, underscores, and hyphens" >&2
        exit 2
    fi
done

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$repo_root/build/$preset"
metadata_file="$build_dir/benchmark/benchmark_metadata.txt"
baseline_json="$build_dir/results/$baseline_label/$kernel.json"
contender_json="$build_dir/results/$contender_label/$kernel.json"

if [[ ! -f "$metadata_file" ]]; then
    echo "error: benchmark metadata not found: $metadata_file" >&2
    exit 1
fi
if [[ ! -f "$baseline_json" ]]; then
    echo "error: baseline result not found: $baseline_json" >&2
    exit 1
fi
if [[ ! -f "$contender_json" ]]; then
    echo "error: contender result not found: $contender_json" >&2
    exit 1
fi

benchmark_source_dir=$(sed -n 's/^google_benchmark_source_dir=//p' "$metadata_file")
compare_tool="$benchmark_source_dir/tools/compare.py"
requirements="$benchmark_source_dir/tools/requirements.txt"

if [[ ! -f "$compare_tool" ]]; then
    echo "error: Google Benchmark comparison tool not found: $compare_tool" >&2
    exit 1
fi

if ! python3 -c 'import numpy; import scipy' >/dev/null 2>&1; then
    echo "error: Google Benchmark comparison dependencies are not installed." >&2
    echo "Install them in a virtual environment:" >&2
    echo "  python3 -m venv .venv-benchmark" >&2
    echo "  .venv-benchmark/bin/python -m pip install -r '$requirements'" >&2
    echo "  source .venv-benchmark/bin/activate" >&2
    echo "Then rerun this script." >&2
    exit 1
fi

if [[ $mode == layouts ]]; then
    python3 "$compare_tool" filters "$baseline_json" "$baseline_filter" "$contender_filter"
else
    python3 "$compare_tool" benchmarks "$baseline_json" "$contender_json"
fi
