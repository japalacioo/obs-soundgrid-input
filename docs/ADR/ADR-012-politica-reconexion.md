# ADR-012: Política de reconexión

**Estado:** Aceptada · **Fecha:** 2026-11-17

**Disparadores:**
- Evento `DeviceLost` o `ResetRequest` del driver.
- Estado `ERROR`.
- Ausencia de callbacks durante 3 s con el stream en marcha.
- Nunca el silencio legítimo.

**Política:**
- Backoff exponencial 1, 2, 4, 8… s hasta 30 s, con ±20 % de jitter.
- Máximo 50 intentos por incidente; después hace falta "Reconectar" manual.
- Solo se reabre el receptor local.

**Pruebas:** SDD10_* y T55.
