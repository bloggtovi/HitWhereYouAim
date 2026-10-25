# Modifications to Hit Where You Aim

This is a modified version of [tossaponk/HitWhereYouAim](https://github.com/tossaponk/HitWhereYouAim)
(GPL-3.0-or-later with the original exceptions). Changes made on 2026-10-25:

- `src/MultiplayerGuard.h` (new): an actor that is not the local player but uses the Player NPC base
  (0x7) is treated as a remote human player. Such actors are skipped by target depth prediction
  (`GetPotentialPlayerTarget`) and never used for target leading. This rule is not verified in-game.
- `src/Telemetry.h` (new): optional, read-only telemetry. When `[Debug] bEnableTelemetry=1`, launch and
  impact data of the player's projectiles is published to the shared-memory ring buffer
  `Local\HitWhereYouAim.Telemetry` (format v1). It does not change projectile behavior. Off by default.
- `src/Hooks.cpp`, `src/Hooks.h`, `src/Utils.h`, `src/Settings.h`, `src/Settings.cpp`: hook points for the two items above.
- `CMakeLists.txt`: version 1.0.1, new headers listed.
- `.github/workflows/build.yml` (new): Windows build with GitHub Actions.
