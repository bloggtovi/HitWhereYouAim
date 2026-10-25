# Cambios sobre el original (tossaponk/HitWhereYouAim @ master, v1.1)

## Nuevo en 1.2.0: bloqueo de objetivo (`src/TargetLock.h`)
- Al pulsar el **clic izquierdo** la mira salta **al instante** sobre el adversario más cercano a ella, en el mismo fotograma del clic, así que sirve con clic rápido o sostenido. Mientras se mantiene, la mira sigue al objetivo aunque se mueva el jugador o el objetivo. Al soltar, se libera. El clic sigue atacando con normalidad, porque el evento no se consume.
- Configurable: `iTargetLockKey` (tecla), `iTargetLockMode` (1 = mantener, 0 = alternar) y `fTargetLockRotationSpeed` (0 = instantáneo, >0 = giro suave).
- Usa el mismo selector de objetivos del mod: prioriza hostiles, descarta seguidores y exige línea de visión.
- Apunta al torso (hueso `NPC Spine2`) o, en criaturas, al centro de su volumen.
- Se suelta solo si el objetivo muere, se aleja más de `fTargetLockMaxDistance` o queda fuera de vista más de `fTargetLockLoseSightTime` segundos. También al cambiar de celda o cargar partida.
- Funciona en primera y tercera persona, también con la cámara libre de tercera persona. No actúa en VATS, kill cams ni muebles.
- Con TrueHUD muestra la barra de vida del objetivo fijado. Los avisos en el HUD están desactivados por defecto (`bTargetLockNotification`) para no saturar la pantalla con cada clic.

## Compatibilidad con otros mods
- **Hooks no fatales:** el original llamaba a `REX::FAIL`, que cierra el juego, cuando no encontraba la instrucción a modificar porque otro mod ya la había parcheado. Ahora ese hook se omite con un aviso en el log y el resto del mod sigue funcionando.
- **Clic robusto:** el bloqueo lee el estado del botón directamente del dispositivo de entrada del juego en cada fotograma, además de escuchar los eventos. Si otro mod consume el evento del clic, el bloqueo sigue funcionando.
- **Menús:** se ignoran los clics en cualquier menú que muestre el cursor o pause el juego. Esto cubre también los menús que SkyrimSouls RE deja sin pausa.
- **TrueHUD opcional:** si otro mod ya controla la barra de objetivo, el bloqueo funciona igual, solo que sin mostrarla.

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
