# SDD-14: ruta B, SoundGrid nativo sin driver

**Estado:** `BLOQUEADO POR PROTOCOLO` · **Fecha:** 17 de noviembre de 2026 · **Implementación:** ninguna (`src/soundgrid_native/` contiene solo un README).

## Puerta de entrada (SDD-14)

| Condición | Estado | Evidencia |
|---|---|---|
| 1. API o protocolo legalmente utilizable | **NO** | SoundGrid es propietario; no hay SDK ni especificación pública; el acceso solo es por partnership con Waves (ver [FEASIBILITY.md §3.3](FEASIBILITY.md)) |
| 2. LV1 Classic V17 identificada | NO EVALUADO | Sin hardware |
| 3. Patch o ruta de audio con control permitido | NO | Sin API; además, escribir en la LV1 está prohibido por defecto (ADR-006) |
| 4. PCM, reloj y canales verificados | NO EVALUADO | |
| 5. Ensayo de 2 h | NO EVALUADO | |
| 6. Windows y macOS probados por separado | NO EVALUADO | |

## Qué NO se hizo, a propósito
- No se capturó tráfico SoundGrid, no se hizo ingeniería inversa ni se escaneó la red. Lo prohíben las reglas 6 y 8 del plan, y no hay red de laboratorio autorizada.
- No se creó un `NativeSoundGridTransport` simulado. Un mock con ese nombre se podría confundir con una capacidad real.

## Para desbloquear
1. El propietario del proyecto envía a Waves la consulta de [WAVES_SDK_REQUEST.md](WAVES_SDK_REQUEST.md) por un canal oficial.
2. Si Waves concede acceso, se hace una revisión legal del acuerdo (redistribución, confidencialidad y licencia GPL del plugin).
3. Se monta un laboratorio aislado con una LV1 V17 que no esté en producción.
4. Se implementa `NativeSoundGridTransport` detrás de `IAudioTransport`, sin tocar el núcleo, y se ejecutan las puertas 2 a 6 y la prueba T20.

Mientras tanto, la **ruta A** (driver oficial) es el MVP, y la documentación dice explícitamente que requiere el driver de Waves y su configuración inicial.
