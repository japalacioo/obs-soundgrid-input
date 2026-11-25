# Limitaciones conocidas (v0.3.0)

**Estado de validación:** prueba con hardware Waves **PARCIAL** en Windows 11 con OBS 33.0.1: se recibió audio de una eMotion LV1 Classic V17 por el SoundGrid Driver V17 (17.3.37.122) hasta OBS. Faltan las pruebas HIL de duración (T17/T18), reconexión con audio real (T44–T47), patch confirmado con el operador (T14/T15) y macOS.

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
7. **Vigilancia global.** Sin fuentes, el plugin escucha solo las entradas 1–2 del primer driver Waves SoundGrid (no configurable todavía). Con el aviso de silencio activado, una pausa larga de la consola también enciende la barra.
7b. **Varios clientes.** En la prueba, el driver Waves SoundGrid V17 aceptó dos clientes ASIO a la vez (OBS y la sonda). Si el driver de Waves admite un solo cliente, el plugin no puede compartirlo con un DAW o con la fuente ASIO nativa de OBS 33.1+ (lo informará como BUSY o INIT_FAILED). Varias fuentes de este plugin sí comparten el dispositivo.
8. **Sin control de la consola ni del reloj.** No cambia la frecuencia, el buffer, el patch, la ganancia, el mute ni las escenas.
    - De Waves solo **abre** el Control Panel y el servicio `SGDawNodeService` si están cerrados (Windows, opción por fuente), con el mismo comando que registra Waves. No cambia su configuración: si el driver necesita volver a elegir la tarjeta de red tras asignarlo en la LV1, eso sigue siendo manual.
    - No se ha probado si reabrir el Control Panel por sí solo recupera el audio después de un fallo del driver; lo que transporta el audio es el servicio.
9. **macOS experimental.**
    - Desde la 0.4.0 hay instalador `.pkg` universal (Apple Silicon e Intel). En macOS 26 con Apple Silicon y OBS 33.0.2 pasan las 141 pruebas automáticas, el plugin carga y el monitor del Multiview funciona con el simulador. En Intel no se ha ejecutado.
    - **No se ha probado con hardware Waves** ni con el SoundGrid Driver para Mac: ni la captura Core Audio con la LV1 ni la telemetría de red de macOS.
    - En Mac el plugin no abre ni vigila el Control Panel de Waves y no lee su tarjeta de red: hay que elegirla a mano en la fuente.
    - Waves desaconseja grabar en Apple Silicon (cortes intermitentes) y más de 32 canales por el Ethernet integrado del Mac.
10. **Interfaz.**
    - En Windows, la pérdida de audio se muestra con una barra roja en la parte superior de OBS (ADR-014). En macOS y Linux todavía no: el estado está en las propiedades de la fuente.
    - No hay dock Qt. El detalle está en las propiedades de la fuente (Herramientas > SoundGrid Monitor) y se refresca con "Actualizar estado".
    - La barra se superpone a la parte alta de los paneles de OBS mientras dura el aviso.
    - Las alertas sonoras y las notificaciones del SO no están implementadas, aunque el plan las considera opcionales y desactivadas por defecto.
11. **Distribución.**
    - Windows: el binario no está firmado y no hay instalador `.exe`; se distribuye como ZIP con script de instalación por usuario.
    - macOS: el `.pkg` no está firmado ni notarizado (Gatekeeper pide "Abrir igualmente"). Firmar y notarizar requiere las credenciales de desarrollador de Apple del propietario.
12. **Intel Core Ultra:** Waves no lo soporta para rec/pb.
13. **Medidor de sonoridad (vista LUFS del monitor).**
    - Sin dialogue gating: el preset de Netflix (que mide el diálogo) es orientativo.
    - Sin configuración de canales personalizada, sin exportación de informes y sin análisis de archivos.
    - El DR es una aproximación (media de PSR): Youlean no publica su algoritmo.
    - Probado en macOS con el simulador; sin probar en Windows ni con audio real de la LV1.
