#!/usr/bin/env bash
set -euo pipefail

usage()
{
    echo "Usage: $0 <preset> <run-label> [Google Benchmark options...]" >&2
    echo "Presets: benchmark-portable, benchmark-x86-64-native, benchmark" >&2
}

if [[ $# -lt 2 ]]; then
    usage
    exit 2
fi

preset=$1
run_label=$2
shift 2

case "$preset" in
    benchmark-portable|benchmark-x86-64-native|benchmark) ;;
    *)
        echo "error: unsupported benchmark preset: $preset" >&2
        usage
        exit 2
        ;;
esac

if [[ ! "$run_label" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
    echo "error: run label must use only letters, digits, dots, underscores, and hyphens" >&2
    exit 2
fi

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$repo_root/build/$preset"
metadata_file="$build_dir/benchmark/benchmark_metadata.txt"
result_dir="$build_dir/results/$run_label"
annotator="$repo_root/benchmark/annotate_result.py"

if [[ ! -f "$metadata_file" ]]; then
    echo "error: benchmark metadata not found: $metadata_file" >&2
    echo "Configure and build the '$preset' preset first." >&2
    exit 1
fi
if [[ ! -f "$annotator" ]] || ! command -v python3 >/dev/null 2>&1; then
    echo "error: Python 3 and the benchmark result annotator are required" >&2
    exit 1
fi

executables=(
    benchmark_polynomial_evaluation
    benchmark_drift
    benchmark_field_subset
    benchmark_read_only_reduction
)
result_names=(
    polynomial
    drift
    field_subset
    read_only_reduction
)

for executable in "${executables[@]}"; do
    binary="$build_dir/benchmark/$executable"
    if [[ ! -x "$binary" ]]; then
        echo "error: benchmark executable not found: $binary" >&2
        echo "Build the '$preset' preset first." >&2
        exit 1
    fi
done

if [[ -e "$result_dir" ]]; then
    echo "error: result label already exists; refusing to overwrite: $result_dir" >&2
    exit 1
fi

cpu_model=""
if [[ -r /proc/cpuinfo ]]; then
    cpu_model=$(awk -F ': ' '/^model name/{print $2; exit}' /proc/cpuinfo)
elif command -v sysctl >/dev/null 2>&1; then
    cpu_model=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || true)
fi
if [[ -z "$cpu_model" ]]; then
    cpu_model=$(uname -m)
fi

mkdir -p "$result_dir"

for index in "${!executables[@]}"; do
    binary="$build_dir/benchmark/${executables[$index]}"
    output="$result_dir/${result_names[$index]}.json"
    echo "Running ${executables[$index]} -> $output"
    "$binary" \
        "--benchmark_out=$output" \
        --benchmark_out_format=json \
        "$@"
    python3 "$annotator" "$output" "$metadata_file" "$run_label" "$cpu_model"
done

echo "Saved benchmark results to $result_dir"
