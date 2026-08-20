import { defineConfig, doxygen, markdown } from "sourcey";

export default defineConfig({
  name: "FieldPack",
  repo: "https://github.com/dannylong-math/FieldPack",
  editBranch: "main",
  prettyUrls: "slash",
  theme: {
    colors: {
      primary: "#0F766E",
      light: "#5EEAD4",
      dark: "#115E59",
    },
    fonts: {
      sans: "Inter",
    },
    layout: {
      content: "112rem",
    },
    css: ["./custom.css"],
  },
  navigation: {
    tabs: [
      {
        tab: "Guides",
        slug: "",
        source: markdown({
          groups: [
            {
              group: "Getting Started",
              pages: ["introduction", "benchmarks", "optimization-reports"],
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
