# ADR-004: obs_source_output_audio encapsulado en la capa OBS con timestamps del reloj de libobs

**Estado:** Aceptada · **Fecha:** 2026-11-17

**Decisión:**
- Solo `src/obs/ObsSoundGridSource.cpp` llama a `obs_source_output_audio()`.
- Lo hace desde un hilo de salida por fuente, nunca desde el callback del driver.
- Envía float32 planar y la frecuencia nativa del bloque.
- Los timestamps salen de `TimestampSmoother` con el reloj `os_gettime_ns()` (inyectado como `IClock`), así que son monotónicos y continuos y solo se re-anclan si la deriva supera 40 ms. El re-anclaje marca una discontinuidad.
