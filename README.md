# crowdSim

A Windows USD viewer (ovstage, ovrtx, GLFW, ImGui) with an Explicit Corridor Map (ECM) / medial-axis navigation prototype. The ECM method is described in `references/ECM.pdf`.

## Requirements

Windows x64, Visual Studio with **Desktop development with C++** and **C++ CMake tools for Windows**, Git LFS, and an NVIDIA RTX GPU/driver compatible with the pinned ovrtx SDK.

## Build and run

```bat
git lfs install
git clone https://github.com/matthiasm-nvidia/crowdSim.git
cd crowdSim
setup_deps.bat
build.bat
run.bat
```

- `setup_deps.bat` downloads the ovstage and ovrtx SDKs pinned in `dependencies.json`, verifies their SHA-256 hashes and installs them under `deps/` (not committed). `-CacheDirectory <dir>` reuses already-downloaded ZIPs.
- `build.bat` finds Visual Studio, and builds Debug and Release with its bundled CMake and Ninja.
- `run.bat` starts the Release viewer (`build\Release\crowdSim_viewer.exe`); arguments are forwarded.

The build records absolute paths, so after moving or renaming the checkout delete `build/` and run `build.bat` again.

## Layout

- `ecm/` ECM and medial-axis library and its tests. Plain C++17 with no USD or SDK dependency; its Voronoi headers are vendored in `ecm/third_party/`.
- `ecm_ovstage/` adapter between ovstage stages and `ecm`: scene triangles in, overlay layers out, plus small ovstage helpers. Uses ovstage only, never ovrtx.
- `src/viewer/` the RTX viewer (GLFW, ImGui, ovrtx)
- `scenes/` example scenes; `warehouseNoRoof.usd` (221 MB) is stored with Git LFS
- `resources/` fonts copied next to the executable
- `third_party/` vendored sources, see `third_party/PROVENANCE.md`

## Using the libraries in another project

`ecm` and `ecm_ovstage` are meant to be reused, typically by adding this repository as a git submodule and the libraries with `add_subdirectory`:

```cmake
find_package(ovstage CONFIG REQUIRED)    # the host's own ovstage SDK
add_subdirectory(crowdSim/ecm_ovstage)   # also adds crowdSim/ecm unless ecm::ecm already exists
target_link_libraries(myApp PRIVATE ecm_ovstage::ecm_ovstage)
```

- Projects without USD add only `crowdSim/ecm` and link `ecm::ecm`.
- `ecm_ovstage` links `ovstage::ovstage_static` by default. Set `ECM_OVSTAGE_TARGET` to the ovstage target your app links, so there is one copy of the SDK.
- The host owns the stage. Adapter functions read or write at an ordinal the host passes in; the host then seals that ordinal, computes the hierarchy and updates its renderer, as `src/viewer/RtxView.cpp` does.
- The libraries start no threads and print nothing; errors are thrown as exceptions.
- `ecm` builds on its own on any platform: `cmake -S ecm -B build-ecm`, which also builds its tests (`ECM_BUILD_TESTS`). In this repository, `-DCROWDSIM_BUILD_VIEWER=OFF` builds only the portable libraries, without the SDKs.
