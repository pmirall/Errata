> **ARCHIVADO (P10-C5). Documento en espanol, sustituido por `README.md`.**
> Se conserva sin editar. Borrarlo seria borrar la historia: es el registro
> de lo que este proyecto fue y de por que dejo de serlo. Lo unico que sigue siendo material es el §2, el cableado: documenta el OTRO mapa de pines, el conflicto que la decision D1 registra y que sigue abierto.
> Nada de este fichero se mantiene desde P10-C5 en adelante.
>
> **ARCHIVED (P10-C5). Spanish document, superseded by `README.md`.**
> Kept unedited; deleting it would delete the history. The only part still material is §2, the wiring: it documents the OTHER pin map, the conflict decision D1 records and which is still open.
> Nothing here is maintained from P10-C5 onwards.

---

# NOTTAMAGOCHI

> **AVISO: este documento describe la versión Nottamagochi y está obsoleto.**
> El proyecto es ahora Pebblebol (ver `docs/PEBBLEBOL_PRODUCT_SYSTEM_SPEC.md`).
> Ya no existen: el clima, Telegram, los minijuegos del navegador, la muerte y el
> memorial, la disciplina, el peso, el cortejo por Bluetooth ni el interruptor de
> la luz — el sueño lo marcan ahora las horas de luz aproximadas. Se reescribe en
> la fase 10; hasta entonces, lo único fiable de este fichero es el §2 de
> cableado y la sección de puesta en marcha.

**Una mascota virtual de 128×64 píxeles que sabe cuándo la has abandonado, mira el tiempo que hace en tu calle para decidir de qué humor está, y te escribe por Telegram para recordártelo.**

Vive dentro de un ESP32-C3 SuperMini del tamaño de una moneda grande. Nace de un huevo, come, se ensucia, se pone enferma, evoluciona a una de seis formas adultas según cómo la trates, se empareja por Bluetooth con la mascota de otra persona, y al final se muere. Cuando se muere, se muere de verdad: no hay revivir, no hay deshacer, no hay guardar partida. Lo que deja es un huevo con su genoma mutado, y la siguiente generación es visiblemente distinta de la anterior. Al cabo de cinco generaciones ya no reconocerás al bicho con el que empezaste.

Lo que hace que esto no sea otro Tamagotchi más es que **el aparato tiene memoria del tiempo real**. Cuando lo desenchufas y vuelves tres días después, no se reinicia: calcula exactamente lo que ha pasado en esos tres días, te lo dice al segundo (`3 d 11 h 06 min`), y se comporta en consecuencia. Puede que te ignore durante cinco minutos. Puede que te esté esperando un huevo frío.

---

## 1. Qué hace

| | |
|---|---|
| **Lo básico** | Saciedad, ánimo, energía, higiene, salud, cariño, disciplina y peso. Caca en pantalla, enfermedades, medicina, luz para dormir, mimos. Un ciclo de cuidado completo se hace en menos de 90 segundos. |
| **Rendimientos decrecientes** | No puedes forzar la felicidad a base de minijuegos: el segundo da un 70 %, el tercero un 45 %, el sexto un 0 %, y el contador se resetea en 3 h. El coste es **volver**, no machacar botones. |
| **Etapas y evolución** | HUEVO → BEBÉ → NIÑO → ADOLESCENTE → ADULTO → ANCIANO. A las 48 h se decide su forma adulta entre **BOLOTA, ZAMPASALTO, BÚHO, PUNKI, MOHO** y la escondida **QUIMERA**, puntuando cómo lo has cuidado. La QUIMERA no es un premio por sobrevivir: sale de una **carga genética** que se acumula cuando una generación tras otra se cría mal o se muere joven, y que **se paga criando bien** (llegar a adulto con nota A resta 3, B resta 2, C resta 1; cada huevo de muerte suma 1). Tres generaciones malas seguidas y tienes una quimera; dos generaciones buenas y la pierdes. |
| **El tiempo real le afecta el humor** | Consulta Open-Meteo cada 30 min para tu latitud/longitud. Lluvia, niebla, tormenta, nieve y calor cambian tanto la pantalla (gotas, copos, inversión de pantalla en los truenos) como la velocidad a la que se le baja el ánimo. Y **su temperamento genético invierte el signo**: un bicho GÓTICO es más feliz cuando llueve. Dos bichos en la misma ciudad no reaccionan igual. |
| **Telegram pasivo-agresivo** | 15 mensajes de culpa escalados en 7 niveles, más 5 positivos. *«He hecho cuentas. Hoy me has mirado 0 veces. A tu móvil, unas 300.»* Con reglas antispam duras: máximo 4 al día, 100 min entre uno y otro, silencio de 23:30 a 08:00. Cualquier interacción tuya resetea el nivel de culpa a cero y borra la cola. |
| **Web en el móvil, con QR** | El aparato enseña un código QR en su propia pantalla. Lo escaneas y se abre una web servida por el propio ESP32 con el bicho animado en directo, botones de acción y **tres minijuegos táctiles**. Sin app, sin nube, sin cuenta. |
| **Emparejamiento por Bluetooth** | Dos aparatos cerca se ven por BLE, se cortejan y producen un **huevo híbrido** que combina los dos genomas. Es la única forma de romper una estirpe. |
| **«Sé que me apagaste»** | Detecta la diferencia entre un cuelgue (no te acusa: *«Me he mareado un momento.»*) y una desconexión real. Simula lo que pasó mientras no estabas y aplica una escalera de reproches de cinco escalones. |
| **Herencia entre generaciones** | Genoma de 16 bytes exactos con quince campos genéticos empaquetados en bits. Al morir muta y **deriva hacia lo que lo mató**: si murió de hambre baja el apetito, si murió de abandono baja la sociabilidad (y las animaciones se vuelven distantes). Nunca hay dos generaciones iguales: si las mutaciones no cambian nada, el juego fuerza una. |
| **God mode** | Menú de trampas escondido para probarlo todo sin esperar días reales. |

---

## 2. Cableado

### Esquema

```
                        ┌──── USB-C ────┐
                     ┌──┴───────────────┴──┐
                5V ──┤ 5V              GP5 ├── libre
               GND ──┤ G               GP6 ├──► SDA  (pantalla)
               3V3 ──┤ 3V3             GP7 ├──► SCL  (pantalla)
     (botón R) GP4 ──┤ 4               GP8 ├── LED integrado, no tocar
     (botón L) GP3 ──┤ 3               GP9 ├── ✗ PROHIBIDO (pin de arranque)
   ✗ NO USAR   GP2 ──┤ 2              GP10 ├── libre
               GP1 ──┤ 1              GP20 ├── RX
               GP0 ──┤ 0              GP21 ├── TX
                     └─────────────────────┘
           ESP32-C3 SuperMini (comprueba la serigrafía de tu clon)


  ESP32-C3                       OLED SSD1306 128×64 I2C
  ────────                       ───────────────────────
     3V3  ──────────────────────►  VCC
     GND  ──────────────────────►  GND
     GP6  ──────────────────────►  SDA
     GP7  ──────────────────────►  SCL

     GP3  ────[ BOTÓN IZQUIERDO ]────► GND
     GP4  ────[ BOTÓN DERECHO   ]────► GND
```

### Tabla

| Señal | Pin del ESP32-C3 | Va a | Notas |
|---|---|---|---|
| OLED SDA | **GPIO6** | SDA de la pantalla | pad MTCK, no es strapping |
| OLED SCL | **GPIO7** | SCL de la pantalla | pad MTDO, no es strapping |
| OLED VCC | **3V3** | VCC de la pantalla | **no la alimentes a 5 V** |
| OLED GND | **GND** | GND de la pantalla | |
| Botón izquierdo (L) | **GPIO3** | un extremo del pulsador | el otro extremo a **GND** |
| Botón derecho (R) | **GPIO4** | un extremo del pulsador | el otro extremo a **GND** |

**No hacen falta resistencias.** El firmware configura los dos pines como `INPUT_PULLUP`, así que el pulsador solo tiene que unir el pin con masa. Botón sin pulsar = HIGH, pulsado = LOW.

No hay zumbador en la versión 1. El ajuste «Sonido» existe y se guarda, pero de momento solo enciende y apaga un iconito en la barra de estado; no suena nada porque no hay nada que suene. El LED integrado (GPIO8) solo se usa para una cosa: parpadear eternamente si la pantalla no aparece en el bus I2C.

### ⚠️ GPIO2 y GPIO9: LÉELO ANTES DE SOLDAR

**Tu prototipo actual tiene un botón en GPIO2. Hay que moverlo a GPIO3.**

En el ESP32-C3, los pines **GPIO2, GPIO8 y GPIO9 son *strapping pins***: el chip **los lee en el instante del encendido** para decidir en qué modo arranca. No son pines normales que casualmente hagan otra cosa; son la entrada de configuración del arranque, y el firmware no puede protegerte de ellos porque el chip los muestrea antes de que exista firmware.

Qué pasa exactamente:

- **GPIO9 en bajo al arrancar = modo descarga (bootloader serie).** Y un botón a masa es, por definición, un pin que se puede quedar en bajo. Si tienes un botón en GPIO9 y lo estás pulsando (o lo pulsas sin querer, o hay un rebote) mientras enchufas el USB, **el aparato arranca en modo descarga y no ejecuta nada**: pantalla negra, nada responde, y todo apunta a que has fundido la placa. No la has fundido. Solo estás pulsando el botón de "grábame". Este es el fallo que más horas hace perder.
- **GPIO2 debe estar en alto al arrancar.** Un botón que lo pone a masa en el momento del encendido mete al chip en una combinación de arranque que no es la que quieres, con resultados que van desde "arranca raro" hasta "no arranca".
- **GPIO8 es además el LED integrado** en esta placa, y en la variante de fábrica de Arduino está declarado también como `SDA`. Por eso este firmware **no usa la asignación I2C por defecto** y llama a `Wire.begin()` explícitamente sobre GPIO6/GPIO7.

Regla práctica, sin matices: **nada que se pueda pulsar, cortocircuitar o dejar colgando debe ir a GPIO2, GPIO8 ni GPIO9.** Los pines libres y seguros de esta placa son GPIO3, GPIO4, GPIO5, GPIO6, GPIO7 y GPIO10. GPIO11–GPIO19 no están sacados al conector.

---

## 3. Ajustes obligatorios del Arduino IDE

Con la placa conectada, en **Herramientas**:

| Opción | Valor | Por qué |
|---|---|---|
| **Placa** | `Nologo ESP32C3 Super Mini` | Si no la ves, vale `ESP32C3 Dev Module`. |
| **Partition Scheme** | **`Huge APP (3MB No OTA/1MB SPIFFS)`** | **Obligatorio. Sin esto no compila.** |
| **USB CDC On Boot** | **`Enabled`** | **Cámbialo a mano si usas `ESP32C3 Dev Module`: viene en `Disabled`.** El C3 no tiene chip USB-serie, el puerto lo crea el firmware. En `Disabled` el puerto desaparece al arrancar el sketch y la subida termina con un error falso (ver §9). |
| **USB Mode** | `Hardware CDC and JTAG` | Valor por defecto, déjalo. |
| **Upload Speed** | `921600` | Baja a `115200` si la subida falla a media transferencia. |
| **Flash Size** | `4MB (32Mb)` | Solo aparece con `ESP32C3 Dev Module`; la entrada `Nologo ESP32C3 Super Mini` no lo lleva porque fija 4 MB. `Huge APP` necesita 4 MB. |
| **Erase All Flash Before Sketch Upload** | `Disabled` | Déjalo así en el día a día: en `Enabled` **borra la mascota y la estirpe** en cada subida. Solo ponlo en `Enabled` cuando quieras empezar de cero de verdad. |



Además necesitas, en el **Gestor de tarjetas**, el paquete **esp32 de Espressif versión 3.1.1**, y en el **Gestor de librerías** la librería **U8g2 de oliver, versión 2.35.30**. Este proyecto está escrito contra esas dos versiones exactas.

### Por qué el Partition Scheme no es opcional

El firmware compilado ocupa **2.066.680 bytes** (2.088.398 si eliges `ESP32C3 Dev Module`, que trae
otros valores por defecto). El esquema de particiones por defecto (`Default 4MB with spiffs`) reserva
solo **1.310.720 bytes** para la aplicación y regala 1,5 MB a un sistema de archivos que este firmware
**no usa para nada**. Con ese esquema el programa ocupa el **159 %** del espacio disponible: no entra.
El error que verás es:

```
Sketch too big; ... text section exceeds available space in board
```

o bien

```
region `iram0_0_seg' overflowed / text section exceeds available space
```

`Huge APP` reasigna ese espacio muerto y sube el límite a **3.145.728 bytes**: el firmware entra al **65 %**
(66 % en el Dev Module), con más de 1 MB de margen. Es un cambio de un solo desplegable y hay que hacerlo **cada vez que cambias de placa seleccionada**, porque el IDE reinicia esa opción.

Para referencia, la compilación por línea de comandos es:

```
arduino-cli compile --warnings all \
  --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app,CDCOnBoot=cdc \
  sketch_aug30b
```

Uso medido el 2026-09-02 (commit `b53cfe4`, con `petfx`/`actfx`): **2.105.548 B de flash (66 %)** y **72.748 B de RAM global (22 %)**.

### USB CDC on boot, y por qué importa

El ESP32-C3 no lleva chip conversor USB-serie: el propio micro habla USB. «USB CDC On Boot = Enabled» es lo que hace que el puerto COM exista y que `Serial.print()` llegue al Monitor Serie. Con la placa `Nologo ESP32C3 Super Mini` ya viene activado por defecto; con `ESP32C3 Dev Module` **viene desactivado** y tienes que ponerlo tú o no verás una sola línea de log.

Ojo con la consecuencia: como el puerto USB lo crea el firmware, **si tu programa se cuelga o entra en un bucle de reinicio, el puerto COM desaparece del IDE**. Eso no es que la placa esté muerta; es que no hay firmware vivo para mantener el USB. Se arregla con el baile de botones de aquí abajo.

### El baile del botón BOOT (cuando no aparece el puerto)

Si el puerto COM no sale en el desplegable, o la subida falla con `Failed to connect to ESP32-C3: No serial data received`:

1. Mantén pulsado **BOOT** (el botón de la placa, no los tuyos).
2. Sin soltarlo, pulsa y suelta **RESET** (`RST`). Si tu clon no lleva botón de reset: sin soltar BOOT, desenchufa y vuelve a enchufar el USB.
3. Suelta **BOOT**.
4. Refresca la lista de puertos en Herramientas → Puerto. Debería aparecer uno nuevo.
5. Dale a Subir. La placa está en modo descarga y aceptará el firmware aunque lo que tuviera dentro estuviera roto.
6. Cuando termine, pulsa **RESET** (o reenchufa) para que arranque normal.

Si aun así no aparece nada: prueba **otro cable USB-C**. Una cantidad ofensiva de cables baratos solo llevan alimentación y ninguna línea de datos. Es la primera causa de «la placa no la detecta el ordenador».

### Puesta en marcha del hardware

Todo lo demás de este README está verificado por compilación, por pruebas en el ordenador o en un navegador. **Estas dos cosas no se pueden saber sin la placa delante**, porque dependen del clon concreto que te haya tocado. Compruébalas la primera vez que enchufes, en este orden, y anota el resultado.

**Antes de nada**, con el aparato ya programado y enchufado, mira el Monitor Serie a 115200 baudios. La primera línea debe ser:

```
[nt] Nottamagochi 1.0.0
```

Si no sale ni eso, no sigas con esta lista: el problema es de puerto o de arranque y se resuelve en «El baile del botón BOOT» de aquí arriba.

#### 1. ¿SSD1306 o SH1106? (`DISPLAY_IS_SH1106` en `config.h`)

Los dos controladores hablan el mismo protocolo pero la SH1106 tiene **132 columnas de memoria y solo 128 visibles**, así que si el firmware la trata como una SSD1306 la imagen sale corrida.

**Qué mirar:** la pantalla de arranque y la barra de estado de la pantalla principal.

| Lo que ves | Qué significa | Qué hacer |
|---|---|---|
| Todo alineado, el marco pegado a los cuatro bordes, ninguna columna rara | Es una SSD1306 | Nada. Déjalo en `0`. |
| **Todo desplazado 2 px** hacia un lado, con una **franja de 2 px de basura o de píxeles fijos** en el borde izquierdo o derecho | Es una SH1106 disfrazada | Pon `#define DISPLAY_IS_SH1106 1`, recompila y sube |
| Pantalla negra del todo, y el LED integrado parpadeando sin parar | La pantalla **no contesta en el bus I2C** | No es este flag. Revisa SDA→GPIO6, SCL→GPIO7, VCC→**3V3** (no 5 V) y GND |

El desplazamiento es constante y se ve a simple vista: no hace falta medir nada. Si dudas, entra en **MENÚ → ESTADO**: las barras horizontales llegan exactamente al borde derecho en una SSD1306 y se cortan dos píxeles antes en una SH1106 mal configurada.

#### 2. Polaridad del LED integrado (`LED_ACTIVE_LOW` en `config.h`)

En el SuperMini el LED de GPIO8 va **cableado a 3V3 en unos clones y a masa en otros**, así que la misma línea de código lo enciende en una placa y lo apaga en otra. El firmware asume **activo en bajo** (`LED_ACTIVE_LOW 1`, `LED_ON` = `LOW`), que es lo habitual.

**El LED se usa para una sola cosa en todo el firmware: parpadear si la pantalla no aparece en el bus I2C** (pantalla de ERROR, `ui/screen_error.cpp`). Eso lo convierte en la única prueba posible, y hay que provocarla a propósito. El aparato ya no se queda colgado: sigue funcionando y el botón A reintenta el arranque de la pantalla.

**Cómo comprobarlo, sin desoldar nada:** desconecta el cable **SDA** de la pantalla (o el de SCL, da igual) y enchufa el aparato. El patrón que emite el firmware es exacto y fácil de reconocer:

> **destello 120 ms — 120 ms apagado — destello 120 ms — pausa de 1,64 s**, y vuelta a empezar (ciclo de 2,12 s).

Es decir: **dos destellos cortos y seguidos, luego dos segundos de nada.** Al mismo tiempo el Monitor Serie escupe una línea `[FATAL] ...` en cada ciclo, así que si dudas de lo que ves, mira el puerto serie para confirmar que el firmware está efectivamente en el bucle.

| Lo que ves | Qué significa | Qué hacer |
|---|---|---|
| **Dos destellos cortos, pausa de 2 s**, en bucle | La polaridad es correcta | Nada |
| Justo lo contrario: el LED está **encendido casi todo el rato** y hace **dos huecos oscuros cortos** cada 2 s | Está invertida | En `config.h`: `LED_ACTIVE_LOW 0`, `LED_ON 1`, `LED_OFF 0`. Recompila y sube |
| El LED no hace nada, ni encendido ni apagado, pero el serie sí saca `[FATAL]` | Ese clon puede no llevar LED en GPIO8 | Anótalo y sigue: no afecta al juego |
| Ni LED ni `[FATAL]` en el serie | No has llegado al bucle: el firmware no arrancó | Vuelve al «baile del botón BOOT» |

Vuelve a conectar el cable I2C cuando termines. **Con la pantalla funcionando el LED no se enciende jamás**, así que si te lo dejas mal puesto no vas a notarlo hasta el día que se te suelte un cable — que es justo el día en que necesitas que el parpadeo funcione.

#### 3. Los dos botones

Con la pantalla ya funcionando, desde la pantalla principal:

- **Toque corto en el izquierdo** → se abre el **menú de 8 iconos**. Desde ahí, **mantener 600 ms el derecho** vuelve a la principal.
- **Toque corto en el derecho** → *no* abre nada: cambia lo que muestra la barra de estado de arriba. También es una señal válida de que el botón derecho responde.

Si el izquierdo hace lo del derecho y viceversa, tienes los cables cruzados: o los intercambias, o intercambias `PIN_BTN_L` y `PIN_BTN_R` en `config.h`.

Si un botón **dispara solo** o se queda pulsado: comprueba que va del pin a **GND** y a nada más. No hacen falta resistencias; el firmware pone `INPUT_PULLUP`. Y relee el aviso de GPIO2/GPIO9 del §2: un botón en GPIO9 hace que la placa arranque en modo descarga y parezca muerta.

---

## 4. Qué editar antes de compilar

Todo está en el **BLOQUE DE CONFIGURACIÓN DE USUARIO** al principio de `config.h`, entre las dos vallas de almohadillas. Es lo único que tienes que tocar. **Si dejas algo vacío (`""`), el aparato se las apaña solo**; ninguna línea es obligatoria.

```c
#define CFG_WIFI_SSID       ""
```
El nombre de tu red WiFi. **Solo redes de 2,4 GHz** — el ESP32-C3 no tiene radio de 5 GHz, así que si tu router publica una única red que combina ambas bandas puede que tengas que separarlas o usar la banda de 2,4 explícitamente. Si lo dejas vacío, el bicho monta **su propia red WiFi abierta** llamada `NOTTAMAGOCHI-XXXX` para que te conectes con el móvil y le digas las credenciales desde el navegador (ver §9).

```c
#define CFG_WIFI_PASS       ""
```
La contraseña de esa red. Vacío si tu red es abierta.

```c
#define CFG_PET_NAME        ""
```
Cómo se llama, **máximo 12 letras**. Si lo dejas vacío se inventa uno él solo a partir de su genética, combinando dos tablas de sílabas (`Bo-`, `Ti-`, `Ña-` × `-ri`, `-tán`, `-que`…). Ese nombre es determinista: la misma estirpe y la misma generación producen siempre el mismo nombre, en cualquier aparato. Puedes dejarlo vacío tranquilamente; es más bonito que se lo ponga él.

```c
#define CFG_TG_TOKEN        ""
#define CFG_TG_CHAT         ""
```
Telegram. **Si dejas cualquiera de los dos vacío, Telegram se queda apagado del todo** y no se intenta ni una conexión. Cómo conseguirlos está en la §5.

Detalle importante: **el token y el chat_id NO se pueden cambiar desde la web ni desde el menú del aparato.** A propósito: un token de bot en un formulario HTTP sin cifrar es una mala idea. Solo se ponen aquí, en tiempo de compilación.

```c
#define CFG_LATITUDE        ""
#define CFG_LONGITUDE       ""
```
Dónde vives, para el tiempo. En grados, **con punto decimal**, entre comillas. Barcelona sería `"41.3874"` y `"2.1686"`. Si los dejas vacíos, el bicho **lo deduce solo de tu conexión a internet** (consulta ip-api.com y se queda con la ciudad y las coordenadas). Eso suele acertar la ciudad, así que déjalos vacíos salvo que la geolocalización por IP te ponga a 300 km.

```c
#define CFG_TZ_STRING       "CET-1CEST,M3.5.0,M10.5.0/3"
```
La zona horaria en formato POSIX, que es la que decide el horario de sueño (23:00–07:00), las horas de silencio de Telegram y la hora que sale en el epitafio. **La de España peninsular ya viene puesta**, con su cambio de hora de marzo y octubre. Otras:

| Sitio | Cadena |
|---|---|
| Canarias | `"WET0WEST,M3.5.0/1,M10.5.0"` |
| México DF | `"CST6"` |
| Argentina | `"ART3"` |
| Chile | `"CLT4CLST,M9.1.6/24,M4.1.6/24"` |

La zona horaria también se puede cambiar desde la web (**Ajustes → Zona horaria**), pero **no entra en vigor hasta el siguiente reinicio**: la cadena POSIX se instala una sola vez, al arrancar. Se guarda bien, solo hay que apagar y encender. Todo lo demás de esa pantalla (brillo, Telegram, WiFi, coordenadas) sí se aplica en el momento.

```c
#define DISPLAY_IS_SH1106   0
```
**Déjalo en 0.** Solo si al encender ves la imagen desplazada 2 píxeles hacia un lado o una columna de basura en el borde, tu pantalla es una SH1106 disfrazada de SSD1306: pon un `1` y recompila.

```c
#define FEATURE_WEATHER     1
#define FEATURE_TELEGRAM    1
#define FEATURE_BLE         1
#define FEATURE_WEB         1
#define GOD_MODE_ENABLED    1
```
Interruptores generales. Poner un `0` **elimina esa parte del firmware en tiempo de compilación** (no la desactiva: la borra), lo que ahorra flash y RAM. Útil si algo te da problemas o si quieres una versión mínima sin radio.

### ⚠️ La trampa de la configuración guardada

La primera vez que arranca, el aparato copia estos valores a su memoria interna (NVS) y **a partir de ahí manda la copia guardada, no `config.h`**. Es lo correcto (así los cambios que hagas desde la web sobreviven a un apagón), pero tiene una consecuencia contraintuitiva:

> **Si cambias `config.h`, recompilas y subes, puede que no notes ningún cambio.**

Para forzar que los valores nuevos entren, después de subir el firmware ve a **AJUSTES → «Empezar de cero»** (doble confirmación) en el propio aparato. Eso borra toda la memoria y vuelve a leer los valores compilados. **Ojo: también mata a tu mascota y a toda su estirpe.** Si solo quieres cambiar el WiFi, es mucho menos violento hacerlo desde la web (§6).

---

## 5. Cómo crear el bot de Telegram

Tarda tres minutos y no requiere cuenta de desarrollador ni nada parecido.

### Paso 1 — Crear el bot

1. Abre Telegram y busca **`@BotFather`** (el que tiene la marca de verificación azul).
2. Pulsa **Iniciar** / escribe `/start`.
3. Escribe **`/newbot`**.
4. BotFather te pide un **nombre visible**. Pon el que quieras: `Nottamagochi`.
5. Te pide un **nombre de usuario**, que tiene que ser único en todo Telegram y **acabar obligatoriamente en `bot`**. Por ejemplo `mi_nottamagochi_2026_bot`. Si está cogido, te lo dirá y podrás probar otro.
6. BotFather responde con un mensaje que contiene una línea así:

   ```
   Use this token to access the HTTP API:
   8123456789:AAH9xK-3nQzVbR7pLmW2sTfYuI0oP1aBcDe
   ```

   **Ese es tu token.** Cópialo entero, incluidos los dos puntos. Va en `CFG_TG_TOKEN`.

> El token es la contraseña de tu bot. Cualquiera que lo tenga puede escribir en su nombre. No lo subas a GitHub. Si se te escapa, `/revoke` en BotFather te da uno nuevo.

### Paso 2 — Conseguir tu chat_id

El bot no puede escribirte hasta que **tú le escribas primero** (regla de Telegram, no del firmware).

1. Busca tu bot por el nombre de usuario que le pusiste (`@mi_nottamagochi_2026_bot`) y abre la conversación.
2. Pulsa **Iniciar** o mándale cualquier cosa: un `hola` vale.
3. Abre en el navegador **esta URL exacta**, sustituyendo `<TU_TOKEN>` por el token del paso 1:

   ```
   https://api.telegram.org/bot<TU_TOKEN>/getUpdates
   ```

   Fíjate en que **la palabra `bot` va pegada al token, sin barra ni espacio**. Queda algo así:

   ```
   https://api.telegram.org/bot8123456789:AAH9xK-3nQzVbR7pLmW2sTfYuI0oP1aBcDe/getUpdates
   ```

4. El navegador devuelve un texto JSON. Busca dentro:

   ```json
   "chat":{"id":123456789,"first_name":"Pau","type":"private"}
   ```

   **El número que hay en `"id"` es tu chat_id.** Cópialo tal cual (si empieza por `-`, cópialo con el signo menos incluido). Va en `CFG_TG_CHAT`, **entre comillas**, porque en `config.h` es una cadena de texto:

   ```c
   #define CFG_TG_CHAT   "123456789"
   ```

5. Si el JSON sale como `{"ok":true,"result":[]}` — vacío — es que el paso 2 no llegó a cuajar: vuelve a escribirle al bot y recarga la URL.

### Paso 3 — Comprobar

Recompila, sube, y en el aparato ve a **AJUSTES → Telegram** para ver el modo actual. Hay tres:

| Modo | Qué manda |
|---|---|
| **OFF** | Nada. |
| **SOLO GRAVES** | Solo prioridad P0: muerte y eclosión. Nada de culpa. |
| **ON** | Todo, con las reglas antispam completas. |

Si has puesto token y chat, el modo arranca en **ON** automáticamente. El primer mensaje tardará: el juego **no manda nada al arrancar** a propósito, para no acribillarte cada vez que enchufas el cacharro.

---

## 6. Cómo jugar desde el móvil

El ESP32 sirve una página web completa desde su propia memoria. No hay servidor externo, ni nube, ni cuenta, ni app que instalar. Si tu router se cae, la web sigue funcionando mientras móvil y bicho estén en la misma red.

### El flujo, de principio a fin

1. En el aparato: **AJUSTES → MÓVIL**. (Para llegar a AJUSTES: desde la pantalla principal, mantén **los dos botones 1,5 s**; o entra al menú con un toque izquierdo y ve hasta el icono AJUSTES.)
2. Al entrar en esa pantalla el bicho **enciende la WiFi solo** si no estaba encendida. Verás `Conectando...` unos segundos.
3. Cuando conecta, la pantalla se parte en dos: a la izquierda un **código QR** de 62×62 px, y a la derecha su **IP**, la palabra **PIN** y un **número de 4 cifras enorme**.
4. Escanea el QR con la cámara del móvil. Te abre una dirección del estilo `http://192.168.1.47/?k=4821`.
5. **El PIN ya va dentro de esa dirección**, así que si entras por el QR no te pide nada: la web se abre con todo desbloqueado.
6. Si prefieres teclear a mano, escribe **la IP** en el navegador. También se publica `http://nottamagochi.local` por mDNS, que va fino en iPhone, iPad y macOS, pero **en Android es una lotería** según el navegador y la versión — si no carga, no es que esté roto: usa la IP. Entrando a mano quedas en modo lectura, y la primera vez que pulses un botón de acción te saldrá una ventanita pidiendo el PIN. Es el número grande de la pantalla.

### Cómo apagar la web del todo

En **AJUSTES** hay dos filas distintas y conviene no confundirlas:

| Fila | Qué hace |
|---|---|
| **Web y QR** | El interruptor. `ON` / `OFF`. |
| **MÓVIL** | Abre la pantalla del QR y del PIN. |

Poner **Web y QR** en `OFF` **cierra el servidor de verdad, en la siguiente vuelta del bucle**: el puerto deja de escuchar, cualquier `/api/action` que llegue se rechaza a nivel de TCP, y si en ese momento había una partida de minijuego en marcha, su vale se quema. Volver a ponerlo en `ON` reabre el puerto igual de rápido y **con el mismo PIN**, así que el QR que ya escaneaste sigue valiendo.

Y si al apagar la web **tampoco queda encendido el clima ni Telegram**, es decir, si ya no hay nada que quiera la red, el aparato **apaga la radio WiFi entera** en lugar de dejarla consumiendo hasta el siguiente reinicio. Efecto secundario que conviene saber: en ese estado la pantalla **MÓVIL** se queda en `Conectando...`, porque no hay ninguna conexión que enseñar. Es correcto, no es un cuelgue.

### Qué hay en la web

- **El bicho, animado en directo.** Los sprites son los mismos bytes que dibuja la OLED: el ESP32 se los manda al navegador como mapas de bits de 1 bit. Lo que ves en el móvil es literalmente lo que ves en el aparato, a mayor tamaño.
- **Botones de acción**: comer, chuche, limpiar, medicina, luz, mimo, jugar. Con sus tiempos de espera propios (20 s la comida, 15 s la limpieza, 25 s el juego…), los mismos que en el aparato: la web no es un atajo para hacer trampas.
- **Ajustes**: WiFi (SSID y contraseña), nombre del bicho, zona horaria, latitud/longitud, modo de Telegram, brillo, silencio. La contraseña de la WiFi **nunca se devuelve** al navegador: el servidor solo contesta si hay una puesta o no.
- **Tres minijuegos táctiles**, en un carrusel horizontal.

### Los tres minijuegos del móvil

Cada uno sube una estadística distinta, y todos suben un poco el ánimo. **No se pueden encadenar**: dos gastan energía y solo uno la devuelve, así que hay que alternarlos. Hay 120 s de espera por juego tras cada partida enviada.

| Juego | Dura | Sube | Cómo se juega |
|---|---|---|---|
| **ZAMPADA** (*Snack Rush*) | 35 s | **Saciedad** (+30 máx.) | El bicho sostiene un cuenco abajo. Caen bolitas de comida y también bombas. Mueves el cuenco arrastrando un dedo por cualquier parte de la pantalla. Coge comida, esquiva bombas. Cada 10 s cae una **empanadilla dorada** al doble de velocidad que vale 5 puntos y te regala 400 ms de cámara lenta. Encadenar aciertos multiplica hasta ×4; una bomba te rompe la racha. |
| **BURBUJAS** (*Bubble Scrub*) | 30 s | **Higiene** (+60 máx.) | El bicho aparece enorme y cubierto de mugre. Lo frotas con el dedo. **La velocidad importa**: un dedo parado no limpia nada y uno teletransportado no cuenta, así que hay que restregar en círculos de verdad. Y **la mugre vuelve a crecer en las casillas que tocan a otras sucias**, por lo que hay que terminar zonas enteras en vez de embadurnar por encima. Hay tres manchas rebeldes que necesitan tres pasadas. Si lo dejas al 100 % antes del segundo 25, +20 de bonus. |
| **NANA** (*Lullaby*) | 40 s | **Energía** (+45 máx.) | Juego de ritmo. Un anillo se contrae hacia el bicho dormido; tocas cuando encaja. 44 pulsos que van acelerando, con dobles sincopados a partir del segundo 20 y **dos carriles independientes** (mitad izquierda / mitad derecha) desde el segundo 28. Los párpados del bicho caen un poco cada 10 aciertos y el fondo se va oscureciendo: la pantalla se duerme con él. Es el único que **no gasta energía**. |

Si hay dos móviles conectados al mismo bicho, el segundo no se queda fuera: pasa a **modo espectador** y ve la partida del otro en directo, con los botones de acción todavía operativos.

### 🔒 Nota de seguridad, sin adornos

**La web va por HTTP plano y el PIN viaja en claro.** Concretamente:

- No hay HTTPS. Un ESP32-C3 no tiene ni RAM ni CPU para servir TLS y ejecutar el juego a la vez.
- El PIN de 4 cifras viaja **en la propia dirección** (`?k=4821`), en texto legible. Cualquiera que esté en tu red WiFi y mire el tráfico lo ve al primer intento. Aparece también en el historial del navegador de tu móvil.
- Es un PIN de 4 cifras: 10.000 combinaciones. Hay un limitador de peticiones que hace lento el probar a lo bruto, pero no es criptografía; es un cartel de «no pases».
- Las lecturas (`/api/state`, los sprites) **están abiertas sin PIN**, a propósito. Que un vecino vea que tu bicho tiene hambre no es una amenaza.
- El PIN se genera nuevo **en cada arranque**. Reiniciar el aparato invalida cualquier enlace antiguo.

**Traducción práctica:** esto está pensado para tu red de casa. El peor caso realista es que alguien de tu casa le dé de comer al bicho sin permiso. **No abras el puerto 80 en tu router. No lo expongas a internet.** Si estás en una red compartida en la que no confías (una oficina, un piso compartido con desconocidos, una residencia), pon `FEATURE_WEB 0` y juega solo con los botones.

---

## 7. Emparejamiento por Bluetooth

### Qué hace falta

- **Dos Nottamagochis**. No funciona contra un móvil ni contra otro aparato: el protocolo es propio (anuncios BLE de 22 bytes con el genoma de 16 bytes dentro y su CRC).
- Los dos a **menos de unos pocos metros** (hace falta RSSI ≥ −70 dBm).
- Los dos bichos con **al menos 30 % de energía** y en etapa **ADOLESCENTE o superior**. Los bebés no se emparejan.
- Los dos **fuera del tiempo de espera de 24 h** por aparato. Hay normas.

### Cómo se hace

1. En **los dos** aparatos: menú (toque izquierdo desde la pantalla principal) → icono **SOCIAL** → seleccionar.
2. Al entrar en SOCIAL, **la WiFi se apaga y se enciende el Bluetooth.** El ESP32-C3 tiene una sola radio de 2,4 GHz y el firmware garantiza que solo hay una pila viva a la vez, así que mientras estés en esta pantalla no hay web, ni tiempo, ni Telegram. Al salir, se restaura lo que hubiera antes.
3. Verás `Buscando...` y luego la lista de bichos encontrados. Si no hay nadie: *«Nadie cerca. Como siempre.»*
4. **Toque izquierdo** recorre la lista, **toque derecho** propone emparejar. Sale una confirmación (`¿Emparejar?`) que, como todas, empieza con el cursor en **NO**.
5. Se resuelve solo. La probabilidad de éxito sale de la sociabilidad genética de los dos: entre un 25 % y un 90 %.
   - **Éxito:** `Ha salido bien.` + `Va a haber huevo.` Los dos pierden 30 de energía y ganan 25 de ánimo. Telegram manda el P03: *«He conocido a alguien. Va a haber huevo. No preguntes.»*
   - **Fracaso:** `No ha cuajado.` −15 de ánimo y 45 min de espera antes de volver a intentarlo.

### Qué es un huevo híbrido

El huevo que sale de un emparejamiento **no es como el que deja un bicho al morir**. Son dos mecánicas opuestas y ese contraste es la gracia del diseño:

| | Huevo de muerte (partenogénesis) | Huevo híbrido (BLE) |
|---|---|---|
| Padres | Uno | Dos |
| Genes numéricos | Mutan mucho (18 % cada uno) y **derivan hacia lo que mató al padre** | **Se promedian** entre los dos padres, con solo un 8 % de mutación |
| Genes categóricos | Deriva lenta, especies adyacentes | **Cruce 50/50** de cada gen, especies de golpe |
| Estirpe | Se conserva **siempre** | Hereda la del padre que mejor cuidado tuviera, con un 6 % de fundar una **NUEVA ESTIRPE** |
| Efecto a largo plazo | La dinastía se vuelve rara, extrema, endogámica | La dinastía se modera y se llena de rasgos ajenos |

Y hay un premio gordo: si los dos padres son de **especies distintas**, hay un **12 % de recombinación** que produce una de las cuatro formas híbridas exclusivas (imposibles de obtener de otra manera) y además deja al hijo **elegible para QUIMERA**, la sexta forma adulta escondida, con mitades desparejadas y ojos distintos.

**El huevo híbrido no reemplaza a tu bicho vivo.** Se guarda aparte, en espera. Eclosiona cuando el bicho actual muera — o inmediatamente si el aparato estaba sin mascota. Nunca hay un estado en el que el aparato esté vacío.

Detalle honesto: la pila BLE del ESP32 pierde unos 672 bytes cada vez que se enciende y se apaga. El firmware lleva la cuenta y **corta a las 32 sesiones por arranque**, avisando con `Reinicia para seguir emparejando.` Es un límite real, no un capricho; reiniciar lo resetea.

---

## 8. Los dos botones

Solo hay dos botones, así que todo se hace con **gestos**. El reconocedor emite ocho:

| Gesto | Cómo se hace |
|---|---|
| `TOQUE L` / `TOQUE R` | Pulsación corta y suelta. |
| `DOBLE L` / `DOBLE R` | Dos pulsaciones en menos de **280 ms**. |
| `MANTENER L` / `MANTENER R` | Mantener **600 ms**. El izquierdo se repite cada 220 ms (scroll rápido). |
| `AMBOS` | Los dos a la vez (dentro de 80 ms el uno del otro). |
| `AMBOS LARGO` | Los dos a la vez durante **1,5 s**. |

Un toque tarda 280 ms en confirmarse, porque hasta que no pasa esa ventana no se sabe si viene un segundo toque. Se nota poco y los minijuegos del aparato leen los flancos crudos (25 ms) para no arrastrar ese retardo.

### Reglas que valen en todas las pantallas

1. **`MANTENER R` = ATRÁS. Siempre.** Las dos excepciones son el minijuego (ahí pausa) y el memorial (ahí está bloqueado).
2. **`AMBOS LARGO` = volver a la pantalla principal desde donde sea.** La única excepción es el memorial.
3. Toda pantalla que no sea la principal, un minijuego o el memorial **vuelve sola al inicio a los 20 s** sin tocar nada. Los últimos 5 s sale una barrita de cuenta atrás de 3 px abajo.
4. Todos los menús son **anillos**: al llegar al final vuelven al principio. No hay callejones sin salida.
5. Toda confirmación empieza con el cursor en **NO**.
6. Abajo del todo siempre están dibujados los gestos disponibles.
7. Una alerta **nunca se come una pulsación**: el primer botón que toques después de una alerta solo la cierra.

### S0 — PANTALLA PRINCIPAL

| Gesto | Acción |
|---|---|
| `TOQUE L` | Abrir el **MENÚ** |
| `TOQUE R` | Cambiar la barra de estado: iconos → barras → texto |
| `DOBLE L` | Encender/apagar la capa de meteorología (modo foto) |
| `DOBLE R` | **MIMO** — corazoncitos, +4 de cariño |
| `MANTENER L` | Ir directo a **ESTADO** |
| `MANTENER R` | Nada (ya estás en casa): el bicho hace un meneo de «no» |
| `AMBOS` | Silenciar / desilenciar |
| `AMBOS LARGO` | Abrir **AJUSTES** |

### S1 — MENÚ (anillo de 8 iconos)

`COMER · LIMPIAR · JUGAR · SALUD · ESTADO · LUZ · SOCIAL · AJUSTES`

| Gesto | Acción |
|---|---|
| `TOQUE L` | Siguiente icono (da la vuelta) |
| `TOQUE R` | Seleccionar |
| `MANTENER L` | Avance rápido, repitiendo cada 220 ms |
| `MANTENER R` | Volver a la pantalla principal |
| `DOBLE L` | Saltar al icono 0 (COMER) |
| `DOBLE R` | **Repetir la última acción hecha** |
| `AMBOS` | Ayuda de una línea sobre el icono marcado, 3 s |
| `AMBOS LARGO` | Pantalla principal |

`LIMPIAR` y `LUZ` actúan directamente. `SALUD` abre una confirmación (dar medicina). `COMER`, `JUGAR`, `ESTADO`, `SOCIAL` y `AJUSTES` abren su pantalla.

### S2/S3/S8/S9 — Listas verticales (COMER, JUGAR, SOCIAL, AJUSTES)

| Gesto | Acción |
|---|---|
| `TOQUE L` | Siguiente elemento (da la vuelta) |
| `TOQUE R` | Confirmar el elemento marcado |
| `MANTENER L` | Avance rápido |
| `MANTENER R` | Subir un nivel |
| `DOBLE L` | Saltar al primero |
| `DOBLE R` | Saltar al último (siempre `Volver` / `Cancelar`) |
| `AMBOS` | Ayuda de una línea, 3 s |
| `AMBOS LARGO` | Pantalla principal |

### S4 — MINIJUEGO EN CURSO (los del aparato: REFLEJOS, MEMORIA, SALTO)

| Gesto | Acción |
|---|---|
| `TOQUE L` / `TOQUE R` | Los controles del juego |
| `MANTENER R` | **Pausa** → `¿Abandonar? Perderás la partida.` |
| `AMBOS LARGO` | Salir a la fuerza; cuenta como derrota |
| Los demás | Ignorados. Un juego no puede tener gestos escondidos. |

### S5/S6 — ESTADO (barras) y ADN (genoma)

| Gesto | Acción |
|---|---|
| `TOQUE L` | Alternar entre barras ↔ ADN |
| `TOQUE R` | Ir al **ÁRBOL GENEALÓGICO** |
| `MANTENER R` | Atrás |
| `DOBLE R` | Enseñar el genoma crudo en 32 caracteres hexadecimales, 5 s |
| `AMBOS` **5 s**, solo en ADN | Entrar en **GOD MODE** (§10) |
| `AMBOS LARGO` | Pantalla principal |

### S7 — ÁRBOL GENEALÓGICO

| Gesto | Acción |
|---|---|
| `TOQUE L` | Antepasado anterior (más viejo) |
| `TOQUE R` | Antepasado siguiente (más nuevo) |
| `MANTENER L` | Saltar a la generación 0 |
| `MANTENER R` | Atrás |
| `DOBLE R` | Alternar retrato ↔ estadísticas |
| `AMBOS LARGO` | Pantalla principal |

Arriba hay una **cinta de dinastía**: una tira de 128×6 px con un glifo de 4 px por generación, con la forma según de qué murió cada una (macizo = vejez, hueco = hambre, tramado = abandono). Es la vitrina de trofeos y el muro de la vergüenza a la vez.

### S10 — CONFIRMACIÓN (cursor por defecto en **NO**)

| Gesto | Acción |
|---|---|
| `TOQUE L` | Cambiar SÍ / NO |
| `TOQUE R` | Ejecutar la opción marcada |
| `MANTENER R` | Cancelar (= NO) |
| `AMBOS LARGO` | Cancelar y a la pantalla principal |

### S11 — ALERTA (el bicho te llama)

| Gesto | Acción |
|---|---|
| Cualquiera | Cierra la alerta **y salta a la pantalla que resuelve el problema**: hambre → COMER, tristeza → JUGAR, alguien mirando la web → MÓVIL. Un botón lo arregla. |
| `MANTENER R` | Cerrar sin hacer nada (el contador de descuido sigue corriendo) |

### S13 — HUEVO

| Gesto | Acción |
|---|---|
| `TOQUE L` / `TOQUE R` **alternados, 10 veces en 20 s** | **Frotar el huevo**: eclosiona ya |
| Cualquier otro | Se menea. `Todavía no.` |
| — | A los 15 min eclosiona solo |
| **Luto** | Durante los **10 primeros minutos tras una muerte** el huevo no eclosiona de ninguna manera: `Todavía no. Dale un momento.` |

### S12 — MEMORIAL

Bloqueado. `AMBOS LARGO` **no** funciona aquí. La única salida es **`MANTENER R` durante 3 s**, con una barra que se rellena y pone `ENTERRAR`. Hay exactamente una manera de salir y requiere intención.

---

## 9. Solución de problemas

### «No ha subido a la placa», pero el log dice `Hash of data verified`

Si al final de la subida ves esto:

```
Wrote 2088672 bytes (...) at 0x00010000 ... Hash of data verified.
Leaving...
Hard resetting with RTC WDT...
A serial exception error occurred: Cannot configure port ...
el puerto seleccionado no existe o tu placa no esta conectada
```

**Ya ha subido.** `Hash of data verified` significa que la flash está escrita y comprobada; el error viene
*después*, cuando esptool intenta volver a abrir el puerto tras el reinicio.

La causa es `USB CDC On Boot`. El ESP32-C3 no lleva chip USB-serie: el puerto COM **lo crea el propio
firmware**. Con la opción en `Disabled` —que es como viene la entrada `ESP32C3 Dev Module`— en cuanto el
sketch arranca el chip deja de presentar un dispositivo serie, el puerto se esfuma de Windows y esptool se
queda hablando con un puerto que ya no existe.

Ponlo en **`Enabled`** y el error desaparece. De paso recuperas el Monitor Serie, que necesitas para el
escaneo de I2C y para el modo dios.

Si alguna vez el puerto no vuelve a aparecer para poder reprogramar: mantén pulsado **BOOT**, enchufa el
USB, y suelta. Eso fuerza el modo descarga, que siempre presenta puerto.

### La pantalla está negra / no se ve nada

En orden, de más probable a menos:

1. **Mira el LED de la placa.** Si el LED integrado hace **dos parpadeos rápidos, una pausa de segundo y medio, y vuelta a empezar, eternamente**, el firmware está vivo y te está diciendo que **no encuentra la pantalla en el bus I2C**. Ha buscado en 0x3C y 0x3D, no ha contestado nadie, y ha parado a propósito. Abre el **Monitor Serie a 115200**: verás un **escaneo completo del bus**, dirección por dirección, con `ACK at 0x..` por cada cosa que conteste, o `bus is silent: check wiring, 3V3 and GND.` si no hay nadie. Ese log es el diagnóstico; en la pantalla no puede salir nada, porque la pantalla es justo lo que falta.
2. **Cables cruzados.** SDA va a **GPIO6** y SCL a **GPIO7**. Es el error más común y no da ningún síntoma distinto de «negro». Cámbialos y prueba.
3. **Alimentación.** VCC de la pantalla a **3V3**, no a 5V. Y comprueba que el módulo OLED no tiene los pines en otro orden: hay clones con VCC y GND intercambiados respecto al modelo de al lado en la misma tienda.
4. **Dirección I2C.** Casi todas son 0x3C, algunas 0x3D. El firmware prueba las dos solo. Si tu módulo tiene un jumper o unas islas de soldadura para elegir dirección y está en otra cosa, ponlo en 0x3C.
5. **Se ve, pero corrida 2 px o con basura en un borde:** no es un fallo de cableado, es que tu pantalla es una **SH1106** vendida como SSD1306. Pon `#define DISPLAY_IS_SH1106 1` en `config.h` y recompila.
6. **Se ve muy tenue:** sube el brillo en AJUSTES → Brillo, o cambia `OLED_CONTRAST_DEFAULT` (140 por defecto, rango 0–255).

### La placa no arranca / se queda muerta

**Casi siempre es GPIO9.** Repasa la §2: si tienes un botón, un cable o cualquier cosa que pueda poner GPIO9 a masa en el momento del encendido, el chip arranca en modo descarga y **no ejecuta tu programa**. Parece una placa fundida y no lo es. Desconecta lo que haya en GPIO9 y GPIO2 y vuelve a enchufar.

Segundo sospechoso: **el cable USB**. Prueba otro. Muchos cables USB-C baratos son solo de carga.

Tercero: si el puerto COM ha desaparecido del IDE, no es que la placa esté muerta, es que no hay firmware manteniendo vivo el USB. Haz el baile del botón BOOT (§3) y sube el firmware otra vez.

### No compila: «Sketch too big» / «text section exceeds available space»

**Partition Scheme.** Herramientas → Partition Scheme → **`Huge APP (3MB No OTA/1MB SPIFFS)`**. El firmware ocupa 2,06 MB y el esquema por defecto solo reserva 1,25 MB. Y recuerda que el IDE **reinicia esa opción cada vez que cambias de placa seleccionada**, así que si de repente vuelve a fallar sin que hayas tocado nada, es que se te ha reseteado el desplegable.

### No se conecta a la WiFi

El aparato lo intenta **3 veces**, con esperas crecientes (2 s, 4 s, 8 s). Si falla las tres, o si no hay SSID configurado, se rinde y **monta su propia red**:

1. Con el móvil, busca una red WiFi abierta llamada **`NOTTAMAGOCHI-XXXX`** (las XXXX son los últimos dígitos de la MAC de tu placa; el nombre exacto sale en la pantalla MÓVIL). No tiene contraseña.
2. Conéctate. La mayoría de móviles abren solos la ventana de «iniciar sesión en la red» — eso es el **portal cautivo**: el aparato responde al sondeo del móvil con una redirección a su propia página.
3. Si no se abre sola, escribe a mano **`192.168.4.1`** en el navegador.
4. En la sección de ajustes de la web, mete tu SSID y contraseña y guarda.
5. El cambio de credenciales **se aplica después de mandar la respuesta**, a propósito: si se aplicara antes, la petición que cambia la red moriría matando su propia conexión. Verás caer el WiFi del bicho un segundo; luego se conecta a tu red.

En esa misma pantalla MÓVIL, cuando está en modo red propia, **un toque de botón alterna entre dos códigos QR**: uno que es directamente el «únete a esta WiFi» (para que no tengas que buscarla en la lista) y otro que es el enlace a la página. Muy cómodo.

Comprobaciones aparte:
- **Solo 2,4 GHz.** El ESP32-C3 no ve las redes de 5 GHz. Si tu router unifica ambas bandas bajo el mismo nombre, a veces hay que separarlas.
- **El SSID distingue mayúsculas** y no admite más de 32 caracteres.
- Si tu red es **abierta**, deja `CFG_WIFI_PASS` vacío. No pongas un espacio.

### Telegram no dice nada

Repasa en este orden:

1. **¿Están puestos los dos valores?** Si `CFG_TG_TOKEN` **o** `CFG_TG_CHAT` está vacío, Telegram queda apagado por completo. Los dos, o ninguno.
2. **¿Le has escrito tú primero al bot?** Telegram no deja que un bot inicie una conversación. Si nunca has pulsado «Iniciar», no recibirás nada nunca.
3. **¿Has borrado la configuración guardada después de cambiar `config.h`?** Ver la trampa del §4. AJUSTES → «Empezar de cero», o cámbialo desde la web.
4. **¿Hay WiFi?** Telegram va por TLS y necesita una conexión de estación funcionando. Si el bicho está en modo red propia (portal), no hay internet.
5. **La barrera de memoria.** Telegram usa TLS, y una sola sesión TLS necesita **dos buffers de 16 KiB en DRAM interna**. El firmware **comprueba antes de intentarlo** que hay al menos **48 KB en un solo bloque contiguo** libres, y si no los hay **no lo intenta** en vez de reiniciarse por falta de memoria. Esto es lo que hace que Telegram se calle a veces sin dar error. Si te pasa mucho: baja el brillo no ayuda, pero **poner `FEATURE_BLE 0`** libera bastante memoria, y **salir de la pantalla SOCIAL** también (el Bluetooth y el WiFi no conviven).
6. **El silencio es normal.** Máximo 4 mensajes al día, 100 minutos entre uno y otro, nada entre las 23:30 y las 08:00 (se acumula y sale un resumen único a las 08:05), 48 h de espera para repetir el mismo mensaje. Y sobre todo: **cualquier interacción tuya con el bicho pone el nivel de culpa a cero y tira la cola entera a la basura.** Si lo cuidas, no te escribe. Ese es el diseño.
7. **Token mal copiado.** Comprueba en el navegador que `https://api.telegram.org/bot<TU_TOKEN>/getMe` devuelve `"ok":true`. Si devuelve `401 Unauthorized`, el token está mal o lo has revocado.

### El bicho ha aparecido muerto / medio muerto tras dejarlo apagado

**No es un fallo. Es la mecánica principal del juego.** El aparato guarda el momento exacto en que lo viste por última vez y, al arrancar, simula todo lo que pasó mientras no estaba encendido.

La escalera de reproches, con los tiempos reales:

| Tiempo fuera | Nivel | Qué pasa |
|---|---|---|
| < 1 h | — | Nada. |
| 1–6 h | **CORTA** | −3 cariño, −5 ánimo. Levanta la cabeza y saca un `?`. *«¿Dónde estabas? {tiempo}.»* |
| 6–24 h | **LARGA** | −12 cariño, −20 ánimo, −5 salud. **Se enfurruña**: te da la espalda 30 s y rechaza la primera comida. |
| 1–3 días | **ABANDONO** | −35 cariño, −45 ánimo, −20 salud. Polvo y telarañas en pantalla. **Ignora todo durante 120 s**, con el contador visible. |
| 3–7 días | **GRAVE** | −70 cariño, −80 ánimo, −45 salud. El sprite se dibuja al 50 % de trama, como desvaído. **300 s de rechazo.** Y se le queda una **muesca permanente de 2 px en la silueta, para toda la vida, que además se hereda.** |

Los rechazos **sí se pueden acortar**: cada mimo (`DOBLE R` en la pantalla principal) quita 20 segundos. Siempre hay un camino de vuelta.

Las cuentas, para que sepas a qué atenerte: **una noche de 8 h es segura** (llega tocado pero recuperable en diez minutos), **un día entero llega vivo y hecho polvo**, y **un fin de semana entero lo mata**. Estando encendido y sin tocarlo, se muere a las **19 h**; apagado, el desgaste va al 55 % y aguanta unas **34 h**. Y a los **3 días está muerto seguro**, en cualquier configuración.

Un par de matices que conviene conocer:

- **Un cuelgue no cuenta como abandono.** El firmware distingue un reinicio por software de un corte de corriente real (con un dato en la memoria RTC, que sobrevive al reinicio pero no al apagón). Si se colgó, te dice *«Me he mareado un momento.»* y no te acusa de nada.
- **Si no consigue la hora por internet**, no se inventa la ausencia, pero tampoco te la perdona a ciegas. El aparato distingue tres situaciones:
  - **No tenía a quién echar de menos.** Primer arranque de un aparato nuevo, cuelgue, o reinicio por software con la memoria RTC intacta (que demuestra que nunca se fue la corriente). Aquí la ausencia es **cero de verdad** y no pasa nada: no hay enfurruñamiento, ni cacas de bienvenida, ni estadísticas por los suelos.
  - **La referencia guardada no es una hora, es un contador de encendido.** Le pasa a un aparato que todavía no ha visto nunca un servidor de hora. De un contador de segundos desde el arranque **no se puede deducir ninguna ausencia**, así que tampoco se cobra ninguna.
  - **Hubo un corte de corriente real y sí había una hora guardada de verdad, pero todavía no ha vuelto a sincronizar.** Solo aquí entra el modo «no lo sé»: aplica el escalón **LARGA como suelo** (nunca peor) y te dice *«No sé cuánto tiempo ha pasado. Sé que fue mucho. Y sé que fuiste tú.»* Cuando el reloj se sincroniza, **recalcula y te cobra la diferencia real** — y solo si la referencia era una hora auténtica, no un contador de encendido.

  El resultado práctico: **un aparato sin WiFi ya no te acusa de abandonarlo en cada arranque**, y provisionar la WiFi por primera vez ya no le mete un castigo de tres días con cicatriz permanente incluida.
- **Si murió mientras no estabas**, no ves un cadáver: ves el memorial con la fecha y hora **verdaderas** de la muerte, la causa, y luego el huevo — con la frase que remata: *«El huevo lleva 1 d 4 h esperando.»*
- **Si el huevo lleva más de 72 h esperando**, sale **frío**: nace con 90 de salud en vez de 100, pero gana una mutación extra forzada, la «marca del superviviente». *«Se enfrió. Ha cambiado.»* El peor desenlace produce el genoma más interesante, a propósito: volver después de una semana nunca te castiga con un reinicio aburrido.

### La página web no carga o va a trompicones

- Comprueba que el móvil está **en la misma red WiFi** que el bicho. Con datos móviles no funciona.
- Prueba la IP directa antes que `nottamagochi.local`: el mDNS falla en algunas redes con aislamiento de clientes.
- Si la web va lenta mientras juegas en el aparato, es normal: cuando hay un móvil hablando, la pantalla OLED **baja a 4 fps a propósito** durante 10 s para dejarle CPU al servidor.
- Un `429` significa que has pulsado demasiado rápido. Hay un limitador de 10 fichas que se rellenan a 4 por segundo.
- Un `403` abre la ventanita del PIN. Si no te lo sabes, míralo en la pantalla MÓVIL del aparato.

### Se le olvida todo cada vez que arranca

Si al arrancar sale *«No consigo recordar nada.»*, es que la memoria NVS no responde o el canario de comprobación ha fallado. El juego **sigue funcionando en RAM**, pero no guarda nada: cada vez que lo enchufes tendrás un huevo nuevo. Lo más habitual es una partición NVS corrupta. La solución es borrar la flash entera y volver a subir el firmware:

```
esptool.py --chip esp32c3 --port COM5 erase_flash
```

(En instalaciones recientes el ejecutable se llama `esptool` a secas. Cambia `COM5` por tu puerto; en Linux/macOS será algo como `/dev/ttyACM0`. Si la placa no responde, ponla en modo descarga con el baile del botón BOOT del §3 antes de lanzar el comando.)

Si prefieres no salir del IDE: Herramientas → **Erase All Flash Before Sketch Upload → `Enabled`**, sube una vez, y **vuelve a ponerlo en `Disabled`** — si te lo dejas puesto, cada subida posterior mata a la mascota.

**Esto borra la mascota, la estirpe y los ajustes.** No hay copia de seguridad. Es lo que hay.

---

## 10. God mode

Un menú de trampas escondido para poder probar en cinco minutos cosas que en juego real tardan días. **No hay ninguna pista en pantalla de que existe.**

### Cómo se entra

**MENÚ → ESTADO → `TOQUE L` (para pasar a la pantalla de ADN) → mantener LOS DOS BOTONES 5 segundos.**

Sale una barra que se rellena con puntitos (`···`). Si sueltas antes, no pasa nada. Solo funciona en la pantalla de ADN, no en la de barras.

Se sale con `MANTENER R`, que vuelve a la pantalla principal y fuerza un guardado.

### Cómo saber que estás dentro

Los 9 píxeles de arriba se invierten en una barra blanca con texto negro que pone **`GOD x60`** (o la velocidad que hayas puesto). **No puedes estar en god mode sin saberlo.**

### Los 12 comandos

| # | Comando | Qué hace |
|---|---|---|
| 1 | `VELOCIDAD` | Multiplica el tiempo: ×1, ×6, ×60, ×360, ×3600. A ×3600, **un segundo real es una hora de juego**: un día entero de vida en 24 segundos. |
| 2 | `SALTAR AUSENCIA` | Se inventa una ausencia de 1 h / 6 h / 24 h / 72 h / 168 h / 720 h y **ejecuta la simulación offline de verdad**, incluida la muerte si toca. La forma rápida de ver toda la escalera de reproches. |
| 3 | `FIJAR STAT` | Elegir una estadística y ponerla a 0 / 25 / 50 / 100. |
| 4 | `FORZAR ETAPA` | Saltar a cualquier etapa de vida; recalcula las puntuaciones de rama. |
| 5 | `FORZAR FORMA` | Elegir a dedo cualquiera de las 6 formas adultas, QUIMERA incluida. |
| 6 | `MATAR` | Elegir la causa y ver la muerte entera, **los 22 segundos completos**. No hay atajo a propósito: si estás probando, la sufres tú también. |
| 7 | `GENOMA` | `ALEATORIO` / `EDITAR` gen a gen / `VOLCAR` 32 hex por serie y pantalla / `CARGAR` pegando 32 hex por el puerto serie. |
| 8 | `CLIMA` | Forzar cualquier código meteorológico WMO, día/noche y temperatura, ignorando la API. |
| 9 | `TELEGRAM` | Mandar cualquiera de los mensajes T01–T15 a demanda, saltándose el antispam. |
| 10 | `BLE FALSO` | Fabricar un genoma de pareja y ejecutar el emparejamiento entero **sin necesitar un segundo aparato**. |
| 11 | `RELOJ` | Ver `now_epoch`, `last_seen_epoch`, el desfase virtual y el estado del SNTP. |
| 12 | `BORRAR TODO` | Doble confirmación (dos ventanas distintas, las dos empezando en NO) → borra la NVS y empieza con un huevo de generación 0. |

### Las reglas que impiden que god mode sea trampa de verdad

- **No toca el reloj del sistema.** Acumula un desfase virtual aparte, así que cuando vuelves a ×1 las cuentas de ausencia siguen siendo coherentes en vez de convertirse en un disparate.
- **Entrar marca el genoma para siempre.** Se pone un bit `god_tainted` que **nunca se borra** y que se hereda a todos los descendientes. El árbol genealógico imprime un `*` junto a esa generación. La vitrina de trofeos no miente.
- **Telegram se calla** mientras estás dentro, salvo el comando 9.
- **Los anuncios BLE llevan un bit de «depuración»**, y los aparatos de verdad rechazan ese genoma. No se contaminan las dinastías de otra gente.
- **No resucita.** God mode puede matar; no puede revivir. No hay excepción.
- Cada cambio de estado suelta una línea parseable por el puerto serie:
  `GOD,<epoch>,<etapa>,<sac>,<ánimo>,<energía>,<higiene>,<salud>,<cq>,<gen>,<genoma_hex32>`

Si no quieres nada de esto en tu compilación, pon `GOD_MODE_ENABLED 0` en `config.h` y desaparece del binario.

---

## Apéndice A — Los ficheros

| Fichero | Qué es |
|---|---|
| `sketch_aug30b.ino` | Punto de entrada. `setup()` cablea los módulos en orden de dependencia, `loop()` los bombea. **Cero lógica de juego.** |
| `config.h` | El bloque de usuario y todas las constantes. Solo datos. |
| `nt_types.h` | Estructuras compartidas: `Genome` (16 B), `PetSave` (128 B), `Config` (256 B), `PendingEgg` (24 B), `AncestorRecord` (12 B), enumeraciones. Todos los tamaños están fijados con `static_assert`. |
| `strings_es.h` | **Todo** el texto en castellano, en una tabla numerada. |
| `sim.cpp/.h` | El juego: estadísticas, decaimiento, enfermedad, evolución, muerte, ausencia. Sin reloj propio, sin radio, sin coma flotante. |
| `genome.cpp/.h` | Los 16 bytes, la herencia, las mutaciones. |
| `render.cpp/.h` | **El único sitio donde se construye un objeto U8G2.** Buffer completo, planificador de fps. |
| `ui.cpp/.h` | La máquina de estados de 16 pantallas y todos los gestos. |
| `input.cpp/.h` | El reconocedor de gestos de dos botones. |
| `storage.cpp/.h` | NVS: guardado, configuración, antepasados, huevo pendiente. |
| `gametime.cpp/.h` | Reloj, SNTP, zona horaria. |
| `net.cpp/.h` | **El único sitio que toca el ciclo de vida de la radio.** Una sola pila viva a la vez. |
| `webui.cpp/.h` + `index_html.h` | El servidor HTTP y la página (**47.181 B** en flash, incluida desde **una sola** unidad de compilación). |
| `weather.cpp/.h` | Open-Meteo y la tabla de códigos WMO. |
| `telegram.cpp/.h` | Cola, antispam, escalera de culpa. |
| `ble_social.cpp/.h` | Anuncios BLE, cortejo, cría. |
| `qr.cpp/.h` | Generador de códigos QR (versiones 1–4, nivel L). |
| `godmode.cpp/.h` | El menú de trampas. |
| `sprites.h` | Todos los mapas de bits, en PROGMEM. |

## Apéndice B — Estado y límites conocidos

- **Compila limpio** contra el core esp32 3.1.1 y U8g2 2.35.30: cero avisos en los ficheros del proyecto.
- **No hay zumbador** en la versión 1. El ajuste «Sonido» guarda un flag y muestra un icono; no suena nada.
- **El token de Telegram solo se pone en `config.h`.** No es editable desde la web a propósito.
- **Si la pantalla no responde en el bus I2C, el aparato se para** y parpadea el LED. No sigue simulando a ciegas. Si prefieres que siga funcionando sin pantalla, hay que cambiar una línea en el `.ino`.
- **La web es HTTP plano y el PIN viaja en claro.** Ver §6.
- **El Bluetooth corta a las 32 sesiones por arranque** por una fuga de ~672 B de la pila del ESP32 en cada ciclo. Reiniciar lo resetea.
- La configuración guardada en NVS **manda sobre `config.h`** a partir del segundo arranque. Ver la trampa del §4.
- **Tras un corte de corriente, el bicho tarda un rato en poder comer bien.** El techo de ganancia por hora (lo que impide farmear) no se guarda en memoria persistente, así que al arrancar se supone gastado y se rellena con el tiempo real transcurrido. Consecuencia medida: los primeros **20 s** el aparato contesta honestamente «espera», pero entre el segundo **21 y el 59** contesta **«está lleno»** aunque el bicho esté al 19 % de saciedad, y una comida no vale los 30 puntos completos hasta unos **29 minutos** después. No es un bloqueo y el bicho no corre peligro (pierde 6 puntos de saciedad en esa ventana), pero el mensaje es engañoso. Se arregla del todo guardando ese contador en NVS; no está hecho.
- **Con la web apagada, la pantalla MÓVIL se queda en `Conectando...`** si tampoco hay clima ni Telegram encendidos, porque la radio está deliberadamente apagada. Es correcto, pero no lo explica en pantalla.
- **No probado sobre hardware físico. Nada de este firmware se ha ejecutado nunca en una placa real.** Todo lo que dice este README está verificado por compilación, por pruebas en el ordenador (simulación, genoma, entrada, QR, almacenamiento, HTTP) o en un navegador real contra un servidor simulado. Los dos puntos que **solo** se pueden cerrar con la placa delante están en la lista de «Puesta en marcha del hardware» del §3.
