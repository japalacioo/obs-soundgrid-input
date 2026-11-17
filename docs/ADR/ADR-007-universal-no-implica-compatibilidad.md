# ADR-007: Un binario universal de macOS no presupone que Waves V17 sea compatible en arm64

**Estado:** Aceptada · **Fecha:** 2026-11-17

El CMake genera binarios x86_64+arm64. La documentación de Waves menciona Rosetta en Apple Silicon. La compatibilidad se certifica por CPU solo con HIL.
