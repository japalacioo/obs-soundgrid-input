# Registro de decisiones (ADR)

| ADR | Título | Estado |
|---|---|---|
| [ADR-001](ADR-001-core-iaudiotransport.md) | Un solo núcleo C++20 con adaptadores por SO detrás de IAudioTransport | Aceptada |
| [ADR-002](ADR-002-rutas-A-B.md) | Ruta A (driver oficial Waves) como MVP; ruta B bloqueada | Aceptada |
| [ADR-003](ADR-003-certificacion-por-plataforma.md) | Certificación separada por plataforma y CPU | Aceptada |
| [ADR-004](ADR-004-salida-obs.md) | obs_source_output_audio encapsulado en la capa OBS con timestamps del reloj de libobs | Aceptada |
| [ADR-005](ADR-005-monitor-no-bloquea-rt.md) | El monitor nunca bloquea la ruta de tiempo real | Aceptada |
| [ADR-006](ADR-006-sin-escritura-lv1.md) | Sin configuración ni patch remoto en la LV1 | Aceptada |
| [ADR-007](ADR-007-universal-no-implica-compatibilidad.md) | Un binario universal de macOS no presupone que Waves V17 sea compatible en arm64 | Aceptada |
| [ADR-008](ADR-008-lanzamiento-por-niveles.md) | Lanzamiento por niveles de evidencia | Aceptada |
| [ADR-009](ADR-009-dispositivo-compartido.md) | DeviceHub: un stream por dispositivo, repartido entre varias fuentes | Aceptada |
| [ADR-010](ADR-010-sin-remuestreo-propio.md) | Sin remuestreo propio: OBS convierte a la frecuencia del proyecto | Aceptada |
| [ADR-011](ADR-011-calculo-latencia.md) | Qué latencia se mide y cuál no | Aceptada |
| [ADR-012](ADR-012-politica-reconexion.md) | Política de reconexión | Aceptada |
| [ADR-013](ADR-013-persistencia-alertas.md) | Persistencia de configuración y de alertas | Aceptada |
| [ADR-014](ADR-014-ui-sin-qt.md) | Panel SoundGrid Monitor sin Qt | Aceptada (provisional) |
| [ADR-015](ADR-015-sdk-libobs-local.md) | Compilar contra la instalación de OBS mediante un SDK local | Aceptada |
