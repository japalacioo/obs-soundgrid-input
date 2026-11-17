# Hardware y software soportados

| Elemento | Requisito / estado |
|---|---|
| Consola | Waves eMotion LV1 Classic V17: **objetivo, NO validado** (sin HIL) |
| Driver | Waves SoundGrid Driver V17 (ASIO en Windows, Core Audio en macOS): **NO validado** |
| OBS Studio | 33.0.1: **validado** (carga, fuente, propiedades y persistencia). Otras 33.x: probable, no verificado |
| Windows | 10/11 x64: **validado en Windows 11 build 26300** |
| macOS | Sonoma 14 o posterior (requisito de Waves V17): **no compilado ni probado** |
| CPU Windows | x64. Waves no soporta Intel Core Ultra para rec/pb |
| CPU macOS | Intel y Apple Silicon (binario universal previsto). Waves menciona Rosetta y cortes en Apple Silicon |
| NIC | Ethernet Gigabit dedicada al switch SoundGrid. Para USB-C, Waves recomienda chipset ASIX AX88179A |
| Canales | Hasta 128 entradas del driver por dispositivo; hasta 8 por fuente OBS; hasta 32 fuentes por dispositivo |
| Frecuencias | 44.1, 48, 88.2 y 96 kHz (las del driver; OBS remuestrea a la del proyecto) |

## Drivers ASIO probados (no Waves)
| Driver | Resultado |
|---|---|
| ReaRoute ASIO (x64), de REAPER | Abre 16 canales a 48 kHz en int32 con buffer de 1024. Sin REAPER abierto corre a 1,99× tiempo real; el plugin lo detecta como anomalía |
