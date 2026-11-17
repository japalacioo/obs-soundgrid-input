# Plan de pruebas

El catálogo de casos T01–T57 y sus criterios está en el plan maestro (§5). Este documento dice **dónde** se implementa cada grupo; los resultados están en [TEST_REPORT.md](TEST_REPORT.md).

| Grupo | Dónde | Etiqueta CTest | Ambiente |
|---|---|---|---|
| DSP, buffers, conversión y timestamps (T03–T08) | `tests/unit/test_audio.cpp` | `unit` | Cualquier SO, sin hardware |
| Transporte, hub, reconexión y estrés (T02–T07, T10, T11, T41, T52, T55, SDD10) | `tests/unit/test_transport.cpp` | `unit` | Simulador con reloj virtual y tiempo real |
| Alertas (T25–T39, T51, T54) | `tests/unit/test_alerts.cpp` | `unit` | Reloj virtual |
| Logs, cola y ajustes (T40, T43, T53) | `tests/unit/test_sinks_settings.cpp` | `unit` | Disco temporal |
| Sesión de extremo a extremo sin OBS (T02, T44/T45 simulados, T50, T52, T10) | `tests/unit/test_session.cpp` | `unit` | Simulador + NIC simulada |
| Tiempo real (T48, T49) | `tests/unit/test_realtime.cpp` | `unit` | `operator new` instrumentado; `SG_T49_SECONDS=30` para la versión larga |
| Plataforma: NIC y drivers reales sin LV1 | `tests/integration/test_platform_local.cpp` | `platform` | Hardware local; informativas en CI |
| libobs real (T01, T02, T03, T08, T09, T10, T40, T50) | `tests/integration/obs_host_test.cpp` | `obs` | Requiere OBS instalado (`SG_OBS_BIN_DIR`) |
| Resistencia (T42; versión simulada de T17) | `tests/soak/soak_main.cpp` | `soak` | 15 s en CTest; `sg_soak 120` para 2 h |
| HIL (T12–T21, T24, T44–T47, T56, T57) | `tests/hil/HIL_PROCEDURE.md` | — | LV1 V17 real, fuera de show |
