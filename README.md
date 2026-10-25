# Hit Where You Aim

Makes arrows, bolts and spells go toward the point under the crosshair.

## Requirements (in game)

- Skyrim SE (1.5.97) or AE (1.6+)
- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- Optional: [TrueHUD](https://www.nexusmods.com/skyrimspecialedition/mods/62775) for the debug overlays (projectile path, crosshair impact, target prediction)
- Optional: MCM Helper / SkyUI for in-game configuration

## Installation

Copy the contents of the build package (`build/package/Release/Data`, or the `HitWhereYouAim` artifact from the GitHub Actions run) into the game's `Data` folder, or install it as a mod with your mod manager:

```
Data/SKSE/Plugins/HitWhereYouAim.dll
Data/MCM/Config/HitWhereYouAim/settings.ini
```

## Target lock

Press **V** (configurable with `iTargetLockKey` in `[TargetLock]`) to lock the camera onto the actor nearest the crosshair. The screen center then follows the target every frame, whether you or the target move, so shots fired while locked go straight at it. Press the key again to release. The lock also releases when the target dies, goes beyond `fTargetLockMaxDistance`, or stays out of sight longer than `fTargetLockLoseSightTime`. With TrueHUD installed, the locked actor's health bar is shown.

Settings are read from `Data/MCM/Config/HitWhereYouAim/settings.ini` and then overridden by `Data/MCM/Settings/HitWhereYouAim.ini` if present. If neither file exists the plugin falls back to built-in defaults instead of aborting the game.

## Building

### Prerequisites

- Windows x64
- Visual Studio 2022 with C++ desktop tools
- CMake 3.21 or newer
- vcpkg in `VCPKG_ROOT`
- Git

### Initialize

```powershell
git submodule update --init --recursive
$env:VCPKG_ROOT = "D:/Tools/vcpkg"
```

### Build

```powershell
cmake --preset windows-vcpkg
cmake --build --preset release
```

`windows-vcpkg` targets SE and AE. Use `vs2022-all` to include VR (requires the `extern/CommonLibSSE/extern/openvr` submodule).

The DLL is written to `build/Release`, and a ready-to-install layout is assembled in `build/package/Release/Data`.

### Continuous integration

`.github/workflows/build.yml` builds the plugin on every push and uploads the mod package and PDB as workflow artifacts, so a local toolchain is not required.

### Optional deployment

Set `SkyrimPluginTargets` to a semicolon-separated list of `SKSE/Plugins` directories, then enable `COPY_BUILD`:

```powershell
$env:SkyrimPluginTargets = "D:/Games/Skyrim Special Edition/Data/SKSE/Plugins;D:/Games/Skyrim VR/Data/SKSE/Plugins"
cmake --preset windows-vcpkg -DCOPY_BUILD=ON
cmake --build --preset release
```

## License

Hit Where You Aim is licensed under GPL-3.0-or-later with the Modding Exception and GPL-3.0 Linking Exception (with Corresponding Source).

See `COPYING` and `EXCEPTIONS` for the complete terms. Third-party components retain their original licenses and copyright notices.
