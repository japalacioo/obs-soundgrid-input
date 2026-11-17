# Procedimiento HIL con Waves eMotion LV1 Classic V17 (SDD-15)

> Estas pruebas **no se automatizan a ciegas**. Hacen falta una ventana autorizada **fuera de show**, la presencia del responsable de sonido y una copia de la sesión de la LV1. El plugin no escribe en la consola; todos los cambios de patch los hace el operador y deben poder revertirse.

## 0. Preparación (registrar en el informe)
- Versiones exactas: LV1 (software y firmware), SoundGrid Driver, Windows o macOS (y CPU), OBS, plugin (hash SHA-256 de la DLL o el bundle), modelo de NIC y switch.
- Sesión LV1: frecuencia, buffer de red, reloj (fuente) y canales asignados al driver en Setup > Inventory.
- Copia de la sesión y fotos de Inventory y Patch **antes** de empezar.
- Generador de señal seguro a bajo nivel. Ejemplo: oscilador de la LV1 a −20 dBFS en un **bus de prueba**, nunca en el Main del show.

## 1. Captura básica (T14, T15, T56)
1. Patch > Output: bus de prueba L → entrada 1 del driver; R → entrada 2. Usa 1 kHz en L y 2 kHz en R (o L solo, para identificar el lado).
2. En OBS, fuente SoundGrid Input: dispositivo = driver Waves, NIC = la de SoundGrid, modo estéreo con L = 1 y R = 2.
3. Graba 60 s en OBS (WAV o MKV/PCM).
4. Verifica: L = 1 kHz, R = 2 kHz, nivel −20 dBFS ±0,5 dB y sin inversión ni swap. Compara con una captura en Tracks Live si está disponible, para aislar driver y plugin.
5. Repite con canales 1–8 en modo multicanal (T15), con un tono distinto por canal.
6. T56: cruza L y R en el patch y comprueba que el plugin lo refleja (el plugin no corrige el patch).

## 2. Persistencia y "sin panel" (T19)
1. Con la configuración funcionando, reinicia el PC **sin abrir** el SoundGrid Driver Control Panel.
2. Abre OBS y anota si la fuente vuelve a STREAMING sola, cuánto tarda y qué muestra el panel.

## 3. Resistencia (T17, T18, T21)
1. Graba 2 h (T17) y, si se puede, 8 h (T18) a 48 kHz con 2 canales.
2. Tras la prueba: Exportar diagnóstico. Revisa en `diagnostics.json` desbordes, discontinuidades y reconexiones, y en `alerts.jsonl` los eventos.
3. Revisa CPU y RAM de OBS (Administrador de tareas o Monitor de actividad) al inicio y al final.
4. T21: desconecta el driver o el cable durante la grabación y comprueba que no hay ruido, que el silencio es seguro y que el WAV es continuo cuando el audio se recupera.

## 4. Alertas reales (T44–T47, T57)
| Prueba | Acción (fuera de show) | Resultado esperado |
|---|---|---|
| T44 | Desconectar el cable Ethernet del PC durante 10 s y reconectar | Un incidente A04 notificado; A02/A03 como consecuencias; A10 tras la recuperación; el audio vuelve sin intervención (anota el tiempo) |
| T45 | Desactivar y reactivar el driver receptor (quitarlo de Inventory o detener el servicio) con la NIC UP | A03 (no A04); reconexión automática con backoff |
| T46 | Forzar la NIC a 100 Mbps (propiedades del adaptador) en un banco aislado | A05 sin afirmar pérdidas; A06 solo si suben los contadores |
| T47 | Bus de prueba en mute 15 s con la fuente **armada**; luego otra fuente no armada | A01 solo en la armada; sin A02, A03 ni A04 |
| T57 | Cambiar de puerto en el switch o de NIC | Alertas correctas; la consola no se modifica (comprobar con fotos antes y después) |

## 5. FOH intacto (T24)
Compara fotos y capturas de Patch, Inventory, reloj, ganancias y escenas antes y después de toda la sesión. Deben ser idénticas salvo los cambios de patch que hizo el operador para la prueba.

## 6. Informe
Copia `TEST_REPORT-TEMPLATE.md` como `TEST_REPORT-<OS>-<CPU>.md` (por ejemplo `TEST_REPORT-Windows-x64.md`), rellénalo y adjunta WAV, logs, `alerts.jsonl`, `diagnostics.json` y fotos. Lo firma el responsable de sonido. Una prueba solo pasa de `NOT_RUN` a `PASS` con esa evidencia.
