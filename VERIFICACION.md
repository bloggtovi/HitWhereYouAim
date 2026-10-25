# Informe de verificación: HitWhereYouAim 1.2.0

Entorno: Skyrim SE/AE 1.6.1170 (Steam), SKSE 2.2.6, Address Library 1.6.1170, 19 plugins SKSE
(SkyMP, SkyrimSouls RE, TrueHUD, OAR, SkyPatcher, Engine Fixes, RaceMenu, HDT-SMP, entre otros).

## 1. Compilación (GitHub Actions, MSVC 2022, `/W4 /WX`)
| Commit | Resultado |
|---|---|
| `1ad614f` | Falló al enlazar: 14 símbolos sin resolver por `BSWin32KeyboardDevice::IsPressed` → corregido |
| `673988d` | Correcto |
| `421b6c7` | Correcto (es la DLL instalada) |

## 2. Pruebas unitarias (`tests/BallisticSolverTests.cpp`)
354 comprobaciones y 0 fallos. Cada caso resuelve el disparo, simula la trayectoria con gravedad y con
el objetivo en movimiento, y comprueba el impacto (error 0,0000 unidades):
- objetivos estáticos a 20, 40, 60 y 100 m, cuesta arriba y cuesta abajo, y hechizos sin gravedad;
- objetivos en movimiento: corriendo de lado, cargando, huyendo, a caballo;
- 300 escenarios aleatorios;
- un objetivo fuera de alcance, que debe rechazarse;
- la conversión de ángulos al sistema de Skyrim (`SolveBallisticIntercept` / `SetProjectileAngle`) en 4 direcciones.

## 3. Verificación estática contra el binario
`SkyrimSE.exe` está cifrado en disco por SteamStub (entropía 7,95 bits/byte, punto de entrada en
`.bind`), así que los patrones solo se pueden comprobar en memoria. No se eliminó el DRM.

## 4. Verificación en memoria del juego en ejecución (solo lectura)
Sin el mod cargado, con los 18 plugins restantes:
- los 5 patrones se encuentran y todas las llamadas apuntan a funciones originales de `SkyrimSE.exe`;
- **ningún otro plugin modifica estos puntos**;
- la llamada del hook de lanzamiento va a `SkyrimSE.exe+0x764F10`, la misma dirección que anotó el autor original.

Con el mod 1.2.0 cargado:
| Punto | Destino |
|---|---|
| Ángulo de lanzamiento de proyectiles | trampolín SKSE → `HitWhereYouAim.dll+0x239d0` |
| Pose de apuntado en 3.ª persona | trampolín SKSE → `HitWhereYouAim.dll+0x23e10` |
| Inicialización de proyectiles | trampolín SKSE → `HitWhereYouAim.dll+0x2df0` |
| Hechizo de llamas (giro horizontal / vertical) | trampolín SKSE → `HitWhereYouAim.dll+0x23a70` / `+0x23a60` |
| Hechizo de rayo (giro horizontal / vertical) | trampolín SKSE → `HitWhereYouAim.dll+0x23a90` / `+0x23a80` |
| `PlayerCharacter::Update` (vtable 0xAD) | `HitWhereYouAim.dll+0x23e30` |
| Gravedad del mundo | 686,61 u/s² (9,81 m/s²) |

Logs: SKSE `loaded correctly`; `HitWhereYouAim.log` sin avisos (ningún hook omitido).

## 5. Fallos encontrados en la revisión y corregidos
1. Error de enlazado (ver 1).
2. El ratón se leía con `BSInputDevice::IsPressed`, que vale 0 en el fotograma del clic → ahora se lee el estado crudo de DirectInput.
3. En tercera persona se ignoraba `freeRotation`, en ambos ejes → corregido según la referencia de True Directional Movement.
4. Riesgo de clic fantasma por el desfase entre buffers de entrada → filtro de 2 fotogramas.
5. Error del original: `freeRotation.x < 0.1f` sin valor absoluto.
6. Detección de menús → pila de contextos de entrada del motor (compatible con SkyrimSouls RE).

## 6. Pendiente: prueba funcional en partida
Requiere jugar. Ver la lista de pruebas en la respuesta de la sesión.
