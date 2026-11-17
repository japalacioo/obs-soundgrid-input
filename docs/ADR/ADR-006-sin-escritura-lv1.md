# ADR-006: Sin configuración ni patch remoto en la LV1

**Estado:** Aceptada · **Fecha:** 2026-11-17

El plugin no escribe nada en la consola ni en el driver de Waves:
- No toca ganancia, mute, escenas, reloj ni patch.
- No cambia la frecuencia ni el buffer del driver: si el driver no está a la frecuencia pedida, falla con `FORMAT_UNSUPPORTED`.
- "Reconectar" solo reabre el receptor local.
