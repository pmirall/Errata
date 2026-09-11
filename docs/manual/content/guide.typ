// =============================================================================
//  docs/manual/content/guide.typ - the player's guide, front half of the
//  booklet. Page numbers are never written down; see pg() in lib.typ.
//
//  Bilingual through `bi`: one shared illustration, two narrow columns under
//  it. Screens come from tools/pbm2svg.py; nothing here is drawn by hand
//  except the three `drawing()` placeholders, which need the finished
//  enclosure before they can be accurate.
//
//  On-screen wording quoted with `scr()` is copied from
//  Errata/src/core/strings_es.h. If a label changes there, change it here.
// =============================================================================

#import "../lib.typ": *

// --- 2. What is in the box ---------------------------------------------------
#pagebreak()
= En la caja / In the box

#drawing("exploded", "Exploded view: device, 2x AAA, manual", height: 38mm)

#bi[
  #mark(1, [La Errata.], [The Errata.])
  #mark(2, [Dos pilas AAA.], [Two AAA cells.])
  #mark(3, [Este manual.], [This manual.])
][]

#v(1mm)
#warnbox[
  #set text(size: 7pt)
  *Antes de empezar.* Lee las páginas #pg(<sec-safety>) a #pg(<sec-warranty>). Contienen la información de
  seguridad y legal del producto. \
  #text(fill: luma(25%))[*Before you start.* Read pages #pg(<sec-safety>) to #pg(<sec-warranty>). They carry the
  product's safety and legal information.]
]

// --- 3. Batteries ------------------------------------------------------------
#pagebreak()
= Pon las pilas / Insert the batteries

#drawing("batteries", "Opening the shell, cell orientation, polarity marks")

#bi[
  #mark(1, [Abre la tapa del compartimento por la parte trasera.],
           [Open the compartment cover on the back.])
  #mark(2, [Coloca las dos pilas AAA siguiendo las marcas *+* y *-*
            moldeadas en la carcasa.],
           [Fit the two AAA cells following the *+* and *-* marks moulded into
            the shell.])
  #mark(3, [Cierra la tapa hasta oír el clic.],
           [Close the cover until it clicks.])
][]

#v(1mm)
#bi[
  Las pilas van en serie: el *+* de una toca el *-* de la otra. Si el
  dispositivo no enciende, casi siempre es una pila del revés.
][
  The cells sit in series: the *+* of one touches the *-* of the other. If the
  device does not switch on, it is almost always a cell the wrong way round.
]

#v(1mm)
#warnbox[
  #set text(size: 7pt)
  No mezcles pilas nuevas con usadas ni tipos distintos. No intentes recargar
  pilas alcalinas. Página #pg(<sec-batteries>). \
  #text(fill: luma(25%))[Do not mix new and used cells, or different types. Do
  not attempt to recharge alkaline cells. Page #pg(<sec-batteries>).]
]

// --- 4. First power-on -------------------------------------------------------
#pagebreak()
= Enciende / Switch on

#grid(columns: (1fr, 1fr, 1fr), column-gutter: 2mm,
  screen("intro_type", width: 30mm), screen("intro_build", width: 30mm),
  screen("intro_fail", width: 30mm),
)

#v(1.5mm)
#bi[
  La primera vez que le pones las pilas, la Errata no arranca en un menú:
  arranca escribiendo código. Tarda unos dieciséis segundos.

  Se compila, la barra llega al 92 % y se para. #scr[ERROR: 3 BUGS]. La banda
  se rompe en líneas y por la grieta salen tres bichos, uno a uno.

  Esos tres son tus candidatos, y el último fotograma de la película ya es la
  pantalla donde eliges.
][
  The first time you fit the cells, the Errata does not open on a menu: it
  opens writing code. It takes about sixteen seconds.

  It compiles, the bar reaches 92% and stops. #scr[ERROR: 3 BUGS]. The band
  tears into scanlines and three bugs climb out of the tear, one at a time.

  Those three are your candidates, and the film's last frame is already the
  screen where you choose.
]

#v(1.5mm)
#warnbox[
  #set text(size: 7pt)
  *Cualquier botón se la salta, y solo hace eso:* no elige Bug por ti ni
  mueve el cursor. Puedes saltártela sin miedo a quedarte con un bicho que no
  has mirado. Solo se ve en el primer arranque de verdad. \
  #text(fill: luma(25%))[*Any button skips it, and only that:* it does not pick
  a Bug for you and does not move the cursor. You can skip it without ending
  up with a creature you never looked at. It plays on a true first run only.]
]

// --- 5. First boot: starter, then name, then clock ----------------------------
#pagebreak()
= Elige y ponle nombre / Choose and name

#grid(columns: (1fr, 1fr, 1fr), column-gutter: 2mm,
  screen("intro_bugs", width: 30mm), screen("setup_starter", width: 30mm),
  screen("setup_name", width: 30mm),
)

#v(1.5mm)
#bi[
  Primero *eliges*, y después le pones *nombre*. Ese orden es a propósito:
  conoces al bicho antes de bautizarlo.

  Los tres candidatos son uno de cada tipo del triángulo del combate, y los
  tres pueden evolucionar. No hay uno mejor ni ninguno es una trampa: elige el
  que te guste.

  *A* pasa de uno a otro y *mantén A* para quedártelo.

  Para el nombre, *A* recorre las letras y *B mantenido* pasa a la siguiente.
  *Mantén A* para aceptar.

  Al final te pedirá la *hora*. Ajústala: tu Bug duerme de noche y come de
  día, y sin fecha no puedes explorar.

  Si prefieres saltarte las preguntas, pulsa *A + B*.
][
  You *choose* first and *name* second. That order is deliberate: you meet the
  creature before you christen it.

  The three candidates are one of each type in the battle triangle, and all
  three can evolve. None is better and none is a trap: pick the one you like.

  *A* moves between them and *hold A* keeps one.

  For the name, *A* walks the letters and *hold B* moves to the next. *Hold A*
  to accept.

  Last it asks for the *time*. Set it: your Bug sleeps at night and eats by
  day, and without a date you cannot explore.

  If you would rather skip the questions, press *A + B*.
]

#v(1.5mm)
#align(center, screen("time_entry", width: 40mm))

// --- 5. THE TWO BUTTONS - the most important page in the manual --------------
#pagebreak()
= Los dos botones / The two buttons

#drawing("buttons", "Device front with A and B labelled", height: 26mm)

#bi[
  Todo el juego se maneja con dos botones. *A* a la izquierda, *B* a la derecha.
][
  The whole game runs on two buttons. *A* on the left, *B* on the right.
]

#v(1.5mm)
#block(stroke: 0.5pt + black, inset: 1.8mm, radius: 0.6mm, width: 100%)[
  #set text(size: 6.8pt, hyphenate: false)
  #grid(
    columns: (19mm, 1fr, 1fr), column-gutter: 2mm, row-gutter: 1.4mm,
    [*Gesto / Gesture*], [*Qué hace*], [#text(fill: luma(25%))[*What it does*]],
    grid.hline(stroke: 0.4pt),
    [*A* toque], [Mueve el cursor, o la acción principal],
      [#text(fill: luma(25%))[Moves the cursor, or the main action]],
    [*A* mantenido], [Repite lo anterior],
      [#text(fill: luma(25%))[Repeats the above]],
    [*B* toque], [*Elige* la línea marcada],
      [#text(fill: luma(25%))[*Chooses* the highlighted line]],
    [*B* mantenido], [*Atrás.* En todas las pantallas],
      [#text(fill: luma(25%))[*Back.* On every screen]],
    [*A + B*], [Ayuda de la pantalla actual],
      [#text(fill: luma(25%))[Help for the current screen]],
    [*A + B* largo], [Vuelve a INICIO desde donde sea],
      [#text(fill: luma(25%))[Returns to HOME from anywhere]],
  )
]

#v(1.5mm)
#warnbox[
  #set text(size: 7pt)
  *Lo único que sorprende:* salir cuesta *mantener B*, no tocarlo. Un toque de
  *B* elige, que es lo que uno hace a todas horas; irse hacia atrás es un gesto
  deliberado y no un roce. \
  #text(fill: luma(25%))[*The one surprise:* leaving costs a *held B*, not a
  tap. A tap on B chooses, which is what you do constantly; going back is a
  deliberate gesture rather than a brush.]
]

#v(1mm)
#bi[
  En la pantalla de inicio no hay nada a lo que volver, así que allí *B* hace
  una caricia, *A + B* apaga y enciende el sonido, y *A + B* largo abre los
  ajustes.
][
  On the home screen there is nothing to go back to, so there *B* strokes the
  Bug, *A + B* switches the sound off and on again, and a long *A + B* opens
  the settings.
]

// --- 6. The home screen ------------------------------------------------------
#pagebreak()
= La pantalla de inicio / The home screen

#align(center, screen("home_starter", width: 58mm, marks: (
  (0.46, 0.10, 1), (0.79, 0.10, 2), (0.90, 0.50, 3),
  (0.50, 0.62, 4), (0.20, 0.93, 5),
)))

#v(2mm)
#mark(1, [El nombre de tu Bug.], [Your Bug's name.])
#mark(2, [Su nivel.], [Its level.])
#mark(3, [Salud, comida y ánimo.], [Health, food and mood.])
#mark(4, [Tu Bug. Su postura te dice cómo está.],
         [Your Bug. Its posture tells you how it is doing.])
#mark(5, [Lo que hacen *A* y *B* en esta pantalla.],
         [What *A* and *B* do on this screen.])

#v(1.5mm)
#bi[
  Desde aquí, *A* abre el menú principal: #scr[BUG] #scr[CUIDAR] #scr[JUGAR]
  #scr[CAJA] #scr[WIKI] #scr[RED] #scr[ENLACE] #scr[AJUSTES].

  Los menús son anillos: al pasar del último vuelves al primero.
][
  From here, *A* opens the main menu: #scr[BUG] #scr[CUIDAR] #scr[JUGAR]
  #scr[CAJA] #scr[WIKI] #scr[RED] #scr[ENLACE] #scr[AJUSTES].

  Menus are rings: past the last item you are back at the first.
]

// --- Bug states -----------------------------------------------------------
#pagebreak()
= Cómo está tu Bug / How your Bug is

#bi[
  No hace falta abrir ningún menú: la pantalla de inicio ya te lo dice.
][
  You do not have to open a menu: the home screen already tells you.
]

#v(2mm)
#grid(columns: (1fr, 1fr, 1fr), column-gutter: 2mm,
  screen("home_sleeping", width: 30mm), screen("home_sick", width: 30mm),
  screen("home_corrupted", width: 30mm),
)

#v(1.5mm)
#set text(size: 6.8pt)
#table(
  columns: (0.8fr, 1fr, 1fr),
  stroke: 0.3pt + luma(50%), inset: 1.2mm,
  [*Lo que ves*], [*Qué pasa*], [#text(fill: luma(25%))[*What is happening*]],

  [Ojos cerrados], [Duerme. De noche gasta mucho menos. Déjalo.],
  [#text(fill: luma(25%))[Asleep. It uses far less at night. Leave it be.]],

  [Postura caída], [Está enfermo. Usa #scr[SALUD] o un *Parche*.],
  [#text(fill: luma(25%))[Sick. Use #scr[SALUD] or a *Parche*.]],

  [Píxeles que bailan], [*Corrupto.* Dura 24 h y se pasa solo. Un *Antivirus* lo quita antes.],
  [#text(fill: luma(25%))[*Corrupted.* It lasts 24 h and clears itself. An *Antivirus* ends it sooner.]],

  [Ranura vacía], [No llevas ninguno seleccionado. Elige uno en #scr[CAJA].],
  [#text(fill: luma(25%))[None selected. Pick one from #scr[CAJA].]],
)

#v(1.5mm)
#set text(size: 8pt)
#warnbox[
  #set text(size: 7pt)
  La corrupción no es una avería y no se cura sola con comida: es un estado que
  dura 24 h, cambia su aspecto y afecta al combate. También abre dos evoluciones
  que no existen de otra forma. \
  #text(fill: luma(25%))[Corruption is not a fault and food does not fix it: it
  is a 24-hour state that changes how it looks and how it fights. It also opens
  two evolutions that exist no other way.]
]

// --- 7. Care -----------------------------------------------------------------
#pagebreak()
= Cuidar / Care

#grid(columns: (1fr, 1fr), column-gutter: 3mm,
  screen("care_list"),
  screen("alert_hungry"),
)

#v(1.5mm)
#bi[
  En #scr[CUIDAR] tienes #scr[Comida], #scr[Chuche], #scr[LIMPIAR],
  #scr[SALUD] y #scr[MOCHILA].

  Tu Bug avisa cuando necesita algo. No hace falta estar pendiente todo el
  día: esto no es un Tamagotchi de los duros. *Tu Bug no puede morir.*

  Solo el Bug que llevas seleccionado necesita cuidados. Los que están en la
  Caja se recuperan solos, poco a poco, a lo largo de unas 24 horas.
][
  Under #scr[CUIDAR] you get #scr[Comida] (a meal), #scr[Chuche] (a treat),
  #scr[LIMPIAR] (clean), #scr[SALUD] (health) and #scr[MOCHILA] (the bag).

  Your Bug tells you when it needs something. You do not have to watch it
  all day: this is not one of the harsh Tamagotchis. *Your Bug cannot die.*

  Only the Bug you have selected needs care. The ones in the Box recover on
  their own, slowly, over about 24 hours.
]

// --- The bag ------------------------------------------------------------------
#pagebreak()
= La mochila / The bag

#grid(columns: (1fr, 1fr), column-gutter: 3mm,
  screen("care_bag_two"),
  screen("care_bag_empty"),
)

#v(1.5mm)
#bi[
  Explorando encuentras objetos. Están en #scr[MOCHILA], dentro de
  #scr[CUIDAR]. Cada uno hace una cosa concreta.
][
  Exploring turns up items. They live in #scr[MOCHILA], inside #scr[CUIDAR].
  Each one does one specific thing.
]

#v(1.5mm)
#set text(size: 6.8pt)
#table(
  columns: (0.75fr, 1fr, 1fr),
  stroke: 0.3pt + luma(50%), inset: 1.1mm,
  [*Objeto / Item*], [*Para qué*], [#text(fill: luma(25%))[*What for*]],
  [Bit Dulce \ Byte Dulce \ Megadulce], [Comida, de menos a más.],
    [#text(fill: luma(25%))[Food, small to large.]],
  [Cebo], [Hace más probable el próximo encuentro.],
    [#text(fill: luma(25%))[Makes the next encounter likelier.]],
  [Jaula Hash], [Sube la probabilidad de capturar.],
    [#text(fill: luma(25%))[Raises your capture chance.]],
  [Parche], [Cura. Quita el estado enfermo.],
    [#text(fill: luma(25%))[Heals. Clears the sick state.]],
  [Antivirus], [Lo único que corta la corrupción antes de las 24 h.],
    [#text(fill: luma(25%))[The only thing that ends corruption before 24 h.]],
  [Turbo Chip \ Escudo RAM], [Mejoran una estadística durante unas rondas de combate.],
    [#text(fill: luma(25%))[Buff one stat for a few battle rounds.]],
  [Llave Raíz], [Desbloquea evoluciones que piden un objeto.],
    [#text(fill: luma(25%))[Unlocks evolutions that require an item.]],
)

#v(1.5mm)
#set text(size: 8pt)
#bi[
  Si un objeto no sirve en ese momento, el aparato te lo dice
  (#scr[Ahora no hace nada]) y no lo gasta.
][
  If an item would do nothing right now, the device says so
  (#scr[Ahora no hace nada]) and does not spend it.
]

// --- The card -----------------------------------------------------------------
#pagebreak()
= La ficha / The card

#grid(columns: (1fr, 1fr), column-gutter: 3mm,
  screen("status_a_starter"),
  screen("status_b_genome"),
)

#v(1.5mm)
#bi[
  #scr[ESTADO], dentro de #scr[CUIDAR], abre la ficha completa. Son *dos
  páginas*: *A* pasa de una a otra.

  La primera trae nivel, experiencia, tipo y estadísticas. La segunda trae el
  *genoma* y los *rasgos*: los datos internos que hacen que dos Bugs de la
  misma especie no sean iguales.

  El *tipo* decide el triángulo del combate.
][
  #scr[ESTADO], inside #scr[CUIDAR], opens the full card. There are *two
  pages*: *A* moves between them.

  The first has level, experience, type and stats. The second has the *genome*
  and the *traits*: the internal data that makes two Bugs of the same
  species different from each other.

  *Type* decides the battle triangle.
]

// --- 8. Play ------------------------------------------------------------------
#pagebreak()
= Jugar / Play

#align(center, screen("play_list", width: 54mm))

#v(2mm)
#bi[
  Hay *seis* minijuegos. Suben la felicidad y dan experiencia. Cada partida
  dura entre *5 y 15 segundos* y ninguno pide reflejos fuera de la propia
  partida.

  Después de jugar hay *dos minutos* de espera antes de la siguiente. No es un
  castigo: evita que el juego se convierta en pulsar el mismo botón sin parar.

  Todos se manejan con los mismos dos botones. Las dos páginas siguientes
  explican cada uno.
][
  There are *six* minigames. They raise happiness and give experience. A round
  lasts *5 to 15 seconds* and none of them asks for reflexes outside the round
  itself.

  After playing there is a *two minute* wait before the next one. It is not a
  punishment: it stops the game becoming one button pressed forever.

  All six use the same two buttons. The next two pages explain each one.
]

// --- The six games, one page per pair -------------------------------------------
// Two games per page, and the pair is not arbitrary: each page holds the two
// that are easiest to confuse with each other, so the contrast does the
// explaining. PING/SECUENCIA are the two "watch then press" games,
// CORTAFUEGOS/BUFFER the two "hold a position" games, and PAQUETES/BORRAR the
// two where the right move is often to do nothing.
#pagebreak()
= PING y SECUENCIA / PING and SEQUENCE

#grid(columns: (1fr, 1fr), column-gutter: 3mm,
  screen("mg_ping_lit", width: 42mm), screen("mg_sequence_show", width: 42mm),
)

#v(2mm)
#bi[
  *PING.* Se enciende un lado y tienes que pulsar *ese* lado. Cinco rondas.

  Antes de encenderse hay una espera al azar, así que mantener los dos botones
  no vale de nada: pulsar antes de tiempo no puntúa. Espera a ver la luz.
][
  *PING.* One side lights up and you press *that* side. Five rounds.

  The light comes after a random wait, so holding both buttons gains nothing:
  pressing early scores nothing. Wait until you see it.
]

#v(2mm)
#bi[
  *SECUENCIA.* El aparato enseña una serie de *A* y *B* y tú la repites. Tres
  niveles, de 3, 4 y 5 símbolos.

  Mira la secuencia entera antes de contestar. Hay un tiempo límite para
  responder, pero es más que suficiente si no te precipitas.
][
  *SEQUENCE.* The device shows a run of *A* and *B* and you repeat it. Three
  levels, of 3, 4 and 5 symbols.

  Watch the whole sequence before answering. There is a deadline to reply, but
  it is generous if you do not rush.
]

#pagebreak()
= PAQUETES / PACKETS

#grid(columns: (1fr, 1fr), column-gutter: 3mm,
  screen("mg_packet_flood_run", width: 42mm),
  screen("mg_packet_flood_reroute", width: 42mm),
)

#v(2mm)
#bi[
  Cada paquete lleva un *número*, y ese número está escrito en una de las dos
  bocas. Hay que leerlo y mandarlo a la suya.

  No es un juego de reflejos: la respuesta nunca está donde está el estímulo.
  Tienes que apartar la vista del paquete, buscar el número y volver.

  A mitad de partida *las bocas se cambian de sitio*. Los números no se mueven;
  las puertas sí. Todo lo que habías memorizado deja de valer y no queda más
  remedio que volver a leer.
][
  Each packet carries a *number*, and that number is printed on one of the two
  mouths. You read it and send it to its own.

  This is not a reaction game: the answer is never where the stimulus is. You
  have to look away from the packet, find the number, and come back.

  Halfway through, *the mouths swap sides*. The numbers do not move; the doors
  do. Everything you had memorised stops being true and the only way back is to
  start reading again.
]

#v(2mm)
#warnbox[
  #set text(size: 7pt)
  *Fallar cuesta más que acertar.* Si no llegas a leer un paquete, déjalo caer:
  adivinar sale peor que no hacer nada. Nunca puedes bajar de cero. \
  #text(fill: luma(25%))[*Getting it wrong costs more than getting it right.*
  If you cannot read a packet in time, let it fall: guessing is worse than
  doing nothing. You can never go below zero.]
]

#pagebreak()
= CORTAFUEGOS y BUFFER / FIREWALL and BUFFER

#grid(columns: (1fr, 1fr), column-gutter: 3mm,
  screen("mg_firewall_run", width: 42mm),
  screen("mg_buffer_run", width: 42mm),
)

#v(2mm)
#bi[
  *CORTAFUEGOS.* Cinco carriles y un escudo que mueves con los dos botones.
  Llegan ocho paquetes, uno a uno, y el escudo tiene que estar *en ese carril*
  en el instante del impacto.

  Quedarse a un carril vale lo mismo que quedarse en la pared: nada. Y quedarse
  quieto puntúa *cero*, siempre, porque el paquete nunca elige el carril donde
  ya estás.
][
  *FIREWALL.* Five lanes and a shield you move with the two buttons. Eight
  packets arrive, one at a time, and the shield must be *in that lane* at the
  instant of impact.

  One lane away is worth what the far wall is worth: nothing. And standing
  still scores *zero*, always, because a packet never picks the lane you are
  already in.
]

#v(2mm)
#bi[
  *BUFFER.* Una zona segura que se mueve y un cursor que mantienes dentro. No
  hay rondas ni objetivos: puntúa la *fracción de tiempo* que pasas dentro.

  Es el que más despista, y por una razón concreta: el botón no coloca el
  cursor, *lo empuja*. Es pilotar, no apuntar. La primera corrección siempre se
  pasa de largo.
][
  *BUFFER.* A safe zone that drifts and a cursor you keep inside it. No rounds
  and no targets: you score the *fraction of the time* you spend inside.

  It is the one that catches people, for a specific reason: the button does not
  place the cursor, it *pushes* it. Flying, not pointing. Your first correction
  always overshoots.
]

#pagebreak()
= BORRAR / DELETE

#grid(columns: (1fr, 1fr), column-gutter: 3mm,
  screen("mg_delete_run", width: 42mm),
  screen("mg_delete_purged", width: 42mm),
)

#v(2mm)
#bi[
  Cinco huecos. Aparecen bloques con una mecha visible y se van solos. Unos
  están sanos y otros corruptos.

  Un botón *mueve* el cursor, el otro *gasta* una de siete cargas de borrado
  sobre lo que tengas debajo.

  Las siete cargas no se recuperan, y siempre habrá más de un bloque corrupto a
  la vez, así que no puedes con todos: hay que elegir. Borra solo los rotos,
  porque cada bloque sano que borres es una carga tirada.

  Da igual lo rápido que vayas. Solo cuenta cuántos rotos aciertas.
][
  Five slots. Blocks appear on a visible fuse and vanish on their own. Some are
  healthy and some are corrupted.

  One button *walks* the caret, the other *spends* one of seven purge charges
  on whatever is under it.

  The seven charges never come back, and there will always be more than one
  corrupted block at once, so you cannot get them all: you have to choose.
  Purge only the broken ones, because every healthy block you purge is a charge
  thrown away.

  Speed does not matter. Only how many broken ones you get.
]

// --- 9-10. Explore (spread) --------------------------------------------------
#pagebreak()
= Explorar / Explore

#bi[
  Esta es la idea central del producto. *Los Bugs viven en las redes Wi-Fi
  que te rodean.* Llévate el aparato encima y usa #scr[RED] para mirar.
][
  This is the core idea of the product. *Bugs live in the Wi-Fi networks
  around you.* Carry the device with you and use #scr[RED] to look.
]

#v(2mm)
#align(center, screen("network_scanning", width: 54mm))

#v(2mm)
#bi[
  Una red puede darte un Bug salvaje, uno raro, un objeto, un suceso
  especial, o nada. Cada red se agota durante *un par de horas* como mínimo:
  quedarte quieto en casa no sirve de mucho. Hay que moverse.
][
  A network can give you a wild Bug, a rare one, an item, a special event,
  or nothing. Each network then goes quiet for *at least a couple of hours*:
  standing still at home gets you little. You have to move.
]

#pagebreak()
= Qué te da una red / What a network gives

#grid(columns: (1fr, 1fr, 1fr), column-gutter: 2mm,
  screen("encounter_wild_tear", width: 30mm), screen("encounter_wild_form", width: 30mm),
  screen("encounter_item", width: 30mm),
)

#v(1.5mm)
#bi[
  De mirar una red sale una de cinco cosas: un Bug salvaje, uno raro, un
  objeto, un suceso especial, o nada. Que salga nada es normal y no es un
  fallo.

  Cuando aparece un Bug, la pantalla se rompe y el bicho se monta desde los
  pies. Dura un segundo y *solo se ve una vez por encuentro*: si fallas un
  intento de captura y vuelves, no te la repite.

  Un *suceso especial* puede dejar a tu Bug *corrupto durante 24 h*. No es
  una avería: cambia su aspecto, cambia cómo combate, y abre dos evoluciones
  que no puedes conseguir de otra forma.

  Después de mirarla, esa red concreta se agota durante *dos horas exactas*.
  Otras redes siguen valiendo, así que lo que hay que hacer es moverse, no
  esperar.
][
  Checking a network gives one of five things: a wild Bug, a rare one, an
  item, a special event, or nothing. Nothing is a normal outcome, not a fault.

  When a Bug turns up the screen tears and the creature assembles from the
  feet up. It lasts a second and *plays once per encounter*: miss a throw and
  come back, and it does not replay.

  A *special event* can leave your Bug *corrupted for 24 h*. It is not a
  fault: it changes how it looks, changes how it fights, and opens two
  evolutions you cannot get any other way.

  Once checked, that particular network goes quiet for *exactly two hours*.
  Other networks still work, so the thing to do is move, not wait.
]

#v(1.5mm)
#align(center, screen("encounter_special", width: 34mm))

#pagebreak()
= Capturar / Catching

#grid(columns: (1fr, 1fr), column-gutter: 3mm,
  screen("capture_ready", width: 42mm),
  screen("capture_caught_seal", width: 42mm),
)

#v(1.5mm)
#bi[
  Ante un Bug salvaje puedes capturarlo o dejarlo marchar. Dejarlo marchar
  es una opción legítima, no una derrota.

  *Tienes dos intentos.* Si los dos fallan, se va. No hay un tercero.

  La probabilidad depende sobre todo de la *diferencia de nivel*: cuanto más
  fuerte sea comparado con el tuyo, más difícil. Nunca es imposible, y nunca
  está garantizado.

  Una *Jaula Hash* sube esa probabilidad. Úsala antes de tirar, no después de
  fallar.
][
  Facing a wild Bug you can catch it or let it go. Letting it go is a
  legitimate choice, not a loss.

  *You get two attempts.* If both fail, it leaves. There is no third.

  The odds depend mostly on the *level gap*: the stronger it is, the harder.
  Never impossible, never certain.

  A *Jaula Hash* raises those odds. Use it before you throw, not after you
  miss.
]

#pagebreak()
= Tu privacidad / Your privacy

#warnbox[
  #set text(size: 7pt)
  *Errata nunca se conecta a esas redes.* Solo mira qué nombres hay en el
  aire, igual que hace tu móvil cuando abres la lista de Wi-Fi. \\
  #text(fill: luma(25%))[*The Errata never connects to those networks.* It
  only looks at what names are in the air, the same way your phone does when
  you open its Wi-Fi list.]
]

#v(2mm)
#bi[
  El aparato:

  - no se conecta a redes ajenas;
  - no envía nada a ningún servidor;
  - no pide cuenta ni registro;
  - no guarda los nombres de red más allá de lo necesario;
  - no enseña a otros aparatos las redes que ha visto.

  El escaneo Wi-Fi es un sensor de juego, no un sistema de recogida de datos.
  Más detalle en la página #pg(<sec-warranty>).
][
  The device:

  - does not connect to other people's networks;
  - does not send anything to any server;
  - needs no account and no sign-up;
  - does not keep network names longer than needed;
  - does not show other devices the networks it has seen.

  Wi-Fi scanning is a game sensor, not a data collection system. More on
  page #pg(<sec-warranty>).
]

// --- 11. Capture and the Box -------------------------------------------------
#pagebreak()
= La Caja / The Box

#grid(columns: (1fr, 1fr, 1fr), column-gutter: 2mm,
  screen("box_list", width: 30mm), screen("box_card", width: 30mm),
  screen("box_list_full", width: 30mm),
)

#v(1.5mm)
#bi[
  La Caja guarda hasta *diez* Bugs. Desde #scr[CAJA] puedes leer la ficha de
  cada uno, cambiar cuál llevas seleccionado, o soltarlo.

  El seleccionado es el que llevas encima, el que cuidas y el que crece. Los
  demás se recuperan solos, poco a poco, mientras están guardados.

  Con la Caja llena no puedes capturar nada. Suelta algo antes de salir.
][
  The Box holds up to *ten* Bugs. From #scr[CAJA] you can read each one's
  card, change which one you carry, or release it.

  The selected one is the one you carry, care for and grow. The rest recover on
  their own, slowly, while they are stored.

  With a full Box you cannot catch anything. Release something before you go
  out.
]

// --- The wiki ----------------------------------------------------------------
// It follows the Box because the firmware's own reason for putting WIKI next to
// CAJA in the ring is the reason a reader needs: the Box is what you HOLD and
// the wiki is what you have MET.
//
// THE PAGE IT SITS ON WAS ALREADY IN THE BOOKLET AND WAS ALREADY BEING WASTED.
// "Que te da una red" overran its page by one screen, leaving the next leaf 6%
// inked - not empty enough for tools/page_fill.py to call it an orphan, and far
// too empty to be worth a leaf of a saddle-stitched A6. That screen is 6 mm
// narrower now and the section closes on its own page, which is where this
// section comes from. The booklet is still 40 pages.
#pagebreak()
= La wiki / The wiki

#grid(columns: (1fr, 1fr), column-gutter: 3mm,
  screen("dex_known", width: 42mm),
  screen("dex_unknown", width: 42mm),
)

#v(1.5mm)
#bi[
  #scr[WIKI], justo después de #scr[CAJA] en el menú, lista las *sesenta*
  especies: una ficha por pantalla, con el cuerpo, el número, el nombre y su
  estado -- #scr[SIN VER], #scr[VISTO] o #scr[EN LA CAJA].

  De la que no conoces ves la silueta, pero no el nombre: #scr[???].

  Se llena sola. Guardar un Bug en la Caja lo deja #scr[EN LA CAJA]; cruzarte
  con uno o pelear contra él lo deja #scr[VISTO], aunque se escape.

  Aquí no hay nada que elegir, y *A* mantenido va hacia atrás en vez de
  repetir. Se abre en la primera especie que te falta por capturar.

  Arriba a la derecha llevas la cuenta: las que ya tienes, de sesenta.
][
  #scr[WIKI], right after #scr[CAJA] in the menu, lists all *sixty* species:
  one card per screen, with the body, the number, the name and its state --
  #scr[SIN VER] (unseen), #scr[VISTO] (seen) or #scr[EN LA CAJA] (in the Box).

  One you have not met keeps its silhouette but not its name: #scr[???].

  It fills itself in. Putting a Bug in the Box marks it #scr[EN LA CAJA];
  meeting one or fighting one marks it #scr[VISTO], even if it gets away.

  There is nothing to choose here, and *hold A* steps backwards rather than
  repeating. It opens on the first species you have yet to catch.

  The count in the top right is how many you hold, out of sixty.
]

// --- 12. Evolution ------------------------------------------------------------
#pagebreak()
= Evolución / Evolution

#grid(columns: (1fr, 1fr, 1fr), column-gutter: 2mm,
  screen("evolution_egg", width: 30mm), screen("evolution_egg_cold", width: 30mm),
  screen("confirm_evolve", width: 30mm),
)

#v(1.5mm)
#bi[
  Al subir de nivel, un Bug puede quedar *listo para evolucionar*. El
  aparato te avisa y te pregunta: la evolución no ocurre a tus espaldas.

  Que esté listo *no significa que vaya a evolucionar*. El nivel es solo una de
  las condiciones. Otras evoluciones piden además felicidad, un objeto como la
  *Llave Raíz*, o haber jugado lo suficiente. Y dos solo se abren si tu Bug
  está *corrupto*.

  Si falta alguna condición, sigue esperando hasta que se cumpla. No se pierde
  nada.

  Evolucionar cambia aspecto, estadísticas y a veces ataques. Un huevo, además,
  necesita que lo lleves encima: frío quiere decir abandonado.
][
  On levelling up, a Bug may become *ready to evolve*. The device tells you
  and asks: evolution never happens behind your back.

  Ready *does not mean it will evolve*. Level is only one of the conditions.
  Other evolutions also want happiness, an item such as the *Llave Raíz*, or
  enough time played. And two open only while your Bug is *corrupted*.

  If a condition is missing it simply keeps waiting until it is met. Nothing is
  lost.

  Evolving changes looks, stats and sometimes attacks. An egg also needs
  carrying: cold means neglected.
]

// --- Battle, in full ----------------------------------------------------------
#pagebreak()
= Combate: preparar / Battle: setting up

#grid(columns: (1fr, 1fr), column-gutter: 3mm,
  screen("battle_pick"),
  screen("battle_intro"),
)

#v(1.5mm)
#bi[
  Dos Erratas cerca pueden combatir. No hace falta internet ni router.

  Cada jugador lleva *hasta tres* Bugs, pero pelea *uno a uno*. Eliges el
  equipo antes de empezar; con uno basta.

  El combate va por *rondas*: los dos elegís acción y luego se resuelven las
  dos, primero la del más rápido. No es por turnos alternos.
][
  Two Erratas nearby can fight. No internet and no router needed.

  Each player brings *up to three* Bugs but fights *one at a time*. You pick
  the team before you start; one is enough.

  A battle runs in *rounds*: you both choose an action and then both resolve,
  the faster one first. It is not alternating turns.
]

#v(2mm)
#bi[
  *El triángulo de tipos.* Cada Bug y cada ataque tienen un tipo:
][
  *The type triangle.* Every Bug and every attack has a type:
]

#v(1mm)
#align(center, block(stroke: 0.5pt + black, inset: 2mm, radius: 0.6mm)[
  #set text(size: 8pt, weight: "bold")
  SEÑAL #sym.arrow.r CORRUPTO #sym.arrow.r SISTEMA #sym.arrow.r SEÑAL
])

#v(1mm)
#warnbox[
  #set text(size: 7pt)
  *La regla que nadie adivina:* la ventaja de tipo se aplica *una sola vez por
  Bug y combate*. Gastarla en un golpe flojo la desperdicia. \
  #text(fill: luma(25%))[*The rule nobody guesses:* a type advantage applies
  *once per Bug per battle*. The device says so when it fires --
  #scr[explota la debilidad] -- and once spent it is gone.]
]

#pagebreak()
= Combate: pelear / Battle: fighting

#grid(columns: (1fr, 1fr), column-gutter: 3mm, row-gutter: 2mm,
  screen("battle_menu"), screen("battle_hit"),
  screen("battle_protect"), screen("battle_faint"),
)

#v(1.5mm)
#bi[
  En tu turno eliges entre *cuatro ataques*, *protegerte*, usar un objeto o
  *cambiar* de Bug.

  *Cambiar cuesta la ronda entera*: entras y encajas el golpe del rival sin
  devolverlo. A veces compensa; casi nunca por costumbre.

  *Protegerse* anula el golpe de esa ronda. No abuses: es una ronda que no
  haces daño.

  Cuando un Bug *cae*, sacas otro. Cuando caen los tres, se acaba.
][
  On your turn you choose between *four attacks*, *protect*, an item, or
  *switching* Bug.

  *Switching costs the whole round*: you come in and take the opponent's hit
  without answering. Sometimes worth it; almost never as a habit.

  *Protect* cancels that round's hit. Do not lean on it: it is a round in which
  you deal no damage.

  When a Bug *faints* you send out another. When all three are down, it is
  over.
]

#v(1.5mm)
#align(center, screen("battle_result", width: 46mm))

#v(1mm)
#bi[
  Ganar da experiencia. Perder no te quita nada ni mata a nadie.
][
  Winning gives experience. Losing costs you nothing and kills no one.
]

// --- Link, trade, breed ---------------------------------------------------------
#pagebreak()
= Enlace e intercambio / Link and trade

#grid(columns: (1fr, 1fr, 1fr), column-gutter: 2mm,
  screen("link_searching", width: 30mm), screen("link_peers", width: 30mm),
  screen("link_card", width: 30mm),
)

#v(1.5mm)
#bi[
  #scr[ENLACE] busca otras Erratas cerca y te enseña quién hay. Los dos
  tenéis que aceptar: nadie puede conectarse contigo sin que lo veas.

  Al elegir a alguien salen tres opciones: #scr[COMBATE], #scr[INTERCAMBIO] y
  #scr[CRIAR].

  *Intercambiar* manda un Bug y recibe otro. Si el enlace se corta a medias,
  el aparato lo repara solo: no se pierde ni se duplica ninguno.
][
  #scr[ENLACE] looks for other Erratas nearby and shows you who is there.
  Both of you must accept: nobody can connect to you without you seeing it.

  Choosing someone offers three options: #scr[COMBATE], #scr[INTERCAMBIO] and
  #scr[CRIAR].

  *Trading* sends one Bug and receives another. If the link drops halfway
  through, the device repairs it on its own: none is lost and none is
  duplicated.
]

#v(1.5mm)
#bi[
  *Criar* no es un intercambio: no se va nadie. Cada aparato se queda con su
  Bug y gana un *huevo* propio.

  Cría el que llevas seleccionado, y no otro de la Caja. Los dos jugadores
  tenéis que aceptar la pareja en vuestro aparato: una negativa la cancela para
  los dos. No todas las parejas valen.

  Un Bug de la primera fase aún no puede, y el aparato lo dice antes de llamar
  al otro: #scr[Ese bug es muy pequeño aún]. Con la Caja llena el huevo no te
  cabe, aunque el del otro jugador sí llegue.
][
  *Breeding* is not a trade: nobody leaves. Each device keeps its own Bug and
  gains an *egg* of its own.

  It breeds the one you are carrying, not another from the Box. Both players
  accept the pair on their own device: one refusal cancels it for both. Not
  every pair works.

  A Bug still in its first stage cannot yet, and the device says so before it
  calls the other: #scr[Ese bug es muy pequeño aún]. With a full Box there is no
  room for your egg, though the other player's still arrives.
]

// --- 14. The creator ---------------------------------------------------------
#pagebreak()
= El creador / The creator

#grid(columns: (1fr, 1fr), column-gutter: 3mm,
  screen("creator_portal"),
  screen("creator_offline"),
)

#v(1.5mm)
#bi[
  La Errata puede abrir su propia página para que diseñes un Bug desde el
  móvil. No hay servidor de por medio: el aparato monta su propia red Wi-Fi.

  *El código QR es esa red, no la página.* Apúntale la cámara, únete a la red y
  la página se abre sola. Debajo del código tienes el nombre de la red, por si
  prefieres entrar a mano desde la lista de Wi-Fi del móvil.

  El *PIN* grande de la pantalla es lo que autoriza al móvil, y no va dentro
  del código: se lee del aparato, que lo tienes delante.

  Cierra el creador cuando termines. Si lo dejas abierto sin usarlo, se cierra
  solo y apaga la radio.
][
  The Errata can serve its own page so you can design a Bug from your phone.
  There is no server involved: the device brings up its own Wi-Fi network.

  *The QR code is that network, not the page.* Point the camera at it, join the
  network, and the page opens by itself. The network's name is printed under
  the code, in case you would rather join by hand from the phone's Wi-Fi list.

  The large *PIN* on the screen is what authorises the phone, and it is not
  inside the code: you read it off the device, which is in front of you.

  Close the creator when you are done. Left open and unused, it closes itself
  and switches the radio off.
]

// --- 15. Settings and battery ------------------------------------------------
#pagebreak()
= Ajustes y pilas / Settings and battery

// THE MANUAL SCREEN MOVED UP INTO THE STRIP AND "Acerca de" LEFT IT, and the
// second half of that is the page's rent. P10-C9 turned the sound row from two
// states into four, which is a paragraph this page had no room for; the
// booklet is saddle-stitched, so the alternative was 44 pages for one
// paragraph. Of the four screens that were here, settings_info was the one the
// prose spends three words on and the one no player has to recognise - a
// diagnostic dump that is unreadable at 30 mm anyway. The other three each
// carry a sentence: the sound row, the QR, and the closing countdown.
#grid(columns: (1fr, 1fr, 1fr), column-gutter: 2mm,
  screen("settings_list", width: 30mm), screen("manual", width: 30mm),
  screen("menu_settings_countdown", width: 30mm),
)

#v(1.5mm)
#bi[
  En #scr[AJUSTES] están la hora, el sonido, el brillo, la información del
  aparato y #scr[Manual].

  El sonido es un anillo de cuatro: #scr[ALTO], #scr[MEDIO], #scr[BAJO] y
  #scr[OFF]. Cada toque de *B* baja un paso y da un clic a ese nivel. Desde
  #scr[ALTO], la primera pulsación no apaga: baja el volumen. El silencio
  está al final.

  #scr[Manual] enseña un código QR que lleva el móvil a este mismo manual, al
  día: escanéalo si pierdes este cuadernillo.
][
  #scr[AJUSTES] holds the clock, sound, brightness, device information and
  #scr[Manual].

  Sound is a ring of four: #scr[ALTO] (high), #scr[MEDIO] (mid), #scr[BAJO]
  (low) and #scr[OFF]. Each tap of *B* steps down and clicks at that level.
  From #scr[ALTO] the first press does not switch it off: it turns the volume
  down. Silence is at the end.

  #scr[Manual] shows a QR code that takes your phone to this same manual, kept
  up to date: scan it if you lose this booklet.
]

#v(1.5mm)
#bi[
  *Las pantallas se cierran solas.* Si dejas el aparato quieto veinte segundos
  vuelve al inicio, y antes avisa con la barrita fina de abajo. Toca cualquier
  botón y se queda donde estaba.

  *Para que las pilas duren.* La radio es, con diferencia, lo que más gasta:

  - baja el brillo;
  - usa #scr[RED] cuando de verdad vayas a explorar, no de fondo;
  - cierra el creador al terminar;
  - quita las pilas si no vas a jugar en semanas.
][
  *Screens close by themselves.* Leave the device for twenty seconds and it
  returns home, warning you first with the thin bar along the bottom. Press any
  button and it stays where it was.

  *To make the cells last.* The radio is by far the biggest drain:

  - turn the brightness down;
  - use #scr[RED] when you actually mean to explore, not in the background;
  - close the creator when you finish;
  - take the cells out if you will not play for weeks.
]

// --- When something goes wrong -------------------------------------------------
#pagebreak()
= Cuando algo va mal / When something goes wrong

#grid(columns: (1fr, 1fr, 1fr), column-gutter: 2mm,
  screen("error_save_corrupt", width: 30mm), screen("error_save_newer", width: 30mm),
  screen("help_line", width: 30mm),
)

#v(1.5mm)
#bi[
  El aparato guarda la partida *dos veces*. Si al arrancar encuentra una
  copia dañada -- casi siempre por quitar las pilas mientras guardaba --
  recupera la anterior él solo y te lo dice. No pierdes la colección.

  Si encuentra una partida de una versión *más nueva* que su firmware, se
  niega a tocarla en vez de estropearla. Eso es deliberado.

  *En cualquier pantalla, pulsa A + B para ver qué hacen los botones ahí.* Es
  la ayuda del propio aparato. La única excepción es la pantalla de inicio:
  allí ese gesto apaga y enciende el sonido.
][
  The device saves your game *twice*. If it finds a damaged copy at startup --
  almost always from pulling the cells mid-save -- it restores the earlier one
  by itself and tells you. Your collection survives.

  If it finds a save from a *newer* version than its firmware, it refuses to
  touch it rather than damage it. That is deliberate.

  *On any screen, press A + B to see what the buttons do there.* That is the
  device's own help. The one exception is the home screen: there the same
  gesture switches the sound off and on.
]

// --- 16. Troubleshooting -----------------------------------------------------
#pagebreak()
= Problemas / Troubleshooting

#set text(size: 6.6pt)
#table(
  columns: (1fr, 1.2fr, 1.2fr),
  stroke: 0.3pt + luma(50%), inset: 1.2mm,
  [*Síntoma / Symptom*], [*Causa / Cause*], [*Solución / Fix*],

  [No enciende \ #text(fill: luma(25%))[Will not switch on]],
  [Pilas gastadas o al revés \ #text(fill: luma(25%))[Flat cells, or reversed]],
  [Revisa *+* y *-*. Pon dos pilas nuevas \
   #text(fill: luma(25%))[Check *+* and *-*. Fit two fresh cells]],

  [Pantalla en blanco o rayada \ #text(fill: luma(25%))[Blank or streaked screen]],
  [Contacto flojo del panel \ #text(fill: luma(25%))[Loose panel contact]],
  [Quita las pilas, espera 10 s, vuelve a ponerlas \
   #text(fill: luma(25%))[Remove the cells, wait 10 s, refit them]],

  [Se reinicia solo \ #text(fill: luma(25%))[Restarts on its own]],
  [Pilas casi vacías \ #text(fill: luma(25%))[Nearly flat cells]],
  [Cambia las dos a la vez \
   #text(fill: luma(25%))[Replace both at once]],

  [#scr[RED] no encuentra nada \ #text(fill: luma(25%))[#scr[RED] finds nothing]],
  [Redes ya agotadas, o no hay \
   #text(fill: luma(25%))[Networks spent, or none nearby]],
  [Muévete y vuelve en un par de horas \
   #text(fill: luma(25%))[Move, and come back in a couple of hours]],

  [Aviso de partida dañada \ #text(fill: luma(25%))[Damaged save warning]],
  [Se quitaron las pilas al guardar \
   #text(fill: luma(25%))[Cells pulled during a save]],
  [El aparato recupera la copia anterior solo \
   #text(fill: luma(25%))[The device restores its own earlier copy]],

  // lang: "en" on the English halves of this row and not on the others: the
  // table sits outside bi(), so it inherits the document's Spanish, and
  // Spanish hyphenation breaks "home" as "ho-me". The rest of the table has
  // the same defect and no word in it breaks badly enough to be worth
  // reflowing six rows to prove it.
  [No suena \ #text(fill: luma(25%), lang: "en")[No sound]],
  [#scr[Sonido] está en #scr[OFF], o *A + B* lo apagó desde la pantalla de inicio \
   #text(fill: luma(25%), lang: "en")[#scr[Sonido] is #scr[OFF], or *A + B* silenced it from the home screen]],
  [En #scr[AJUSTES], pulsa *B* sobre #scr[Sonido] hasta #scr[ALTO] \
   #text(fill: luma(25%), lang: "en")[In #scr[AJUSTES], tap *B* on #scr[Sonido] until #scr[ALTO]]],

  [El móvil no ve el creador \ #text(fill: luma(25%))[Phone cannot see the creator]],
  [El creador está cerrado \ #text(fill: luma(25%))[The creator is closed]],
  [Ábrelo desde el menú y vuelve a escanear el QR \
   #text(fill: luma(25%))[Open it from the menu and rescan the QR]],
)

#v(1.5mm)
#grid(columns: (1fr, 1fr), column-gutter: 3mm,
  screen("error_save_corrupt", width: 40mm),
  screen("confirm_wipe", width: 40mm),
)

#v(1.2mm)
#set text(size: 7pt)
#bi[
  Al borrar la partida, el cursor empieza siempre en *NO*. Es a propósito.
][
  When wiping the save, the cursor always starts on *NO*. That is deliberate.
]

// --- 17. Glossary ------------------------------------------------------------
#pagebreak()
= Glosario / Glossary

#set text(size: 7pt)
#bi[
  *Bug* -- la criatura. Un bicho digital atrapado en las redes.

  *Caja* -- donde guardas tus Bugs. Diez plazas.

  *Wiki* -- la lista de las sesenta especies y de cuáles has conocido.

  *Seleccionado* -- el Bug que llevas encima y que cuidas.

  *Genoma* -- los datos internos que hacen a cada Bug distinto.

  *Rasgo* -- una peculiaridad interna que cambia cómo se comporta.

  *Corrupción* -- el desgaste que un Bug acumula. Cambia su aspecto.

  *Enlace* -- la conexión directa entre dos Erratas cercanas.

  *Encuentro* -- lo que sale de mirar una red con #scr[RED].
][
  *Bug* -- the creature. A digital bug trapped in the networks.

  *Box* -- where you keep your Bugs. Ten slots.

  *Wiki* -- the list of the sixty species, and which ones you have met.

  *Selected* -- the Bug you carry and care for.

  *Genome* -- the internal data that makes each Bug different.

  *Trait* -- an internal quirk that changes how it behaves.

  *Corruption* -- the wear a Bug builds up. It changes how it looks.

  *Link* -- the direct connection between two nearby Erratas.

  *Encounter* -- what comes out of checking a network with #scr[RED].
]
