// Unit tests for startup.js: the workers it starts and how the page
// reports them ready. Run with: node startup-test.js
'use strict';

const assert = require('assert');
const { loadPage, test, runTests } = require('./test-env.js');

test('starts the solver and half as many shuffle workers as logical CPUs', () => {
  const page = loadPage({ hardwareConcurrency: 8 });
  assert.match(page.run('worker.url'), /^worker\.js\?v=[0-9a-z]+$/);
  const pool = page.run('shuffleWorkers');
  assert.strictEqual(pool.length, 4);
  for (const w of pool) assert.match(w.url, /^shuffle-worker\.js\?v=[0-9a-z]+$/);
  assert.strictEqual(page.run('worker.onmessage === onSolverMessage'), true);
});

test('?workers=N overrides the shuffle pool size', () => {
  assert.strictEqual(loadPage({ search: '?workers=3' }).run('shuffleWorkers.length'), 3);
  assert.strictEqual(loadPage({ hardwareConcurrency: 1 }).run('shuffleWorkers.length'), 1);
});

test('Solve and Shuffle enable as their workers report ready', () => {
  const page = loadPage({ hardwareConcurrency: 4 });
  const [a, b] = page.run('shuffleWorkers');
  assert.strictEqual(page.byId('solve').disabled, true);
  assert.strictEqual(page.byId('shuffle10').disabled, true);

  page.run('worker').send('ready');
  assert.strictEqual(page.byId('solve').disabled, false);
  assert.strictEqual(page.byId('status').textContent, 'Loading solver…', 'shuffle workers still loading');

  a.send('ready');
  assert.strictEqual(page.byId('shuffle10').disabled, true, 'only one of two loaded');
  b.send('ready');
  assert.strictEqual(page.byId('shuffle10').disabled, false);
  assert.strictEqual(page.byId('shuffle100').disabled, false);
  assert.strictEqual(page.byId('status').textContent, 'Ready.');
});

test('a link loaded before the workers keeps its message', () => {
  const page = loadPage();
  page.byId('status').textContent = 'Loaded deal from link.';
  page.run('worker').send('ready');
  for (const w of page.run('shuffleWorkers')) w.send('ready');
  assert.strictEqual(page.byId('status').textContent, 'Loaded deal from link.');
});

test('play started from a link before the solver loads asks it once ready', () => {
  const page = loadPage();
  page.run(`const hands = { west: '- Q76 Q95 -', north: '- AJ K86 7', east: '- KT JT2 K',
      south: '53 4 A7 J' };
    for (const seat of SEATS) setHandValue(seat, hands[seat]);
    ddDeal = formatPBN(hands); startPlay('S', 'east', 0)`);
  const solver = page.run('worker');
  assert.strictEqual(solver.messages.length, 0);
  solver.send('ready');
  assert.strictEqual(solver.messages[0][0], 'solve_plays');
  assert.strictEqual(page.byId('solve').disabled, true, 'still playing');
});

runTests();
