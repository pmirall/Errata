// =============================================================================
//  tools/page_test.mjs - THE CREATOR PAGE, DRIVEN IN A REAL BROWSER (P8-C4).
//
//  ==========================================================================
//  WHAT THIS PROVES AND WHAT IT DOES NOT
//  ==========================================================================
//  IT DRIVES THE SHIPPED BYTES. The page under test is extracted from
//  Pebblebol/src/data/index_html.h - the blob GET / serves, raw-string
//  delimiters and all - not from web/creator/*.js. A harness that loaded the
//  sources would pass while the generator mangled them.
//
//  IT ANSWERS WITH THE SHIPPED SCHEMA. GET /api/schema replies with the exact
//  document in Pebblebol/src/data/creator_schema_json.h, so every number the
//  page computes from is the number the device would have sent.
//
//  IT IS JUDGED BY THE SHIPPED DECODER. The body the page POSTs is piped into
//  tests/bin/creator_decode, which links THE REAL networking/creator_parse.cpp
//  and THE REAL game/validate.cpp. That is the only place the two ends of the
//  XBM sentence can be compared: the page writes the bits, the device reads
//  them, and nobody else in this tree can see both.
//
//  IT IS NOT A PHONE. A headless Chromium at a desktop viewport is not a thumb
//  on a 6-inch screen. Spec section 67's "Mobile editor works" and "Sprite
//  editor works" are BENCH items and this run does not tick either of them,
//  and must not be quoted as if it did. What it can see: that the page loads,
//  that the schema drives it, that drawing works through pointer events, that
//  the exported bytes are the ones the device decodes, and that the budget bar
//  and the device agree on the percentage.
//
//  ==========================================================================
//  THE FAKE DEVICE IS A FAKE, AND HERE IS EXACTLY HOW IT DIFFERS
//  ==========================================================================
//  It is ~60 lines of node http. It serves the real page and the real schema,
//  accepts one PIN, and answers the shapes networking/creator_server.cpp
//  answers. IT HAS NO PIN LOCKOUT, NO RATE LIMITER, NO BODY CAP, NO IDLE
//  TIMER AND NO FLASH - every one of those is driven by a host binary
//  (tests/test_creator_gate.cpp, tests/test_creator_api.cpp) or by nothing at
//  all yet, and this file must not be read as covering them.
//
//  Usage:  node tools/page_test.mjs            (expects `make -C tests pagetool`)
//  Exit 0 on pass, 1 on any failed assertion, 2 when it cannot run at all.
// =============================================================================
import { readFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.dirname(HERE);
const DEC = path.join(ROOT, 'tests', 'bin', 'creator_decode');

let fails = 0, checks = 0;
function check(ok, what) {
  checks++;
  if (!ok) { fails++; console.log('  FAIL  ' + what); }
  else console.log('  ok    ' + what);
}
function eq(a, b, what) { check(a === b, what + '  (' + JSON.stringify(a) + ' vs ' + JSON.stringify(b) + ')'); }

// ---- the shipped bytes ------------------------------------------------------
function between(text, open, close, what) {
  const a = text.indexOf(open);
  const b = text.lastIndexOf(close);
  if (a < 0 || b < 0 || b <= a) { console.error('cannot find ' + what); process.exit(2); }
  return text.slice(a + open.length, b);
}

const PAGE = between(readFileSync(path.join(ROOT, 'Pebblebol/src/data/index_html.h'), 'utf8'),
                     'R"PBHTML(', ')PBHTML";', 'the page blob in index_html.h');
const SCHEMA = between(readFileSync(path.join(ROOT, 'Pebblebol/src/data/creator_schema_json.h'), 'utf8'),
                       'R"JSON(', ')JSON";', 'the schema blob in creator_schema_json.h');

// ---- the fake device --------------------------------------------------------
const PIN = '4242';
let lastBody = null, lastPath = null;

function readBody(req) {
  return new Promise(res => {
    let b = '';
    req.on('data', c => { b += c; });
    req.on('end', () => res(b));
  });
}

const srv = createServer(async (req, res) => {
  const url = req.url.split('?')[0];
  const j = (code, obj) => {
    res.writeHead(code, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
    res.end(typeof obj === 'string' ? obj : JSON.stringify(obj));
  };
  if (url === '/' && req.method === 'GET') {
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-cache' });
    res.end(PAGE);
    return;
  }
  if (url === '/api/schema' && req.method === 'GET') return j(200, SCHEMA);
  const gate = req.headers['x-pin'];
  if (gate !== PIN) return j(403, { err: 'pin' });
  if (url === '/api/state') {
    return j(200, {
      v: 1, fw: 'test', content: 693, name: 'BANCO',
      box: { used: 1, free: 9 }, cs: { used: 0, free: 10 },
      body: 2048, cal: 1, epoch: 1767225600, ro: 0
    });
  }
  const body = await readBody(req);
  lastBody = body; lastPath = url;
  if (url === '/api/ping') return j(200, { ok: 1 });
  if (url === '/api/time') return j(200, { ok: 1, cal: 3, epoch: 1767225600 });
  if (url === '/api/validate' || url === '/api/pebble') {
    const d = decode(body);
    if (d.cp !== 'CP_OK') return j(400, { err: 'parse', why: d.cp });
    if (d.vr !== 'VR_OK') return j(422, { err: 'invalid', why: d.vr });
    if (url === '/api/validate')
      return j(200, { ok: 1, pct: +d.pct, stat: +d.stat, atk: +d.atk, box: 9, cs: 10 });
    return j(200, { ok: 1, slot: 2, cs: 0, species: 200, id: 7, pct: +d.pct });
  }
  return j(404, { err: 'arg' });
});

// ---- the shipped decoder ----------------------------------------------------
function decode(body) {
  const r = spawnSync(DEC, { input: body, encoding: 'utf8' });
  if (r.error || r.status !== 0) {
    console.error('creator_decode failed: run `make -C tests pagetool`');
    process.exit(2);
  }
  const out = {};
  for (const line of r.stdout.split('\n')) {
    const i = line.indexOf('=');
    if (i > 0) out[line.slice(0, i)] = line.slice(i + 1);
  }
  return out;
}

// The device's own answer for a whole list of (stat, attack) pairs.
function devicePct(pairs) {
  const r = spawnSync(DEC, ['--pct'],
                      { input: pairs.map(p => p[0] + ' ' + p[1]).join('\n') + '\n',
                        encoding: 'utf8', maxBuffer: 1 << 24 });
  if (r.error || r.status !== 0) { console.error('creator_decode --pct failed'); process.exit(2); }
  return r.stdout.trim().split('\n').map(Number);
}

// =============================================================================
//  THE RUN
// =============================================================================
const require = createRequire(import.meta.url);
let chromium;
try {
  ({ chromium } = require('/opt/node22/lib/node_modules/playwright'));
} catch (e) {
  try { ({ chromium } = require('playwright')); }
  catch (e2) { console.error('playwright not available'); process.exit(2); }
}

// The cells the harness paints, chosen so the assertion below cannot pass by
// accident: they straddle all three bytes of a row, they include column 0 and
// column 23 (the two ends a bit-order mistake moves), and they are asymmetric
// left-to-right so a horizontal mirror is a different picture.
const CELLS = [[0, 0], [1, 0], [7, 0], [8, 0], [9, 0], [15, 0], [16, 0], [23, 0],
               [3, 5], [4, 5], [5, 5],
               [0, 23], [23, 23], [11, 12]];

// The same cells, encoded the way networking/creator_parse.h says the record
// holds them: 3 bytes per row, LSB of each byte is the LEFTMOST pixel. Written
// out HERE, independently of sprite_editor.js, so the test compares two
// derivations and not one function with itself.
function expectHex(cells) {
  const b = new Uint8Array(72);
  for (const [x, y] of cells) b[y * 3 + (x >> 3)] |= (1 << (x & 7));
  return Array.from(b).map(v => v.toString(16).padStart(2, '0')).join('');
}

const run = async () => {
  const page = await chromium.launch().then(b => b.newPage().then(p => ({ b, p })));
  const { b: browser, p } = page;
  // UNCAUGHT EXCEPTIONS AND THE PAGE'S OWN console.error, AND NOT THE
  // BROWSER'S NETWORK NOTES. Chromium logs every non-2xx fetch as a console
  // error, and two of this run's assertions are DELIBERATE refusals (a wrong
  // PIN, an over-budget move set) - counting those would make the check fail
  // precisely when the page is behaving correctly, which is the shape of a
  // test that has to be ignored and then is.
  const errors = [];
  p.on('pageerror', e => errors.push('exception: ' + String(e)));
  p.on('console', m => {
    if (m.type() !== 'error') return;
    if (/Failed to load resource/.test(m.text())) return;
    errors.push('console: ' + m.text());
  });

  await p.setViewportSize({ width: 390, height: 844 });   // a phone-shaped viewport
  await p.goto('http://127.0.0.1:8731/', { waitUntil: 'networkidle' });

  console.log('1. the page loads and the schema drives it');
  await p.waitForFunction('window.PB && window.PB.schema()');
  const S = await p.evaluate('window.PB.schema()');
  eq(S.sprite.w, 24, 'the schema serves a 24-wide grid');
  eq(S.sprite.f, 2, 'and two frames');
  // THE EDITOR'S OWN GEOMETRY, not the schema's copy of it. A page that read
  // the document and then drew on a hard-coded grid would pass every line
  // above; this is the one that says the editor is driven BY the document.
  eq(await p.evaluate('SE.w()'), S.sprite.w, 'the editor took its width from the document');
  eq(await p.evaluate('SE.h()'), S.sprite.h, 'and its height');
  eq(await p.evaluate('SE.frames()'), S.sprite.f, 'and its frame count');
  eq(S.atk.length, 34, 'every attack row reached the page');
  eq(S.an.length, S.atk.length, 'one name per attack row');
  eq(S.tn.length, 4, 'four type names, NEUTRAL last');
  check(errors.length === 0, 'no page error while loading (' + errors.join(' | ') + ')');

  console.log('2. the PIN gate');
  await p.fill('#pin', '1111');
  await p.click('#btn-pin');
  await p.waitForFunction('document.getElementById("pin-msg").textContent.length > 0');
  const wrong = await p.textContent('#pin-msg');
  check(/PIN incorrecto/.test(wrong), 'a wrong PIN is refused with the device\'s own reason');
  await p.fill('#pin', PIN);
  await p.click('#btn-pin');
  await p.waitForSelector('#devbox:not([hidden])');
  eq(await p.textContent('#d-name'), 'BANCO', 'the device name came from GET /api/state');

  console.log('3. name, type, stats');
  await p.evaluate('window.PB.show(1)');
  await p.fill('#name', 'Bícho');                 // an accented name, UTF-8 on the wire
  await p.waitForFunction('document.getElementById("name-msg").textContent.length > 0');
  eq(await p.textContent('#name-msg'), 'Correcto.', 'an accented name is accepted locally');
  await p.fill('#name', 'B"cho');
  check(/^El car.cter/.test(await p.textContent('#name-msg')),
     'a quote is refused by the page because the device reader has no escapes');
  await p.fill('#name', 'Bícho');
  await p.evaluate('window.PB.show(2)');
  await p.click('#typebox button:nth-child(1)');       // SIGNAL
  await p.evaluate('window.PB.show(3)');
  await p.evaluate('var m=window.PB.model; m.base[0]=6;m.base[1]=6;m.base[2]=5;m.base[3]=5;');
  await p.evaluate('window.PB.show(3)');
  eq(await p.evaluate('window.PB.statUsed()'), 22, 'the four stats total the band ceiling');

  console.log('4. the sprite editor, driven through pointer events');
  await p.evaluate('window.PB.show(4)');
  // THE BOX IS RE-READ BEFORE EVERY TAP. Clicking a tool button below the
  // canvas scrolls it into view, which moves the canvas - and a cached
  // bounding box then puts the tap somewhere else entirely. The first version
  // of this harness cached it once and silently painted nothing for three
  // sections, which is exactly the kind of quiet no-op a test must not have.
  const tap = async (x, y) => {
    const b = await p.locator('#pad').boundingBox();
    await p.mouse.move(b.x + (x + 0.5) * (b.width / 24),
                       b.y + (y + 0.5) * (b.height / 24));
    await p.mouse.down();
    await p.mouse.up();
  };
  for (const [x, y] of CELLS) await tap(x, y);
  const lit0 = await p.evaluate('SE.lit(0)');
  eq(lit0, CELLS.length, 'every tap lit exactly one cell');
  await p.click('#fcopy');
  eq(await p.evaluate('SE.lit(1)'), CELLS.length, 'COPIAR 1 -> 2 copied the frame');

  console.log('5. undo, clear and flip');
  // Frame 2 is made DIFFERENT so the two frames cannot be confused downstream,
  // and the extra cell is painted with a real tap so DESHACER is measured
  // against a real stroke rather than against a test seam.
  await p.click('#f1');
  await tap(12, 12);
  eq(await p.evaluate('SE.lit(1)'), CELLS.length + 1, 'a tap on frame 2 lit one more cell');
  await p.click('#t-undo');
  eq(await p.evaluate('SE.lit(1)'), CELLS.length,
     'DESHACER took back exactly the last stroke and nothing else');
  await tap(12, 12);
  await p.click('#f0');
  const hBefore = await p.evaluate('SE.hex(0)');
  eq(hBefore, expectHex(CELLS), 'the frame is the pattern the taps painted');

  // THE FLIP IS ASSERTED AGAINST THE MIRRORED PATTERN, NOT AGAINST "IT
  // CHANGED". The first version of this check said hFlip !== hBefore and
  // hFlip flipped twice === hBefore, and a mutation that mirrored only the
  // first quarter of each row passed BOTH: a partial swap still changes the
  // frame and is still its own inverse. Only naming the picture can see it.
  await p.click('#t-fliph');
  eq(await p.evaluate('SE.hex(0)'), expectHex(CELLS.map(([x, y]) => [23 - x, y])),
     'ESPEJO H is the row reversed, every column');
  await p.click('#t-fliph');
  eq(await p.evaluate('SE.hex(0)'), hBefore, 'ESPEJO H twice is the identity');
  await p.click('#t-flipv');
  eq(await p.evaluate('SE.hex(0)'), expectHex(CELLS.map(([x, y]) => [x, 23 - y])),
     'ESPEJO V is the column reversed, every row');
  await p.click('#t-flipv');
  eq(await p.evaluate('SE.hex(0)'), hBefore, 'ESPEJO V twice is the identity');

  const otherBefore = await p.evaluate('SE.hex(1)');
  await p.click('#t-clear');
  eq(await p.evaluate('SE.lit(0)'), 0, 'LIMPIAR emptied the frame');
  eq(await p.evaluate('SE.hex(1)'), otherBefore,
     'and LIMPIAR left the OTHER frame alone');
  await p.click('#t-undo');
  eq(await p.evaluate('SE.hex(0)'), hBefore, 'DESHACER took LIMPIAR back');

  console.log('6. the fill tool');
  await p.click('#t-fill');
  await tap(12, 20);
  check(await p.evaluate('SE.lit(0)') > 400, 'RELLENO flooded the background');
  await p.click('#t-draw');
  await p.click('#t-undo');
  eq(await p.evaluate('SE.hex(0)'), hBefore, 'DESHACER took the fill back');

  console.log('7. attacks and the section 36 budget bar');
  await p.evaluate('window.PB.show(5)');
  // Picked BY CLICKING the rows the page offers, cheapest first, so the pool
  // filter, the disabled state and the running cost are all exercised by the
  // path a user takes.
  // The cheapest DAMAGING move plus the three cheapest of anything: the set a
  // user reaching for a legal Pebble builds, and the one the device accepts.
  // The first version of this harness took the four cheapest outright and the
  // device answered VR_CS_NO_DAMAGING_MOVE - which is a real rule, so it moved
  // to section 10 as a hostile case instead of being papered over here.
  const picks = await p.evaluate(`(function(){
    var S = window.PB.schema(), M = window.PB.model, pool = [], i;
    for (i = 0; i < S.atk.length; i++)
      if (S.atk[i][1] === M.type || S.atk[i][1] === S.types) pool.push(S.atk[i]);
    pool.sort(function (a, b) { return a[4] - b[4]; });
    var got = [], k;
    for (k = 0; k < pool.length; k++) if (pool[k][2] > 0) { got.push(pool[k][0]); break; }
    for (k = 0; k < pool.length && got.length < S.moves; k++)
      if (got.indexOf(pool[k][0]) < 0) got.push(pool[k][0]);
    return got;
  })()`);
  for (const id of picks) {
    await p.locator('#atklist button', { hasText: '' }).nth(
      await p.evaluate(`(function(){
        var S = window.PB.schema(), M = window.PB.model, n = 0, i;
        for (i = 0; i < S.atk.length; i++) {
          var a = S.atk[i];
          if (a[1] !== M.type && a[1] !== S.types) continue;
          if (a[0] === ${id}) return n;
          n++;
        }
        return -1;
      })()`)).click();
  }
  eq((await p.evaluate('window.PB.model.moves')).length, 4,
     'four moves picked by tapping the rows the page offered');
  check(await p.evaluate('window.PB.atkUsed()') <= 185,
        'the picked set is inside the served attack budget');
  const pagePct = await p.evaluate('window.PB.pct(window.PB.statUsed(), window.PB.atkUsed())');

  // THE BAR AGAINST THE DEVICE OVER THE WHOLE INPUT DOMAIN, not at the one
  // input this Pebble happens to produce. Four stats of 1..10 is S in 4..40 and
  // four moves of the served costs is A in 0..400 with room to spare; 14,837
  // pairs, each answered by game/validate.cpp's creator_power_pct() in the
  // binary above and by window.PB.pct() in the browser.
  //
  // WHY THE WIDE SWEEP EXISTS: the single-input version of this check could not
  // fail for the reason it named. Replacing the page's integer form with
  // balance.json's float sentence left it green, because the two agree on every
  // pair - stated in the report as an equivalent mutant rather than hidden.
  // What the sweep CAN see is a wrong constant, a lost rounding term and a
  // missing saturation, which is what a copied budget actually looks like.
  const pairs = [];
  for (let sv = 4; sv <= 40; sv++) for (let av = 0; av <= 400; av++) pairs.push([sv, av]);
  const devAll = devicePct(pairs);
  const pageAll = await p.evaluate(`(function(){
    var out = [], sv, av;
    for (sv = 4; sv <= 40; sv++) for (av = 0; av <= 400; av++) out.push(window.PB.pct(sv, av));
    return out;
  })()`);
  let firstBad = -1;
  for (let i = 0; i < pairs.length; i++) if (pageAll[i] !== devAll[i]) { firstBad = i; break; }
  check(firstBad < 0,
        'the bar and game/validate.cpp agree on all ' + pairs.length + ' (S,A) pairs' +
        (firstBad < 0 ? '' : '  first split at S=' + pairs[firstBad][0] +
         ' A=' + pairs[firstBad][1] + ': page ' + pageAll[firstBad] +
         ' vs device ' + devAll[firstBad]));
  check(pageAll[pairs.length - 1] === 100, 'and both saturate at 100');

  console.log('8. the body the page POSTs, judged by the real device decoder');
  await p.evaluate('window.PB.show(6)');
  await p.click('#btn-validate');
  await p.waitForFunction('document.querySelector("#verdict .code")');
  const verdict = await p.textContent('#verdict');
  const body = lastBody;
  check(lastPath === '/api/validate', 'the page posted to /api/validate');
  const d = decode(body);
  eq(d.cp, 'CP_OK', 'the real reader accepts the page\'s document');
  eq(d.vr, 'VR_OK', 'the real validator accepts it');
  // COMPARED AS BYTES, NOT AS TEXT. The device's field is Latin-1, so 'í' is
  // the single byte 0xED - which is not valid UTF-8, so reading the decoder's
  // stdout as text turns it into a replacement character and the comparison
  // would fail on a name that arrived perfectly. The bytes are the claim.
  eq(d.name_bytes, '42ed63686f', 'the name is the Latin-1 bytes 42 ed 63 68 6f');
  eq(d.base, '6,6,5,5', 'the four stats arrived in order');
  eq(d.moves, picks.join(','), 'the four moves arrived in order');
  eq(d.compat, '0', 'a creator species carries compat group 0 and never breeds');
  eq(+d.pct, pagePct, 'THE BUDGET BAR AND THE DEVICE AGREE ON THE PERCENTAGE');
  check(verdict.indexOf(d.pct + ' %') >= 0, 'the page showed the device\'s own percentage');

  console.log('9. THE XBM ROUND TRIP - the page writes the bits, the device reads them');
  eq(d.sprite0, expectHex(CELLS),
     'frame 1 decodes to exactly the cells the harness painted, LSB-first');
  eq(d.sprite1, expectHex(CELLS.concat([[12, 12]])),
     'frame 2 decodes to the copied frame plus its one extra cell');
  check(d.sprite0 !== d.sprite1, 'the two frames are not the same bytes');

  console.log('10. THE PAGE IS NOT THE CONTROL - a set the page would never');
  console.log('    offer is refused by the device anyway');
  const legal = await p.evaluate('window.PB.model.moves.slice(0)');
  await p.evaluate(`(function(){
    var S = window.PB.schema(), M = window.PB.model, pool = [], i;
    for (i = 0; i < S.atk.length; i++)
      if (S.atk[i][1] === M.type || S.atk[i][1] === S.types) pool.push(S.atk[i]);
    pool.sort(function (a, b) { return b[4] - a[4]; });   // most expensive first
    M.moves = [pool[0][0], pool[1][0], pool[2][0], pool[3][0]];
  })()`);
  check(await p.evaluate('window.PB.atkUsed()') > 185,
        'the hostile set really is over the served budget');
  await p.evaluate('window.PB.show(6)');
  await p.click('#btn-validate');
  await p.waitForFunction('document.querySelector("#verdict .code")');
  const hostile = decode(lastBody);
  eq(hostile.vr, 'VR_CS_ATTACK_BUDGET',
     'the device refuses it BY NAME even though the page built the document');
  check(/presupuesto/i.test(await p.textContent('#verdict')),
        'and the page repeats the device\'s reason rather than its own');
  await p.evaluate('window.PB.model.moves = ' + JSON.stringify(legal));

  console.log('11. upload');
  await p.evaluate('window.PB.show(8)');
  await p.click('#btn-upload');
  await p.waitForSelector('#donebox:not([hidden])');
  eq(await p.textContent('#u-species'), '200', 'the device told the page which species id it used');
  eq(await p.textContent('#u-cs'), '0', 'and which cs slot');
  eq(lastPath, '/api/pebble', 'the upload went to /api/pebble');

  check(errors.length === 0, 'no page error in the whole run (' + errors.join(' | ') + ')');

  await browser.close();
};

srv.listen(8731, '127.0.0.1', async () => {
  try {
    await run();
  } catch (e) {
    console.log('  FAIL  harness threw: ' + (e && e.message));
    fails++;
  }
  srv.close();
  console.log(fails ? ('PAGE TEST FAIL ' + fails + '/' + checks)
                    : ('PAGE TEST OK ' + checks + '/' + checks));
  process.exit(fails ? 1 : 0);
});
