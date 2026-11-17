# ADR-002: Ruta A (driver oficial Waves) como MVP; ruta B bloqueada

**Estado:** Aceptada · **Fecha:** 2026-11-17

**Contexto:** el objetivo de "conectar el cable y recibir audio sin el panel de Waves" requiere el protocolo SoundGrid. Es propietario y no tiene SDK público (ver FEASIBILITY.md).

**Decisión:**
- **Windows:** ruta A con `AsioTransport` sobre el SoundGrid ASIO Driver V17.
- **macOS:** ruta A con `CoreAudioTransport` sobre el SoundGrid Core Audio V17.
- **Ruta B:** `BLOQUEADO POR PROTOCOLO`. No hay implementación ni simulación con ese nombre.

**Consecuencias:**
- La documentación declara que hacen falta el driver, la selección de NIC, la asignación en Inventory y el patch en la LV1.
- La ruta A nunca se presenta como "sin driver".
