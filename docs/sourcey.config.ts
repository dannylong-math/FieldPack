import { defineConfig, doxygen, markdown } from "sourcey";

export default defineConfig({
  name: "FieldPack",
  repo: "https://github.com/dannylong-math/FieldPack",
  editBranch: "main",
  prettyUrls: "slash",
  navigation: {
    tabs: [
      {
        tab: "Guides",
        slug: "",
        source: markdown({
          groups: [
            {
              group: "Getting Started",
              pages: ["introduction"],
            },
          ],
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
