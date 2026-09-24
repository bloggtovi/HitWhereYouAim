# Generic Skyrim SKSE Plugin Template

A minimal CMake/vcpkg starter for a native SKSE plugin using CommonLibSSE-NG.

## Create a new mod

1. Rename the repository folder.
2. Configure these cache values in `CMakeUserPresets.json`, on the command line, or in your IDE:
   - `PLUGIN_NAME`
   - `PLUGIN_VERSION`
   - `PLUGIN_AUTHOR`
   - `PLUGIN_DESCRIPTION`
3. Replace the sample code in `src/main.cpp`.
4. Rename and expand `config/plugin.ini` if configuration is needed.

## Prerequisites

- Windows x64
- Visual Studio 2022 with C++ desktop tools
- CMake 3.21 or newer
- vcpkg in `VCPKG_ROOT`
- Git

## Initialize

```powershell
git submodule update --init --recursive
$env:VCPKG_ROOT = "D:/Tools/vcpkg"
```

## Build

```powershell
For all versions
cmake --preset vs2022-all -DPLUGIN_NAME=ExamplePlugin -DPLUGIN_VERSION=0.1.0
Exclude VR
cmake --preset vs2022-se-ae -DPLUGIN_NAME=ExamplePlugin -DPLUGIN_VERSION=0.1.0
cmake --build --preset release
```

## Optional deployment

Set a semicolon-separated list of `SKSE/Plugins` directories, then enable `COPY_BUILD`:

```powershell
$env:SkyrimPluginTargets = "D:/Games/Skyrim Special Edition/Data/SKSE/Plugins;D:/Games/Skyrim VR/Data/SKSE/Plugins"
cmake --preset windows-vcpkg -DCOPY_BUILD=ON
cmake --build --preset release
```