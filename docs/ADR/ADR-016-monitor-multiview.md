# ADR-016: Monitor de audio en el Multiview como escena propia

**Estado:** Aceptada · **Fecha:** 2026-11-20

**Contexto:**
- La pantalla con el Multiview de OBS se ve desde el puesto de la consola. El operador de la consola quiere ver ahí, en el recuadro inferior derecho, el audio de la escena que el director tiene al aire: medidores L/R, forma de onda, Peak/RMS, clip y nombre de la fuente.
- Cada escena tiene su propia fuente "SoundGrid Input", aunque todas vengan del mismo driver.
- El Multiview es código del frontend de OBS (Qt). No hay API para que un plugin dibuje en un recuadro ni para cambiar el orden de las escenas. El Multiview dibuja las escenas en el orden de la lista, omitiendo las que tienen `show_in_multiview = false`.

**Decisión:**
- Fuente de vídeo `soundgrid_audio_monitor` (sin Qt, API gráfica de libobs) del tamaño del lienzo, dentro de una escena propia, "Monitor de audio SoundGrid". El Multiview la dibuja como cualquier escena.
- **Herramientas > SoundGrid: monitor de audio en el Multiview** crea la escena y calcula, según el diseño de Multiview configurado y las escenas ocultas, en qué posición de la lista debe arrastrarla el operador para que ocupe el último recuadro (abajo a la derecha salvo en "Horizontal, abajo" y "Vertical, derecha").
- La fuente que se muestra es la SoundGrid visible que esté más arriba en la escena de programa (o vista previa, o una fuente fija), incluidos grupos y escenas anidadas. La escena al aire se toma de los eventos del frontend en el hilo de interfaz, no del hilo de gráficos.
- El audio es el que el plugin entrega a OBS (antes de filtros y del volumen del mezclador): es lo que manda la consola. Si OBS silencia la fuente, la cabecera lo indica en ámbar.
- Estilo de OBS: colores y niveles del medidor de volumen de OBS (verde < −20 dBFS, amarillo hasta −9, rojo; caída 11,76 dB/s; peak hold 20 s; marca de RMS negra); marco y rojo de programa del Multiview; textos con las mismas fuentes de texto privadas que el Multiview (`text_ft2_source` / `text_gdiplus`).
- **Protección:** si la escena del monitor llega a programa o a vista previa (un clic en el Multiview), el plugin vuelve a la escena anterior. Nunca debe salir en la emisión.
- La lógica (historial, niveles, balística, columnas de la forma de onda, distribución, selección de fuente, plan del Multiview) está en `sg_core` (`src/monitor/`) con pruebas unitarias; la capa OBS solo dibuja. Una prueba con libobs real renderiza por GPU y comprueba los píxeles.

**Consecuencias:**
- El operador mueve la escena a mano una vez (OBS no tiene API de orden). Si hay menos escenas que recuadros, el monitor queda en el primer recuadro libre.
- La escena del monitor aparece en la lista de escenas del director. Con la opción de cambiar de escena con un clic en el Multiview, un clic puede llegar a programa durante un instante antes de que el plugin lo revierta; se recomienda desactivar esa opción en el ordenador del Multiview.
- La escena que ocupaba el último recuadro deja de verse en el Multiview.
- El monitor no ve los filtros ni el volumen de OBS.
