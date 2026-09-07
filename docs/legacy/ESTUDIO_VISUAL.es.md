> **ARCHIVADO (P10-C5). Documento en espanol, sustituido por `docs/content.md`.**
> Se conserva sin editar. Borrarlo seria borrar la historia: es el registro
> de lo que este proyecto fue y de por que dejo de serlo. Estudio de pulido visual escrito contra el codigo de Nottamagochi. El atlas y las animaciones que de verdad se construyeron son los de las fases 4, 6, 9 y 10.
> Nada de este fichero se mantiene desde P10-C5 en adelante.
>
> **ARCHIVED (P10-C5). Spanish document, superseded by `docs/content.md`.**
> Kept unedited; deleting it would delete the history. A visual-polish study written against the Nottamagochi code. The atlas and the animations that were actually built are phases 4, 6, 9 and 10.
> Nothing here is maintained from P10-C5 onwards.

---

# Nottamagochi — Estudio de pulido visual

Qué se puede hacer, de verdad, en un SSD1306 de 128x64 y 1 bit movido por un
ESP32-C3, para que esto se sienta como un producto y no como una demo.

Todo lo que hay aquí está contrastado contra el código real del sketch. Los
costes en milisegundos son cálculos sobre el bus y la librería, no medidas
tomadas en la placa; están marcados como tales donde importa. Las cifras de
flash y RAM salen de una compilación real con el FQBN de la placa
(`esp32:esp32:esp32c3`, `PartitionScheme=huge_app`, `CDCOnBoot=cdc`).

**Versión interactiva:** `docs/estudio_visual.html` — el mismo estudio con
simulaciones en vivo de la pantalla (reposo actual vs propuesto, los cuatro
temperamentos, el tramado recortado a la silueta, la inercia del carrusel y el
parpadeo). Se abre haciendo doble clic; no necesita servidor.

---

## 0. Las tres cosas que más importan

1. **El cuello de botella es el bus I2C, no la CPU.** `sendBuffer()` cuesta
   ~24 ms de los 50 ms de presupuesto por frame. La CPU está al ralentí: le
   sobran unos 4 millones de ciclos por frame. Consecuencia directa: **todo
   efecto que no obligue a mandar un frame nuevo es prácticamente gratis**, y
   el SSD1306 tiene una familia entera de esos efectos que ahora mismo no
   usamos ni una vez.

2. **El bicho está clavado en el centro de la pantalla.** Se mueve ±1 px
   arriba y abajo y alterna entre 2 fotogramas cada 420 ms. Nunca anda, nunca
   se gira, nunca toca un borde, nunca mira nada. Tiene 44 px de escenario
   libre a cada lado y no los pisa jamás. Este es el hueco más grande del
   proyecto.

3. **Cuatro genes cosméticos se heredan, mutan, se imprimen como texto en la
   pantalla de ADN… y no cambian ni un píxel.** `gene_pattern` (16 valores),
   `gene_body_size` (8), `gene_ear_horn` (4) y `gene_rare` sólo existen como
   palabras. Dos bichos de la misma especie y etapa se dibujan byte a byte
   idénticos. El juego te miente sobre tu propia mascota.

---

## 1. El presupuesto real

### Lo que cuesta un frame

| Concepto | Coste | Nota |
|---|---|---|
| Framebuffer | 1024 B | 128x64 a 1 bit, sin grises. La única "gama" es el tramado |
| `sendBuffer()` a 400 kHz | **~23,9 ms** | ~1064 B en el cable x 9 bit-times |
| Presupuesto por frame a 20 fps | 50 ms | `FRAME_BUDGET_US` |
| **Sobrante para dibujar** | **~26 ms** | ≈ 4,1 M ciclos a 160 MHz |
| `drawXBM` de 40x40 | ~200 operaciones de byte | despreciable |
| `rd_dither_rect` a pantalla completa | 1024 operaciones de byte | despreciable |
| Un comando de registro vía `sendF()` | ~4 B ≈ **90 µs** | **~250x más barato que un frame** |

La lectura importante: dibujar es gratis, **enviar** es caro. Con 20 fps ya
estamos al 48 % de ocupación del bus. Cualquier plan que pase por "subir los
FPS" choca contra esto; el guardia de sobrecoste de `rd_end_frame()`
simplemente empujará el siguiente frame y además le robará tiempo a
`web_service()` y `tg_service()`.

### Espacio libre

| Recurso | Usado | Libre |
|---|---|---|
| Flash (huge_app) | 2.105.548 B (66 %, medido 2026-09-02) | **~1,04 MB** |
| RAM (globales) | 72.748 B (22 %, medido 2026-09-02) | ~249 KB |
| Arte de sprites | 10.247 B (medido por el compilador, P9-C3) | 1.017 B hasta `SPRITE_DATA_BYTES_MAX` = 11.264 B |

Flash y RAM no son la restricción de nada de lo que viene. La cifra de arte
estaba obsoleta hasta P9-C3: decía 10.893 B contra un tope de 14.336 B, dos
números que ya no existían. Hoy `SPRITE_DATA_BYTES` la calcula el compilador
recorriendo las tablas (64 sets de 24x24x2 más iconos y emotes) y el tope es el
estado final más un margen declarado, no una asignación de transición. Subirlo
NO es "basta con": hay que mover `PB_DATA_BYTES_MAX` en `tools/gen_sprites.py`
y `SPRITE_DATA_BYTES_MAX` en `data/sprites.h` a la vez, y decirlo en el commit.

### Las dos palancas para comprar movimiento

**A. Subir el I2C a 800 kHz.** `OLED_BUS_CLOCK_HZ` está en 400000 con el
comentario "800k only after HW validation". A 800 kHz `sendBuffer()` baja a
~12 ms y 30 fps pasan a ser cómodos. Es un cambio de una línea. El riesgo es
real pero acotado: la hoja de datos del SSD1306 especifica 400 kHz, aunque
en la práctica la mayoría de módulos aguantan 800 kHz–1 MHz con cables
cortos. Se valida en cinco minutos con la placa delante: si el panel se
corrompe o deja de responder, se vuelve atrás. **Conviene hacerlo pronto,
porque duplica el margen de todo lo demás.**

**B. Actualizaciones parciales.** `updateDisplayArea(tx, ty, tw, th)` existe
en esta versión de U8g2 (`U8g2lib.h:208`) y trabaja en tiles de 8 px. La
banda del bicho son las filas 8..55 = 6 filas de tile. Si sólo se ha movido
el bicho y ocupa ≤48 px de ancho, son 6x6 = 36 tiles = 288 B ≈ 6,5 ms, un
factor 4. El precio es llevar la contabilidad de la región sucia, que es
complejidad real y fácil de romper. **Recomendación: no lo hagas de forma
general.** Resérvalo para casos concretos de alta frecuencia (el minijuego
de reflejos, el temblor del bicho) donde sabes exactamente qué cambia.

---

## 2. Nivel 0 — los efectos que salen gratis

El SSD1306 tiene registros que cambian **cómo se presenta** el framebuffer
sin tocarlo. Cuestan 2 bytes en el cable en lugar de 1064. Ahora mismo no
usamos ninguno excepto el contraste.

El mecanismo ya está disponible en esta versión de U8g2:

```cpp
rd_u8g2().sendF("ca", 0xD3, offset);   // comando + argumento
```

(`U8g2lib.h:72` → `u8x8_cad_vsendf`, y el CAD del SSD1306 en
`u8x8_cad.c:534` trata correctamente `SEND_ARG`. Verificado leyendo la
librería; no probado todavía en la placa.)

| Comando | Efecto | Para qué |
|---|---|---|
| `0x81` contraste | brillo 0..255 | respiración ambiental, fundidos, latido, fogonazo, atenuar al dormir |
| `0xA6`/`0xA7` | invertir toda la pantalla | **rayo**, golpe, impacto en minijuego, alerta seria |
| `0xD3` display offset | desplaza todas las filas (con envolvente) | **temblor de pantalla**, retroceso al aterrizar |
| `0x40..0x7F` start line | scroll vertical del buffer (con envolvente) | transiciones, deriva al dormir |
| `0xA8` multiplex ratio | encoge la altura activa | **colapso tipo CRT** al morir, "ojo que se cierra" al dormir |
| `0xA0`/`0xA1`, `0xC0`/`0xC8` | espejo horizontal / vertical instantáneo | volteo en el apareamiento, confusión, huevo de pascua |
| `0xAE`/`0xAF` | panel on/off | cortes duros |

**Tres avisos honestos:**

- `0xD3` y `0x40` **envuelven**. Un temblor de ±3 px hace que 3 filas de
  abajo reaparezcan arriba. Para un temblor de 150 ms eso no se lee como un
  fallo, se lee como un temblor. Para una transición de deslizamiento sí se
  nota. Conclusión: **temblor sí, deslizamiento sólo si aceptas la envolvente.**
- El contraste del SSD1306 no es perceptualmente lineal y la parte baja del
  rango es muy oscura. Para efectos da igual; para "brillo del usuario" hay
  que curvarlo.
- El scroll por hardware (`0x26`/`0x29`) es autónomo y precioso para una
  marquesina, pero escribir en la GDDRAM mientras está activo da artefactos.
  Úsalo sólo en pantallas estáticas, o no lo uses.

**Coste/impacto: el mejor del documento.** Que el rayo de `fx_lightning()`
dispare un `0xA7` de 40 ms más un `0xD3` de temblor son literalmente dos
líneas, y cambia por completo lo que se siente en una tormenta.

---

## 3. El bicho: estado base y locomoción

### Lo que hay hoy

```
2 fotogramas por cuerpo, alternando cada 420 ms   ->  2,4 fps efectivos
kBob[8] = {0,0,1,1,0,0,-1,-1} a 220 ms/paso       ->  ±1 px, ciclo de 1,76 s
x = sprite_center_x(w)                            ->  SIEMPRE el centro
```

La banda del bicho son las filas 9..55 (47 px). Un adulto de 40x40 se centra
en x=44, así que hay **44 px de escenario libre a cada lado** que no se usan
nunca.

### El escenario

Antes de mover nada, la banda necesita ser un sitio, no un vacío:

- **Suelo**: una línea punteada a la altura de la base del cuerpo. Una línea
  de código, y de repente el bicho está *sobre* algo.
- **Sombra**: una elipse tramada al 50 % bajo el cuerpo, que se estrecha y se
  separa cuando salta. Es el truco más barato que existe para que un sprite
  de 1 bit deje de flotar en el vacío. Con el suelo y la sombra ya parece
  otro juego, y no hace falta arte nuevo.

### La máquina de comportamiento

Un pequeño autómata que posee la posición X del bicho:

```
QUIETO -> ANDAR_IZQ / ANDAR_DER -> GIRAR -> OLFATEAR -> SENTARSE
       -> MIRAR_ARRIBA -> BRINCAR -> ATENCIÓN (mira al jugador)
```

Lo interesante no es el autómata, es **de dónde salen los tiempos**. El
genoma ya lleva todo lo necesario y no llega a la pantalla:

| Gen | Rango | Qué controla en el movimiento |
|---|---|---|
| `gene_temper_class` | 4 clases | el carácter del movimiento (ver abajo) |
| `gene_sociability` | 0..15 | ¿se acerca al centro o se pega a un borde? |
| `gene_body_size` | 0..7 | velocidad de andar y amplitud del bote |
| `gene_luck` | 0..7 | frecuencia de las animaciones raras |
| Ánimo (`sim_mood_score`) | 0..100 | amplitud general: en MISERIA casi no se mueve |
| Energía | 0..100 | con poca energía, más QUIETO y SENTARSE |

Las cuatro clases de temperamento ya existen en `nt_types.h:380`:

- **SOLAR** — saltitos frecuentes y cortos, cambia de dirección alegremente.
- **TRANQUILO** — paradas largas, andar lento, poca amplitud.
- **NERVIOSO** — ráfagas cortas, cambios de dirección constantes, un jitter
  de 1 px cuando está parado.
- **GÓTICO** — casi inmóvil, mira hacia el lado contrario, deriva despacio
  hacia una esquina y se queda ahí.

Sembrar el PRNG del autómata con `lineage_id ^ genoma` hace que **el mismo
bicho se mueva siempre igual y sus hermanos se muevan distinto**. Eso
convierte el movimiento en un rasgo heredable visible, que es exactamente el
tipo de detalle por el que la gente se encariña con un Tamagotchi.

### Mirar hacia donde anda

Un bicho que anda a la izquierda mirando a la derecha está roto. Hay que
espejar el sprite.

XBM es LSB-first, así que espejar una fila = invertir el orden de los bytes
de la fila **y** invertir los bits de cada byte (LUT de 256 B en flash).
Coste: ~200 operaciones para un sprite de 40x40, y un buffer de rasguño de
~200 B que sólo se recalcula cuando cambia la orientación.

**Pega real:** los anchos 24, 32 y 40 son múltiplos de 8 y salen limpios. El
CHILD es de **28 px**, con 4 bits de relleno en cada fila, así que hay que
desplazar 4 bits después de invertir. O se programa ese caso, o se regenera
el arte de CHILD a 32 px de ancho. Lo segundo es más limpio y hay flash de
sobra.

---

## 4. Reaccionar a la pantalla

El bicho debería notar que la pantalla existe:

| Estímulo | Reacción | Coste |
|---|---|---|
| **Borde de pantalla** | choque, aplastarse 1 px, darse la vuelta | trivial |
| **Caca** | esquivarla; o si higiene y disciplina están bajas, sentarse al lado. Además: la caca debería aparecer **donde estaba el bicho**, no en 4 posiciones fijas (`kx[POOP_MAX]`) | bajo |
| **Lluvia** | mirar arriba, encogerse; en nieve, acurrucarse | bajo |
| **Rayo** | dar un respingo (reutiliza `s_wiggle_ms`) sincronizado con `fx_lightning` | ~5 líneas |
| **Sol** | perseguir la chispa que ya dibuja `fx_sparkle` | bajo |
| **Botones** | girarse a mirar al jugador ~1 s ante cualquier gesto. El gancho ya existe: `ui_handle()` llama a `rd_request_frame()` | ~10 líneas |
| **Sus propias barras** | con una stat muy baja, ir andando hasta debajo del icono correspondiente de la barra de estado y quedarse mirándolo | medio, pero es de lo más expresivo que se puede hacer sin arte |

Y **escalado de aburrimiento**: si no lo tocas en N minutos, las animaciones
se vuelven más largas y más raras (tumbarse, mirar fijamente al jugador,
golpear el suelo con la pata). Ahí es donde se lee la personalidad.

---

## 5. Los genes cosméticos que no se ven

Este es el arreglo con mejor relación verdad/esfuerzo del proyecto.

### Patrón del pelaje — sale prácticamente gratis

`rd_dither_rect()` escribe directamente en el framebuffer y respeta el color
de dibujo (`render.cpp:430`). El fondo es 0 y el cuerpo es 1. Por lo tanto:

```cpp
px_spr(x, y, r);                       // cuerpo sólido
u.setDrawColor(0);
rd_dither_rect(x, y, r.w, r.h, level); // borra según Bayer
u.setDrawColor(1);
```

Borrar un 0 deja un 0, así que **el patrón sólo aparece dentro de la
silueta**. Recorte perfecto, sin máscara, sin buffer auxiliar, sin arte
nuevo: una llamada más por frame. `gene_pattern` elige el nivel de tramado.

Para conseguir familias de patrón distintas (moteado, rayado, a cuadros)
hace falta una variante de `rd_dither_rect` que acepte un desfase en el
índice Bayer y matrices alternativas — el `px & 3` / `y & 3` de ahora está
clavado a la rejilla de pantalla. Es una modificación pequeña y contenida.

### El resto

| Gen | Realización en 1 bit | Coste |
|---|---|---|
| `gene_ear_horn` (0..3) | XBM de ~8x6 estampado en un ancla de cabeza. Hace falta una tabla de anclas: 38 sets x 2 B = 76 B, más ~24 B por cuerno | bajo, requiere arte nuevo |
| `gene_body_size` (0..7) | dibujar el cuerpo dos veces desplazado 1 px = dilatación, bicho visiblemente más gordo. Un `drawXBM` extra | trivial |
| `gene_rare` | órbita permanente de chispas (`EMO_SPARK` ya existe) | trivial |
| `gene_mutations` >= 3 | asimetría: un píxel suelto que parpadea, o un jitter desigual | trivial |
| `gene_palette` (0..7) | **no tiene sentido en 1 bit.** Reasignarlo al nivel de tramado del patrón, o dejarlo como dato de linaje | — |

---

## 6. Animaciones de hechos

### Lo que ya existe

| Hecho | Estado actual |
|---|---|
| Muerte | **excelente**. 22 s en 7 fases: latido que se ralentiza de 60 a 0 bpm, colapso en 4 pasos de tramado, 3 s de negro absoluto, epitafio línea a línea, enterrar manteniendo pulsado, linaje, huevo que aparece. Esto es el listón |
| Comer | cambio de pose estático, 2,5 s |
| Mimo | 3 corazones subiendo, 1,4 s |
| Evolución | 4 chispas orbitando, 4 s. **No hay revelación** |
| "Nope" | wiggle de ±1 px, 350 ms |
| **Eclosión** | **nada.** `sim_hatch()`, un toast, y a casa |

### Arte ya compilado que no se usa nunca

Esto ya está dibujado, ya ocupa flash y no se ve jamás:

**Emotes (6 de 12):** `EMO_SWEAT`, `EMO_TEAR`, `EMO_QUESTION`, `EMO_GERM`,
`EMO_BOWL`, `EMO_BUBBLES`
**Iconos:** `ICO_SNACK`, `ICO_HEART`, `ICO_SKULL`, `ICO_BATTERY`, `ICO_HOME`

Es decir: el cuenco para comer, las burbujas para limpiar y el germen para la
medicina **ya están ahí**. Las animaciones de acción de abajo no necesitan
arte nuevo.

### Lo que falta, por orden de impacto

**1. La eclosión.** Es el nacimiento de la mascota y ahora mismo es un toast.
Debería ser el segundo mejor momento del juego después de la muerte:

| Paso | Duración |
|---|---|
| huevo tambaleándose, acelerando | 1,2 s |
| sprite de grieta + temblor `0xD3` (3 sacudidas) | 0,6 s |
| fogonazo blanco `0xA7` | 80 ms |
| esquirlas saliendo despedidas | 0,4 s |
| el bebé aparece pequeño y crece | 0,8 s |
| mira a un lado, mira al otro | 0,8 s |
| aparece el nombre | 0,6 s |

Todo con arte que ya existe (`sprite_egg`, `EMO_SPARK`) más los dos efectos
de registro. **Es el elemento suelto de mayor impacto de todo el estudio.**

**2. La evolución.** Las chispas están, falta la revelación. Reutilizando
exactamente la maquinaria del colapso de la muerte: congelar → disolver el
cuerpo viejo con tramado ascendente → fogonazo → aparecer el nuevo con
tramado descendente → tres brincos. ~30 líneas de código ya probado.

**3. Comer.** El cuenco entra deslizándose desde el borde, el bicho **anda
hasta él**, tres bocados, migas cayendo (2-3 píxeles), el cuenco se vacía,
brinco de satisfacción, corazón. Depende de la locomoción de la sección 3.

**4. Limpiar.** Burbujas barriendo de izquierda a derecha sobre la caca, la
caca disolviéndose con tramado, chispa final.

**5. Medicina.** La pastilla baja, el bicho hace una mueca, `EMO_GERM` se
desvanece.

**6. Dormirse.** Ahora es instantáneo. Debería ser: bostezo, andar a una
esquina, acurrucarse, **el contraste baja a `OLED_CONTRAST_DIM` en 2 s** (2
bytes por paso, gratis), empiezan las Z. Despertar es la rampa al revés más
un estiramiento.

**7. Fin de minijuego.** Ahora es sólo texto. Confeti al ganar, nubarrón al
perder.

**8. Apareamiento BLE.** Un cortejo: los dos sprites se acercan, corazones,
fogonazo, huevo. Es caro, pero es la recompensa de una funcionalidad entera
que ahora termina en una pantalla de texto.

---

## 7. Movimiento de interfaz

Hoy **todas las pantallas cortan en seco** (`screen_enter()` no anima nada) y
**todas las listas saltan**.

| Mejora | Cómo | Coste |
|---|---|---|
| **Inercia del carrusel de menú** | `x = cx - 6 + d*24` ya es paramétrico: añade un `s_ring_px` que decae a 0 en ~140 ms tras cada paso | **~8 líneas.** Es la interacción estrella del aparato y la que más se nota |
| **Deslizamiento del cursor de lista** | igual con la `y` del recuadro resaltado en `draw_list()`, 120 ms | ~10 líneas |
| **Transiciones entre pantallas** | disolución por tramado en 3 frames (la máquina ya está probada en `DP_COLLAPSE`) | 3 frames extra = 72 ms de bus |
| **Barras de stats animadas** | `rd_bar()` pinta el valor real de golpe. Interpola hacia el objetivo a ~4 %/frame: al dar de comer **se ve llenarse** | 7 B de RAM. Enorme calidad percibida por casi nada |
| **Modales que crecen** | el rectángulo se abre desde una línea horizontal en 100 ms | trivial, parece carísimo |
| **Toasts que suben** | entran deslizándose 11 px en 100 ms en vez de aparecer | trivial |
| **Destello de pulsación** | invertir la mitad correspondiente de la franja de affordance mientras el botón está físicamente pulsado (`input_raw()` está disponible) | ~4 líneas, respuesta táctil inmediata |
| **Barrido del subrayado de cabecera** | al entrar en una pantalla, la línea del header se dibuja de izquierda a derecha en 100 ms | trivial |

---

## 8. Brillitos

- **Parpadeo.** El bicho no parpadea nunca. Un overlay de ojos de 4x2 px,
  cerrados 90 ms cada 3–6 s con intervalo irregular (por hash, no periódico).
  **Este es el detalle que más hace por que una criatura parezca viva**, y
  cuesta ~16 B de arte más una tabla de anclas de ojos.
- **Sombra** bajo el cuerpo (ya mencionada en §3).
- **Squash & stretch.** Al aterrizar: 1 px más bajo y 2 px más ancho durante
  2 frames. Vende el peso.
- **Anticipación.** Un frame de agacharse antes de saltar, un frame de
  inclinarse antes de andar.
- **Estela.** En movimientos rápidos, redibujar la posición anterior con
  tramado `RD_D25`.
- **Respiración de contraste.** ±12 alrededor del punto de ajuste, ciclo de
  4 s. Cuesta 2 bytes por frame y hace que el panel parezca vivo aunque no se
  mueva nada.
- **Chispa en la barra de estado** cuando una stat llega al 100 %.

---

## 9. El LED: un canal de salida entero apagado

`PIN_LED` (GPIO5) **sólo se usa en la pantalla de ERROR cuando el panel no
responde** (`ui/screen_error.cpp`). Fuera de ese fallo, está apagado siempre.

El C3 tiene LEDC, así que puede hacer PWM real:

- respiración lenta sincronizada con el ánimo (rápida y alegre en EUFÓRICO,
  casi imperceptible en MISERIA)
- doble parpadeo ante una alerta
- fundido a negro al dormirse
- latido sincronizado con la fase `DP_HEARTBEAT` de la muerte

Es la única forma de que el aparato te diga algo **sin que estés mirando la
pantalla**. Coste: ~30 líneas y un canal LEDC. Tiene que respetar `CF_MUTE`
(o un flag nuevo) y las horas nocturnas, porque un LED respirando en un
dormitorio a las 3 de la mañana es un problema, no un detalle bonito.

---

## 10. Lo que hay que rechazar, y por qué

| Idea | Por qué no |
|---|---|
| Grises por alternancia de frames | El SSD1306 no tiene gris por píxel. Alternar frames a 20 fps parpadea de forma visible y **duplica** la carga del bus. El tramado ordenado ya da 17 niveles y no parpadea |
| Partículas a pantalla completa a 30 fps | Limitado por el bus a 400 kHz. Sólo viable **después** de validar los 800 kHz |
| Rotación o escalado de sprites en runtime | Espejar sí (inversión de bits). Rotación arbitraria no: no hay ni ancho de banda ni sentido en 1 bit |
| Scroll por hardware con escritura simultánea | Artefactos garantizados. Sólo en pantallas estáticas |
| Subir `FPS_NORMAL` por encima de 20 sin tocar el bus | El guardia de sobrecoste de `rd_end_frame()` lo estrangula y además le roba tiempo a la radio |
| Animar durante `rd_web_busy()` | Ya se cae a `FPS_LOW` a propósito. Respétalo: el móvil conectado tiene prioridad |

---

## 11. Hoja de ruta sugerida

### Nivel 0 — horas, y se nota muchísimo
1. Ceremonia de eclosión
2. Inercia del carrusel + deslizamiento del cursor de lista
3. Barras de stats animadas
4. Parpadeo + sombra + suelo
5. El rayo como fogonazo real (`0xA7`) y temblor (`0xD3`)

### Nivel 1 — la capa "está vivo"
6. Máquina de comportamiento ocioso: deambular, orientación, espejado
7. Reacción a bordes, caca, clima y botones
8. Rampa de contraste al dormir y despertar
9. Transiciones entre pantallas por disolución

### Nivel 2 — la capa "es MI bicho"
10. Genes cosméticos a píxeles (patrón, cuernos, tamaño, raro)
11. Revelación de evolución
12. Animaciones de comer / limpiar / medicina (el arte ya está compilado)

### Nivel 3 — infraestructura
13. Validar 800 kHz en la placa → luego 30 fps
14. Actualización parcial sólo para la banda del bicho
15. El LED como segundo canal

### Sobre el orden

Los niveles 0 y 1 no dependen de nada. El punto 13 (los 800 kHz) es la única
cosa que **habilita** trabajo posterior, así que si tienes la placa delante
conviene probarlo pronto aunque el resto vaya después: cinco minutos de
prueba que duplican el margen de todo lo demás.

---

## 12. Estado de implementación (1 sep 2026)

Los **niveles 0 y 1 completos**, más la parte barata del nivel 2, están implementados y
compilan limpio: **2.105.548 B de flash (66 %)**, **72.748 B de RAM (22 %)** (medido 2026-09-02 en `b53cfe4`), exit 0, sin
avisos en ficheros del proyecto.

| Fichero | Antes | Ahora |
|---|---|---|
| `petfx.cpp` / `petfx.h` | — | **nuevos**, 68.300 + 8.505 B |
| `ui.cpp` | 96.801 | 139.652 B |
| `render.cpp` / `render.h` | 23.954 / 11.811 | 41.866 / 21.606 B |
| `weather.cpp` / `weather.h` | 40.807 / 5.030 | 43.567 / 6.902 B |
| `config.h`, `ui.h`, `strings_es.h`, el `.ino` | — | ampliados |

### Lo que entró

Ceremonia de eclosión de 4,5 s en 7 fases; autómata de reposo con deambular, orientación
por espejado, parpadeo irregular, suelo y sombra; los cuatro temperamentos del genoma
dirigiendo el movimiento; patrón de pelaje por tramado recortado a la silueta; esquiva de
cacas; inercia del carrusel y cursor de lista deslizante; barras de stats animadas; rampa
de contraste al dormir y despertar; rayo como fogonazo e temblor de registro; eco táctil
de los botones; transiciones por disolución; y los efectos de registro del SSD1306
(`rd_flash`, `rd_shake`, `rd_contrast_ramp`, `rd_breathe`, `rd_fx_settle_now`).

### El contrato de escenario

La lección más cara de la implementación, y la que conviene no olvidar:

```
columnas   0..13   HUD izquierdo (distintivo de clima)
columnas  14..113  ESCENARIO - el cuerpo del bicho, y solo el cuerpo
columnas 114..127  HUD derecho (cara de ánimo)
```

En un framebuffer de 1 bit **no puedes tener a la vez "el distintivo siempre legible" y
"el bicho nunca dañado" si comparten píxeles**. Cuatro rondas de revisión adversarial se
fueron en descubrir eso a base de medir píxeles destruidos. La separación está impuesta
por el compilador: `UI_HUD_L_END` / `UI_HUD_R_BEGIN` viven en `config.h`, los dos
distintivos se colocan con esos símbolos, y `petfx.h` lleva

```c
static_assert(PETFX_STAGE_L >= UI_HUD_L_END && PETFX_STAGE_R < UI_HUD_R_BEGIN, ...);
```

Mover un icono o ensancharlo rompe la compilación en vez de romper la pantalla.

Orden de composición de HOME, y no se reordena para arreglar un artefacto local — se
mueve al que estorba fuera de los píxeles del otro:

```
decorado de clima -> suelo -> cacas -> CUERPO -> emotes -> HUD -> partículas -> banner/toast -> franja
```

### Lo que sigue pendiente

Del nivel 2: los cuernos de `gene_ear_horn` (necesitan arte nuevo y una tabla de anclas de
cabeza) y la revelación de evolución. Del nivel 3: validar los 800 kHz en la placa,
actualización parcial de la banda, y el LED como segundo canal.

### Riesgo residual

Todo lo anterior está verificado en arneses de host que reconstruyen el framebuffer real
de 128x64 (barridos de millones de fotogramas con cero píxeles de cuerpo destruidos dentro
del escenario), pero **nada de esto se ha visto todavía en la placa**. El bug del arranque
que costó el día anterior compilaba y enlazaba perfectamente. Copia del firmware anterior,
por si acaso, en `nt_backup_pre_polish`.
