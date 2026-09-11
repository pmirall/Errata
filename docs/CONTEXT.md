# Errata — contexto de diseño

Briefing para una IA (o persona) que va a diseñar algo para este producto:
carcasa, packaging, sprites, pantallas, identidad o material gráfico. **No es
documentación de firmware.** Lo que hay aquí son las restricciones que no se
negocian y el porqué de cada una.

Si vas a tocar el manual impreso, lee además `docs/manual/README.md` — tiene
reglas propias.

---

## 1. Qué es

Un bicho virtual portátil. Un Tamagotchi, pero el bicho es un **bug
informático**: un defecto, no un animal.

*Errata* son las correcciones que un libro publicado lleva impresas. Los tipos
de criatura ya eran SIGNAL / CORRUPT / SYSTEM y los ataques ya se llamaban
PING, OVERCLOCK y DEPURAR antes de que el producto tuviera ese nombre. El
aparato iba de depurar antes de llamarse así.

**El bicho no puede morir.** Es deliberado y está en la primera página del
manual. No es un Tamagotchi de los duros: los cuidados son relajados, los
bichos guardados se recuperan solos, y nada castiga por dejarlo en un cajón.

**Explorar es mirar redes Wi-Fi.** El aparato escanea, ve qué nombres hay en el
aire, y de ahí salen bichos, objetos y sucesos. **Nunca se conecta a ninguna.**
No hay cuenta, no hay servidor, no hay nube. Dos aparatos cercanos hablan
directamente entre ellos (ESP-NOW) para pelear, intercambiar y criar.

---

## 2. El hardware, que es toda la restricción

| | |
|---|---|
| Cerebro | ESP32-C3 SuperMini |
| Pantalla | OLED 0.96", **128 × 64, 1 bit** |
| Entrada | **Dos botones**, A y B. No hay más |
| Sonido | Piezo pasivo, una voz de onda cuadrada |
| Alimentación | 2 × AAA en serie |
| Carcasa | ABS impreso en 3D, portapilas integrado |
| BOM | ~8,99 € |

**Un bit por píxel.** No hay grises, no hay color, no hay antialiasing, no hay
opacidad. Un píxel está encendido o apagado. El único gris posible es **tramado
ordenado** (25 %, 50 %, 75 %), y a 0.96" un tramado fino se lee como textura,
no como tono.

**Dos botones para todo.** No hay cruceta, ni scroll, ni táctil. Cuatro gestos
en total: toque A, toque B, A mantenido, B mantenido — más A+B a la vez, que
siempre abre la ayuda de esa pantalla. Todo menú es un **anillo**: pasado el
último vuelves al primero, porque no hay forma de retroceder en una lista.

**Sándwich físico**, de delante hacia atrás: OLED y los dos botones → placa y
piezo → las dos AAA. Cambiar pilas es abrir la tapa trasera, nada de
destornilladores.

---

## 3. La retícula de pantalla

Las 64 filas están repartidas y **no se negocian**:

```
filas  0 ..  8   barra de estado        (9 px)
filas  9 .. 55   contenido              (47 px)
filas 56 .. 63   tira de affordances    (8 px)  ← qué hace cada botón aquí
```

La tira de abajo está en **todas** las pantallas: es lo que le dice al jugador
qué hacen A y B, y es la razón de que dos botones basten. Nunca se dibuja
encima de ella.

Cuatro fuentes, todas de mapa de bits:

| Nombre | Tamaño | Para qué | Ojo |
|---|---|---|---|
| `GF_BODY` | 5×8, avance 5 | el texto normal | |
| `GF_HEAD` | t0_11b, negrita | cabeceras | proporcional |
| `GF_TINY` | 4×6, avance 4 | etiquetas, estados | **solo ASCII** |
| `GF_BIG` | 9×19 | la hora | **solo dígitos** |

Los textos son **UTF-8** y la UI está **en español**. Eso significa acentos y
`ñ`, y significa que `GF_TINY` no puede llevarlos.

El cuerpo de un bicho es una caja de **24 × 24**.

---

## 4. Cómo es un bicho

La regla entera, y está escrita para poder repetirse:

> **Una masa sólida con agujeros perforados, que falla un poco más en cada
> etapa.**

Hay sesenta especies en veinte familias. Cinco leyes que cumplen las sesenta
(el desarrollo largo está en `docs/creature_style.md`):

1. **El cuerpo es una masa sólida.** Tinta rellena, no contorno. Un contorno de
   1 px no sobrevive a 1×, así que la forma la lleva la silueta y lo que se le
   *quita*, nunca el detalle interior encendido.
2. **El ojo es un hueco perforado**, no un punto dibujado.
3. **Nada mide un píxel.** Un píxel suelto a 0.96" es suciedad, no un rasgo.
4. **Los pies van en la última fila con tinta**, para que el bicho se apoye en
   algo en vez de flotar.
5. **El segundo fotograma es movimiento, no parpadeo.** Dos fotogramas por
   pose; el segundo tiene que leerse como que el bicho se ha movido.

**Crecer es estropearse.** De la etapa 0 a la 2 un bicho no se hace más grande
ni más fiero: se hace **más roto**, dentro de la misma silueta y la misma caja
de 24×24. El daño es el eje de crecimiento. Es lo que hace que la ficción
—criaturas que son defectos— se sostenga visualmente en vez de ser solo el
nombre.

---

## 5. Identidad

- **Monocromo, un solo tinte.** No hay paleta de marca porque el producto no
  puede enseñar color. Todo el material gráfico hereda esa disciplina: el
  manual impreso va a **una tinta, negro**, y no por ahorrar — el color
  duplicaría el coste y no añadiría nada a un producto que es pixel art de 1
  bit.
- **Pixel art, no retro-nostalgia.** La estética sale de la restricción real
  del panel, no de imitar una consola de los ochenta.
- **El nombre es femenino en español.** *Una* Errata. "¡ERRATA ENCONTRADA!",
  "Acerca otra Errata". Se corrigió a mano en el renombrado porque ningún
  reemplazo automático puede hacer la concordancia.
- **Los bichos se llaman *bug*** en los dos idiomas del manual, sin traducir.
  Es la palabra del producto.

---

## 6. Lo impreso

El manual va en la caja: **A6 (105 × 148 mm), 40 páginas, grapado al lomo, una
tinta**, español e inglés **en el mismo cuadernillo** (párrafo a párrafo, nunca
dos cuadernillos ni un idioma en apéndice).

Reglas que la compilación impone, por si diseñas algo que lo toque:

- **40 páginas, múltiplo de 4.** Una hoja nueva cuesta cuatro páginas.
- **Ninguna captura se dibuja a mano.** Todas se generan del firmware.
- **Ningún dato legal va en prosa.** Salen todos de un TOML.
- **Ningún número de página se escribe.** Las referencias son etiquetas.

Está publicado en `pmirall.github.io/Errata/uso.pdf`, y el aparato tiene una
pantalla con un QR que apunta ahí. Ese nombre tan corto no es gusto: el QR que
cabe en un panel de 0.96" solo puede llevar **32 bytes**, y hay un
`static_assert` que rompe la compilación si la URL crece.

---

## 7. Lo que todavía no existe

Honestidad sobre el estado, para que no diseñes sobre algo que no está:

- **La carcasa no está diseñada.** Hay un sándwich conceptual y unas medidas de
  componentes, nada más. Es lo más grande que falta.
- **Tres dibujos del manual están sin hacer** y marcados con cajas de trazos:
  el despiece de la caja, cómo se ponen las pilas, y la cara del aparato con A
  y B. No se pueden dibujar bien hasta que exista la carcasa, y un diagrama
  equivocado es peor que un hueco marcado.
- **Nadie ha oído el piezo.** No hay sonador montado en ninguna placa en la que
  haya corrido esto.
- **No hay foto del panel real**, solo capturas sintéticas.
- **El bloque legal del manual está en TODO** y la compilación en modo imprenta
  se niega a generar el PDF hasta que se rellene.

---

## 8. Dónde mirar

| | |
|---|---|
| `docs/creature_style.md` | las cinco leyes, desarrolladas, con las veinte familias |
| `docs/ERRATA_HARDWARE_AND_BATTERY_SPEC.md` | pantalla, pilas, piezo, sándwich físico |
| `docs/ERRATA_PRODUCT_SYSTEM_SPEC.md` | la ficción y los sistemas de juego |
| `docs/manual/README.md` | el cuadernillo y sus reglas |
| `docs/manual/assets/screens/*.svg` | **84 capturas reales**, generadas del firmware |
| `docs/uso.pdf` | el manual tal y como está hoy |

Las 84 capturas son lo más útil de esta lista: son el aparato, no una
interpretación de él.
