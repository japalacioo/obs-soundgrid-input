# Borrador de consulta a Waves (NO ENVIADO)

> Lo debe enviar el propietario del proyecto por un canal oficial de Waves (soporte técnico o el contacto de <https://www.waves.com/soundgrid-for-hardware-and-software-developers>). El agente no envía mensajes en nombre del usuario.

**Asunto:** Integración de software receptor con SoundGrid (eMotion LV1 Classic V17): disponibilidad de SDK/API

Estamos desarrollando un plugin de código abierto (GPLv3) para OBS Studio que recibe audio de una consola eMotion LV1 Classic V17 a través del Waves SoundGrid Driver V17 (ASIO en Windows y Core Audio en macOS). Para el uso en vivo nos gustaría confirmar:

1. **SDK o API para receptores:** ¿existe un SDK, especificación o programa de partners que permita a un software de terceros recibir audio SoundGrid sin instalar el SoundGrid Driver? ¿Con qué condiciones de licencia y redistribución? ¿Son compatibles con un plugin GPLv3?
2. **Descubrimiento y metadatos:** ¿hay una API pública o licenciable para descubrir nodos SoundGrid e identificar una LV1, y para leer los nombres de canal y bus de la sesión, como hace Tracks Live?
3. **Configuración del driver:** ¿se puede consultar o configurar de forma programática, con una API documentada, la NIC seleccionada y el estado del driver sin abrir el SoundGrid Driver Control Panel?
4. **Persistencia:** ¿el driver V17 conserva la NIC y la asignación de Setup > Inventory tras reiniciar el PC? ¿Necesita que el Control Panel esté abierto o basta con el servicio `SGDawNodeService`?
5. **Varios clientes:** ¿el SoundGrid ASIO Driver V17 admite más de una aplicación cliente a la vez (por ejemplo OBS y un DAW)?
6. **Telemetría:** ¿el driver expone contadores de paquetes perdidos, xruns o latencia de red que una aplicación cliente pueda leer?
7. **macOS:** ¿el driver Core Audio V17 es nativo en Apple Silicon o se ejecuta con Rosetta? ¿Qué modo (User o Kernel) recomiendan para grabar 2–32 canales?

No modificamos la configuración de la consola: el plugin solo lee los canales que el operador parchea al driver.
