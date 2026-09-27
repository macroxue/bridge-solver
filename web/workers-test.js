// Unit tests for worker.js and shuffle-worker.js, with a fake wasm Module in
// place of the solver. Run with: node workers-test.js
'use strict';

const assert = require('assert');
const { loadWorker, test, runTests } = require('./test-env.js');

const plain = (x) => JSON.parse(JSON.stringify(x));
const HANDS = { west: 'W', north: 'N', east: 'E', south: 'S' };

test('worker.js: solve answers with the trimmed table and its time', () => {
  const w = loadWorker('worker.js');
  const calls = [];
  w.context.Module.solve = (...args) => { calls.push(args); return 'N 7 7 6 6\n'; };
  w.send('solve', HANDS);
  assert.deepStrictEqual(calls, [['W', 'N', 'E', 'S']]);
  const [type, result, ms] = w.posted[0];
  assert.deepStrictEqual([type, result], ['solve', 'N 7 7 6 6']);
  assert.strictEqual(typeof ms, 'number');
});

test('worker.js: solve_plays passes its arguments through and echoes the request id', () => {
  const w = loadWorker('worker.js');
  const calls = [];
  w.context.Module.solve_plays = (...args) => { calls.push(args); return 'H2:+0 '; };
  w.send('solve_plays', HANDS, 11, 2, 2, 'H2', 7);
  assert.deepStrictEqual(calls, [['W', 'N', 'E', 'S', 11, 2, 2, 'H2']]);
  const [type, result, , requestId] = w.posted[0];
  assert.deepStrictEqual([type, result, requestId], ['solve_plays', 'H2:+0', 7]);
});

test('worker.js: a failure reports the solver\'s stderr and the request id', () => {
  const w = loadWorker('worker.js');
  w.context.Module.printErr('bad card');
  w.context.Module.solve_plays = () => { throw new Error('abort'); };
  w.send('solve_plays', HANDS, 11, 2, 2, '', 3);
  assert.deepStrictEqual(plain(w.posted[0]), ['error', 'solve_plays', '[1] bad card', 3]);
  w.context.Module.solve = () => 'N 1 1 1 1';
  w.send('solve', HANDS);
  assert.strictEqual(w.posted[1][0], 'solve', 'errors are cleared once reported');
});

test('worker.js: reports ready and aborts', () => {
  const w = loadWorker('worker.js');
  w.context.Module.onRuntimeInitialized();
  w.context.Module.printErr('boom');
  w.context.Module.onAbort();
  assert.deepStrictEqual(plain(w.posted), [['ready'], ['abort', '[1] boom']]);
});

test('shuffle-worker.js: parseSolveResult reads columns by DECLARER_COLUMNS', () => {
  const w = loadWorker('shuffle-worker.js');
  const rows = plain(w.run("parseSolveResult('N  9  8  4  5  0.1 s\\nS  6  6  7  7  0.2 s\\n')"));
  assert.deepStrictEqual(rows, [{ S: 9, N: 8, W: 4, E: 5 }, { S: 6, N: 6, W: 7, E: 7 }]);
});

test('shuffle-worker.js: counts each declarer\'s tricks for both directions', () => {
  const w = loadWorker('shuffle-worker.js');
  const seats = [];
  let round = 0;
  w.context.Module.shuffle_and_solve = (west, north, east, south, shuffled, discardSuitBottom) => {
    seats.push(shuffled);
    assert.strictEqual(discardSuitBottom, true);
    // South takes 9 or 10 in NT, alternately; everything else is fixed.
    const nt = 9 + (round++ % 2);
    return `N ${nt} 8 4 5 0 s\nS 6 6 7 7 0 s\nH 6 6 7 7 0 s\nD 6 6 7 7 0 s\nC 6 6 7 7 0 s\n`;
  };
  w.send('shuffle', HANDS, 4);
  assert.deepStrictEqual(seats, ['WE', 'WE', 'WE', 'WE', 'NS', 'NS', 'NS', 'NS']);
  const [type, data] = w.posted[w.posted.length - 1];
  assert.strictEqual(type, 'shuffle');
  assert.strictEqual(data.rounds, 4);
  assert.strictEqual(data.ew.counts.S[0][9], 2);
  assert.strictEqual(data.ew.counts.S[0][10], 2);
  assert.strictEqual(data.ns.counts.E[0][5], 4);
  assert.strictEqual(data.ew.counts.N[1].reduce((a, b) => a + b, 0), 4, 'one count per round');
  // Progress every 5 of the 8 solves, and at the end.
  assert.deepStrictEqual(plain(w.posted.filter(m => m[0] === 'shuffle_progress')),
    [['shuffle_progress', 5, 8], ['shuffle_progress', 8, 8]]);
});

test('shuffle-worker.js: a failed round is skipped, not fatal', () => {
  const w = loadWorker('shuffle-worker.js');
  let calls = 0;
  w.context.Module.shuffle_and_solve = () => {
    if (++calls === 2) throw new Error('bad round');
    return 'N 7 7 6 6 0 s\nS 7 7 6 6 0 s\nH 7 7 6 6 0 s\nD 7 7 6 6 0 s\nC 7 7 6 6 0 s\n';
  };
  const error = console.error;
  console.error = () => {};
  try {
    w.send('shuffle', HANDS, 3);
  } finally {
    console.error = error;
  }
  const data = w.posted[w.posted.length - 1][1];
  assert.strictEqual(data.ew.counts.S[0][7], 2, 'one of the three E/W rounds failed');
  assert.strictEqual(data.ns.counts.S[0][7], 3);
});

runTests();
