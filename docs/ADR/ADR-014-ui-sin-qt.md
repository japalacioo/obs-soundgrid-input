# ADR-014: Panel SoundGrid Monitor sin Qt

**Estado:** Aceptada (provisional) · **Fecha:** 2026-11-17

**Contexto:** un dock Qt necesita las cabeceras y bibliotecas de desarrollo de Qt 6.11 de la misma ABI que OBS, que no están en el entorno de desarrollo.

**Decisión:**
- El "SoundGrid Monitor" es el grupo **Estado y alertas** de las propiedades de la fuente (API C de `obs_properties`).
- Se abre desde **Herramientas > SoundGrid Monitor** (`obs_frontend_open_source_properties`).

**Limitación:** no hay notificaciones emergentes ni un dock permanente. Las alertas van al log de OBS y a `alerts.jsonl`.

**Siguiente paso:** dock Qt opcional cuando haya un SDK Qt compatible.
