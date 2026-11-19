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

- `src/viewer/` viewer, `ecm/` ECM and medial-axis library and tests
- `scenes/` example scenes; `warehouseNoRoof.usd` (221 MB) is stored with Git LFS
- `resources/` fonts copied next to the executable
- `third_party/` vendored sources, see `third_party/PROVENANCE.md`
