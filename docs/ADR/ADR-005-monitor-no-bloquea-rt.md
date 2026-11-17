# ADR-005: El monitor nunca bloquea la ruta de tiempo real

**Estado:** Aceptada · **Fecha:** 2026-11-17

**Decisión:**
- El callback solo convierte, mapea, escribe en anillos SPSC preasignados y actualiza contadores atómicos.
- Monitor, alertas, logs y UI corren en hilos propios y leen instantáneas.
- La cola de eventos es acotada (1024) y cuenta los descartes.

**Evidencia:**
- T48: 0 asignaciones en el callback con buffers de 32 a 4096 frames.
- T49: un consumidor congelado 3 s no frena el driver.
- T42 (soak con 12 hilos de carga): 0 cortes.
