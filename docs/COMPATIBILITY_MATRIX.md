# Matriz de compatibilidad (v0.3.0, 17 de noviembre de 2026)

Estados: `NO_EVALUADO`, `SIMULADO`, `COMPILA`, `PROBADO_LOCAL`, `PROBADO_HIL`, `BLOQUEADO`, `NO_SOPORTADO`.
`PROBADO_LOCAL` significa probado en el equipo de desarrollo con hardware y drivers reales que **no son Waves**. Solo la captura con Waves en Windows tiene una prueba HIL, y es **parcial**.

Equipo de desarrollo: i7-1255U · Windows 11 build 26300 · OBS 33.0.1 · MSVC 19.44 (VS 2022) · CMake 4.4.3.

## Por capacidad y plataforma

| Capacidad | Windows 10/11 x64 | macOS Intel | macOS Apple Silicon |
|---|---|---|---|
| Compilar núcleo y pruebas sin hardware | `PROBADO_LOCAL` (106/106 pruebas) | `NO_EVALUADO` (CI definida, no ejecutada) | `NO_EVALUADO` |
| Plugin carga en OBS 33.0.1 | `PROBADO_LOCAL` (GUI real y libobs sin interfaz) | `NO_EVALUADO` | `NO_EVALUADO` |
| Fuente simulada en OBS | `PROBADO_LOCAL` (1 kHz/2 kHz verificados tras OBS) | `NO_EVALUADO` | `NO_EVALUADO` |
| Captura con un driver de audio real | `PROBADO_LOCAL` con ReaRoute ASIO (16 canales, 48 kHz) | `NO_EVALUADO` (Core Audio sin compilar) | `NO_EVALUADO` |
| Captura con Waves SoundGrid V17 + LV1 | `PROBADO_HIL` **parcial** (driver 17.3.37.122: 4 entradas a 48 kHz hasta OBS; faltan duración, reconexión y patch confirmado) | `NO_EVALUADO` | `NO_EVALUADO` |
| Telemetría NIC (enlace, velocidad, errores) | `PROBADO_LOCAL` (Realtek GbE, sin admin) | `NO_EVALUADO` (código escrito) | `NO_EVALUADO` |
| Alertas A01–A10, correlación y reconexión | `SIMULADO` (reloj virtual y simulador) | `SIMULADO` (mismo núcleo; sin ejecutar en Mac) | `SIMULADO` |
| Panel de estado (propiedades) y Herramientas > SoundGrid Monitor | `PROBADO_LOCAL` (propiedades construidas en libobs; menú registrado en la GUI) | `NO_EVALUADO` | `NO_EVALUADO` |
| Aviso rojo de audio perdido en la ventana de OBS | `PROBADO_LOCAL` (barra Win32, probada con el simulador) | `NO_SOPORTADO` (sin implementar) | `NO_SOPORTADO` |
| Reapertura del Control Panel y del servicio de Waves | `PROBADO_LOCAL` (Control Panel; el servicio solo con pruebas unitarias) | `NO_SOPORTADO` | `NO_SOPORTADO` |
| Dock Qt dedicado | `NO_SOPORTADO` en 0.3.0 (ADR-014) | `NO_SOPORTADO` | `NO_SOPORTADO` |
| Paquete | `PROBADO_LOCAL` (instalador `.exe` y ZIP; instalación y desinstalación verificadas) | `NO_EVALUADO` (script `.pkg` escrito) | `NO_EVALUADO` |
| Firma de código / notarización | `NO_EVALUADO` (sin certificado) | `NO_EVALUADO` | `NO_EVALUADO` |
| Detección de la LV1 y nombres de canal LV1 | `BLOQUEADO` (sin API) | `BLOQUEADO` | `BLOQUEADO` |
| SoundGrid nativo sin driver (ruta B) | `BLOQUEADO` (protocolo) | `BLOQUEADO` | `BLOQUEADO` |

## Por SDD

| SDD | Estado | Evidencia principal |
|---|---|---|
| 00 Viabilidad | Hecho | [FEASIBILITY.md](FEASIBILITY.md), ADR-001 y ADR-002 |
| 01 Repositorio y CI | Hecho en local · CI definida, no ejecutada | `CMakeLists.txt`, `.github/workflows/ci.yml` |
| 02 IAudioTransport + Mock | Hecho | `MockTransport` con 6 tipos de fallo; pruebas T02–T07 y SDD10 |
| 03 ASIO Windows | Hecho, `PROBADO_LOCAL` sin Waves | `Platform_ASIO.*` con ReaRoute |
| 04 Core Audio macOS | Código escrito, `NO_EVALUADO` | `src/platform/macos/*` |
| 05 Compatibilidad cruzada | Parcial: solo Windows | Este documento |
| 06 NIC por SO | Windows `PROBADO_LOCAL`; macOS escrito | `Platform_NIC.*` |
| 07 AudioEngine | Hecho | T03–T08, T48, T49 |
| 08 Selección de canales | Hecho (puertos del driver; nunca nombres LV1) | T03, T04, T52, panel |
| 09 Alertas | Hecho, `SIMULADO` | T25–T39, T51, T54 |
| 10 Reconexión | Hecho, `SIMULADO` | SDD10_*, T55, T44/T45 simulados |
| 11 UX | Parcial: propiedades y menú de Herramientas; sin dock Qt | ADR-014 |
| 12 Seguridad y configuración | Hecho | T40, T43; sin telemetría remota |
| 13 CI/CD y empaquetado | Windows `PROBADO_LOCAL`; macOS escrito | `packaging/` |
| 14 Ruta B | `BLOQUEADO` | [NATIVE_FEASIBILITY.md](NATIVE_FEASIBILITY.md) |
| 15 HIL | `NO_EVALUADO` | Procedimiento en `tests/hil/` |
