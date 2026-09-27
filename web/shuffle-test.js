// Unit tests for shuffle.js: merging shuffle results, the shuffle tables and
// the shuffle pool's messages. Run with: node shuffle-test.js
'use strict';

const assert = require('assert');
const { loadPage, test, runTests } = require('./test-env.js');

const plain = (x) => JSON.parse(JSON.stringify(x));

// Shuffle data as the workers send it: counts[declarer][strain row][t] is
// how many rounds took exactly t tricks. `fill(d, row)` gives each cell.
function batch(rounds, fill) {
  const direction = () => {
    const counts = {};
    for (const d of ['S', 'N', 'W', 'E']) {
      counts[d] = Array.from({ length: 5 }, (_, row) => {
        const c = new Array(14).fill(0);
        for (const [t, n] of Object.entries(fill(d, row))) c[t] = n;
        return c;
      });
    }
    return { counts };
  };
  return { rounds, elapsedMs: 10, ew: direction(), ns: direction() };
}

const page = loadPage();
const FULL = {
  west: 'KT8 KJ9875 Q8 75', north: '42 AQT63 K52 T84',
  east: 'AJ65 42 J963 KQJ', south: 'Q973 - AT74 A9632',
};
const ENDING = { west: '- Q76 Q95 -', north: '- AJ K86 7', east: '- KT JT2 K', south: '53 4 A7 J' };
const handsKey = (hands) => ['west', 'north', 'east', 'south'].map(s => hands[s]).join('|');

test('mergeShuffleData: the first batch as is, then sums rounds and counts', () => {
  const a = batch(10, () => ({ 9: 10 }));
  assert.strictEqual(page.run(`mergeShuffleData(null, ${JSON.stringify(a)})`).rounds, 10);
  const merged = plain(page.run(`mergeShuffleData(${JSON.stringify(a)},
    ${JSON.stringify(batch(5, () => ({ 9: 2, 10: 3 })))})`));
  assert.strictEqual(merged.rounds, 15);
  assert.strictEqual(merged.elapsedMs, 20);
  assert.strictEqual(merged.ew.counts.S[0][9], 12);
  assert.strictEqual(merged.ns.counts.E[4][10], 3);
});

test('trickStats: average and making-or-more percentages, rounded down', () => {
  const stats = plain(page.run('trickStats([0,0,0,0,0,0,0,0,0,5,5,0,0,0])'));
  assert.strictEqual(stats.avg, 9.5);
  assert.strictEqual(stats.pct[9], 100);
  assert.strictEqual(stats.pct[10], 50);
  assert.strictEqual(stats.pct[11], 0);
  // One 13-trick deal in 110 is 0.9%, shown as 0.
  const counts = new Array(14).fill(0);
  counts[12] = 109;
  counts[13] = 1;
  assert.strictEqual(page.run(`trickStats(${JSON.stringify(counts)})`).pct[13], 0);
});

test('singleDummyCounts: N/S declarers from shuffling E/W, W/E from shuffling N/S', () => {
  const data = batch(1, (d, row) => ({ [row]: 1 }));
  data.ew.counts.S[1][11] = 7;
  data.ns.counts.W[4][2] = 5;
  const counts = plain(page.run(`singleDummyCounts(${JSON.stringify(data)})`));
  assert.strictEqual(counts.S.S[11], 7, 'row 1 is spades, from the E/W shuffle');
  assert.strictEqual(counts.W.C[2], 5, 'row 4 is clubs, from the N/S shuffle');
  assert.strictEqual(counts.N.N[0], 1, 'row 0 is notrump');
});

test('shuffleHandLength: 13 with no shuffles, else the shuffled deal\'s length', () => {
  page.run('shuffleHandsKey = null');
  assert.strictEqual(page.run('shuffleHandLength()'), 13);
  page.run(`shuffleHandsKey = ${JSON.stringify(handsKey(ENDING))}`);
  assert.strictEqual(page.run('shuffleHandLength()'), 6);
});

test('renderShuffleTable: 7+ to 13+ with SD Par for a full deal', () => {
  page.run(`shuffleHandsKey = ${JSON.stringify(handsKey(FULL))};
    shuffleAccumulator = ${JSON.stringify(batch(10, (d) => ({ [d === 'S' ? 9 : 8]: 10 })))};
    renderShuffleTable(shuffleAccumulator)`);
  const html = page.byId('shuffleTable').innerHTML;
  assert.match(html, /<th>S\/N<\/th><th>avg<\/th><th>7\+<\/th>/);
  assert.match(html, /<th>13\+<\/th><\/tr>/);
  // South 9 and North 8 tricks in every round differ; West and East agree.
  assert.match(html, /<td>9\.0\/8\.0<\/td>/);
  assert.match(html, /<td>8\.0<\/td>/);
  assert.match(html, /id="sdPar"/);
});

test('renderShuffleTable: an ending shows the player on lead, 1+ to 6+ and no SD Par', () => {
  // With South on lead, East declares: East taking 0 means N/S take all 6.
  page.run(`shuffleHandsKey = ${JSON.stringify(handsKey(ENDING))};
    shuffleAccumulator = ${JSON.stringify(batch(10, (d) => ({ [d === 'E' ? 0 : 3]: 10 })))};
    renderShuffleTable(shuffleAccumulator)`);
  const html = page.byId('shuffleTable').innerHTML;
  assert.match(html, /<th>S\/N lead<\/th><th>avg<\/th><th>1\+<\/th>/);
  assert.match(html, /<th>6\+<\/th><\/tr>/);
  assert.match(html, /<td>6\.0\/3\.0<\/td>/, 'South leads: 6 - East\'s 0; North leads: 6 - West\'s 3');
  assert.doesNotMatch(html, /sdPar/);
});

test('shuffle table titles fold and unfold', () => {
  page.run(`shuffleHandsKey = ${JSON.stringify(handsKey(FULL))};
    shuffleAccumulator = ${JSON.stringify(batch(10, () => ({ 8: 10 })))}; renderShuffleTable(shuffleAccumulator)`);
  const h3 = { dataset: { dir: 'ew' } };
  page.fire('shuffleTable', 'click', { target: { closest: () => h3 } });
  const html = page.byId('shuffleTable').innerHTML;
  assert.match(html, /<h3 data-dir="ew">▸ /);
  assert.match(html, /<h3 data-dir="ns">▾ /);
  page.fire('shuffleTable', 'click', { target: { closest: () => h3 } });
});

test('runShuffleBatch: splits rounds across the pool and merges the answers', () => {
  page.run(`for (const seat of SEATS) setHandValue(seat, ${JSON.stringify(FULL)}[seat]);
    clearShuffleResults(); busy = false; workerReady = true;
    shuffleWorkersReadyCount = shuffleWorkers.length`);
  const pool = page.run('shuffleWorkers');
  pool.forEach(w => (w.messages.length = 0));
  page.run('runShuffleBatch(10)');
  // 4 workers: 3 + 3 + 2 + 2 rounds.
  assert.deepStrictEqual(plain(pool.map(w => w.messages[0][2])), [3, 3, 2, 2]);
  assert.strictEqual(page.run('busy'), true);
  assert.match(page.byId('status').textContent, /^Shuffling… 0\/20 \(4 workers\)/);

  pool.forEach((w, i) => page.run(`onShuffleWorkerMessage(${i}, { data: ['shuffle_progress', 2] })`));
  assert.match(page.byId('status').textContent, /^Shuffling… 8\/20/);
  pool.forEach((w, i) => {
    const rounds = w.messages[0][2];
    page.run(`onShuffleWorkerMessage(${i},
      { data: ['shuffle', ${JSON.stringify(batch(rounds, () => ({ 8: rounds })))}] })`);
  });
  assert.strictEqual(page.run('shuffleAccumulator.rounds'), 10);
  assert.match(page.byId('status').textContent, /^Shuffled 10 times each way in [\d.]+ s \(4 workers\)\.$/);
  assert.strictEqual(page.run('busy'), false);
});

test('runShuffleBatch: results accumulate for the same hands, and reset for new ones', () => {
  const pool = page.run('shuffleWorkers');
  const answer = () => pool.forEach((w, i) => {
    const rounds = w.messages[w.messages.length - 1][2];
    page.run(`onShuffleWorkerMessage(${i},
      { data: ['shuffle', ${JSON.stringify(batch(rounds, () => ({ 8: rounds })))}] })`);
  });
  page.run('runShuffleBatch(10)');
  answer();
  assert.strictEqual(page.run('shuffleAccumulator.rounds'), 20);
  // Swap West's C5 and East's CJ: a different deal, so results restart.
  page.run("setHandValue('west', 'KT8 KJ9875 Q8 J7'); setHandValue('east', 'AJ65 42 J963 KQ5')");
  page.run('runShuffleBatch(10)');
  answer();
  assert.strictEqual(page.run('shuffleAccumulator.rounds'), 10);
});

test('shuffle progress keeps the note about a waiting link', () => {
  page.run("pendingBatch = { doneByWorker: [0], totalUnits: 20, workers: 1 }; deferredUrlHash = '#x'");
  page.run("onShuffleWorkerMessage(0, { data: ['shuffle_progress', 5] })");
  assert.strictEqual(page.byId('status').textContent,
    'Shuffling… 5/20 (1 worker) The new link opens when this finishes.');
  page.run('pendingBatch = null; deferredUrlHash = null');
});

test('a shuffle worker crash disables Shuffle', () => {
  page.run('busy = true; shuffleWorkersReadyCount = shuffleWorkers.length');
  page.run("onShuffleWorkerMessage(0, { data: ['abort', 'boom'] })");
  assert.strictEqual(page.byId('status').textContent, 'Solver crashed: boom');
  assert.strictEqual(page.byId('shuffle10').disabled, true);
  assert.strictEqual(page.run('busy'), false);
});

runTests();
