# Cambios sobre el original (tossaponk/HitWhereYouAim @ master, v1.1)

## 1.5.2: nunca contra jugadores
El bloqueo de objetivo, el recálculo al soltar y el guiado ignoran a los personajes controlados por
otros jugadores. Los clientes multijugador (SkyMP) crean a los jugadores remotos como actores
basados en el registro `Player` (0x7) de Skyrim.esm. Cualquier actor distinto del jugador local con
esa base queda excluido en `TargetLock::IsValidTarget()`, por donde pasan todas esas funciones.

## 1.5.1: diagnóstico v3
Vida del objetivo antes y después de cada flecha (impacto confirmado por daño), distancia entre
`data.location` y el modelo 3D de la flecha, comprobación de que el motor sigue la velocidad que
impone el guiado, y motivo exacto por el que se detiene el guiado.

## Nuevo en 1.5.0: guiado en vuelo hasta el impacto
Los datos de la tanda 1.4.0 mostraron dos fuentes de error que ningún disparo calculado al soltar
puede resolver:
- un **desfase constante del punto de salida**, de 0,7-2 m sin importar la distancia (un error de ángulo crecería con la distancia);
- **animales que se desvían** de la trayectoria prevista hasta 5 m durante el vuelo.

Cambios:
- **Guiado en vuelo** (`bTargetLockGuidance`, activado; `GuidanceMath.h` + `ProjectileGuidance.h`): en cada fotograma, hasta el impacto, se recalcula la intercepción desde la posición y la velocidad reales de la flecha hacia la posición y la velocidad actuales del torso, con gravedad, y se gira la flecha hacia esa solución, hasta `fTargetLockGuidanceTurnRate` °/s. Se detiene al impactar, si el objetivo cae o si la flecha ya lo pasó (nunca da la vuelta).
- **La mira se mantiene sobre el objetivo hasta el impacto**, no solo durante el margen de soltar.
- **Pruebas de simulación** (`tests/GuidanceTests.cpp`): 60 fps, gravedad, objetivos en zigzag de hasta 6 m/s, error de salida de hasta 1,5 m y un fotograma de retraso. Se exige impactar el 100 % de 1000 disparos aleatorios. Una simulación independiente en Python dio 300/300 impactos (peor caso: 8 cm del centro) frente a 2,5 m de fallo sin guiado.
- **Límite físico:** el guiado no atraviesa paredes, rocas ni árboles. Si hay un obstáculo sólido entre la flecha y el objetivo, chocará con él.

## Nuevo en 1.4.0: calibración con datos reales
Análisis de 42 disparos registrados en partida. Solo 3 de los 32 fallos con objetivo fijado podían
deberse al cálculo. El factor dominante era la potencia: 18 de 42 disparos salieron por debajo del
35 % (clic rápido), con flechas lentas que tardaban 1-2 s en llegar o que no podían alcanzar el
objetivo.
- **Velocidad completa con objetivo fijado** (`bTargetLockFullSpeed`, activado): la flecha vuela a la velocidad de un arco tensado del todo aunque se suelte enseguida. El daño sigue dependiendo del tensado real.
- **Diagnóstico v2:** veredicto por flecha (HIT / PROBABLE HIT / GRAZE / MISS) y separación del error en balístico (la flecha frente al punto previsto) y desviación del objetivo. La muestra de salida se excluye del ajuste, porque su marca de tiempo no es exacta y sesgaba la gravedad medida.

## Nuevo en 1.3.0: precisión del impacto
- **Recálculo final con valores reales:** el disparo a un objetivo fijado se recalcula en `ProjectileInitHook`, cuando el motor ya fijó la velocidad inicial (que incluye la potencia del tensado y los multiplicadores de arma y perks) y el punto de salida reales. Antes solo se estimaban: con un 5 % de error en la velocidad, a 100 m la flecha cae casi un metro por debajo.
- Al objetivo fijado ya no se le aplican la penetración de 20 cm ni el límite de 100 m del modo normal.
- **Objetivos lejanos:** el selector busca también en la lista de actores lejanos (`middleHighActorHandles`) y su alcance pasa de 60 a 150 m.
- **Visibilidad:** un rayo físico desde la cámara en la capa de línea de visión del motor, sin límite de distancia. La condición `GetLineOfSight` queda como respaldo.
- **Pose del arco instantánea** con objetivo fijado: el brazo apunta al torso sin suavizado, y el punto de salida de la flecha ya no queda atrás.
- **Diagnóstico de vuelo** (`bProjectileDiagnostics`, activado durante la calibración): por cada flecha, `HitWhereYouAim.log` registra la velocidad y la gravedad reales, ajustadas por mínimos cuadrados sobre la trayectoria, la distancia mínima al torso del objetivo y qué impactó.

## Nuevo en 1.2.0: bloqueo de objetivo (`src/TargetLock.h`)
- Al pulsar el **clic izquierdo** la mira salta **al instante** sobre el adversario más cercano a ella, en el mismo fotograma del clic, así que sirve con clic rápido o sostenido. Mientras se mantiene, la mira sigue al objetivo aunque se mueva el jugador o el objetivo. Al soltar, se libera. El clic sigue atacando con normalidad, porque el evento no se consume.
- Configurable: `iTargetLockKey` (tecla), `iTargetLockMode` (1 = mantener, 0 = alternar) y `fTargetLockRotationSpeed` (0 = instantáneo, >0 = giro suave).
- **Selector propio:** elige al adversario cuyo torso está más cerca de la mira en pantalla. Mide la distancia corrigiendo el aspecto de la pantalla y exige línea de visión. Los hostiles cuentan dentro de la ventana de búsqueda. Los no hostiles (por ejemplo, un ciervo al cazar) solo cuentan si la mira está encima. Nunca elige seguidores.
- **Sin saltos:** cada clic elige una vez y el objetivo queda fijo mientras se mantiene el botón.
- **Abatidos excluidos:** muertos, desangrándose, inconscientes, derribados o levantándose nunca se eligen. Si el objetivo cae, se suelta al instante y la mira no vuelve a él.
- **El proyectil va al objetivo fijado:** con un objetivo fijado, la flecha o el virote apunta a su torso, aunque el rayo de la mira toque antes un cuerpo caído delante.
- **Margen al soltar:** arco y ballesta disparan al soltar el botón, así que el bloqueo se mantiene `fTargetLockReleaseDelay` segundos (0,5 por defecto) después de soltar.
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
