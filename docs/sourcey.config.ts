import { defineConfig, doxygen, markdown } from "sourcey";

export default defineConfig({
  name: "MyProject",
  repo: "https://github.com/dannylong-math/cpp-template",
  editBranch: "main",
  navigation: {
    tabs: [
      {
        tab: "Guide",
        slug: "",
        source: markdown({
          groups: [{ group: "Getting Started", pages: ["introduction"] }],
        }),
      },
      {
        tab: "C++ API",
        slug: "api",
        source: doxygen({
          xml: "../build/doxygen/xml",
          language: "cpp",
        }),
      },
    ],
  },
});
