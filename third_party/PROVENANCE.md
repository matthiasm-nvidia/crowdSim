# Third-party sources

Vendored and committed so a fresh clone builds after `setup_deps.bat` (which only downloads the ovstage and ovrtx SDKs).

| Directory | Origin | License |
|-----------|--------|---------|
| `glfw/` | GLFW 3.4.0, https://github.com/glfw/glfw (CI/editor metadata removed) | `glfw/LICENSE.md` |
| `imgui/` | Dear ImGui snapshot reporting `1.52 WIP`, trimmed to the files used (`imgui.cpp`, `imgui_draw.cpp`, GLFW backend, stb headers); exact upstream commit unknown | `imgui/LICENSE.txt` |
| `boost_voronoi/` | Seven unmodified Boost.Polygon Voronoi headers from tag `boost-1.88.0`, plus a local `boost/cstdint.hpp` shim | `boost_voronoi/LICENSE_1_0.txt` |

`resources/fonts/` contains Noto Sans (license in `resources/fonts/LICENSE`).
