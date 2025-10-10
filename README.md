# RayVox-Engine
 
## Build

### Requirements
Make sure you have the following installed before building:
- [CMake](https://cmake.org/) ≥ 3.19
- [Ninja](https://ninja-build.org/) (required for building with the presets)
- Visual Studio 2022 with "Desktop development with C++" workload 

#### Using CMake Presets
This project includes a `CMakePresets.json` file, so you can build it easily:

```bash
cmake --preset debug
cmake --build --preset debug
```
Or with the CMake Tools extension if you're using vscode