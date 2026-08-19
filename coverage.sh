#!/usr/bin/env bash
set -euo pipefail

readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly BUILD_DIR="${REPO_ROOT}/build/coverage-local"
readonly TOOLS_DIR="${REPO_ROOT}/build/coverage-tools"
readonly GCOVR_VERSION="8.6"
readonly PYTHON_BIN="${PYTHON:-python3}"
readonly CLANG_BIN="${CC:-clang}"
readonly CLANGXX_BIN="${CXX:-clang++}"

if [[ -L "${BUILD_DIR}" || -L "${TOOLS_DIR}" ]]; then
    echo "error: coverage build and tools directories must not be symbolic links" >&2
    exit 1
fi

if [[ ! -x "${TOOLS_DIR}/bin/python" ]] ||
    ! "${TOOLS_DIR}/bin/python" -c \
        "import importlib.metadata; raise SystemExit(importlib.metadata.version('gcovr') != '${GCOVR_VERSION}')" \
        2>/dev/null; then
    echo "Setting up gcovr ${GCOVR_VERSION} in ${TOOLS_DIR#"${REPO_ROOT}/"}..."
    "${PYTHON_BIN}" -m venv "${TOOLS_DIR}"
    "${TOOLS_DIR}/bin/python" -m pip install --quiet "gcovr==${GCOVR_VERSION}"
fi

cmake \
    -S "${REPO_ROOT}" \
    -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER="${CLANG_BIN}" \
    -DCMAKE_CXX_COMPILER="${CLANGXX_BIN}" \
    -DBUILD_TESTING=ON \
    -DBUILD_BENCHMARKS=OFF \
    -DENABLE_SANITIZERS=OFF \
    -DENABLE_COVERAGE=ON

# Coverage counters accumulate between executions. Remove only counters from
# this script's fixed, validated build tree before running the tests again.
find "${BUILD_DIR}" -type f -name '*.gcda' -delete

cmake --build "${BUILD_DIR}" --parallel 2
ctest --test-dir "${BUILD_DIR}" --output-on-failure

readonly LLVM_COV="$(${CLANG_BIN} -print-prog-name=llvm-cov)"
readonly GCOVR="${TOOLS_DIR}/bin/gcovr"

echo
echo "Line coverage"
"${GCOVR}" \
    --root "${REPO_ROOT}" \
    --filter "${REPO_ROOT}/include/" \
    --exclude "${REPO_ROOT}/build/" \
    --gcov-executable "${LLVM_COV} gcov" \
    --txt-metric line \
    --txt-summary \
    "${BUILD_DIR}"

echo
echo "Branch coverage"
"${GCOVR}" \
    --root "${REPO_ROOT}" \
    --filter "${REPO_ROOT}/include/" \
    --exclude "${REPO_ROOT}/build/" \
    --gcov-executable "${LLVM_COV} gcov" \
    --txt-metric branch \
    --txt-summary \
    --fail-under-line 100 \
    --fail-under-branch 100 \
    "${BUILD_DIR}"
