import { defineConfig, doxygen } from "sourcey";

export default defineConfig({
  name: "FieldPack",
  repo: "https://github.com/dannylong-math/FieldPack",
  editBranch: "main",
  navigation: {
    tabs: [
      {
        tab: "C++ API",
        slug: "",
        source: doxygen({
          xml: "../build/doxygen/xml",
          language: "cpp",
        }),
      },
    ],
  },
});
