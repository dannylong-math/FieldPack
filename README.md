# MyProject

[![Unit tests](https://github.com/dannylong-math/cpp-template/actions/workflows/ci.yml/badge.svg?branch=main&event=push)](https://github.com/dannylong-math/cpp-template/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/dannylong-math/cpp-template/graph/badge.svg)](https://codecov.io/gh/dannylong-math/cpp-template)
[![Documentation](https://github.com/dannylong-math/cpp-template/actions/workflows/docs.yml/badge.svg?branch=main&event=push)](https://github.com/dannylong-math/cpp-template/actions/workflows/docs.yml)

A C++23 header-only library template with Boost.UT tests, Google Benchmark,
GCC/Clang CI, Codecov coverage, and Doxygen/Sourcey documentation published to
GitHub Pages.

The badges show the latest `main` results. Each pull request shows its own
formatting, GCC, Clang, coverage, and documentation results in the GitHub checks
panel.

## Start a project from this template

1. Click **Use this template** on GitHub and create the new repository.
2. Preview the automatic rename. The namespace defaults to a snake-case version
   of the project name and can be overridden with `--namespace`:

   ```shell
   ./bootstrap.py \
     --project-name VectorMath \
     --repository YOUR-ORG/vector-math \
     --description "Automatic differentiation utilities" \
     --dry-run
   ```

3. Remove `--dry-run` and run the command again. The script updates CMake,
   Doxygen, Sourcey, npm metadata, badges, namespaces, includes, and the example
   header directory. Repeating the same command is safe and makes no changes.
4. Replace or remove the example header, unit test, and benchmark.
5. Decide which compiler entries to keep in `.github/workflows/ci.yml`. For a
   Clang-only project (including many Enzyme projects), delete the GCC object
   from the `matrix.include` list; no other workflow changes are needed.
6. Add the repository to [Codecov](https://app.codecov.io/), copy its upload
   token, and create an Actions repository secret named `CODECOV_TOKEN` under
   **Settings → Secrets and variables → Actions**. For Dependabot PRs, also add
   it as a Dependabot secret.
7. Under **Settings → Pages**, select **GitHub Actions** as the Pages source.
   The documentation workflow deploys after a PR is merged to `main` (and after
   any other push to `main`).
8. Under **Settings → Actions → General → Workflow permissions**, select
   **Read and write permissions** so the formatting workflow can push its
   automatic commit to repository-owned PR branches.
9. Under **Settings → Branches → Branch protection rules**, protect `main` and
   require `Unit tests (GCC)`, `Unit tests (Clang)`, `Coverage`,
   `Build documentation`, and `Apply clang-format`. Only require unit-test
   checks for compilers retained in the matrix. Coverage uses Clang, so it
   continues to work unchanged in an Enzyme/Clang-only project.
10. Review `.clang-format`, `.clang-tidy`, `format.sh`, warning flags, sanitizer
    settings, and the optimization flags in `CMakePresets.json` for the new
    project.
11. Run the local workflows below and open the first PR.

## Build and test

Requirements are CMake 3.21 or newer, a C++23 compiler, Git, and Make. The
debug preset enables AddressSanitizer and UndefinedBehaviorSanitizer.

```shell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Available configure/build presets are `debug`, `debug-tidy`, `release`,
`release-max`, and `benchmark`.

## Formatting

Run the formatter locally with:

```shell
./format.sh
```

Every PR to `main` is formatted with clang-format 18. For branches in this
repository, the workflow commits and pushes any changes as
`style: apply clang-format`, then dispatches fresh formatting, test/coverage,
and documentation runs for the new commit. GitHub does not let the base
repository token push to contributor forks, so an unformatted fork PR fails
with instructions to run `./format.sh` locally. Requiring the
`Apply clang-format` check therefore prevents unformatted code from merging in
either case.

## Benchmarks

Benchmarks are disabled by default so a normal configure does not fetch Google
Benchmark. Enabling `BUILD_BENCHMARKS` downloads the pinned Google Benchmark
release. Every `.cpp` file under `benchmark/` is discovered recursively and
built as a separate executable.

```shell
cmake --preset benchmark
cmake --build --preset benchmark
./build/benchmark/benchmark/benchmark_example_benchmark
```

For trustworthy timings, use a release build, avoid running on a busy machine,
and consider fixing CPU frequency and affinity for serious measurements.

## Documentation

Requirements are Doxygen and Node.js 22.12 or newer. Doxygen parses headers under
`include/` into XML; Sourcey turns that XML and the Markdown guides in `docs/`
into a static site.

```shell
cmake -E make_directory build/doxygen
doxygen Doxyfile
npm ci --prefix docs
npm run --prefix docs build
```

The result is written to `docs/dist/`. For live editing, generate the XML and
then run `npm run --prefix docs dev`.

## CMake options

| Option | Default | Purpose |
| --- | ---: | --- |
| `BUILD_TESTING` | `ON` | Fetch Boost.UT and build all tests under `tests/`. |
| `BUILD_BENCHMARKS` | `OFF` | Fetch Google Benchmark and build all benchmarks. |
| `ENABLE_SANITIZERS` | `OFF` | Enable AddressSanitizer and UndefinedBehaviorSanitizer. |
| `ENABLE_CLANG_TIDY` | `OFF` | Run clang-tidy during compilation. |
| `ENABLE_COVERAGE` | `OFF` | Add GCC/Clang coverage instrumentation for tests. |

Each `.cpp` file under `tests/` is also discovered recursively and registered
as an independent CTest executable.
