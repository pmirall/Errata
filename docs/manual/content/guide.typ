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
//  Pebblebol/src/core/strings_es.h. If a label changes there, change it here.
// =============================================================================

#import "../lib.typ": *

// --- 2. What is in the box ---------------------------------------------------
#pagebreak()
= En la caja / In the box

#drawing("exploded", "Exploded view: device, 2x AAA, manual", height: 38mm)

#bi[
  #mark(1, [El Pebblebol.], [The Pebblebol.])
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

#grid(columns: (1fr, 1fr), column-gutter: 3mm,
  screen("boot_splash"),
  screen("time_entry"),
)

#v(1.5mm)
#bi[
  Al poner las pilas el aparato arranca solo. Verás el nombre del producto y la
  versión del firmware.

  La primera vez te pedirá la *hora*. Ajústala: el Pebble duerme de noche y
  come de día, y sin hora correcta su ritmo no cuadra con el tuyo.

  Con *A* cambias de digito. Con *B mantenido* sumas uno. Cuando la hora sea
  correcta, mantén *B* sobre el último campo para guardarla.
][
  The device starts as soon as the cells go in. You will see the product name
  and the firmware version.

  The first time, it asks for the *time*. Set it: the Pebble sleeps at night
  and eats by day, and with the wrong time its rhythm will not match yours.

  *A* moves between digits. *Hold B* adds one. When the time is right, hold *B*
  on the last field to save it.
]

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
    [*B* toque], [*Atrás.* En todas las pantallas],
      [#text(fill: luma(25%))[*Back.* On every screen]],
    [*B* mantenido], [*Elige* la línea marcada],
      [#text(fill: luma(25%))[*Chooses* the highlighted line]],
    [*A + B*], [Ayuda de la pantalla actual],
      [#text(fill: luma(25%))[Help for the current screen]],
    [*A + B* largo], [Vuelve a INICIO desde donde sea],
      [#text(fill: luma(25%))[Returns to HOME from anywhere]],
  )
]

#v(1.5mm)
#warnbox[
  #set text(size: 7pt)
  *Lo único que sorprende:* para elegir hay que *mantener B*, no tocarlo. Un
  toque de *B* siempre va hacia atrás. \
  #text(fill: luma(25%))[*The one surprise:* to choose something you *hold B*,
  you do not tap it. A tap on *B* always goes back.]
]

#v(1mm)
#bi[
  En la pantalla de inicio no hay nada a lo que volver, así que allí *B* hace
  una caricia y *A + B* largo abre los ajustes.
][
  On the home screen there is nothing to go back to, so there *B* strokes the
  Pebble and a long *A + B* opens the settings.
]

// --- 6. The home screen ------------------------------------------------------
#pagebreak()
= La pantalla de inicio / The home screen

#align(center, screen("home_starter", width: 58mm, marks: (
  (0.46, 0.10, 1), (0.79, 0.10, 2), (0.90, 0.50, 3),
  (0.50, 0.62, 4), (0.20, 0.93, 5),
)))

#v(2mm)
#mark(1, [El nombre de tu Pebble.], [Your Pebble's name.])
#mark(2, [Su nivel.], [Its level.])
#mark(3, [Salud, comida y ánimo.], [Health, food and mood.])
#mark(4, [Tu Pebble. Su postura te dice cómo está.],
         [Your Pebble. Its posture tells you how it is doing.])
#mark(5, [Lo que hacen *A* y *B* en esta pantalla.],
         [What *A* and *B* do on this screen.])

#v(1.5mm)
#bi[
  Desde aquí, *A* abre el menú principal: #scr[PEBBLE] #scr[CUIDAR]
  #scr[JUGAR] #scr[CAJA] #scr[RED] #scr[ENLACE] #scr[AJUSTES].

  Los menús son anillos: al pasar del último vuelves al primero.
][
  From here, *A* opens the main menu: #scr[PEBBLE] #scr[CUIDAR] #scr[JUGAR]
  #scr[CAJA] #scr[RED] #scr[ENLACE] #scr[AJUSTES].

  Menus are rings: past the last item you are back at the first.
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
  En #scr[CUIDAR] tienes #scr[COMER], #scr[LIMPIAR], #scr[SALUD],
  #scr[ESTADO] y #scr[LUZ].

  Tu Pebble avisa cuando necesita algo. No hace falta estar pendiente todo el
  día: esto no es un Tamagotchi de los duros. *Tu Pebble no puede morir.*

  Solo el Pebble que llevas seleccionado necesita cuidados. Los que están en la
  Caja se recuperan solos, poco a poco, a lo largo de unas 24 horas.
][
  Under #scr[CUIDAR] you get #scr[COMER] (feed), #scr[LIMPIAR] (clean),
  #scr[SALUD] (health), #scr[ESTADO] (status) and #scr[LUZ] (light).

  Your Pebble tells you when it needs something. You do not have to watch it
  all day: this is not one of the harsh Tamagotchis. *Your Pebble cannot die.*

  Only the Pebble you have selected needs care. The ones in the Box recover on
  their own, slowly, over about 24 hours.
]

// --- 8. Play -----------------------------------------------------------------
#pagebreak()
= Jugar / Play

#align(center, screen("play_list", width: 54mm))

#v(2mm)
#bi[
  Los minijuegos suben la felicidad y dan algo de experiencia. Duran poco a
  propósito: el Pebblebol esta pensado para ratos sueltos, no para sesiones
  largas.

  Ningún minijuego pide reflejos rápidos fuera del propio minijuego.
][
  Minigames raise happiness and give a little experience. They are short on
  purpose: the Pebblebol is built for odd moments, not long sessions.

  No minigame asks for fast reactions outside the minigame itself.
]

// --- 9-10. Explore (spread) --------------------------------------------------
#pagebreak()
= Explorar / Explore

#bi[
  Esta es la idea central del producto. *Los Pebbles viven en las redes Wi-Fi
  que te rodean.* Llévate el aparato encima y usa #scr[RED] para mirar.
][
  This is the core idea of the product. *Pebbles live in the Wi-Fi networks
  around you.* Carry the device with you and use #scr[RED] to look.
]

#v(2mm)
#align(center, screen("network_scanning", width: 54mm))

#v(2mm)
#bi[
  Una red puede darte un Pebble salvaje, uno raro, un objeto, un suceso
  especial, o nada. Cada red se agota durante *un par de horas* como minimo:
  quedarte quieto en casa no sirve de mucho. Hay que moverse.
][
  A network can give you a wild Pebble, a rare one, an item, a special event,
  or nothing. Each network then goes quiet for *at least a couple of hours*:
  standing still at home gets you little. You have to move.
]

#pagebreak()
= Tu privacidad / Your privacy

#warnbox[
  #set text(size: 7pt)
  *El Pebblebol nunca se conecta a esas redes.* Solo mira que nombres hay en el
  aire, igual que hace tu móvil cuando abres la lista de Wi-Fi. \
  #text(fill: luma(25%))[*The Pebblebol never connects to those networks.* It
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
= Capturar y la Caja / Capture and the Box

#grid(columns: (1fr, 1fr), column-gutter: 3mm, row-gutter: 2mm,
  screen("box_list"), screen("box_card"),
  screen("box_actions"), screen("box_empty"),
)

#v(1.5mm)
#bi[
  Cuando aparece un Pebble salvaje puedes intentar capturarlo. No siempre sale:
  cuanto más raro, más cuesta.

  La *Caja* guarda hasta *diez* Pebbles. Desde #scr[CAJA] puedes ver la ficha
  de cada uno, cambiar cual llevas seleccionado, o soltarlo.

  El Pebble seleccionado es el que llevas encima, el que cuidas y el que crece.
][
  When a wild Pebble appears you can try to capture it. It does not always
  work: the rarer it is, the harder it gets.

  The *Box* holds up to *ten* Pebbles. From #scr[CAJA] you can read each one's
  card, change which one you are carrying, or release it.

  The selected Pebble is the one you carry, the one you care for and the one
  that grows.
]

// --- 12. Evolution -----------------------------------------------------------
#pagebreak()
= Evolución / Evolution

#align(center, screen("evolution_egg", width: 54mm))

#v(2mm)
#bi[
  Un Pebble que sube de nivel puede evolucionar. La evolución cambia su
  aspecto, sus estadísticas y a veces sus ataques.

  Cada evolución vuelve al Pebble un poco más... suyo. No esperes que se
  vuelvan más bonitos.
][
  A Pebble that levels up may evolve. Evolution changes its look, its stats and
  sometimes its attacks.

  Each evolution makes the Pebble a little more itself. Do not expect them to
  get prettier.
]

// --- 13. Battle, trade, link -------------------------------------------------
#pagebreak()
= Combate y enlace / Battle and link

#align(center, screen("link_peers", width: 54mm))

#v(2mm)
#bi[
  Con #scr[ENLACE] dos Pebblebol se encuentran sin necesidad de internet ni de
  router. Solo tienen que estar cerca.

  *Combate.* Cada jugador lleva hasta tres Pebbles. Se pelea de uno en uno, por
  turnos, con cuatro ataques por Pebble y un triángulo de tres tipos. Cambiar
  de Pebble cuesta el turno.

  *Intercambio y cría.* También puedes intercambiar Pebbles con otro jugador, o
  criar dos compatibles.
][
  With #scr[ENLACE] two Pebblebols find each other with no internet and no
  router. They just have to be close.

  *Battle.* Each player brings up to three Pebbles. You fight one at a time, in
  turns, with four attacks per Pebble and a three-type triangle. Switching
  Pebble costs you the turn.

  *Trade and breed.* You can also trade Pebbles with another player, or breed
  two compatible ones.
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
  El Pebblebol puede abrir su propia página para que diseñes un Pebble desde el
  móvil. El aparato crea una red propia, te enseña un código QR y un PIN, y tu
  te conectas a el. No hay servidor de por medio.

  Cierra el creador cuando termines. La página solo debería estar abierta
  mientras la usas.
][
  The Pebblebol can serve its own page so you can design a Pebble from your
  phone. The device makes its own network, shows a QR code and a PIN, and you
  connect to it. There is no server involved.

  Close the creator when you are done. The page should only be open while you
  are using it.
]

// --- 15. Settings and battery ------------------------------------------------
#pagebreak()
= Ajustes y pilas / Settings and battery

#grid(columns: (1fr, 1fr), column-gutter: 3mm,
  screen("settings_list"),
  screen("settings_info"),
)

#v(1.5mm)
#bi[
  En #scr[AJUSTES] están la hora, el sonido, el brillo y la información del
  aparato.

  *Para que las pilas duren:*

  - baja el brillo;
  - usa #scr[RED] cuando de verdad vayas a explorar, no de fondo;
  - cierra el creador al terminar;
  - quita las pilas si no vas a jugar en semanas.

  La radio es, con diferencia, lo que más gasta.
][
  #scr[AJUSTES] holds the clock, sound, brightness and device information.

  *To make the cells last:*

  - turn the brightness down;
  - use #scr[RED] when you actually mean to explore, not in the background;
  - close the creator when you finish;
  - take the cells out if you will not play for weeks.

  The radio is by far the biggest drain.
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
  [Pilas gastadas o al reves \ #text(fill: luma(25%))[Flat cells, or reversed]],
  [Revisa *+* y *-*. Pon dos pilas nuevas \
   #text(fill: luma(25%))[Check *+* and *-*. Fit two fresh cells]],

  [Pantalla en blanco o rayada \ #text(fill: luma(25%))[Blank or streaked screen]],
  [Contacto flojo del panel \ #text(fill: luma(25%))[Loose panel contact]],
  [Quita las pilas, espera 10 s, vuelve a ponerlas \
   #text(fill: luma(25%))[Remove the cells, wait 10 s, refit them]],

  [Se reinicia solo \ #text(fill: luma(25%))[Restarts on its own]],
  [Pilas casi vacias \ #text(fill: luma(25%))[Nearly flat cells]],
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
  *Pebble* -- la criatura. Un bicho digital atrapado en las redes.

  *Caja* -- donde guardas tus Pebbles. Diez plazas.

  *Seleccionado* -- el Pebble que llevas encima y que cuidas.

  *Genoma* -- los datos internos que hacen a cada Pebble distinto.

  *Rasgo* -- una peculiaridad heredada que cambia cómo se comporta.

  *Corrupción* -- el desgaste que un Pebble acumula. Cambia su aspecto.

  *Enlace* -- la conexión directa entre dos Pebblebol cercanos.

  *Encuentro* -- lo que sale de mirar una red con #scr[RED].
][
  *Pebble* -- the creature. A digital bug trapped in the networks.

  *Box* -- where you keep your Pebbles. Ten slots.

  *Selected* -- the Pebble you carry and care for.

  *Genome* -- the internal data that makes each Pebble different.

  *Trait* -- an inherited quirk that changes how it behaves.

  *Corruption* -- the wear a Pebble builds up. It changes how it looks.

  *Link* -- the direct connection between two nearby Pebblebols.

  *Encounter* -- what comes out of checking a network with #scr[RED].
]
