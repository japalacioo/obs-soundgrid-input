# Limitaciones conocidas (v0.1.0)

**Estado de validación:** NO PROBADO EN HARDWARE con Waves eMotion LV1 Classic V17 ni con el SoundGrid Driver V17. La captura ASIO se probó con otro driver ASIO real (ReaRoute) y el plugin con OBS 33.0.1 real en Windows 11.

1. **Requiere el driver de Waves.**
   - El plugin es un cliente del SoundGrid Driver V17 (ASIO o Core Audio). Para la primera configuración hacen falta el driver, la selección de NIC en su panel, la asignación en la LV1 (Setup > Inventory) y el patch (Patch > Output).
   - "Conectar el cable y recibir audio sin el panel de Waves" (ruta B) está **BLOQUEADO**: SoundGrid es propietario y no tiene SDK público.
2. **No identifica la consola.** Detectar una NIC o un driver con "SoundGrid" en el nombre no demuestra que haya una LV1. El panel muestra "Consola LV1: No identificada".
3. **Nombres de canal.** Se muestran las entradas o puertos del driver, nunca los nombres de canal de la LV1 (no hay API autorizada). El indicador de patch queda "No verificado".
4. **Máximo 8 canales por fuente** (límite de OBS). Para más canales, crea varias fuentes sobre el mismo dispositivo: comparten un único stream de hasta 128 canales. Los modos de 3 a 8 canales usan las disposiciones envolventes de OBS (2.1, 4.0, 4.1, 5.1 y 7.1; el de 7 canales se completa a 7.1 con un canal en silencio). OBS **mezcla a estéreo** según esa disposición: para stems independientes, usa fuentes mono o estéreo.
5. **Latencia.**
   - Solo se mide el tramo driver → OBS.
   - La latencia de entrada que informa el driver es una estimación.
   - La latencia de extremo a extremo (consola → OBS → salida) no se mide.
6. **Telemetría de red.** Viene de la NIC del SO: enlace, velocidad y errores. No hay contadores de paquetes SoundGrid.
7. **Varios clientes.** Si el driver de Waves admite un solo cliente, el plugin no puede compartirlo con un DAW o con la fuente ASIO nativa de OBS 33.1+ (lo informará como BUSY o INIT_FAILED). Varias fuentes de este plugin sí comparten el dispositivo.
8. **Sin control de la consola ni del reloj.** No cambia la frecuencia, el buffer, el patch, la ganancia, el mute ni las escenas.
9. **macOS no validado.** El código Core Audio y NIC de macOS está escrito pero no se ha compilado ni probado. Waves desaconseja grabar en Apple Silicon (cortes intermitentes) y más de 32 canales por el Ethernet integrado del Mac.
10. **Interfaz.**
    - En Windows, la pérdida de audio se muestra con una barra roja en la parte superior de OBS (ADR-014). En macOS y Linux todavía no: el estado está en las propiedades de la fuente.
    - No hay dock Qt. El detalle está en las propiedades de la fuente (Herramientas > SoundGrid Monitor) y se refresca con "Actualizar estado".
    - La barra se superpone a la parte alta de los paneles de OBS mientras dura el aviso.
    - Las alertas sonoras y las notificaciones del SO no están implementadas, aunque el plan las considera opcionales y desactivadas por defecto.
11. **Distribución.**
    - Windows: el binario no está firmado y no hay instalador `.exe`; se distribuye como ZIP con script de instalación por usuario.
    - macOS: firma y notarización solo con credenciales del propietario.
12. **Intel Core Ultra:** Waves no lo soporta para rec/pb.
