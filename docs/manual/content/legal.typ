// =============================================================================
//  docs/manual/content/legal.typ - the legal block, back half of the booklet.
//
//  Everything identifying comes from product_facts.toml via fact(). Nothing
//  here may hard-code a company, an address, a URL or a registration number.
//
//  Layout is `seq`, not `bi`: continuous legal prose reads badly in a 40 mm
//  column and is read rarely, so Spanish runs full width and English follows.
//
//  POSITIONING. Errata is placed on the market as a 14+ electronic device,
//  NOT as a toy, so Directive 2009/48/EC, EN 71 and EN IEC 62115 are out of
//  scope. That position is only defensible if the product says so plainly and
//  consistently - a regulator may reclassify a product that is "obviously
//  intended for children". The safety page is what makes the position hold.
//  soften it.
// =============================================================================

#import "../lib.typ": *

// --- 18. Safety --------------------------------------------------------------
#pagebreak()
= Seguridad / Safety <sec-safety>

#warnbox[
  #set text(size: 8pt)
  *ESTO NO ES UN JUGUETE.* \
  No apto para menores de 14 años.
  #v(1mm)
  #text(fill: luma(25%))[*THIS IS NOT A TOY.* \
  Not suitable for children under 14 years of age.]
]

#seq[
  Errata es un aparato electrónico de ocio para personas de 14 años o más.
  No está diseñado ni ensayado como juguete y no cumple los requisitos
  aplicables a los juguetes. Manténgalo fuera del alcance de los niños
  pequeños: contiene piezas pequeñas y pilas.

  *Advertencias generales.*

  - No sumerja el aparato ni lo exponga a lluvia, humedad ni líquidos. No es
    estanco.
  - No lo desmonte, modifique ni repare. En su interior no hay piezas que el
    usuario pueda sustituir, salvo las pilas.
  - No lo use ni lo guarde por debajo de 0 °C ni por encima de 40 °C.
  - No lo deje caer ni lo aplaste. Si la carcasa se agrieta, deje de usarlo y
    retire las pilas.
  - El aparato emite destellos y sonidos. Una proporción muy pequeña de
    personas puede sufrir crisis ante luces intermitentes. Si usted o alguien
    de su familia tiene epilepsia fotosensible, consulte a un médico antes de
    usarlo. Deje de jugar de inmediato si nota mareo, visión alterada,
    contracciones o desorientación.
  - Descanse unos minutos cada media hora de juego.
  - No acerque el zumbador al oído.
  - Utilícelo solo para lo que se describe en este manual.
][
  Errata is a leisure electronic device for people aged 14 and over. It is
  not designed or tested as a toy and does not meet the requirements that apply
  to toys. Keep it out of reach of small children: it contains small parts and
  batteries.

  *General warnings.*

  - Do not immerse the device or expose it to rain, damp or liquids. It is not
    waterproof.
  - Do not disassemble, modify or repair it. There are no user-serviceable
    parts inside other than the batteries.
  - Do not use or store it below 0 °C or above 40 °C.
  - Do not drop or crush it. If the enclosure cracks, stop using it and remove
    the batteries.
  - The device produces flashes and sounds. A very small number of people may
    have seizures triggered by flashing lights. If you or anyone in your family
    has photosensitive epilepsy, consult a doctor before use. Stop playing at
    once if you feel dizziness, altered vision, twitching or disorientation.
  - Take a few minutes' break every half hour of play.
  - Do not hold the sounder close to your ear.
  - Use it only as described in this manual.
]

// --- 19. Batteries -----------------------------------------------------------
#pagebreak()
= Pilas / Batteries <sec-batteries>

#seq[
  El aparato funciona con *dos pilas AAA* (LR03, 1,5 V) colocadas en serie. Las
  pilas son sustituibles por el usuario final sin herramientas ni soldadura.

  - Respete la polaridad *+* y *-* marcada en el compartimento.
  - No mezcle pilas nuevas con usadas, ni marcas o tipos distintos.
  - *No intente recargar pilas alcalinas.* No las cortocircuite, perfore,
    deforme, caliente ni arroje al fuego: pueden reventar o tener fugas.
  - *El aparato no tiene interruptor.* Quitar las pilas es la única forma de
    apagarlo del todo.
  - Retire las pilas si no va a usar el aparato durante semanas, y retire
    siempre las pilas gastadas. Una pila agotada puede tener fugas y dañar el
    aparato de forma irreversible.
  - Si hay fuga, no toque el líquido con las manos desnudas. Limpie el
    compartimento con un paño seco y lávese si hay contacto con la piel.
  - Las pilas no son alimento. En caso de ingestión, acuda de inmediato a un
    servicio médico.
  - Retire las pilas antes de desechar el aparato y deposítelas por separado
    (página #pg(<sec-disposal>)).
][
  The device runs on *two AAA cells* (LR03, 1.5 V) fitted in series. The cells
  are replaceable by the end user with no tools and no soldering.

  - Observe the *+* and *-* polarity marked in the compartment.
  - Do not mix new and used cells, or different brands or types.
  - *Do not attempt to recharge alkaline cells.* Do not short-circuit,
    puncture, deform, heat or burn them: they may burst or leak.
  - *The device has no on/off switch.* Taking the cells out is the only way to
    switch it off completely.
  - Remove the cells if the device will be unused for weeks, and always remove
    spent cells. A flat cell can leak and damage the device beyond repair.
  - If a cell leaks, do not touch the fluid with bare hands. Wipe the
    compartment with a dry cloth and wash if it contacts skin.
  - Batteries are not food. If swallowed, seek medical attention immediately.
  - Remove the cells before disposing of the device and hand them in separately
    (page #pg(<sec-disposal>)).
]

// --- 20. Radio and conformity ------------------------------------------------
#pagebreak()
= Radio y conformidad / Radio and conformity <sec-radio>

#seq[
  #box(stroke: 1pt + black, inset: 1.5mm, radius: 0.5mm,
       text(size: 11pt, weight: "bold", "CE"))
  #h(2mm) Este producto lleva el marcado CE.

  *Datos de radio (Directiva 2014/53/UE, art. 10.8).*

  - Banda de frecuencia: #radio-band("es")
  - Potencia máxima radiada: #fact("radio.max_power_dbm")
  - Restricciones de puesta en servicio: #radio-restrictions("es")

  *Declaración UE de conformidad simplificada.* Por la presente,
  #fact("entity.name") declara que el tipo de equipo radioeléctrico
  #fact("product.name") #fact("product.model") es conforme con la Directiva
  2014/53/UE. El texto completo de la declaración UE de conformidad está
  disponible en la dirección Internet siguiente: #fact("contact.doc_url")

  El aparato usa la radio de 2,4 GHz de tres maneras, y en ninguna se conecta a
  la red de nadie:

  - *Escucha* qué redes hay alrededor. El escaneo es pasivo: ni se asocia ni
    pide nada, solo oye lo que las redes ya emiten.
  - *Habla* directamente con otra Errata cercana, sin router.
  - *Crea su propia red* mientras el creador está abierto, para que su página
    se vea desde el móvil. Es la única situación en la que el aparato emite de
    forma continuada, y termina en cuanto cierras el creador.
][
  #v(1mm)
  This product bears the CE marking.

  *Radio data (Directive 2014/53/EU, art. 10.8).*

  - Frequency band: #radio-band("en")
  - Maximum radiated power: #fact("radio.max_power_dbm")
  - Restrictions on putting into service: #radio-restrictions("en")

  *Simplified EU declaration of conformity.* Hereby, #fact("entity.name")
  declares that the radio equipment type #fact("product.name")
  #fact("product.model") is in compliance with Directive 2014/53/EU. The full
  text of the EU declaration of conformity is available at the following
  internet address: #fact("contact.doc_url")

  The device uses its 2.4 GHz radio in three ways, and joins nobody's network
  in any of them:

  - It *listens* for the networks around it. The scan is passive: it neither
    associates nor asks for anything, it only hears what networks already
    broadcast.
  - It *talks* directly to another nearby Errata, with no router.
  - It *makes a network of its own* while the creator is open, so its page can
    be reached from a phone. That is the only situation in which the device
    transmits continuously, and it ends when you close the creator.
]

// --- 21. Identity, incidents, substances -------------------------------------
#pagebreak()
= Fabricante e incidencias / Manufacturer and incidents <sec-identity>

#seq[
  *Fabricante y persona responsable en la UE* (Reglamento (UE) 2023/988, art.
  16 y 19):

  #address-block

  Correo electrónico: #fact("contact.email") \
  Sitio web: #fact("contact.website")

  *Identificación del producto.* #fact("product.name")
  #fact("product.model"), revisión de hardware #fact("product.hw_revision").
  El número de lote figura impreso en el compartimento de las pilas con el
  formato #fact("product.batch_format").

  *Incidentes de seguridad.* Si este producto le ha causado o ha estado a punto
  de causarle un daño, comuníquelo a #fact("contact.safety_email"). Atendemos y
  registramos todos los avisos.

  *Sustancias.* Cumple la Directiva 2011/65/UE (RoHS) y el Reglamento (CE)
  1907/2006 (REACH). Para información sobre sustancias extremadamente
  preocupantes por encima del 0,1 % en peso: #fact("contact.email").
][
  *Manufacturer and EU responsible person* (Regulation (EU) 2023/988, arts. 16
  and 19):

  #address-block

  E-mail: #fact("contact.email") \
  Website: #fact("contact.website")

  *Product identification.* #fact("product.name") #fact("product.model"),
  hardware revision #fact("product.hw_revision"). The batch number is printed
  inside the battery compartment in the format #fact("product.batch_format").

  *Reporting a safety incident.* If this product has harmed you or came close
  to it, report it to #fact("contact.safety_email"). We answer and log every
  report.

  *Substances.* Complies with Directive 2011/65/EU (RoHS) and Regulation (EC)
  1907/2006 (REACH). For information on substances of very high concern above
  0.1 % by weight: #fact("contact.email").
]

// --- 22. Disposal ------------------------------------------------------------
#pagebreak()
= Residuos / Disposal <sec-disposal>

// The crossed-out wheeled bin of EN 50419, drawn here rather than imported as
// a binary nobody can diff. The mark is prescribed, so the proportions matter:
// a lidded bin on two wheels, a cross over it, and the solid bar beneath that
// says the product was placed on the market after 13 August 2005.
//
// P10-M5 must check this against the published figure in EN 50419 before the
// first print run. Getting a legally prescribed mark approximately right is
// not the same as getting it right.
#let wheelie-bin = box(width: 13mm, height: 15mm, {
  let ink = black
  // lid
  place(dx: 1.4mm, dy: 1.2mm, rect(width: 8.2mm, height: 1.1mm, fill: ink))
  // handle above the lid
  place(dx: 4.4mm, dy: 0.5mm, rect(width: 2.2mm, height: 0.7mm, fill: ink))
  // body, tapering slightly the way the standard figure does
  place(dx: 2.0mm, dy: 2.6mm, polygon(fill: ink,
    (0mm, 0mm), (7.0mm, 0mm), (6.4mm, 7.4mm), (0.6mm, 7.4mm)))
  // wheels
  place(dx: 2.7mm, dy: 10.2mm, circle(radius: 0.85mm, fill: ink))
  place(dx: 6.9mm, dy: 10.2mm, circle(radius: 0.85mm, fill: ink))
  // the cross, drawn past the bin on both diagonals so it reads as a crossing
  // out rather than as decoration on the bin
  place(dx: 0.8mm, dy: 1.0mm, line(
    start: (0mm, 0mm), end: (10.4mm, 10.0mm), stroke: 1.3pt + ink))
  place(dx: 11.2mm, dy: 1.0mm, line(
    start: (0mm, 0mm), end: (-10.4mm, 10.0mm), stroke: 1.3pt + ink))
  // the solid bar: placed on the market after 13 August 2005
  place(dx: 0.8mm, dy: 12.6mm, rect(width: 10.6mm, height: 1.5mm, fill: ink))
})

#grid(columns: (auto, 1fr), column-gutter: 3mm,
  wheelie-bin,
  [
    #set text(size: 6.5pt)
    #set par(leading: 2.6pt)
    El contenedor tachado significa que ni el aparato ni sus pilas pueden
    tirarse a la basura doméstica. \
    #text(fill: luma(25%))[The crossed-out bin means neither the device nor its
    batteries may go in household waste.]
  ],
)

#v(1.5mm)
#seq[
  *Aparato (RAEE).* Al final de su vida útil, deposite la Errata en un punto
  limpio, en la tienda donde lo compró o en cualquier punto de recogida de
  RAEE. Directiva 2012/19/UE y RD 110/2015.

  Número de productor RII-AEE: #fact("registers.rii_aee") \
  Sistema de responsabilidad ampliada: #fact("registers.scrap")

  *Pilas.* *Retire siempre las pilas antes de desechar el aparato* y
  deposítelas en un contenedor de pilas. Reglamento (UE) 2023/1542 y Real
  Decreto 710/2015. Son sustituibles y extraíbles por el usuario final sin
  herramientas.

  Número RII-PYA: #fact("registers.rii_pya")
][
  *Device (WEEE).* At the end of its life, take the Errata to a civic
  amenity site, to the shop where you bought it, or to any WEEE collection
  point. Directive 2012/19/EU and Spanish RD 110/2015.

  RII-AEE producer number: #fact("registers.rii_aee") \
  Extended producer responsibility scheme: #fact("registers.scrap")

  *Batteries.* *Always remove the batteries before disposing of the device* and
  put them in a battery collection bin. Regulation (EU) 2023/1542 and Royal
  Decree 710/2015. They are replaceable and removable by the end user without
  tools.

  RII-PYA number: #fact("registers.rii_pya")
]

// --- 23. Warranty, withdrawal, privacy, security -----------------------------
#pagebreak()
= Garantía, privacidad y seguridad / Warranty, privacy, security <sec-warranty>

#seq[
  *Garantía legal.* Este producto está cubierto por la garantía legal de
  conformidad de #fact("commercial.warranty_years") años prevista en el Real
  Decreto Legislativo 1/2007. Dispondremos de piezas de repuesto y asistencia
  técnica durante #fact("commercial.spare_parts_years") años desde que el
  modelo deje de fabricarse. Reclamaciones en #fact("contact.support_url").

  *Desistimiento.* En las compras a distancia dispone de
  #fact("commercial.withdrawal_days") días naturales para desistir sin
  justificación, conforme a la Directiva 2011/83/UE. Condiciones en
  #fact("contact.website").

  *Privacidad.* Errata funciona sin cuenta, sin registro y sin conexión a
  internet. No recoge ni transmite datos personales.

  Del escaneo de redes conviene ser preciso, porque es lo único que el aparato
  observa de su entorno. El nombre de la red y la dirección física del router
  existen únicamente dentro de la función que lee el resultado del escaneo, y
  se destruyen ahí mismo: lo que sale de esa función es un número resumen y una
  etiqueta de tipo de red. Ese número se calcula con una sal propia de cada
  aparato, así que el mismo router da un número distinto en dos Erratas y no
  puede usarse para cruzar datos entre unidades. Ningún nombre de red se guarda,
  se enseña en pantalla ni se manda a otro aparato.

  La página del creador se sirve desde el propio aparato y no envía nada al
  exterior. Protección de datos: #fact("contact.email").

  *Seguridad del producto con elementos digitales.* Punto único de contacto:
  #fact("contact.safety_email"). Política de divulgación coordinada de
  vulnerabilidades: #fact("contact.vuln_disclosure_url"). Lista de materiales
  de software (SBOM): #fact("contact.sbom_url"). Soporte de seguridad hasta
  #fact("commercial.support_end_date").

  *Este producto no se actualiza por sí solo.* No tiene actualización
  inalámbrica: no busca versiones nuevas, no se conecta a internet y nada
  llega al aparato sin que usted lo instale. Mientras dure el soporte
  publicaremos el firmware corregido y las instrucciones para instalarlo por
  USB en #fact("contact.support_url").

  Para retirar el aparato de forma segura, borre la partida desde
  #scr[AJUSTES] antes de cederlo o desecharlo: eso elimina sus Bugs y el
  identificador aleatorio del aparato.
][
  *Legal guarantee.* This product carries the
  #fact("commercial.warranty_years")-year legal guarantee of conformity under
  Spanish Royal Legislative Decree 1/2007. We will hold spare parts and offer
  technical assistance for #fact("commercial.spare_parts_years") years after
  the model stops being manufactured. Claims at #fact("contact.support_url").

  *Right of withdrawal.* On distance sales you have
  #fact("commercial.withdrawal_days") calendar days to withdraw without giving
  a reason, under Directive 2011/83/EU. Terms at #fact("contact.website").

  *Privacy.* The Errata works with no account, no sign-up and no internet
  connection. It neither collects nor transmits personal data.

  The network scan is worth being precise about, because it is the only thing
  the device observes about its surroundings. A network's name and a router's
  hardware address exist only inside the function that reads the scan result,
  and they are destroyed there: what leaves that function is a summary number
  and a network-type label. The number is computed with a salt unique to each
  device, so the same router yields a different number on two Erratas and
  cannot be used to correlate them. No network name is stored, shown on screen
  or sent to another device.

  The creator page is served by the device itself and sends nothing outside.
  Data protection: #fact("contact.email").

  *Security of a product with digital elements.* Single point of contact:
  #fact("contact.safety_email"). Coordinated vulnerability disclosure policy:
  #fact("contact.vuln_disclosure_url"). Software bill of materials (SBOM):
  #fact("contact.sbom_url"). Security support until
  #fact("commercial.support_end_date").

  *This product does not update itself.* There is no over-the-air update: it
  does not look for new versions, does not connect to the internet, and nothing
  reaches the device unless you install it. For as long as support lasts we
  will publish corrected firmware and instructions for installing it over USB
  at #fact("contact.support_url").

  To decommission the device safely, wipe the save from #scr[AJUSTES] before
  passing it on or discarding it: that removes your Bugs and the device's
  random identifier.
]
