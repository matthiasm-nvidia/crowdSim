# Third-party sources

Vendored and committed so a fresh clone builds after `setup_deps.bat` (which only downloads the ovstage and ovrtx SDKs).

| Directory | Origin | License |
|-----------|--------|---------|
| `glfw/` | GLFW 3.4.0, https://github.com/glfw/glfw (CI/editor metadata removed) | `glfw/LICENSE.md` |
| `imgui/` | Dear ImGui snapshot reporting `1.52 WIP`, trimmed to the files used (`imgui.cpp`, `imgui_draw.cpp`, GLFW backend, stb headers); exact upstream commit unknown | `imgui/LICENSE.txt` |

The Boost.Polygon Voronoi headers used by the ECM library live with it in `ecm/third_party/boost_voronoi/` (see its `README.md`), so `ecm/` can be copied or added to another project on its own.

`resources/fonts/` contains Noto Sans (license in `resources/fonts/LICENSE`).
