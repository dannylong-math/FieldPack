---
title: Introduction
description: API documentation and guides for MyProject.
---

# MyProject

MyProject is a C++23 header-only library created from the cpp-template
repository. Replace this page with installation instructions, examples, design
notes, and other project guides.

The **C++ API** tab is generated from the Doxygen comments in `include/`.

## Build locally

From the repository root, generate the Doxygen XML and then build the Sourcey
site:

```shell
cmake -E make_directory build/doxygen
doxygen Doxyfile
npm ci --prefix docs
npm run --prefix docs build
```

The static site is written to `docs/dist/`.
