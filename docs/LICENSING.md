# Licencias

| Componente | Licencia | Cómo se usa |
|---|---|---|
| OBS SoundGrid Input (este proyecto) | **GPLv3 o posterior** ([LICENSE](../LICENSE)) | — |
| OBS Studio / libobs 33.0.1 | GPLv2 o posterior | Se enlaza dinámicamente con `obs.dll` y `obs-frontend-api.dll`. Las cabeceras se obtienen en tiempo de compilación de github.com/obsproject/obs-studio (no se distribuyen) |
| `src/platform/windows/third_party/obs-win-asio/{asio-compat.h, iasiodrv.h}` | GPLv3 o posterior (pkv, proyecto OBS), derivadas del Steinberg ASIO SDK 2.3.4 bajo GPLv3 | Se incluyen sin cambios, con su cabecera de copyright y un README de procedencia |
| Steinberg ASIO SDK | Licencia dual: propietaria o GPLv3 | No se distribuye; solo las cabeceras mínimas anteriores |
| GoogleTest 1.15.2 | BSD-3-Clause | Solo pruebas; se descarga al configurar (FetchContent con SHA-256 fijado) y no se distribuye |
| Waves SoundGrid Driver / SDK | Propietario | **No se distribuye ni se incluye.** El usuario lo instala desde Waves |

**Por qué GPLv3:** las cabeceras ASIO solo están disponibles con licencia libre bajo GPLv3, y OBS (GPLv2 o posterior) es compatible con una obra combinada bajo GPLv3. Si un distribuidor quisiera otra licencia, tendría que firmar el acuerdo propietario de Steinberg y sustituir esas cabeceras.

**Marcas:** "ASIO" es marca registrada de Steinberg Media Technologies GmbH. "Waves", "SoundGrid" y "eMotion LV1" son marcas de Waves Audio Ltd. Este proyecto no está afiliado ni respaldado por Steinberg ni por Waves.
