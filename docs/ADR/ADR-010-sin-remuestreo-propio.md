# ADR-010: Sin remuestreo propio: OBS convierte a la frecuencia del proyecto

**Estado:** Aceptada · **Fecha:** 2026-11-17

Cada bloque lleva la frecuencia del driver (44.1, 48, 88.2 o 96 kHz) en `samples_per_sec`, y OBS remuestrea con su resampler. Así se evita el doble remuestreo (regla del plan §3). Un cambio de frecuencia se marca como discontinuidad (T06).
