# ADR-012: Política de reconexión

**Estado:** Aceptada · **Fecha:** 2026-11-17

**Disparadores:**
- Evento `DeviceLost` o `ResetRequest` del driver.
- Estado `ERROR`.
- Ausencia de callbacks durante 3 s con el stream en marcha.
- Nunca el silencio legítimo.

**Política:**
- Backoff exponencial 1, 2, 4, 8… s hasta 30 s, con ±20 % de jitter.
- Un incidente a menos de 30 s del anterior (dispositivo intermitente) **continúa** la progresión del backoff; tras 30 s estables se vuelve a empezar desde 1 s.
- Máximo 50 intentos por racha; después el estado es ERROR "reintentos agotados" y hace falta "Reconectar" manual.
- Solo se reabre el receptor local.
- Las operaciones con el driver se serializan con un candado propio (`opMu_`), distinto del de estado: la UI y el monitor nunca esperan a un `open()` o `init()` lento (revisión del 17-nov, punto 4).

**Pruebas:** SDD10_* y T55.
