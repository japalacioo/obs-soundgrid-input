# SDD-09 — Monitoreo, alertas e histéresis

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** P0 · **Dependencias:** 02,06,07 · **Entregable:** `HealthMonitor`, `AlertEngine` y reglas A01–A10.

**Historia:** Como operador quiero alertas claras sin falsas alarmas por silencios normales de una transmisión.

**Reglas iniciales configurables (no SLA ni cifras de hardware garantizadas):**

| ID | Regla | Umbral inicial | Evidencia principal |
|---|---|---|---|
| A01 | Silencio de fuente armada | RMS < -60 dBFS por 10 s, tras 3 s de gracia | PCM recibido + nivel bajo; silenciar regla si fuente no armada |
| A02 | Interrupción de stream | sin nuevos frames > 1 s | frame counter estancado estando `STREAMING` |
| A03 | Transporte/driver perdido | evento inmediato + debounce de 500 ms | error abierto/cerrado o estado driver |
| A04 | Red sin enlace | carrier DOWN confirmado > 1 s | estado NIC elegida |
| A05 | Enlace negociado lento | <1 Gbps sostenido 5 s | velocidad negociada, no throughput SoundGrid |
| A06 | Errores de NIC | incremento sostenido sobre línea base | contadores OS disponibles; `UNKNOWN` si no existen |
| A07 | Latencia alta | > 80 ms durante 5 s (inicial, calibrable); solo si medible | latencia real medida o etiqueta explícita «estimada» |
| A08 | Audio discontinuo | > 3 xruns/min (inicial) o exceso sostenido de cola 2 s | underrun/overrun y huecos de timestamp |
| A09 | Clipping | peak ≥ −0.1 dBFS en 3 ventanas dentro de 2 s | muestreo PCM, duración configurable |
| A10 | Recuperación | condición saludable sostenida 5 s | señal/restablecimiento de stream + red/driver |

**Diseño y tareas:** ventana temporal configurable, debounce/histéresis, supresión por jerarquía de causas (`NIC DOWN` suprime alertas redundantes de stream), severidad `INFO/WARN/CRITICAL`, acknowledgment sin silenciar audio, eventos con evidencia y método de medición. No medir «latencia total de extremo a extremo» sin reloj/instrumentación válida.

**Aceptación:** 10 reglas probadas en Windows/macOS con clock virtual; silencio deliberado sin PCM es distinguible de PCM silencioso; correlación evita tormenta de alertas. **Pruebas:** 10 reglas × trigger/resolución + regresiones persistencia.

