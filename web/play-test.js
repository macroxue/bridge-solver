// Unit tests for play.js: card helpers, replaying tricks, and playing out a
// contract or ending against the solver's answers. Run with: node play-test.js
'use strict';

const assert = require('assert');
const { loadPage, test, runTests } = require('./test-env.js');

const plain = (x) => JSON.parse(JSON.stringify(x));

const FULL = {
  west: 'KT8 KJ9875 Q8 75', north: '42 AQT63 K52 T84',
  east: 'AJ65 42 J963 KQJ', south: 'Q973 - AT74 A9632',
};
// deals/old/squeeze.4: spades, South to lead, N/S take all 6.
const ENDING = { west: '- Q76 Q95 -', north: '- AJ K86 7', east: '- KT JT2 K', south: '53 4 A7 J' };

const page = loadPage();
page.run('workerReady = true');

// Puts `hands` in the inputs as a solved deal, so play can start from it.
function solved(hands) {
  page.run(`exitPlay(); for (const seat of SEATS) setHandValue(seat, ${JSON.stringify(hands)}[seat]);
    ddDeal = formatPBN(${JSON.stringify(hands)})`);
}
const lastPlaysRequest = () => {
  const messages = page.run('worker.messages').filter(m => m[0] === 'solve_plays');
  return plain(messages[messages.length - 1]);
};
// Answers the latest solve_plays request.
const answer = (result) => page.run(`onSolvePlays(${JSON.stringify(result)}, playState.requestId)`);

test('nextSeat/prevSeat: clockwise West, North, East, South', () => {
  assert.strictEqual(page.run("nextSeat('south')"), 'west');
  assert.strictEqual(page.run("prevSeat('west')"), 'south');
  assert.strictEqual(page.run("prevSeat(nextSeat('east'))"), 'east');
});

test('parseHandCards and cardStr', () => {
  assert.deepStrictEqual(plain(page.run("parseHandCards('AK - Q2 -').map(cardStr)")), ['SA', 'SK', 'DQ', 'D2']);
});

test('displayRank/rankHtml: T shows as 10', () => {
  assert.strictEqual(page.run("displayRank('T')"), '10');
  assert.strictEqual(page.run("displayRank('Q')"), 'Q');
  assert.match(page.run("rankHtml('T')"), />10</);
});

test('resolveWildcards: each X is the lowest rank not yet claimed', () => {
  const resolved = plain(page.run(`resolveWildcards({ west: 'AX - - -', north: '2 - - -',
    east: 'XX - - -', south: 'K - - -' })`));
  assert.deepStrictEqual(resolved, { west: 'A2 - - -', north: '2 - - -', east: '34 - - -', south: 'K - - -' });
});

test('winningCard: highest of the suit led, unless trumped', () => {
  const trick = (cards) => JSON.stringify(cards.map(c => ({ suit: c[0], rank: c[1] })));
  assert.strictEqual(page.run(`winningCard(${trick(['H4', 'HK', 'S2', 'HA'])}, null)`).rank, 'A');
  assert.strictEqual(page.run(`winningCard(${trick(['H4', 'HK', 'S2', 'HA'])}, 'S')`).suit, 'S');
  assert.strictEqual(page.run(`winningCard(${trick(['H4', 'D9', 'DA', 'H5'])}, 'C')`).rank, '5');
});

test('contractLabel: level, and how far short of it declarer is', () => {
  assert.strictEqual(page.run("contractLabel(4, 10, 'S')"), '4S');
  assert.strictEqual(page.run("contractLabel(4, 11, 'S')"), '4S');
  assert.strictEqual(page.run("contractLabel(1, 4, 'H')"), '1H-3');
});

test('startPlay: refuses a table solved for other hands', () => {
  solved(FULL);
  page.run("setHandValue('west', 'KT8 KJ9875 Q8 J7'); setHandValue('east', 'AJ65 42 J963 KQ5')");
  page.run("startPlay('D', 'north', 11)");
  assert.strictEqual(page.run('playState'), null);
  assert.strictEqual(page.byId('status').textContent,
    'The hands changed since the table was solved. Solve again to play.');
});

test('startPlay: a full deal asks the solver for the contract\'s target', () => {
  solved(FULL);
  page.run("startPlay('D', 'north', 11)");
  assert.match(page.byId('playStatus').innerHTML, /5.*diams.* by North/);
  // [type, hands, target tricks, trump, lead seat index, played, request id]
  const [type, , target, trump, lead, played] = lastPlaysRequest();
  assert.deepStrictEqual([type, target, trump, lead, played], ['solve_plays', 11, 2, 2, '']);
  // A contract that fails still targets a 1-level contract, 7 tricks.
  page.run("startPlay('S', 'south', 4)");
  assert.strictEqual(lastPlaysRequest()[2], 7);
  assert.match(page.byId('playStatus').innerHTML, /1.*spades.*-3 by South/);
});

test('startPlay: an ending targets declarer\'s DD tricks and is labeled like a problem', () => {
  solved(ENDING);
  page.run("startPlay('S', 'east', 0)");
  assert.strictEqual(lastPlaysRequest()[2], 0);
  assert.match(page.byId('playStatus').innerHTML, /spades.* trump, South to lead, 6 of 6/);
  assert.match(page.byId('playHint').textContent, /side on lead/);
  page.run('exitPlay()');
  solved(FULL);
  page.run("startPlay('D', 'north', 11)");
  assert.doesNotMatch(page.byId('playHint').textContent, /side on lead/);
});

test('onSolvePlays: an ending\'s results are for the side on lead', () => {
  solved(ENDING);
  page.run("startPlay('S', 'east', 0)");
  answer('S5:+0 H4:+1 DA:+1 D7:+1 CJ:+1');
  assert.deepStrictEqual(plain(page.run('playState.pendingPlays')),
    { S5: '0', H4: '-1', DA: '-1', D7: '-1', CJ: '-1' });
});

test('onSolvePlays: a single choice plays itself', () => {
  solved(FULL);
  page.run("startPlay('D', 'north', 11)");
  answer('H2:+0 C6:+0');
  page.run("playCard('east', { suit: 'H', rank: '2' })");
  answer('H3:+0');  // South's only sensible card
  assert.deepStrictEqual(plain(page.run('playState.history.map(p => [cardStr(p), p.auto])')),
    [['H2', false], ['H3', true]]);
  assert.strictEqual(lastPlaysRequest()[5], 'H2H3');
});

test('Undo: back to the last real choice, without auto-playing it again', () => {
  // Continuing from the previous test: H2 by East, then H3 auto by South.
  page.run('undo()');
  assert.strictEqual(page.run('playState.history.length'), 0);
  answer('H2:+0');  // even a single choice waits for a click after Undo
  assert.strictEqual(page.run('playState.history.length'), 0);
  assert.deepStrictEqual(plain(page.run('playState.pendingPlays')), { H2: '+0' });
  page.run("playCard('east', { suit: 'H', rank: '2' })");
  answer('H3:+0');
  assert.strictEqual(page.run('playState.history.length'), 2, 'auto-play is back on');
});

test('Undo Trick: back to the start of the trick', () => {
  page.run('undoTrick()');
  assert.strictEqual(page.run('playState.history.length'), 0);
  assert.strictEqual(page.run('playState.autoPlay'), false);
});

test('replayState: the trick\'s winner leads next, and tricks are counted', () => {
  solved(ENDING);
  page.run("startPlay('S', 'east', 0, 'S5HQD8HK')");
  const state = plain(page.run('replayState()'));
  assert.strictEqual(state.seat, 'south', 'South\'s trump wins');
  assert.deepStrictEqual([state.nsTricks, state.ewTricks], [1, 0]);
  assert.deepStrictEqual(plain(page.run("remainingCards('south').map(cardStr)")), ['S3', 'H4', 'DA', 'D7', 'CJ']);
});

test('startPlay: replays given cards, stopping at the first illegal one', () => {
  solved(ENDING);
  // West, with no spades, may discard HQ; but North doesn't hold S2.
  page.run("startPlay('S', 'east', 0, 'S5HQS2D8')");
  assert.strictEqual(page.run('playState.history.map(cardStr).join("")'), 'S5HQ');
  // South leads DA in trick 2, and East discards HT while holding diamonds.
  page.run("startPlay('S', 'east', 0, 'S5HQD8HKDAD5D6HT')");
  assert.strictEqual(page.run('playState.history.map(cardStr).join("")'), 'S5HQD8HKDAD5D6');
});

test('startPlay: a fully played deal doesn\'t ask the solver', () => {
  solved(ENDING);
  const before = page.run('worker.messages.length');
  page.run(`startPlay('S', 'east', 0,
    'S5HQD8HKS3H7C7HTH4H6HADJHJDTCJDQDKD2D7D9D6CKDAD5')`);
  assert.strictEqual(page.run('worker.messages.length'), before);
  assert.match(page.byId('playStatus').innerHTML, /\(final\).*NS 6 &ndash; EW 0/);
});

test('exitPlay: back to entry, with Solve available again', () => {
  page.run('exitPlay()');
  assert.strictEqual(page.run('playState'), null);
  assert.strictEqual(page.byId('solve').disabled, false);
  assert.strictEqual(page.byId('north-S').disabled, false);
});

runTests();
