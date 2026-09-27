// Unit tests for app.js: hand parsing and validation, deal entry, the DD
// table, par text and the busy/ready state. Run with: node app-test.js
'use strict';

const assert = require('assert');
const { loadPage, test, runTests } = require('./test-env.js');

// Values from the page are from another realm; compare them as plain data.
const plain = (x) => JSON.parse(JSON.stringify(x));

const FULL = {
  west: 'KT8 KJ9875 Q8 75', north: '42 AQT63 K52 T84',
  east: 'AJ65 42 J963 KQJ', south: 'Q973 - AT74 A9632',
};
// deals/old/squeeze.4: spades, South to lead, N/S take all 6.
const ENDING = { west: '- Q76 Q95 -', north: '- AJ K86 7', east: '- KT JT2 K', south: '53 4 A7 J' };

const page = loadPage();
const validate = (hands) => page.run(`validateHands(${JSON.stringify(hands)})`);

test('parseRanks: splits ranks, reads 10 as T and upper-cases', () => {
  assert.deepStrictEqual(plain(page.run("parseRanks('KJ1042')")), ['K', 'J', 'T', '4', '2']);
  assert.deepStrictEqual(plain(page.run("parseRanks('akx')")), ['A', 'K', 'X']);
});

test('validateHands: accepts a full deal and an ending', () => {
  assert.strictEqual(validate(FULL), null);
  assert.strictEqual(validate(ENDING), null);
});

test('validateHands: accepts X wildcards', () => {
  assert.strictEqual(validate({ ...FULL, west: 'KT8 KJ9875 Q8 7X' }), null);
});

test('validateHands: rejects bad characters, ranks and duplicates', () => {
  assert.match(validate({ ...FULL, west: 'KT8 KJ9875 Q8 7!' }), /^West: only card ranks/);
  assert.match(validate({ ...FULL, west: 'KT8 KJ9875 Q8 71' }), /^West: invalid rank '1'/);
  assert.strictEqual(validate({ ...FULL, west: 'KT8 KJ9875 Q8 K5' }), 'Duplicate card: CK');
});

test('validateHands: rejects empty, oversized and uneven hands', () => {
  assert.strictEqual(validate({ ...ENDING, west: '- - - -' }), 'West: has 0 cards, expected 1 to 13.');
  assert.strictEqual(validate({ ...ENDING, west: 'AKQJT98765432 2 - -' }),
    'West: has 14 cards, expected 1 to 13.');
  assert.strictEqual(validate({ ...ENDING, west: '- Q76 Q9 -' }),
    'Hands differ in length: West 5, North 6, East 6, South 6.');
});

test('handLength: counts cards per hand', () => {
  assert.strictEqual(page.run(`handLength(${JSON.stringify(FULL)})`), 13);
  assert.strictEqual(page.run(`handLength(${JSON.stringify(ENDING)})`), 6);
});

test('getHandValue/setHandValue: round-trip through the suit inputs, with voids', () => {
  page.run("setHandValue('south', 'Q973 - AT74 A9632')");
  assert.strictEqual(page.byId('south-H').value, '');
  assert.strictEqual(page.run("getHandValue('south')"), 'Q973 - AT74 A9632');
});

test('randomDeal: deals 13 distinct valid cards to each seat', () => {
  for (let i = 0; i < 20; ++i) {
    const hands = plain(page.run('randomDeal()'));
    assert.strictEqual(validate(hands), null);
    const cards = Object.values(hands).flatMap(h => h.split(' ').flatMap(
      (suit, s) => (suit === '-' ? [] : [...suit].map(r => 'SHDC'[s] + r))));
    assert.strictEqual(new Set(cards).size, 52);
  }
});

test('Sample deal: one optgroup per collection, one option per bundled deal', () => {
  const groups = page.byId('sampleDeal').children;
  assert.deepStrictEqual(groups.map(g => g.label), ['Freak', 'Hard']);
  const deals = plain(page.run('DEALS'));
  assert.strictEqual(groups[1].children.length, Object.keys(deals.hard).length);
  assert.strictEqual(groups[0].children[0].value, 'freak:0');
});

test('Sample deal: every bundled deal parses into a valid deal', () => {
  const deals = plain(page.run('DEALS'));
  for (const dir of Object.keys(deals)) {
    for (const [num, text] of Object.entries(deals[dir])) {
      const hands = plain(page.run(`parseDealFile(${JSON.stringify(text)})`));
      assert.strictEqual(validate(hands), null, `${dir} ${num}`);
    }
  }
});

test('renderTable: declarer columns for a full deal, player-on-lead columns for an ending', () => {
  page.run("ddDeal = null; renderTable('N 7 6 6 6\\nS 6 6 7 7\\nH 6 6 7 7\\nD 7 7 6 6\\nC 7 7 6 6')");
  let html = page.byId('table').innerHTML;
  assert.match(html, /<th><\/th><th>South<\/th>/);
  assert.match(html, /data-strain="N" data-declarer="south" data-tricks="7">7</);

  // squeeze.4's spades: South 5, North 5, West 1, East 0 as declarer. With
  // South on lead East declares, so South's column shows 6 - 0 = 6.
  page.run(`ddDeal = formatPBN(${JSON.stringify(ENDING)});
    renderTable('N 5 5 1 0\\nS 5 5 1 0\\nH 3 3 2 2\\nD 4 4 2 2\\nC 5 5 1 1')`);
  html = page.byId('table').innerHTML;
  assert.match(html, /<th>Lead<\/th>/);
  assert.match(html, /data-strain="S" data-declarer="east" data-tricks="0">6</);
  assert.match(html, /data-strain="S" data-declarer="south" data-tricks="5">1</);
});

test('ddHandLength: 13 with no table, else the solved deal\'s length', () => {
  page.run('ddDeal = null');
  assert.strictEqual(page.run('ddHandLength()'), 13);
  page.run(`ddDeal = formatPBN(${JSON.stringify(ENDING)})`);
  assert.strictEqual(page.run('ddHandLength()'), 6);
});

test('parText: one side, both views, and a passed-out deal', () => {
  const one = page.run(`parText(computePar(['N 1 1 11 11', 'S 2 2 11 11', 'H 0 0 13 13',
    'D 5 5 8 8', 'C 0 0 12 12'], 'E-W'))`);
  assert.match(one, /^EW \+2210, 7<span class="red">&hearts;<\/span>= by EW$/);
  const both = page.run(`parText(computePar(['N 7 7 7 7', 'S 6 6 6 6', 'H 6 6 6 6',
    'D 6 6 6 6', 'C 6 6 6 6'], 'All'))`);
  assert.strictEqual(both, 'NS +90, 1NT= by NS if N/S bid first; EW +90, 1NT= by EW if E/W bid first');
  const zero = page.run(`parText(computePar(['N 5 5 3 3', 'S 5 5 5 5', 'H 4 4 6 6',
    'D 6 6 3 3', 'C 6 6 3 3'], 'N-S'))`);
  assert.strictEqual(zero, '0');
});

test('renderPar: DD Par only for a full deal, following the Vul select', () => {
  page.run(`ddDeal = formatPBN(${JSON.stringify(FULL)}); vulnerability = 'None';
    renderTable('N 7 7 6 6\\nS 6 6 6 7\\nH 7 7 6 6\\nD 7 7 6 6\\nC 8 8 5 5')`);
  assert.match(page.byId('ddPar').innerHTML, /^DD Par: NS \+90, 1NT= by NS, 2/);
  assert.strictEqual(page.byId('vul').value, 'None');
  page.run(`ddDeal = formatPBN(${JSON.stringify(ENDING)}); renderPar()`);
  assert.strictEqual(page.byId('ddPar').innerHTML, '');
});

test('Vul select: Up/Down cycles with wrap-around', () => {
  page.run("vulnerability = 'None'");
  const press = (key) => page.fire('vul', 'keydown', { key, preventDefault() {} });
  press('ArrowUp');
  assert.strictEqual(page.run('vulnerability'), 'All');
  press('ArrowDown');
  press('ArrowDown');
  assert.strictEqual(page.run('vulnerability'), 'N-S');
  assert.strictEqual(page.byId('vul').value, 'N-S');
});

test('updateSolveBtn: Solve only when idle, not playing and the solver is loaded', () => {
  const state = (busy, playing, ready) => {
    page.run(`busy = ${busy}; playState = ${playing ? '{}' : 'null'}; workerReady = ${ready};
      updateSolveBtn()`);
    return page.byId('solve').disabled;
  };
  assert.strictEqual(state(false, false, true), false);
  assert.strictEqual(state(true, false, true), true);
  assert.strictEqual(state(false, true, true), true);
  assert.strictEqual(state(false, false, false), true);
  page.run('busy = false; playState = null; workerReady = true');
});

test('setBusyState: locks deal entry while busy', () => {
  page.run('workerReady = true; setBusyState(true)');
  assert.strictEqual(page.byId('deal').disabled, true);
  assert.strictEqual(page.byId('north-S').disabled, true);
  assert.strictEqual(page.byId('pasteBox').disabled, true);
  page.run('setBusyState(false)');
  assert.strictEqual(page.byId('deal').disabled, false);
  assert.strictEqual(page.byId('solve').disabled, false);
});

test('Solve: validates first, then posts the hands and records the deal', () => {
  page.run(`for (const seat of SEATS) setHandValue(seat, ${JSON.stringify(FULL)}[seat])`);
  page.run("setHandValue('west', 'KT8 KJ9875 Q8 K5')");
  page.fire('solve', 'click');
  assert.strictEqual(page.byId('status').textContent, 'Duplicate card: CK');

  page.run(`setHandValue('west', ${JSON.stringify(FULL.west)})`);
  const messages = page.run('worker.messages');
  const before = messages.length;
  page.fire('solve', 'click');
  assert.strictEqual(messages.length, before + 1);
  assert.deepStrictEqual(plain(messages[messages.length - 1]), ['solve', FULL]);
  assert.strictEqual(page.run('busy'), true);
  assert.strictEqual(page.run('ddDeal'), page.run(`formatPBN(${JSON.stringify(FULL)})`));

  // The worker's answer shows the table and ends the busy state.
  page.run("onSolverMessage({ data: ['solve', 'N 7 6 6 6\\nS 6 6 7 7\\nH 6 6 7 7\\nD 7 7 6 6\\nC 7 7 6 6', 42] })");
  assert.strictEqual(page.byId('status').textContent, 'Solved in 42 ms.');
  assert.strictEqual(page.run('busy'), false);
  assert.match(page.byId('table').innerHTML, /data-declarer="south" data-tricks="7"/);
});

test('onSolverMessage: a crash disables Solve and drops a waiting link', () => {
  page.run("busy = true; deferredUrlHash = '#deal=x'; workerReady = true");
  page.run("onSolverMessage({ data: ['abort', 'boom'] })");
  assert.strictEqual(page.byId('status').textContent, 'Solver crashed: boom');
  assert.strictEqual(page.run('deferredUrlHash'), null);
  assert.strictEqual(page.byId('solve').disabled, true);
  page.run('workerReady = true; updateSolveBtn()');
});

test('commitPasteBox: loads a pasted deal and its vulnerability', () => {
  page.byId('pasteBox').value =
    '[Vulnerable "EW"] [Deal "N:42.AQT63.K52.T84 AJ65.42.J963.KQJ Q973..AT74.A9632 KT8.KJ9875.Q8.75"]';
  page.run('commitPasteBox()');
  assert.strictEqual(page.run('vulnerability'), 'E-W');
  assert.strictEqual(page.run("getHandValue('east')"), FULL.east);
  assert.strictEqual(page.byId('status').textContent, 'Loaded deal.');

  page.byId('pasteBox').value = 'not a deal';
  page.run('commitPasteBox()');
  assert.strictEqual(page.byId('status').textContent, "That doesn't look like a recognized deal format.");
});

runTests();
