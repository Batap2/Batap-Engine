# Batap Engine
 
## Build

### Requirements
Make sure you have the following installed before building:
- [CMake](https://cmake.org/) ≥ 3.19
- [Ninja](https://ninja-build.org/) (required for building with the presets)
- Visual Studio 2022 with "Desktop development with C++" workload 

#### Using CMake Presets
This project includes a `CMakePresets.json` file, so you can build it easily:

```bat
build_msvc.bat <preset-name> [--configure]
```

- `<preset-name>` : name of the CMake preset to use (as defined in `CMakePresets.json`)
  - `msvc-release`
  - `msvc-debug`
  - `msvc-relwithdebinfo`
  - `msvc-debug-asan`
- `--configure` : Forces a CMake reconfiguration before building. Use this on the first build or after changes to CMake files or source layout.

VSCode users: build tasks are already defined in the `.vscode` folder.