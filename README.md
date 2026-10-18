# Hit Where You Aim

Makes arrows, bolts and spells go toward the point under the crosshair.

## Prerequisites

- Windows x64
- Visual Studio 2022 with C++ desktop tools
- CMake 3.21 or newer
- vcpkg in `VCPKG_ROOT`
- Git
- CommonLibSSE-NG

## Initialize

```powershell
git submodule update --init --recursive
$env:VCPKG_ROOT = "D:/Tools/vcpkg"
```

## Build

```powershell
cmake --preset vs2022-se-ae
cmake --build --preset release
```

## Optional deployment

Set `SkyrimPluginTargets` to a semicolon-separated list of `SKSE/Plugins` directories, then enable `COPY_BUILD`:

```powershell
$env:SkyrimPluginTargets = "D:/Games/Skyrim Special Edition/Data/SKSE/Plugins;D:/Games/Skyrim VR/Data/SKSE/Plugins"
cmake --preset windows-vcpkg -DCOPY_BUILD=ON
cmake --build --preset release
```

## License

Hit Where You Aim is licensed under GPL-3.0-or-later with the Modding Exception and GPL-3.0 Linking Exception (with Corresponding Source).

See `COPYING` and `EXCEPTIONS` for the complete terms. Third-party components retain their original licenses and copyright notices.
