# Compiler optimization reports

FieldPack can turn Clang or GCC optimization records into a local HTML report.
The workflow is opt-in: normal debug, release, test, and benchmark builds never
receive report flags.

## Generate a report

For Clang:

```shell
cmake --preset optimization-report-clang
cmake --build --preset optimization-report-clang --target optimization-report
```

For GCC:

```shell
cmake --preset optimization-report-gcc
cmake --build --preset optimization-report-gcc --target optimization-report
```

The target prints the generated index path. It is normally
`build/optimization-report-clang/report/index.html` or the corresponding GCC
path. Open that file directly in a browser; the report has no web server or
external asset requirement.

Each preset has its own binary directory. This prevents cached compiler
selection and one compiler's records from contaminating the other report. The
configuration probes the machine-readable record flag and stops with a clear
error if the selected compiler does not advertise support.

## What is captured

The report workload instantiates polynomial evaluation, two-dimensional drift,
and a two-hot-field subset update for both SoA and `aosoa<64>`. Its size is 65,
so each traversal contains full chunks and a tail. A separate data-dependent
loop is deliberately difficult to vectorize, ensuring that a working compiler
produces both successful and missed feedback.

Clang capture is limited to loop vectorization, SLP vectorization, and inlining
records. GCC capture uses its compressed machine-readable optimization record
and also saves human-readable vectorization diagnostics. Raw records and HTML
are generated below `build/` and are not committed.

## Reading the report

The summary counts passed, missed, and analysis remarks. The table can be
filtered by source file, kernel/function, compiler, optimization pass, and
result. Its **Area** column separates public library headers, benchmark kernel
code, and other project support code. Records outside the FieldPack source tree
are excluded by default, keeping dependency implementation details out of the
initial view.

Selecting a location opens an annotated source page with remarks displayed
after the relevant line. Duplicate template-instantiation remarks are grouped,
and the **Count** column preserves how many occurrences were emitted.

For the representative kernels, useful starting filters are:

- Pass `loop-vectorize`, to see whether a full-chunk or tail loop vectorized.
- Result `missed`, to identify dependence, alias, cost-model, or control-flow
  explanations.
- Pass `inline`, to see whether field/tag accessors disappeared into kernels.
- Function text `soa` or `aosoa`, to compare layout instantiations compiled by
  the same target and flags.

Clang's serialized remarks are documented in the
[Clang Compiler User's Manual](https://clang.llvm.org/docs/UsersManual.html#options-to-emit-optimization-reports).
GCC describes its optimization records and diagnostics under
[Developer Options](https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html).
GCC labels the JSON format experimental, so FieldPack's parser ignores unknown
record keys but reports an actionable error if the top-level structure changes.
