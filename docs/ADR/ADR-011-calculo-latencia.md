# ADR-011: Qué latencia se mide y cuál no

**Estado:** Aceptada · **Fecha:** 2026-11-17

- **Medido:** el tramo "callback del driver → entrega a OBS" (reloj monotónico en ambos extremos). A07 solo se aplica a este tramo.
- **Estimado:** la ocupación de la cola interna y la latencia de entrada que informa el driver (`getLatencies` / `kAudioDevicePropertyLatency`). Se etiquetan como "estimado" o "informado".
- **NO DISPONIBLE:** la latencia de extremo a extremo (consola → red → driver → OBS → salida). Requiere instrumentación externa (impulso y referencia sincronizada) y nunca se deriva del tamaño del buffer.
