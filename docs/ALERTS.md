# Alertas A01–A10

Todas las reglas son configurables por fuente (Propiedades > **Alertas**). Los umbrales iniciales salen del plan §4A y del SDD-09 y **deben calibrarse en laboratorio**. Ninguna alerta cambia el audio, el ruteo ni la consola.

## Reglas

| ID | Alerta | Se activa cuando… | Umbral inicial | Severidad | Fuente de la medida | Qué hacer |
|---|---|---|---|---|---|---|
| A01 | Silencio prolongado | La fuente está **ARMADA**, llegan frames y el RMS máximo de los canales queda bajo el umbral | < −60 dBFS durante 10 s (tras 3 s de gracia) | WARN | PCM (RMS) | Revisa mute, patch en la LV1 y el origen |
| A02 | Flujo interrumpido | No llegan frames con la fuente en marcha | > 1000 ms | CRITICAL | Contador de frames | Revisa el driver, Inventory y el patch |
| A03 | Driver/dispositivo perdido | El transporte está en ERROR, RECONNECTING o UNAVAILABLE | Debounce de 500 ms | CRITICAL | Estado del driver | El plugin reintenta solo; comprueba el driver Waves y que no lo use otra app |
| A04 | Sin conexión de red | La NIC elegida no tiene enlace o desaparece | > 1 s | CRITICAL | NIC del SO | Cable, switch y NIC |
| A05 | Enlace degradado | La velocidad negociada es menor que la esperada | < 1000 Mbps durante 5 s | WARN | NIC del SO | Cable Cat5e/6, puerto del switch. **No implica pérdida de paquetes** |
| A06 | Errores de red | Aumentan los errores o descartes de la NIC | ≥ 10 en 5 s | WARN | NIC del SO (no contadores SoundGrid) | Revisa el enlace |
| A07 | Latencia elevada | El tramo **medido** callback del driver → entrega a OBS supera el umbral | > 80 ms durante 5 s | WARN | Reloj monotónico | CPU y buffers. La latencia extremo a extremo es **NO DISPONIBLE** |
| A08 | Cortes / buffer en riesgo | Desbordes de cola + discontinuidades de timestamp + xruns del driver (si los reporta) en 60 s | > 3 (Transmisión) · > 0 (Grabación) | CRITICAL | Contadores internos | Buffers y CPU |
| A09 | Clipping | Ventanas con pico ≥ −0,1 dBFS | ≥ 3 ventanas en 2 s | WARN | PCM (pico) | Baja el envío en la LV1 |
| A10 | Recuperado | La condición se mantuvo sana | 5 s | INFO | La de la regla original | Informativo |

## Comportamiento
- **Gracia:** 3 s tras crear la fuente, cambiar de dispositivo o reiniciar OBS. Durante ese tiempo no hay alertas.
- **Histéresis:** un incidente se resuelve tras 5 s sanos (`RECOVERING` → `RESOLVED` + A10). El aleteo dentro de esos 5 s no crea incidentes nuevos.
- **Cooldown (30 s):** si la misma regla vuelve a fallar poco después de resolverse, se **reabre** el mismo incidente sin volver a notificar.
- **Correlación:**
  - A04 domina a A02, A03, A05 y A06; A03 domina a A02.
  - Una desconexión de cable genera **un** incidente notificado; sus consecuencias quedan en el log como secundarias.
- **UNKNOWN:** si una métrica no se puede observar (sin NIC elegida, sin contadores, sin timestamps fiables), la regla queda en UNKNOWN. Nunca se muestra OK ni se dispara una alarma falsa.
- **Reconocer:** marca el incidente como ACKNOWLEDGED; la detección continúa.
- **Silenciar avisos 10 min:** los eventos se siguen registrando con `notify=false`.
- **Perfiles:**
  - **Ensayo:** solo se notifican los CRITICAL.
  - **Transmisión:** valores por defecto.
  - **Grabación:** A08 se dispara con cualquier corte.
  - Al cambiar de perfil se cargan sus valores; luego se pueden ajustar a mano.

## Dónde se ven
- **Propiedades de la fuente > Estado y alertas**, o **Herramientas > SoundGrid Monitor**, que abre las propiedades de la primera fuente SoundGrid. Para ver el estado actual, pulsa "Actualizar estado".
- **Log de OBS** (Ayuda > Archivos de registro): líneas `[soundgrid]`. Nivel ERROR para CRITICAL y WARNING para WARN; los eventos no notificados van como INFO.
- **`alerts.jsonl`** en `%APPDATA%\obs-studio\plugin_config\obs-soundgrid-input\` (macOS: `~/Library/Application Support/obs-studio/plugin_config/obs-soundgrid-input/`). Una línea JSON por evento; rota a 5 MB y conserva 5 archivos. No guarda IP ni MAC.
- **Exportar diagnóstico** escribe `diagnostics.json` en la misma carpeta.

## Formato de un evento JSONL
```json
{"time":"2026-11-17T03:00:00.000Z","id":12,"rule":"A04","title":"Sin conexión de red","severity":"CRITICAL",
 "state":"ACTIVE","transition":"ACTIVATED","message":"A04 · Sin conexión de red",
 "evidence":{"observed":"enlace DOWN","threshold":"carrier DOWN > 1000 ms","cause":"Cable Ethernet desconectado o switch sin enlace",
 "action":"Comprueba el cable Ethernet, el switch SoundGrid y la NIC","confidence":"medido","source":"NIC del SO (Windows)"},
 "correlation_id":0,"secondary":false,"notify":true,"platform":"windows","source":"Main L/R"}
```
