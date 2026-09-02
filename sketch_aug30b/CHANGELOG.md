# Changelog

Todas las novedades reseñables de Nottamagochi.
El formato sigue [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el proyecto usa [versionado semántico](https://semver.org/lang/es/).

---

## [1.0.0] — 2026-08-31

Primera versión completa. Firmware entero, de cero, para ESP32-C3 SuperMini con OLED SSD1306 de 128×64 y dos botones.

> **Incluye la revisión de endurecimiento del 2026-08-31** (secciones «Corregido» y «Cambiado» de más abajo), hecha sobre esta misma versión antes de publicarla. El firmware sigue reportando `1.0.0` por el puerto serie: no hubo ninguna versión anterior en circulación que corregir.

### Añadido

**Simulación**
- Doce estadísticas (saciedad, ánimo, energía, higiene, salud, cariño, disciplina, peso, calidad de cuidado, cacas, enfermedad, edad) en aritmética **entera de milipuntos**, con acumulador de resto para que la división entera no derive. **Cero coma flotante en cualquier ruta de estadísticas.**
- Tick lógico a 1 Hz. Una sola función decide cuánto avanza el mundo (`sim_step_seconds()`); ningún otro módulo lee el reloj para lógica de juego.
- Seis etapas de vida (HUEVO → BEBÉ → NIÑO → ADOLESCENTE → ADULTO → ANCIANO) con multiplicadores de decaimiento por etapa y muerte natural entre los 8 y los 12 días según lo bien que lo hayas cuidado.
- Seis formas adultas — **BOLOTA, ZAMPASALTO, BÚHO, PUNKI, MOHO** y la escondida **QUIMERA** — decididas a las 48 h por cinco puntuaciones de rama calculadas sobre acumuladores de toda la vida del bicho.
- Motor de enfermedad, cacas (máximo 4 en pantalla), sobrealimentación, obesidad, ventana de sueño 23:00–07:00 y la penalización por dormir con la luz encendida.
- **Rendimientos decrecientes en los minijuegos**: 100 / 70 / 45 / 25 / 10 / 0 % en una ventana móvil de 3 h, más un techo de ganancia horaria por estadística. No se puede farmear por ninguna de las tres superficies (botones, web, god mode).
- Deseo diario a una hora pseudoaleatoria entre las 09:00 y las 21:00, con ventana de 4 h para cumplirlo.
- Eventos con hora fija: **VISITA** (72 h), **TORMENTA NOCTURNA** (144 h) y **CUMPLEAÑOS** (cada 168 h).
- Siete causas de muerte con atribución por acumuladores de daño. **VEJEZ** es el final bueno y está tratado aparte: animación cálida, el bicho se tumba solo, y el epitafio dice `Se durmió.` en vez de `Murió.`
- Puesta en escena de la muerte de **22 segundos**, exacta y no saltable: latido que se ralentiza de 60 a 0 ppm durante 12 s, colapso en 4 pasos de tramado, **3 segundos de pantalla negra absoluta**, y luego el epitafio línea a línea.

**Genética y herencia**
- Genoma de **16 bytes exactos**, empaquetado, con **quince campos genéticos** en campos de bits y CRC-16/CCITT-FALSE. El mismo blob se guarda en NVS, se transmite por BLE y se hereda, byte a byte.
- 12 especies base, 16 patrones, 8 paletas de tramado, 8 tamaños de cuerpo, 4 variantes de apéndices, 6 genes numéricos y los bits de sexo, rareza y marca de god mode.
- **Huevo de muerte (partenogénesis)**: mutación alta y **deriva dirigida por la causa de la muerte** — muerto de hambre baja el apetito, muerto de abandono baja la sociabilidad. Con **garantía de novedad forzada**: si ninguna mutación cambió nada, el juego fuerza una. Cada huevo de muerte es visiblemente distinto de su padre.
- **Huevo híbrido (BLE)**: promedia los genes numéricos, cruza los categóricos al 50 %, y con un 12 % de probabilidad recombina especies distintas en una de las cuatro formas híbridas exclusivas, además de habilitar QUIMERA.
- Reflexión en los límites en vez de recorte, para que las dinastías no se saturen en 0 o 15 tras seis generaciones.
- Detección de consanguinidad, mutación fundadora (6 %) y contador de mutaciones saturante.
- **Nombres generados de forma determinista** a partir de la estirpe y la generación con dos tablas de sílabas castellanas: la misma estirpe da el mismo nombre en cualquier aparato.

**«Sé que me apagaste»**
- Distinción entre **cuelgue y desconexión real** usando `esp_reset_reason()` más un nonce en memoria RTC (que sobrevive al reinicio por software pero no al corte de corriente). Un cuelgue no acusa a nadie: *«Me he mareado un momento.»*
- Simulación de recuperación a pasos fijos de 1800 s, hasta 2000 pasos (≈41,7 días), con decaimiento al 55 % pero **daño a la salud al 100 %**. La ventana de sueño se respeta también estando apagado.
- **Escalera de ausencia de cinco escalones** (CORTA / LARGA / ABANDONO / GRAVE / MUERTO), con enfurruñamiento, rechazo de interacción con contador visible, telarañas, tramado al 50 % y una **cicatriz permanente de 2 px que se hereda**.
- El tiempo se muestra siempre **exacto** (`3 d 11 h 06 min`), nunca redondeado.
- Modo **ausencia desconocida** cuando no hay reloj fiable: aplica el escalón LARGA como suelo, nunca peor, y **recalcula y cobra la diferencia real** en cuanto el SNTP aterriza.
- **Huevo frío**: si lleva más de 72 h esperando, nace con 90 de salud pero gana una mutación forzada extra, la «marca del superviviente». El peor desenlace produce el genoma más interesante.
- El perdón se puede trabajar: cada mimo quita 20 s del temporizador de rechazo.

**Meteorología**
- Open-Meteo cada 30 min con jitter de ±4 min, sobre HTTP plano.
- **Doce grupos WMO** con multiplicadores propios de ánimo, energía, humor y probabilidad de enfermedad, cada uno con su efecto de pantalla: sol de 8 rayos girando, estrellas parpadeantes, damero de niebla que difumina el sprite por XOR, lluvia inclinada con charco, copos con deriva senoidal y acumulación, **inversión completa de pantalla de 60 ms** en las tormentas, granizo rebotando.
- Capa de temperatura aparente encima, con tiritona por debajo de −2 °C y jadeo con gotas de sudor por encima de 30 °C.
- **Inversión por temperamento**: un bicho GÓTICO es más feliz cuando llueve; uno SOLAR se hunde. Dos bichos en la misma ciudad no reaccionan igual.
- Geolocalización automática por IP cuando no se dan coordenadas.
- Estado `WEATHER_UNKNOWN` tras 3 fallos o datos de más de 6 h: sin efecto, y una línea honesta.

**Telegram**
- **Quince mensajes de culpa** en siete niveles de escalada, más **cinco positivos**, todos en castellano.
- Antispam duro: máximo 4 al día, 100 min mínimo entre mensajes, silencio de 23:30 a 08:00 con **fusión de toda la cola en un solo resumen a las 08:05**, 48 h de espera por identificador y 6 h por tipo de disparador.
- Tres niveles de prioridad: **P0** (muerte, eclosión) se salta el límite diario y las horas de silencio; **P1** solo el límite diario; **P2** cumple todo.
- **Cualquier interacción resetea el nivel de culpa a cero y tira la cola entera.** El silencio es la recompensa.
- Reintentos a 1 / 5 / 25 min y descarte. Al arrancar se tiran los P2 de más de 6 h para no acribillar al reconectar.
- Interruptor de tres posiciones: OFF / SOLO GRAVES / ON.
- **Barrera de memoria previa a TLS**: no se intenta una conexión si no hay 48 KB en un solo bloque contiguo. Prefiere callarse a reiniciarse.

**Web en el móvil**
- Servidor HTTP propio con **ocho rutas registradas** (`/`, `/api/state`, `/api/sprites`, `/api/action`, `/api/game/start`, `/api/game` y `/api/cfg` en GET y en POST) más el manejador de «no encontrado», y una página de **47.181 B** servida desde flash con el `send_P` de cuatro argumentos, sin sistema de ficheros y sin nube.
- **Sprites en directo desde el aparato**: el navegador dibuja exactamente los mismos mapas de bits de 1 bit que la OLED.
- **Tres minijuegos táctiles** — **ZAMPADA** (saciedad), **BURBUJAS** (higiene) y **NANA** (energía) — con canvas a resolución de dispositivo, acumulador fijo de 1/60 para que la puntuación no dependa de los fps, y vibración donde el navegador la soporte.
- **El navegador es hostil por definición**: manda una puntuación, y es el **servidor** quien decide los incrementos, aplica los topes, exige el 80 % de la duración real contra un testigo que él mismo emitió, y **quema el testigo en cada desenlace** para que una puntuación no se pueda reenviar.
- Limitador de peticiones por fichas (10 fichas, 4/s) delante de cada manejador, más tiempos de espera por acción.
- Segundo móvil en **modo espectador** en vez de bloqueo: ve la partida del otro en directo y conserva los botones de acción.
- Ajustes editables desde la web: WiFi, nombre, zona horaria, coordenadas, modo de Telegram, brillo y silencio. **La contraseña nunca se devuelve.** El cambio de credenciales se aplica *después* de mandar la respuesta, para que la petición que cambia la red no muera matando su propia conexión.
- **PIN de 4 cifras** generado nuevo en cada arranque. Las lecturas quedan abiertas a propósito.

**Códigos QR y provisión**
- Generador de QR propio, versiones 1 a 4, nivel L, que cabe en el ancho de 62 px de la pantalla a 2 px por módulo.
- La pantalla MÓVIL enseña el QR con el **PIN ya incrustado en la dirección**: escanear y jugar, sin teclear nada.
- Modo de red propia con **portal cautivo**: cuando no hay WiFi, monta `NOTTAMAGOCHI-XXXX` en 192.168.4.1 y **alterna entre dos QR** con un toque — uno para unirse a la red y otro para abrir la página.
- Publicación mDNS en `nottamagochi.local`.

**Bluetooth social**
- Emparejamiento BLE **sin conexión**, solo por anuncios: 22 bytes de datos de fabricante con el genoma de 16 bytes y su CRC dentro.
- Protocolo de cortejo de tres tramas (baliza, oferta, confirmación) con verificación de CRC y rechazo de firmas ajenas.
- Requisitos: RSSI ≥ −70 dBm, 30 % de energía, etapa ADOLESCENTE o superior, y 24 h de espera por aparato.
- Los dos aparatos producen **el mismo huevo**, de forma determinista.
- El huevo híbrido queda **en espera** sin reemplazar al bicho vivo: eclosiona cuando muere el actual, o de inmediato si el aparato estaba sin mascota.

**Interfaz**
- Máquina de estados de **16 pantallas** con los siete invariantes de navegación implementados al pie de la letra: retroceso universal, vuelta a casa universal, autoretorno a los 20 s con barra de cuenta atrás, menús en anillo, confirmaciones que empiezan en NO, gestos siempre dibujados abajo, y alertas que nunca se comen una pulsación.
- Reconocedor de **ocho gestos** con dos botones, con detección de pulsación simultánea y repetición automática.
- Tres minijuegos en el propio aparato (**REFLEJOS, MEMORIA, SALTO**) que leen flancos crudos de 25 ms para esquivar la ventana de doble toque.
- Pantalla de árbol genealógico con retratos de 16×16 redibujados desde el genoma guardado, y **cinta de dinastía** de un glifo por generación coloreado según la causa de la muerte.
- Planificador de fotogramas adaptativo: 20 fps normales, 4 fps con la energía baja o con un móvil conectado, 1 fps en el memorial.
- **Todo el texto de cara al usuario en castellano**, en una tabla única, dibujado con `drawUTF8()` y fuentes `_tf` para que los acentos y la eñe salgan bien.

**Persistencia**
- NVS con presupuesto de escritura pensado: marca de tiempo cada 60 s, guardado completo cada 5 min o al cambiar de estado. Endurancia estimada muy por encima de los 10 años.
- Autocomprobación con canario al arrancar. Si la NVS falla, **el juego sigue en RAM** y lo dice, en vez de morirse.
- CRC en cada blob (mascota, ajustes, genoma, huevo pendiente). Un guardado de versión ajena o con CRC malo **no se lee**: se empieza con un huevo nuevo antes que interpretar basura.
- Anillo de **16 antepasados** de 12 bytes cada uno, más contadores de dinastía que se conservan para siempre, de modo que la cabecera siempre puede poner `gen 04 / 41`.

**God mode**
- Doce comandos: velocidad ×1 a ×3600, inyección de ausencias falsas que **ejecutan la ruta offline de verdad**, fijar estadísticas, forzar etapa y forma, matar con la puesta en escena completa, editar/volcar/cargar genoma por serie, forzar el clima, disparar mensajes de Telegram, emparejamiento BLE sintético, inspección del reloj y borrado total con doble confirmación.
- **Reloj virtual**: no toca el reloj del sistema, acumula un desfase aparte, de modo que las cuentas de ausencia siguen siendo coherentes al volver a ×1.
- **La marca es permanente**: entrar pone un bit en el genoma que nunca se borra y se hereda; el árbol genealógico imprime un `*`.
- Telegram silenciado dentro, y los anuncios BLE llevan un bit de depuración para que los aparatos reales rechacen el genoma. No se contaminan dinastías ajenas.
- **No resucita.** Puede matar; no puede revivir.

**Arquitectura**
- 14 módulos con capas estrictas: la simulación no tiene reloj, ni generador aleatorio, ni radio propios — se los alimenta el punto de entrada. `net.cpp` es lo único que toca el ciclo de vida de la radio. `render.cpp` es lo único que construye un objeto U8G2. `webui.cpp` es la única unidad de compilación que incluye la página.
- **Invariante de radio única**: WiFi y Bluetooth nunca están vivos a la vez, porque el ESP32-C3 tiene una sola radio de 2,4 GHz.
- Cero coma flotante en las rutas de estadísticas; `snprintf` sobre búferes fijos; nada de concatenar `String`.

### Corregido

- **Pánico en el arranque (`assert failed: xQueueSemaphoreTake queue.c:1709`), encontrado en placa.**
  `web_begin()` abría el socket de escucha en `setup()`, cuando la radio todavía está en `RADIO_OFF`
  y lwIP no existe: `WebServer::begin()` → `lwip_socket()` → `tcpip_send_msg_wait_sem()` →
  `sys_mutex_lock()` tomaba un mutex `NULL` y FreeRTOS abortaba en bucle. Ahora `web_begin()`
  solo abre el socket en `NPH_STA_UP` o `NPH_AP_PORTAL`; como `web_service()` reintenta en cada
  pasada del `loop()`, el servidor se abre solo en cuanto la radio sube. Las rutas se siguen
  registrando una única vez. (`webui.cpp`, antes de `s_srv.begin()`.)
  Solo reproducible en hardware: compilaba y enlazaba perfectamente.

- **Las acciones desde el móvil ahora guardan al instante**, igual que las del aparato
  (`webui.cpp`, en `/api/action` y `/api/game`). Antes solo las cubría el guardado periódico
  de 300 s, lo que dejaba el libro de ganancias repetible tras un corte de corriente
  (~720 puntos/hora frente al techo de diseño de 60/hora). Las dos rutas mutan el mismo
  bicho, así que las dos persisten igual. No añade temporizador: reutiliza la cadencia
  que `store_save()` ya controla.

*(Revisión de endurecimiento del 2026-08-31, sobre esta misma versión. Cada punto sale de un hallazgo con fichero, línea y traza de fallo reproducida.)*

**Ausencia y reloj — dos fallos críticos que se comían la mascota**

- **Todo arranque sin reloj cobraba una AUSENCIA LARGA completa, incluido el primero.** Que el SNTP no hubiera aterrizado se trataba como prueba de una ausencia, cuando es exactamente lo contrario: la falta de prueba. Un aparato nuevo sin WiFi (una configuración perfectamente soportada: los interruptores de web y clima son del usuario) estrenaba con **seis horas de decaimiento aplicadas a un bicho de 200 ms de vida**: bebé de 15 minutos, saciedad al 24 %, ánimo al 31 %, **dos cacas en pantalla**, enfurruñado y rechazando cualquier orden durante 30 s. Y volvía a pasar **en cada reinicio**. Ahora el punto de entrada consulta la clasificación de arranque que `storage.cpp` ya calculaba y que solo se usaba para un aviso: primer arranque, cuelgue y reinicio por software **demuestran** que no hubo corte de corriente, y una referencia de tiempo que no es una hora real no puede describir ningún hueco. En esos casos se cobra **el cero verdadero**. Verificado: todas las estadísticas quedan en 100,000 milipuntos exactos, cero cacas, sin enfurruñamiento.
- **La primera sincronización con internet marcaba a la mascota de por vida.** En un aparato que nunca había visto un servidor de hora, la referencia guardada es el contador de segundos desde el arranque, no una hora. Al aterrizar el SNTP se restaba una de otra y salían **55 años**, que la escalera de ausencia resuelve como **GRAVE**: −40 de salud de golpe y la **cicatriz permanente y heredable**, justo en el instante en que el dueño terminaba de configurar la WiFi. Ahora la corrección retroactiva exige que la referencia parezca de verdad una hora (`NT_EPOCH_SANE_MIN`, 2017-01-01), y `store_touch_lastseen()` se niega a sustituir una hora auténtica por un contador de encendido.
- **Resta de horas sin signo sin proteger** en el cálculo de soledad: durante la simulación de una ausencia el reloj se rebobina, y la resta desbordaba a ~4.290 millones de segundos, aplicando el multiplicador de soledad (×1,5) al desgaste del ánimo durante toda la recuperación. Era la única resta de épocas sin proteger de todo el árbol; las demás se auditaron una a una.

**Equilibrio del juego**

- **A partir de la tercera generación, todos los adultos eran QUIMERA, para siempre.** El contador de mutaciones solo subía, y la forma escondida se activaba a partir de 3. En una semana de juego real el bloque de puntuación de ramas quedaba muerto, la silueta dejaba de cambiar, y la QUIMERA (la forma con menos desgaste de todas — 900/700/900/900 frente al 1000 neutro — y de las más longevas, ×1,10) se convertía en un premio por limitarse a sobrevivir. Ahora el contador es una **carga genética que la buena crianza paga**: llegar a adulto con nota A resta 3, B resta 2, C resta 1, contra el +1 de cada huevo de muerte. Una estirpe bien criada nunca da quimeras; una estirpe descuidada o que pierde a sus crías antes de la edad adulta da una a la tercera generación y la arrastra hasta que alguien la críe bien. Verificado sobre 20 generaciones: **5 formas adultas distintas** en rotación, ninguna quimera forzada, y la quimera «ganada» sigue apareciendo exactamente en la generación 3 de una estirpe maltratada.
- **Reiniciar el aparato devolvía enteros los cuatro contadores anti-farmeo.** El techo de ganancia por hora, los seis tiempos de espera por acción y el de los minijuegos se rellenaban al arrancar, así que un ciclo de reinicio de dos segundos restauraba el techo horario completo. Ahora un arranque desde memoria supone los contadores gastados y los rellena **al ritmo del tiempo real transcurrido**: un reinicio vale su propia duración de reloj y ni un segundo más. (Tiene un coste para el jugador honesto: ver «Limitaciones conocidas».)

**Configuración, web y radio**

- **`Config::crc16` no se escribía nunca en RAM**, así que el detector de cambios del punto de entrada no podía dispararse jamás y `apply_config()` no volvía a ejecutarse en toda la sesión. Consecuencia visible: **el brillo puesto desde el móvil no llegaba a la pantalla hasta el siguiente reinicio**. El sellado ahora ocurre sobre la estructura del llamante, y también cuando la NVS no está disponible, para que un aparato funcionando solo en RAM siga aplicando sus ajustes.
- **El servidor HTTP ignoraba su propio interruptor.** «Web y QR» en `OFF` solo le decía al gestor de radio que ya no hacía falta WiFi; si la radio seguía encendida por el clima o por Telegram, `/`, `/api/state`, `/api/action` y `/api/cfg` **seguían sirviendo**. Ahora el interruptor cierra y reabre el puerto en la siguiente vuelta del bucle, conservando el PIN.
- **La radio WiFi no se apagaba nunca.** Una vez encendida seguía consumiendo hasta el siguiente reinicio aunque el usuario hubiera apagado web, clima y Telegram. Ahora se apaga, sin pisar nunca la pila Bluetooth ni la pantalla SOCIAL.
- **El módulo del clima escribía en la configuración por detrás del punto de entrada**, sobre una copia privada leída de la NVS. Había dos objetos `Config` vivos y la ubicación detectada automáticamente «se olvidaba» en cuanto tocabas los ajustes — y al revés, una edición sin guardar podía revertirse sola. Ahora hay un solo objeto y un solo escritor.
- **El comprobador del PIN desbordaba en silencio.** El acumulador de 32 bits daba la vuelta, así que cualquier número congruente con el PIN módulo 2³² también autenticaba (el PIN 3821 lo aceptaba también el `4294971117`). No era una escalada — hay que saberse el PIN para construir el alias — pero el comentario que decía que no podía pasar era falso. Acumulador de 64 bits, y comprobado que las demás llamadas dan resultado idéntico bit a bit.

**Concurrencia, persistencia y rendimiento**

- **Carrera al cerrar el Bluetooth**: al vaciar la cola de anuncios se escribía el índice de escritura, que pertenece a la tarea de Bluetooth. Si esa tarea estaba publicando un anuncio en ese instante, la sesión siguiente podía arrastrar hasta una cola entera de vecinos fantasma. Ahora se vacía moviendo solo el índice de lectura, que sí es nuestro.
- **Un corte de corriente en mitad de un entierro duplicaba el antepasado** en el árbol genealógico de 16 huecos y expulsaba a uno real: pérdida de datos silenciosa y permanente. El orden de las dos escrituras se ha invertido, de forma que un reinicio a destiempo repite un entierro que ya es idempotente.
- **Una lectura de NVS por fotograma** mientras la pantalla SOCIAL estaba abierta (hasta 20 por segundo). Ahora se cachea y se invalida en los cuatro únicos sitios que escriben ese dato.

**Web del móvil**

- **La página no se bloqueaba al abrir un minijuego**: un deslizamiento vertical destinado al juego movía el documento por debajo de la capa fija. Además, si la animación de entrada se quedaba congelada (pestaña en segundo plano), la capa se quedaba **traslúcida y un 6 % más pequeña que la pantalla**, y se veían las barras de estado y los botones de acción **a través** del juego. Ahora hay bloqueo de desplazamiento con restauración exacta del punto en que estabas, en todas las salidas posibles (ABANDONAR, fin natural, error de red, desconexión) y sobreviviendo al diálogo de PAUSA; y la animación de entrada nunca pasa por un fotograma transparente ni más pequeño que la pantalla.

### Cambiado

- La **QUIMERA** deja de ser un destino inevitable y pasa a ser una carga genética reversible. Ver «Corregido».
- Al apagar el último consumidor de red (web, clima y Telegram a la vez), **la radio WiFi se apaga**. Antes se quedaba encendida hasta el siguiente reinicio.
- **Web y QR** en `OFF` ahora cierra de verdad el puerto, no solo la razón para tener WiFi.

### Configuración

- **Bloque de configuración de usuario** al principio de `config.h`, en castellano, con todo lo que hay que tocar en un solo sitio y todo opcional: WiFi, nombre, credenciales de Telegram, coordenadas, zona horaria, bandera de SH1106 y cinco interruptores de funcionalidad que **eliminan** el código en tiempo de compilación.
- **GPIO6/GPIO7 para el I2C y GPIO3/GPIO4 para los botones**, elegidos expresamente para **no pisar ningún pin de arranque**. La asignación I2C por defecto de esta variante de placa (GPIO8/GPIO9) no se usa: GPIO8 choca con el LED integrado y GPIO9 es el pin de modo descarga.

### Compilación

- Core **esp32 3.1.1** de Espressif, librería **U8g2 2.35.30**.
- **Requiere Partition Scheme = `Huge APP (3MB No OTA/1MB SPIFFS)`.**
- Flash: **2.066.680 B (65 % de 3.145.728)**. RAM global: **70.788 B (21 %)**.
- Compila **sin un solo aviso** en los ficheros del proyecto, con `--warnings all` (los dos únicos avisos de la compilación completa vienen de `mui.c` y `mui_u8g2.c`, dentro de la librería U8g2).
- La página del móvil ocupa **47.181 B** de los 49.152 permitidos (96 %).

### Limitaciones conocidas

- **No hay zumbador en el hardware v1.** El ajuste «Sonido» guarda su bandera y muestra un icono, pero no suena nada. Todo el vocabulario de vibración y pitidos del diseño (el latido de la muerte, las alertas) está descrito pero no emitido.
- **La web va por HTTP plano y el PIN de 4 cifras viaja en claro** dentro de la propia dirección. Pensado para la red de casa. No lo expongas a internet.
- **El token y el chat_id de Telegram solo se ponen en `config.h`**, nunca desde la web. Es deliberado: un token de bot en un formulario sin cifrar es una mala idea.
- **Los ajustes guardados en NVS mandan sobre `config.h`** a partir del segundo arranque. Para que un `config.h` nuevo entre hace falta «Empezar de cero» (que también mata a la mascota) o cambiarlo desde la web.
- **Si la pantalla no contesta en el bus I2C, el aparato se detiene** y parpadea el LED en vez de seguir simulando a ciegas. Es lo que pide el diseño, pero significa que un cable I2C flojo congela a la mascota.
- **El Bluetooth se corta a las 32 sesiones por arranque**: la pila BLE del ESP32 pierde unos 672 B en cada ciclo de encendido/apagado. El firmware lleva la cuenta y avisa. Reiniciar lo resetea.
- **La geolocalización por IP** puede errar bastante según el operador. Si el clima no cuadra con tu ventana, pon las coordenadas a mano.
- **Tras un corte de corriente, el techo de ganancia por hora se supone gastado.** Es la mitad honesta del arreglo anti-farmeo: no se guarda en memoria persistente, así que al arrancar solo se puede suponer lo peor y rellenar con el tiempo real. Coste medido para el jugador que no hizo nada malo, con un adulto al 20 % de saciedad: los primeros **20 s** el aparato responde «espera» (correcto); entre el **21 y el 59** responde **«está lleno»** aunque tenga hambre (mensaje engañoso); y una comida no vale sus 30 puntos completos hasta pasados unos **29 minutos**. En esa ventana el bicho pierde 6 puntos de saciedad, así que no corre peligro. El arreglo completo es guardar ese contador (4 bytes) junto al guardado periódico; **no está hecho**. Cualquier valor inicial constante distinto de cero volvería a ser farmeable, así que cero es la elección correcta mientras no exista esa persistencia.
- **La ventana de rendimientos decrecientes de los minijuegos (3 h) también se reinicia** al arrancar. Reconstruirla suponiendo lo peor dejaría el pago de jugar a cero durante tres horas después de cualquier apagón inocente, que es un castigo mucho mayor que la trampa que evita. Se ha dejado como está, a propósito.
- **Con la web apagada y sin clima ni Telegram, la pantalla MÓVIL muestra `Conectando...`** indefinidamente, porque la radio está apagada a propósito. Es coherente, pero no se explica en pantalla. En esa misma situación, si el aparato no tiene credenciales de WiFi guardadas, el portal cautivo tampoco sirve de nada: apagar «Web y QR» apaga también la única forma de provisionar la red desde el móvil.
- **La zona horaria cambiada desde la web no entra en vigor hasta el siguiente reinicio.** La cadena POSIX se instala una sola vez al arrancar. Está documentado así en `gametime.cpp` y se guarda correctamente; solo falta reiniciar.
- **No probado sobre hardware físico. Ninguna versión de este firmware se ha ejecutado nunca en una placa real.** Todo lo verificado lo está por compilación, por pruebas en el ordenador (simulación, genoma, entrada, QR, almacenamiento, HTTP, Bluetooth) y por pruebas en un navegador real contra un servidor simulado. Quedan dos cosas que **solo** se pueden cerrar con la placa delante — el controlador de la pantalla (SSD1306 o SH1106) y la polaridad del LED integrado — y para eso está la lista de «Puesta en marcha del hardware» del §3 del README.

---

[1.0.0]: #100--2026-08-31
