# ADR-009: DeviceHub: un stream por dispositivo, repartido entre varias fuentes

**Estado:** Aceptada · **Fecha:** 2026-11-17

**Contexto:**
- OBS limita cada fuente a 8 canales y 6 pistas.
- ASIO usa callbacks globales y muchos drivers admiten un solo cliente.
- El plan exige perfiles de 16 stems.

**Decisión:**
- `TransportManager` mantiene un `DeviceHub` por id de dispositivo.
- Cada fuente OBS es una `Subscription` con su anillo SPSC y su mapa de canales.
- El hub abre el driver una vez, con hasta 128 canales, y lo cierra cuando no quedan fuentes.
- Cambiar el mapa no reabre el driver (doble buffer + barrera de callback): T52.

**Consecuencias:**
- 16 stems = 8 fuentes estéreo sobre el mismo stream (ver PROFILES.md).
- Hasta 32 fuentes por dispositivo.
