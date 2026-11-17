# ADR-003: Certificación separada por plataforma y CPU

**Estado:** Aceptada · **Fecha:** 2026-11-17

Que el código sea compartido no implica que el driver de Waves funcione en cada CPU o versión de SO. Cada combinación (Windows x64, macOS Intel y macOS Apple Silicon) tiene su propio estado en `COMPATIBILITY_MATRIX.md` y su propio informe HIL `tests/hil/TEST_REPORT-<OS>-<CPU>.md`.
