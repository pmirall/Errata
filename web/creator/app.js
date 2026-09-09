// =============================================================================
//  ERRATA CREATOR - THE PAGE (P8-C4, spec sections 33 to 39).
//
//  ==========================================================================
//  THIS PAGE MIRRORS THE SCHEMA. THE DEVICE RE-VALIDATES. NOTHING HERE IS A
//  CONTROL.
//  ==========================================================================
//  Every rule this file enforces - the stat band, the four-move rule, the
//  power cap, the attack budget, the character set, the sprite geometry - is
//  enforced AGAIN on the device by game/validate.cpp's
//  validate_custom_species(), over the bytes that actually arrived, with no
//  knowledge that this page exists.
//
//  WITH EXACTLY ONE EXCEPTION, NAMED SO THIS PARAGRAPH STAYS TRUE (phase-8
//  exit). SE.anyEmpty() - "Algun fotograma del sprite esta vacio" - has no
//  device twin: game/validate.cpp never inspects the sprite BYTES, and should
//  not, because spec section 35 asks for sprite dimensions, data size and
//  palette, all three of which are structural here (the record's sprite is a
//  fixed array and creator_parse.cpp refuses anything that is not exactly it).
//  A blank creature is LEGAL. That rule is this page being kind, its code
//  column in localProblems() is '-' because there is no device code to name,
//  and the direction is the safe one: the page is NARROWER than the device
//  there, never wider - as it is on the two name characters the paragraph below
//  covers. Anything WIDER than the device would be a rule nobody enforces.
//  Checked one by one at the exit: every other rule in localProblems() and in
//  stepReady() has a named VR_CS_* twin, the leading/trailing space rule
//  included. The device cannot tell a request from this
//  page apart from a request from curl, so it assumes curl
//  (networking/creator_server.h says so at length).
//
//  What is here exists so a user is not refused after five minutes of drawing.
//  That is worth building and it is worth NOTHING as a defence. A page that
//  looks authoritative is exactly how a client-side check becomes a control by
//  accident: somebody reads the greyed-out button, believes the rule is held
//  here, and the next device-side guard is written as "the page already
//  checks that". It does not. It cannot. It runs on the attacker's phone.
//
//  THE ONE PLACE THE PAGE IS DELIBERATELY STRICTER THAN THE DEVICE is the
//  name: creator_name_char_ok() accepts '"' and '\', and this page refuses
//  them, because the device's document reader has no escapes at all
//  (networking/creator_parse.cpp) and a name carrying either would be refused
//  as CP_SYNTAX with no field named. Narrower is the safe direction; wider
//  would be a lie.
//
//  ==========================================================================
//  NOT ONE NUMBER IN THIS FILE IS A COPY OF A DEVICE CONSTANT
//  ==========================================================================
//  GET /api/schema serves data/creator_schema_json.h, which
//  tools/gen_content.py emits from the same content object as
//  data/creator_schema.h in the same run and static_asserts against the
//  compiled constants (plan T4). The stat band, the budgets, the power cap,
//  the sprite geometry, the name length, the id range, the move count, the
//  type names and all 34 attack rows come from that document at load time. A
//  literal here would be a second source of truth, and the failure it produces
//  is a user shown 68 % and refused at 101 %.
//
//  THE ONE EXCEPTION IS API_EXPECTED, AND IT IS NOT A COPY EITHER:
//  tools/gen_index_html.py substitutes core/version.h's CREATOR_API_VERSION
//  into it while generating src/data/index_html.h, and fails the build if the
//  placeholder is missing. It exists so a page cached from an older device
//  says so instead of sending a document the newer one will refuse by number.
//
//  ==========================================================================
//  THE BUDGET BAR IS THE DEVICE'S INTEGER ARITHMETIC, NOT THE SPEC'S SENTENCE
//  ==========================================================================
//  balance.json writes the section 36 percentage in floats. game/validate.cpp
//  cannot use a float in a stat path, so it uses the exact integer form, and
//  pct() below is that form character for character. Implementing the float
//  sentence instead would put the two bars one point apart at some inputs -
//  which is a user who is told 72 % and refused at 73 %.
//
//  ==========================================================================
//  OFFLINE AFTER LOAD (spec section 33)
//  ==========================================================================
//  One document, no external asset, no font, no CDN, no analytics, no
//  storage. The PIN lives in a variable and dies with the tab: writing it to
//  localStorage would outlive the session on a shared phone for no benefit,
//  since the device shows the PIN on its own screen.
//
//  Vanilla ES5-compatible JS. English identifiers and comments; the Spanish
//  the user reads is in index.html and in MSG below.
// =============================================================================
(function () {
  'use strict';

  // The creator HTTP contract this page speaks. Substituted by
  // tools/gen_index_html.py from core/version.h; see the banner.
  var API_EXPECTED = 0 /*@CREATOR_API_VERSION@*/;

  // ---------------------------------------------------------------------------
  //  STATE
  // ---------------------------------------------------------------------------
  var S = null;          // the schema document, once fetched
  var pin = '';          // in memory only, never stored
  var linked = false;    // the device has answered a gated route
  var step = 0;
  var pingTimer = 0, previewTimer = 0;

  var M = {              // the Bug being built
    name: '',
    type: 0,
    base: [],            // sized from the schema
    moves: []            // attack ids, in pick order
  };

  var STEPS = [
    ['sc-connect',  'CONEXI\u00d3N'],
    ['sc-info',     'NOMBRE'],
    ['sc-type',     'TIPO'],
    ['sc-body',     'CUERPO'],
    ['sc-sprite',   'SPRITE'],
    ['sc-attacks',  'ATAQUES'],
    ['sc-validate', 'VALIDAR'],
    ['sc-preview',  'VISTA PREVIA'],
    ['sc-upload',   'ENVIAR']
  ];

  var STAT_LABEL = ['VIDA', 'ATAQUE', 'DEFENSA', 'VELOCIDAD'];

  function $(id) { return document.getElementById(id); }
  function on(id, fn) { var e = $(id); if (e) e.addEventListener('click', fn); }
  function txt(id, s) { var e = $(id); if (e) e.textContent = s; }

  // ---------------------------------------------------------------------------
  //  WHAT THE DEVICE SAID, IN SPANISH - AND ALWAYS WITH ITS OWN CODE BESIDE IT
  //
  //  Every refusal the device can send carries a NAMED code (CP_* from the
  //  reader, VR_CS_* from the validator). The user gets a sentence; the code is
  //  printed next to it in mono, unmapped ones included, because a code this
  //  page has never heard of is exactly the case where hiding it turns a
  //  precise refusal into "no funciona".
  // ---------------------------------------------------------------------------
  var MSG = {
    // transport / gate
    pin:      'PIN incorrecto.',
    nopin:    'El dispositivo no tiene la pantalla del creador abierta.',
    locked:   'Demasiados intentos. Espera.',
    arg:      'El dispositivo no conoce esa ruta.',
    body:     'Esa petici\u00f3n no lleva cuerpo.',
    len:      'Falta la longitud del cuerpo.',
    big:      'El dibujo o el nombre ocupan m\u00e1s de lo que el dispositivo acepta.',
    type:     'Formato de env\u00edo no aceptado.',
    ro:       'El dispositivo est\u00e1 en s\u00f3lo lectura y no puede guardar.',
    nopet:    'El dispositivo todav\u00eda no tiene ning\u00fan Bug.',
    boxfull:  'La caja est\u00e1 llena. Libera un hueco en el dispositivo.',
    csfull:   'No quedan huecos de especie. Libera uno en el dispositivo.',
    install:  'El dispositivo no pudo instalar la especie.',
    box:      'El dispositivo no pudo crear el Bug.',
    flash:    'El dispositivo no pudo guardar en memoria.',
    internal: 'El dispositivo rechaz\u00f3 el Bug ya construido.',
    // the document reader
    CP_EMPTY:       'No lleg\u00f3 nada.',
    CP_SYNTAX:      'El documento no tiene la forma esperada.',
    CP_TRAILING:    'Sobran bytes al final del documento.',
    CP_UNKNOWN_KEY: 'El documento lleva un campo que el dispositivo no conoce.',
    CP_DUP_KEY:     'Un campo aparece dos veces.',
    CP_MISSING_KEY: 'Falta un campo obligatorio.',
    CP_BAD_TYPE:    'Un campo lleva un valor del tipo equivocado.',
    CP_NUMBER:      'Un n\u00famero est\u00e1 fuera de rango.',
    CP_ARRAY_LEN:   'Una lista no tiene la longitud exacta.',
    CP_STRING_LEN:  'El nombre es demasiado largo.',
    CP_NUL:         'El texto lleva un byte nulo.',
    CP_UTF8:        'El nombre lleva un car\u00e1cter fuera de Latin-1.',
    CP_NAME_CHAR:   'El nombre lleva un car\u00e1cter que la pantalla no dibuja.',
    CP_SPRITE_LEN:  'El sprite no tiene el tama\u00f1o exacto.',
    CP_SPRITE_HEX:  'El sprite lleva un car\u00e1cter que no es hexadecimal.',
    CP_VERSION:     'Esta p\u00e1gina habla otra versi\u00f3n del API.',
    // the validator
    VR_CS_BAD_HEADER:      'La cabecera de la especie no es v\u00e1lida.',
    VR_CS_RESERVED:        'Campos reservados no vac\u00edos.',
    VR_CS_BAD_TYPE:        'Tipo no v\u00e1lido.',
    VR_CS_BAD_STAT:        'Una estad\u00edstica est\u00e1 fuera de rango.',
    VR_CS_STAT_BUDGET:     'El total de estad\u00edsticas est\u00e1 fuera de la banda.',
    VR_CS_UNKNOWN_MOVE:    'Un ataque no existe.',
    VR_CS_MOVE_REPEATED:   'Hay un ataque repetido.',
    VR_CS_MOVE_OFF_TYPE:   'Un ataque no es del tipo de la especie ni neutral.',
    VR_CS_NO_DAMAGING_MOVE:'Hace falta al menos un ataque que haga da\u00f1o.',
    VR_CS_POWER_CAP:       'Un ataque supera la potencia m\u00e1xima.',
    VR_CS_ATTACK_BUDGET:   'Los ataques superan el presupuesto.',
    VR_CS_BUDGET_MISMATCH: 'El dispositivo calcula otro coste.',
    VR_CS_BAD_NAME:        'El nombre no es v\u00e1lido.',
    VR_CS_BAD_COMPAT:      'Una especie de creador no cr\u00eda.'
  };

  function say(err, why) {
    var base = MSG[err] || ('El dispositivo rechaz\u00f3 la petici\u00f3n (' + err + ').');
    if (why) base = (MSG[why] || base) + ' ';
    return base;
  }

  var toastTimer = 0;
  function toast(s, kind) {
    var t = $('toast');
    t.textContent = s;
    t.className = kind || '';
    t.hidden = false;
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { t.hidden = true; }, 4200);
  }

  // ---------------------------------------------------------------------------
  //  TRANSPORT
  //
  //  The PIN rides in X-Pin on every request. It used to be a query argument
  //  in this product, which put the secret in the URL and therefore in the
  //  browser history; the device registers exactly this header in
  //  collectHeaders() and reads nothing else.
  //
  //  429 CARRIES AN EMPTY BODY BY DESIGN, so it is handled before any parse:
  //  JSON.parse('') throws, and a page that lets that throw turns "slow down"
  //  into a dead button.
  // ---------------------------------------------------------------------------
  function req(method, path, body) {
    var h = { 'Accept': 'application/json' };
    if (pin) h['X-Pin'] = pin;
    if (body !== undefined) h['Content-Type'] = 'application/json';
    return fetch(path, { method: method, headers: h, body: body, cache: 'no-store' })
      .then(function (r) {
        return r.text().then(function (t) {
          var j = null;
          if (t) { try { j = JSON.parse(t); } catch (e) { j = null; } }
          return { status: r.status, j: j, raw: t };
        });
      });
  }

  // A refusal, turned into one sentence. Returns null when the answer was fine.
  function refusal(r) {
    if (r.status === 429) return 'Demasiadas peticiones seguidas. Prueba otra vez.';
    if (r.status >= 200 && r.status < 300) return null;
    if (!r.j || !r.j.err) return 'El dispositivo contest\u00f3 ' + r.status + '.';
    var s = say(r.j.err, r.j.why);
    if (r.j.err === 'locked' && typeof r.j.s === 'number')
      s = MSG.locked + ' ' + r.j.s + ' s.';
    if (r.j.err === 'big' && typeof r.j.max === 'number')
      s = MSG.big + ' (m\u00e1ximo ' + r.j.max + ' bytes)';
    if (r.j.why) s += '[' + r.j.why + ']';
    return s;
  }

  function codeOf(r) { return (r.j && r.j.why) ? r.j.why : (r.j && r.j.err) || String(r.status); }

  // ---------------------------------------------------------------------------
  //  THE SECTION 36 ARITHMETIC, in the device's integer form.
  //      D   = TOTAL * BUDGET
  //      pct = (50 * (BUDGET*S + TOTAL*A) + D/2) / D      integer division
  //  Saturated at 100, exactly as game/validate.cpp's creator_power_pct().
  // ---------------------------------------------------------------------------
  function pct(statUsed, atkUsed) {
    var T = S.stat.max, B = S.atkbudget;
    var D = T * B;
    var n = 50 * (B * statUsed + T * atkUsed);
    var p = Math.floor((n + Math.floor(D / 2)) / D);
    return p > 100 ? 100 : p;
  }

  function statUsed() {
    var s = 0, i;
    for (i = 0; i < M.base.length; i++) s += M.base[i];
    return s;
  }

  function atkOf(id) {
    var i;
    for (i = 0; i < S.atk.length; i++) if (S.atk[i][0] === id) return S.atk[i];
    return null;
  }

  function atkUsed() {
    var c = 0, i, a;
    for (i = 0; i < M.moves.length; i++) { a = atkOf(M.moves[i]); if (a) c += a[4]; }
    return c;
  }

  // ---------------------------------------------------------------------------
  //  THE LOCAL MIRROR OF validate_custom_species(). Read the banner: this is a
  //  courtesy. Each entry names the DEVICE code it mirrors so the two lists can
  //  be diffed by a human, and so an unmirrored rule is visible as a gap rather
  //  than as an absence.
  // ---------------------------------------------------------------------------
  function nameCharOk(cp) {
    if (cp === 0x22 || cp === 0x5c) return false;   // stricter than the device, see banner
    if (cp >= 0x20 && cp <= 0x7e) return true;
    return [0xa1, 0xaa, 0xb0, 0xb7, 0xba, 0xbf,
            0xc1, 0xc9, 0xcd, 0xd3, 0xda, 0xd1, 0xdc,
            0xe1, 0xe9, 0xed, 0xf3, 0xfa, 0xf1, 0xfc].indexOf(cp) >= 0;
  }

  function nameProblem() {
    var n = M.name, cps = Array.from(n), i, cp;
    if (cps.length === 0) return 'El nombre no puede estar vac\u00edo.';
    if (cps.length > S.name.max) return 'M\u00e1ximo ' + S.name.max + ' caracteres.';
    if (cps[0] === ' ' || cps[cps.length - 1] === ' ')
      return 'Sin espacio al principio ni al final.';
    for (i = 0; i < cps.length; i++) {
      cp = cps[i].codePointAt(0);
      if (!nameCharOk(cp))
        return 'El car\u00e1cter "' + cps[i] + '" no lo dibuja la pantalla.';
    }
    return null;
  }

  // Every rule, in the device's own order, each with the code it mirrors.
  function localProblems() {
    var out = [], i, a, seen = {}, dmg = false, maxp = 0;
    var np = nameProblem();
    if (np) out.push(['VR_CS_BAD_NAME', np]);
    if (M.type < 0 || M.type >= S.types) out.push(['VR_CS_BAD_TYPE', 'Tipo no elegido.']);
    for (i = 0; i < M.base.length; i++)
      if (M.base[i] < S.stat.lo || M.base[i] > S.stat.hi)
        out.push(['VR_CS_BAD_STAT', STAT_LABEL[i] + ' fuera de ' + S.stat.lo + '..' + S.stat.hi]);
    var su = statUsed();
    if (su < S.stat.min || su > S.stat.max)
      out.push(['VR_CS_STAT_BUDGET',
                'Total ' + su + ', la banda es ' + S.stat.min + '..' + S.stat.max + '.']);
    if (M.moves.length !== S.moves)
      out.push(['VR_CS_UNKNOWN_MOVE', 'Hacen falta ' + S.moves + ' ataques.']);
    for (i = 0; i < M.moves.length; i++) {
      if (seen[M.moves[i]]) out.push(['VR_CS_MOVE_REPEATED', 'Ataque repetido.']);
      seen[M.moves[i]] = 1;
      a = atkOf(M.moves[i]);
      if (!a) { out.push(['VR_CS_UNKNOWN_MOVE', 'Ataque desconocido.']); continue; }
      if (a[1] !== M.type && a[1] !== S.types)
        out.push(['VR_CS_MOVE_OFF_TYPE', S.an[i] + ' no es de este tipo.']);
      if (a[2] > 0) dmg = true;
      if (a[2] > maxp) maxp = a[2];
    }
    if (M.moves.length === S.moves && !dmg)
      out.push(['VR_CS_NO_DAMAGING_MOVE', 'Ning\u00fan ataque hace da\u00f1o.']);
    if (maxp > S.powcap)
      out.push(['VR_CS_POWER_CAP', 'Potencia m\u00e1xima ' + S.powcap + '.']);
    var au = atkUsed();
    if (au > S.atkbudget)
      out.push(['VR_CS_ATTACK_BUDGET', 'Coste ' + au + ' sobre ' + S.atkbudget + '.']);
    if (SE.anyEmpty())
      out.push(['-', 'Alg\u00fan fotograma del sprite est\u00e1 vac\u00edo.']);
    return out;
  }

  // ---------------------------------------------------------------------------
  //  THE BUDGET BAR - spec section 36's "PRESUPUESTO 82 %".
  // ---------------------------------------------------------------------------
  function drawBudget() {
    if (!S) return;
    var su = statUsed(), au = atkUsed(), p = pct(su, au);
    var over = su > S.stat.max || au > S.atkbudget;
    $('budget').hidden = (step < 3);
    $('budget').className = over ? 'over' : '';
    txt('budget-pct', p + ' %');
    $('budget-fill').style.width = Math.min(100, p) + '%';
    txt('budget-sub', 'estad\u00edsticas ' + su + '/' + S.stat.max +
                      '  \u00b7  ataques ' + au + '/' + S.atkbudget);
  }

  // ---------------------------------------------------------------------------
  //  NAVIGATION
  // ---------------------------------------------------------------------------
  // IS THIS SCREEN FINISHED? It gates the SIGUIENTE button and nothing else.
  // Read the banner before adding to it: this is a "you have not filled this in
  // yet" affordance, not a permission. Every rule it names is also in
  // localProblems(), and every rule in localProblems() is enforced again on the
  // device EXCEPT the empty-frame check, which the banner at the top of this
  // file names as the one page-only rule and explains. A rule that lived only
  // here AND was wider than the device would be a rule nobody enforces, because
  // the device never sees this button and curl never presses it.
  function stepReady(i) {
    if (i === 0) return linked;
    if (i === 1) return nameProblem() === null;
    if (i === 2) return M.type >= 0 && M.type < S.types;
    if (i === 3) { var s = statUsed(); return s >= S.stat.min && s <= S.stat.max; }
    if (i === 4) return !SE.anyEmpty();
    if (i === 5) {
      if (M.moves.length !== S.moves) return false;
      var k, a;
      for (k = 0; k < M.moves.length; k++) { a = atkOf(M.moves[k]); if (a && a[2] > 0) return true; }
      return false;
    }
    return true;
  }

  function show(i) {
    if (i < 0) i = 0;
    if (i > STEPS.length - 1) i = STEPS.length - 1;
    step = i;
    var k;
    for (k = 0; k < STEPS.length; k++) $(STEPS[k][0]).hidden = (k !== i);
    var lis = $('steps').children;
    for (k = 0; k < lis.length; k++)
      lis[k].className = (k < i) ? 'done' : (k === i ? 'now' : '');
    txt('steps-name', STEPS[i][1]);
    $('btn-back').disabled = (i === 0);
    $('btn-next').disabled = (i === STEPS.length - 1) || !stepReady(i);
    drawBudget();
    if (previewTimer) { clearInterval(previewTimer); previewTimer = 0; }
    if (i === 4) SE.paint();
    if (i === 5) renderAttacks();
    if (i === 6) renderLocal();
    if (i === 7) startPreview();
    window.scrollTo(0, 0);
  }

  function refresh() {
    drawBudget();
    $('btn-next').disabled = (step === STEPS.length - 1) || !stepReady(step);
  }

  // ---------------------------------------------------------------------------
  //  1. CONNECT
  // ---------------------------------------------------------------------------
  function doConnect() {
    var v = $('pin').value.replace(/[^0-9]/g, '');
    if (v.length !== 4) { txt('pin-msg', 'El PIN tiene cuatro cifras.'); return; }
    pin = v;
    $('btn-pin').disabled = true;
    req('GET', '/api/state').then(function (r) {
      $('btn-pin').disabled = false;
      var bad = refusal(r);
      if (bad) { pin = ''; linked = false; txt('pin-msg', bad); return; }
      linked = true;
      txt('pin-msg', '');
      var d = r.j;
      $('devbox').hidden = false;
      txt('d-name', d.name || '\u2014');
      txt('d-fw', d.fw + '  (API ' + d.v + ')');
      txt('d-box', d.box.free + ' de ' + (d.box.free + d.box.used));
      txt('d-cs', d.cs.free + ' de ' + (d.cs.free + d.cs.used));
      txt('d-clock', ['sin hora', 'estimada', 'puesta a mano', 'del tel\u00e9fono'][d.cal] || '?');
      txt('hdr-dev', d.name || '');
      if (d.ro) toast(MSG.ro, 'bad');
      if (d.v !== API_EXPECTED)
        toast('Esta p\u00e1gina es de otra versi\u00f3n del dispositivo (' +
              API_EXPECTED + ' contra ' + d.v + '). Recarga.', 'bad');
      if (d.box.free === 0) toast(MSG.boxfull, 'bad');
      else if (d.cs.free === 0) toast(MSG.csfull, 'bad');
      startPing();
      refresh();
    }).catch(function () {
      $('btn-pin').disabled = false;
      txt('pin-msg', 'No se pudo hablar con el dispositivo.');
    });
  }

  // The keep-alive of spec section 34: the access point stays up while the
  // editor is being used and shuts itself down after the idle grace period.
  // Only a request that PASSES the PIN gate moves the device's idle clock, so
  // this is the one route that keeps the portal open, and it is sent on a timer
  // rather than on activity because a user drawing for four minutes without
  // touching the network is exactly the case the timeout would otherwise cut.
  function startPing() {
    if (pingTimer) clearInterval(pingTimer);
    pingTimer = setInterval(function () {
      req('POST', '/api/ping').then(function (r) {
        if (r.status === 403) { linked = false; toast(say(r.j && r.j.err), 'bad'); }
      }).catch(function () {});
    }, 60000);
  }

  function doClock() {
    var body = '{"v":' + API_EXPECTED + ',"epoch":' +
               Math.floor(Date.now() / 1000) + '}';
    req('POST', '/api/time', body).then(function (r) {
      var bad = refusal(r);
      if (bad) { toast(bad, 'bad'); return; }
      txt('d-clock', ['sin hora', 'estimada', 'puesta a mano', 'del tel\u00e9fono'][r.j.cal] || '?');
      toast(r.j.ok ? 'Reloj sincronizado.' : 'El dispositivo no acept\u00f3 la hora.',
            r.j.ok ? 'ok' : 'bad');
    }).catch(function () { toast('No se pudo hablar con el dispositivo.', 'bad'); });
  }

  // ---------------------------------------------------------------------------
  //  2. INFO
  // ---------------------------------------------------------------------------
  function onName() {
    M.name = $('name').value;
    var cps = Array.from(M.name);
    txt('name-count', cps.length + ' / ' + S.name.max);
    var p = nameProblem();
    txt('name-msg', p || 'Correcto.');
    $('name-msg').className = 'hint ' + (p ? 'bad' : 'ok');
    refresh();
  }

  // ---------------------------------------------------------------------------
  //  3. TYPE
  // ---------------------------------------------------------------------------
  function renderTypes() {
    var box = $('typebox'), i;
    box.innerHTML = '';
    for (i = 0; i < S.types; i++) {
      var b = document.createElement('button');
      b.type = 'button';
      b.style.width = '100%';
      b.style.marginBottom = '8px';
      b.textContent = S.tn[i];
      b.className = (i === M.type) ? 'sel' : '';
      b.setAttribute('data-t', String(i));
      b.addEventListener('click', function () {
        var t = parseInt(this.getAttribute('data-t'), 10);
        if (t === M.type) return;
        M.type = t;
        // Changing the type invalidates the learnset: the device's rule is
        // own-type-or-NEUTRAL, so keeping the picks would mean carrying a set
        // the device refuses by name.
        if (M.moves.length) { M.moves = []; toast('Los ataques se han vaciado.'); }
        renderTypes();
        refresh();
      });
      box.appendChild(b);
    }
  }

  // ---------------------------------------------------------------------------
  //  4. BODY. Spec section 33 calls this screen BODY / COSMETICS. A
  //  CustomSpeciesRec has no cosmetic field other than the sprite - the
  //  cosmetic half of the spec's screen IS the next one - so what lives here is
  //  the build: the four base stats, inside the band the device serves.
  // ---------------------------------------------------------------------------
  function renderStats() {
    var box = $('statbox'), i;
    box.innerHTML = '';
    for (i = 0; i < M.base.length; i++) {
      (function (k) {
        var wrap = document.createElement('div');
        wrap.className = 'stat';
        var lab = document.createElement('div');
        lab.className = 'lab';
        var l = document.createElement('span'); l.textContent = STAT_LABEL[k];
        var v = document.createElement('span'); v.className = 'val'; v.id = 'sv' + k;
        v.textContent = String(M.base[k]);
        lab.appendChild(l); lab.appendChild(v);
        var r = document.createElement('input');
        r.type = 'range'; r.min = String(S.stat.lo); r.max = String(S.stat.hi);
        r.step = '1'; r.value = String(M.base[k]);
        r.setAttribute('aria-label', STAT_LABEL[k]);
        r.addEventListener('input', function () {
          M.base[k] = parseInt(r.value, 10);
          txt('sv' + k, String(M.base[k]));
          updateStatMsg();
          refresh();
        });
        wrap.appendChild(lab); wrap.appendChild(r);
        box.appendChild(wrap);
      })(i);
    }
    txt('stat-band', S.stat.min + ' .. ' + S.stat.max);
    updateStatMsg();
  }

  function updateStatMsg() {
    var s = statUsed();
    txt('stat-total', String(s));
    var m = $('stat-msg');
    if (s < S.stat.min) {
      m.textContent = 'Faltan ' + (S.stat.min - s) + ' puntos.'; m.className = 'hint bad';
    } else if (s > S.stat.max) {
      m.textContent = 'Sobran ' + (s - S.stat.max) + ' puntos.'; m.className = 'hint bad';
    } else {
      m.textContent = 'Dentro de la banda.'; m.className = 'hint ok';
    }
  }

  // ---------------------------------------------------------------------------
  //  6. ATTACKS
  // ---------------------------------------------------------------------------
  function renderAttacks() {
    var list = $('atklist'), i;
    list.innerHTML = '';
    var au = atkUsed();
    for (i = 0; i < S.atk.length; i++) {
      (function (a, name) {
        // Own type or NEUTRAL. S.types is the neutral index: the type ids are
        // 0..types-1 for a species and `types` is NEUTRAL, which is exactly why
        // the device's validator reads `>= TYPE_COUNT` for a species and
        // `> TYPE_NEUTRAL` for an attack.
        if (a[1] !== M.type && a[1] !== S.types) return;
        var picked = M.moves.indexOf(a[0]) >= 0;
        var b = document.createElement('button');
        b.type = 'button';
        b.className = 'atk' + (picked ? ' sel' : '');
        var over = (!picked && a[2] > S.powcap) ||
                   (!picked && au + a[4] > S.atkbudget);
        b.disabled = over || (!picked && M.moves.length >= S.moves);
        var n = document.createElement('span');
        n.className = 'an'; n.textContent = name;
        var m = document.createElement('span');
        m.className = 'am';
        m.textContent = (a[2] ? 'POT ' + a[2] : 'APOYO') + '  ' + a[3] + '%  ' +
                        'C' + a[4];
        b.appendChild(n); b.appendChild(m);
        b.addEventListener('click', function () {
          var at = M.moves.indexOf(a[0]);
          if (at >= 0) M.moves.splice(at, 1);
          else if (M.moves.length < S.moves) M.moves.push(a[0]);
          renderAttacks();
          refresh();
        });
        list.appendChild(b);
      })(S.atk[i], S.an[i]);
    }
    txt('atk-count', M.moves.length + ' / ' + S.moves);
    txt('atk-cost', 'coste ' + au + ' / ' + S.atkbudget);
    var dmg = false, k;
    for (k = 0; k < M.moves.length; k++) { var a2 = atkOf(M.moves[k]); if (a2 && a2[2] > 0) dmg = true; }
    var msg = $('atk-msg');
    if (M.moves.length < S.moves) {
      msg.textContent = 'Elige ' + (S.moves - M.moves.length) + ' m\u00e1s.';
      msg.className = 'hint';
    } else if (!dmg) {
      msg.textContent = 'Al menos uno tiene que hacer da\u00f1o.'; msg.className = 'hint bad';
    } else {
      msg.textContent = 'Conjunto v\u00e1lido para esta p\u00e1gina. Lo decide el dispositivo.';
      msg.className = 'hint ok';
    }
  }

  // ---------------------------------------------------------------------------
  //  7. VALIDATE
  // ---------------------------------------------------------------------------
  function renderLocal() {
    var p = localProblems();
    txt('local-sum', p.length ? (p.length + ' aviso(s)') : 'sin avisos');
    $('local-sum').className = p.length ? 'bad' : 'ok';
    var l = $('local-list'), i;
    l.innerHTML = '';
    for (i = 0; i < p.length; i++) {
      var d = document.createElement('div');
      d.textContent = p[i][1] + '  [' + p[i][0] + ']';
      l.appendChild(d);
    }
  }

  //  THE UPLOAD DOCUMENT. Built by hand rather than with JSON.stringify so the
  //  shape is visible next to the reader that consumes it
  //  (networking/creator_parse.h documents exactly these six keys). The device
  //  reader accepts NO escapes, so a name carrying '"' or '\' would be
  //  CP_SYNTAX; nameCharOk() above refuses both before it can happen, and this
  //  throws rather than sending one if that guard is ever removed.
  function body() {
    var n = M.name, i;
    for (i = 0; i < n.length; i++) {
      var c = n.charCodeAt(i);
      if (c === 0x22 || c === 0x5c || c < 0x20)
        throw new Error('the name carries a character the device reader cannot take');
    }
    var hexes = SE.allHex(), sp = [];
    for (i = 0; i < hexes.length; i++) sp.push('"' + hexes[i] + '"');
    return '{"v":' + API_EXPECTED +
           ',"name":"' + n + '"' +
           ',"type":' + M.type +
           ',"base":[' + M.base.join(',') + ']' +
           ',"moves":[' + M.moves.join(',') + ']' +
           ',"sprite":[' + sp.join(',') + ']}';
  }

  function doValidate() {
    var v = $('verdict');
    var b;
    try { b = body(); } catch (e) { v.innerHTML = ''; v.textContent = String(e.message); return; }
    $('btn-validate').disabled = true;
    req('POST', '/api/validate', b).then(function (r) {
      $('btn-validate').disabled = false;
      v.innerHTML = '';
      var bad = refusal(r);
      var head = document.createElement('div');
      var code = document.createElement('div');
      code.className = 'code';
      if (bad) {
        head.className = 'bad';
        head.textContent = bad;
        code.textContent = 'HTTP ' + r.status + '  ' + codeOf(r);
      } else {
        head.className = 'ok';
        head.textContent = 'El dispositivo lo acepta. Presupuesto ' + r.j.pct + ' %.';
        code.textContent = 'estad\u00edsticas ' + r.j.stat + '  ataques ' + r.j.atk +
                           '  huecos caja ' + r.j.box + '  huecos especie ' + r.j.cs;
        // The device is the authority on the number too: if the bar disagrees
        // with the device's own percentage, the bar is wrong, and saying so is
        // cheaper than a user finding out at 101 %.
        var mine = pct(statUsed(), atkUsed());
        if (mine !== r.j.pct)
          toast('La barra dec\u00eda ' + mine + ' % y el dispositivo dice ' +
                r.j.pct + ' %. Manda el dispositivo.', 'bad');
      }
      v.appendChild(head); v.appendChild(code);
    }).catch(function () {
      $('btn-validate').disabled = false;
      v.textContent = 'No se pudo hablar con el dispositivo.';
    });
  }

  // ---------------------------------------------------------------------------
  //  8. PREVIEW
  // ---------------------------------------------------------------------------
  function startPreview() {
    var box = $('sumbox'), f = 0;
    box.innerHTML = '';
    function kv(k, val) {
      var d = document.createElement('div'); d.className = 'kv';
      var a = document.createElement('span'); a.textContent = k;
      var b = document.createElement('span'); b.textContent = val;
      d.appendChild(a); d.appendChild(b); box.appendChild(d);
    }
    kv('Nombre', M.name);
    kv('Tipo', S.tn[M.type]);
    var i, names = [];
    for (i = 0; i < M.base.length; i++) names.push(STAT_LABEL[i] + ' ' + M.base[i]);
    kv('Estad\u00edsticas', names.join('  '));
    for (i = 0; i < M.moves.length; i++) {
      var idx = -1, k;
      for (k = 0; k < S.atk.length; k++) if (S.atk[k][0] === M.moves[i]) idx = k;
      kv('Ataque ' + (i + 1), idx >= 0 ? S.an[idx] : String(M.moves[i]));
    }
    kv('Presupuesto', pct(statUsed(), atkUsed()) + ' %');
    previewTimer = setInterval(function () {
      f = (f + 1) % SE.frames();
      SE.blit($('q1'), f, 1);
      SE.blit($('q2'), f, 2);
    }, 480);
    SE.blit($('q1'), 0, 1);
    SE.blit($('q2'), 0, 2);
  }

  // ---------------------------------------------------------------------------
  //  9. UPLOAD
  // ---------------------------------------------------------------------------
  function doUpload() {
    var m = $('upload-msg'), b;
    try { b = body(); } catch (e) { m.textContent = String(e.message); return; }
    $('btn-upload').disabled = true;
    req('POST', '/api/bug', b).then(function (r) {
      var bad = refusal(r);
      if (bad) {
        $('btn-upload').disabled = false;
        m.className = 'bad';
        m.textContent = bad + ' [HTTP ' + r.status + ' ' + codeOf(r) + ']';
        return;
      }
      m.className = 'ok';
      m.textContent = 'Creado. El dispositivo no escribe nada hasta que las dos ' +
                      'mitades son buenas, as\u00ed que esto ya est\u00e1 guardado.';
      $('donebox').hidden = false;
      txt('u-slot', String(r.j.slot));
      txt('u-cs', String(r.j.cs));
      txt('u-species', String(r.j.species));
      txt('u-id', String(r.j.id));
      txt('u-pct', r.j.pct + ' %');
    }).catch(function () {
      $('btn-upload').disabled = false;
      m.textContent = 'No se pudo hablar con el dispositivo.';
    });
  }

  // ---------------------------------------------------------------------------
  //  BOOT
  // ---------------------------------------------------------------------------
  function buildSteps() {
    var ol = $('steps'), i;
    for (i = 0; i < STEPS.length; i++) ol.appendChild(document.createElement('li'));
  }

  function wire() {
    on('btn-pin', doConnect);
    on('btn-clock', doClock);
    on('btn-back', function () { show(step - 1); });
    on('btn-next', function () { if (stepReady(step)) show(step + 1); });
    on('btn-validate', doValidate);
    on('btn-upload', doUpload);
    $('pin').addEventListener('keydown', function (e) { if (e.key === 'Enter') doConnect(); });
    $('name').addEventListener('input', onName);
    $('name').setAttribute('maxlength', String(S.name.max));

    on('f0', function () { SE.setFrame(0); frameTabs(); });
    on('f1', function () { SE.setFrame(1); frameTabs(); });
    on('fcopy', function () { SE.copyFrame(0, 1); toast('Fotograma 1 copiado al 2.'); });
    on('t-draw',  function () { SE.setTool('draw');  toolTabs(); });
    on('t-erase', function () { SE.setTool('erase'); toolTabs(); });
    on('t-fill',  function () { SE.setTool('fill');  toolTabs(); });
    on('t-undo',  function () { if (!SE.undo()) toast('Nada que deshacer.'); });
    on('t-clear', function () { SE.clear(); });
    on('t-fliph', function () { SE.flipH(); });
    on('t-flipv', function () { SE.flipV(); });
  }

  function frameTabs() {
    $('f0').className = 'sq' + (SE.frame() === 0 ? ' sel' : '');
    $('f1').className = 'sq' + (SE.frame() === 1 ? ' sel' : '');
    afterDraw();
  }

  function toolTabs() {
    var t = SE.tool();
    $('t-draw').className  = (t === 'draw')  ? 'sel' : '';
    $('t-erase').className = (t === 'erase') ? 'sel' : '';
    $('t-fill').className  = (t === 'fill')  ? 'sel' : '';
  }

  function afterDraw() {
    SE.blit($('p1'), SE.frame(), 1);
    SE.blit($('p2'), SE.frame(), 2);
    txt('pad-on', SE.lit(SE.frame()) + ' px');
    refresh();
  }

  function boot() {
    // /api/schema IS THE ONE UNGATED ROUTE and the page needs it before the
    // user has typed a PIN: it serves compiled constants that are identical on
    // every device running this firmware. It also deliberately does NOT extend
    // the portal, so fetching it cannot hold the access point up.
    req('GET', '/api/schema').then(function (r) {
      if (r.status !== 200 || !r.j) {
        txt('pin-msg', 'El dispositivo no sirvi\u00f3 el esquema.');
        return;
      }
      S = r.j;
      if (S.v !== API_EXPECTED)
        toast('Esta p\u00e1gina es de otra versi\u00f3n del dispositivo. Recarga.', 'bad');
      var i;
      M.base = [];
      for (i = 0; i < S.sprite.f * 0 + 4; i++) M.base.push(S.stat.lo);
      // Start inside the band rather than at the floor of every field, so the
      // first thing the user sees is a legal Bug and not four warnings.
      var spread = Math.floor(S.stat.min / M.base.length);
      for (i = 0; i < M.base.length; i++) M.base[i] = spread;
      i = 0;
      while (statUsed() < S.stat.min) { M.base[i % M.base.length]++; i++; }

      SE.geom(S.sprite.w, S.sprite.h, S.sprite.f);
      SE.mount($('pad'), {
        onChange: afterDraw,
        onHover: function (x, y) {
          txt('pad-xy', x < 0 ? '\u2014' : (x + ' , ' + y));
        }
      });
      buildSteps();
      wire();
      renderTypes();
      renderStats();
      frameTabs();
      toolTabs();
      show(0);
      $('pin').focus();
    }).catch(function () {
      txt('pin-msg', 'No se pudo hablar con el dispositivo.');
    });
  }

  if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', boot);
  else boot();

  // The harness in tools/page_test.mjs drives the page through this seam
  // instead of through synthetic clicks it would have to keep in step with the
  // markup. IT IS NOT A BACK DOOR: everything here is already reachable from
  // the UI, it holds no secret, and it decides nothing the device does not
  // re-decide. It exists so the browser test drives THE SHIPPING PAGE rather
  // than a copy of its logic.
  window.PB = {
    model: M,
    schema: function () { return S; },
    pct: pct,
    body: body,
    show: show,
    setPin: function (p) { pin = p; },
    problems: localProblems,
    statUsed: statUsed,
    atkUsed: atkUsed
  };
})();
