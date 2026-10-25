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

Pressing the **left mouse button** snaps the screen center instantly onto the standing adversary nearest the crosshair on screen (dead, bleeding out or knocked down actors are never picked), in the same frame as the click, so a quick click or a held attack, bow draw or spell goes straight at it. While the button is held the crosshair stays on the target, whether you or the target move; releasing the button releases the lock after `fTargetLockReleaseDelay` seconds, so the arrow fired on release still goes at the target. While locked, arrows and bolts aim at the locked adversary's torso even if the crosshair ray first touches a body lying in front of it. The click still attacks as usual.

Set `iTargetLockMode = 0` for toggle mode (one press locks, the next releases), `iTargetLockKey` to use another key, and `fTargetLockRotationSpeed` above 0 for a smooth turn instead of a snap. The lock also releases when the target dies, goes beyond `fTargetLockMaxDistance`, or stays out of sight longer than `fTargetLockLoseSightTime`. With TrueHUD installed, the locked actor's health bar is shown.

## Compatibility

- If another plugin has already patched one of the engine call sites this plugin hooks, that single hook is skipped with a warning in `HitWhereYouAim.log` instead of aborting the game. Everything else keeps working.
- Target lock reads the key from the game's own input device every frame in addition to listening for input events, so it still works when another plugin consumes the button event first.
- Clicks inside any menu that shows the mouse cursor are ignored, including menus kept unpaused by mods such as SkyrimSouls RE.
- TrueHUD is optional. If another mod already owns the TrueHUD target bar, the lock still works without showing it.

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
