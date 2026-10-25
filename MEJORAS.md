# Cambios sobre el original (tossaponk/HitWhereYouAim @ master, v1.1)

## Nuevo en 1.2.0: bloqueo de objetivo (`src/TargetLock.h`)
- Al pulsar **V** (configurable con `iTargetLockKey`) se fija el actor más cercano a la mira y el centro de la pantalla lo sigue en cada fotograma, aunque se mueva el jugador o el objetivo. Otra pulsación lo suelta.
- Usa el mismo selector de objetivos del mod: prioriza hostiles, descarta seguidores y exige línea de visión.
- Apunta al torso (hueso `NPC Spine2`) o, en criaturas, al centro de su volumen.
- Se suelta solo si el objetivo muere, se aleja más de `fTargetLockMaxDistance` o queda fuera de vista más de `fTargetLockLoseSightTime` segundos. También al cambiar de celda o cargar partida.
- Funciona en primera y tercera persona, también con la cámara libre de tercera persona. No actúa en VATS, kill cams ni muebles.
- Con TrueHUD muestra la barra de vida del objetivo fijado. Muestra un aviso en el HUD (desactivable con `bTargetLockNotification`).

## Errores que podían cerrar el juego
- **Falta de `settings.ini` = cierre al arrancar.** `Settings::Load` llamaba a `REX::FAIL` si no existía `Data/MCM/Config/HitWhereYouAim/settings.ini`, y el repo no traía ese archivo. Ahora se aplican valores por defecto internos y solo se registra un aviso.
- **Valores por defecto nunca aplicados.** Si faltaba una clave en el INI, el ajuste quedaba en `false`/`0` (por ejemplo `bEnabled`). Nuevo `Settings::ApplyDefaults()`.
- **Punteros nulos:**
  - `GetCurrentAmmo()` sin munición en `PlayerGetAimPitch`.
  - `GetMagicCaster()` / `GetMagicNode()` en hechizos de concentración (llamas, rayos).
  - `currentProcess` / `GetWeaponNode()` al disparar montado.
  - Estado de cámara y `WorldRootCamera()` nulos en carga o transiciones.
  - `UI` / cámara / renderer en las utilidades del retículo y del raycast.
- `ClampVectorFromOrigin` dividía entre la longitud antes de comprobar que fuera distinta de cero.

## Comportamiento
- Hechizos de concentración: el pitch calculado se invalida en cada fotograma, así `SetAimPitch` nunca reutiliza un ángulo viejo.
- Pose del arco en tercera persona: al dejar de apuntar, `currentPitch` se reinicia al pitch actual del juego y no a 0. La pose ya no salta a horizontal al empezar a tensar.
- La estela de TrueHUD no registra nada cuando está desactivada o cuando TrueHUD no está instalado.

## Compilación y empaquetado
- Se incluye el submódulo `extern/CommonLibSSE` (commit `add2531`).
- `SKSE_SUPPORT_XBYAK=ON`: `PCH.h` usa `Trampoline::allocate(Xbyak::CodeGenerator&)`, que CommonLib solo declara con esa opción activa.
- El preset `windows-vcpkg` (el que usa `--preset release`) ahora compila solo SE+AE. Antes activaba VR por defecto y exigía el submódulo openvr.
- `COPY_BUILD` añadía `/SKSE/Plugins/` a rutas que, según el README, ya lo incluían.
- Después de cada compilación se arma un paquete listo para instalar en `build/package/<Config>/Data` (DLL + `settings.ini` por defecto en `dist/`).
- Nuevo workflow de GitHub Actions (`.github/workflows/build.yml`) que compila y publica el paquete como artifact.
- La versión pasa a 1.1.0, igual que el último commit upstream.
