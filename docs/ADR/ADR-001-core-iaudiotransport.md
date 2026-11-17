# ADR-001: Un solo núcleo C++20 con adaptadores por SO detrás de IAudioTransport

**Estado:** Aceptada · **Fecha:** 2026-11-17

**Contexto:** el plugin debe funcionar en Windows (ASIO) y macOS (Core Audio), y dejar abierta una ruta nativa SoundGrid sin reescribir el motor.

**Decisión:**
- `sg_core` (biblioteca estática) contiene buffers, mapeo, medidores, timestamps, alertas y la sesión por fuente. No incluye cabeceras de libobs ni del SO.
- Cada transporte implementa `IAudioTransport` (`src/transport/IAudioTransport.h`), con un contrato de hilos documentado: control fuera del hilo de audio, `onCapture` en tiempo real y `stop()` síncrono.
- La compilación condicional se limita a `src/platform/*` y a la fábrica de `TransportManager`.

**Consecuencias:**
- El núcleo se prueba en CI sin OBS ni hardware.
- El plugin OBS (`src/obs`) es una capa fina.
- `MockTransport` permite reproducir todos los fallos.
