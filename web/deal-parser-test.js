// Minimal, dependency-free unit tests for deal-parser.js -- no test
// framework, to match this project's plain-vanilla web/ (no package.json,
// no bundler). Run with: node deal-parser-test.js
'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const { parsePBN, parseDealFile, parsePastedDeal } = require('./deal-parser.js');

const readDeal = (name) => fs.readFileSync(path.join(__dirname, '..', 'deals', 'hard', name), 'utf8');

// The deal every PBN case below encodes, and every deals/hard/deal.17 case
// (a different deal) is checked against separately.
const DEFAULT_HANDS = {
  north: '42 AQT63 K52 T84',
  west: 'KT8 KJ9875 Q8 75',
  east: 'AJ65 42 J963 KQJ',
  south: 'Q973 - AT74 A9632',
};

const tests = [];
const test = (name, fn) => tests.push({ name, fn });

// --- parsePBN ---

test('parsePBN: dealer N', () => {
  const pbn = 'N:42.AQT63.K52.T84 AJ65.42.J963.KQJ Q973..AT74.A9632 KT8.KJ9875.Q8.75';
  assert.deepStrictEqual(parsePBN(pbn), DEFAULT_HANDS);
});

test('parsePBN: same deal, different dealer -> same hands', () => {
  const pbn = 'W:KT8.KJ9875.Q8.75 42.AQT63.K52.T84 AJ65.42.J963.KQJ Q973..AT74.A9632';
  assert.deepStrictEqual(parsePBN(pbn), DEFAULT_HANDS);
});

test('parsePBN: picked out of a full PBN tag/file', () => {
  const tagged = [
    '[Event "Test"]',
    '[Deal "N:42.AQT63.K52.T84 AJ65.42.J963.KQJ Q973..AT74.A9632 KT8.KJ9875.Q8.75"]',
    '[Dealer "N"]',
  ].join('\n');
  assert.deepStrictEqual(parsePBN(tagged), DEFAULT_HANDS);
});

test('parsePBN: non-PBN text returns null', () => {
  assert.strictEqual(parsePBN('AKQJT98765432'), null);
});

test('parsePBN: "10" as an alternative to "T" for ten', () => {
  const pbn = 'N:42.AQ1063.K52.T84 AJ65.42.J963.KQJ Q973..AT74.A9632 KT8.KJ9875.Q8.75';
  assert.deepStrictEqual(parsePBN(pbn), { ...DEFAULT_HANDS, north: '42 AQ1063 K52 T84' });
});

// --- parseDealFile: symbol formats ---

test('parseDealFile: seat-letter + symbol format (deals/hard/deal.17)', () => {
  assert.deepStrictEqual(parseDealFile(readDeal('deal.17')), {
    north: 'A87 KJ54 A8753 4',
    west: 'K52 A83 T AJ9653',
    east: 'Q964 QT2 KQJ942 -',
    south: 'JT3 976 6 KQT872',
  });
});

const VUGRAPH_MULTILINE = [
  'Board:3',
  'Dealer:SOUTH',
  'Vulnerable:E-W',
  '',
  '♠KT9743',
  '♥QJT',
  '♦A',
  '♣J72',
  '♠Q62',
  '♥52',
  '♦8765',
  '♣AKQ4',
  '♠',
  '♥AK94',
  '♦KQJT93',
  '♣T83',
  'Par: -1100',
  '                    N',
  'NS    1    1    6    7    6',
  'EW    12    12    7    5    6',
  '♠AJ85',
  '♥8763',
  '♦42',
  '♣965',
].join('\n');

const VUGRAPH_HANDS = {
  north: 'KT9743 QJT A J72',
  west: 'Q62 52 8765 AKQ4',
  east: '- AK94 KQJT93 T83',
  south: 'AJ85 8763 42 965',
};

test('parseDealFile: unlabeled symbol format with a void suit, multi-line (vugraph)', () => {
  assert.deepStrictEqual(parseDealFile(VUGRAPH_MULTILINE), VUGRAPH_HANDS);
});

test('parseDealFile: ignores a stray seat letter from surrounding prose ("Vulnerable:E-W")', () => {
  // Regression: an earlier version mis-tagged North as West here, because
  // the "W" in "Vulnerable:E-W" sat right before the first hand.
  const hands = parseDealFile(VUGRAPH_MULTILINE);
  assert.strictEqual(hands.north, VUGRAPH_HANDS.north);
});

test('parseDealFile: same vugraph text with newlines collapsed to spaces (what a real <input> paste leaves)', () => {
  assert.deepStrictEqual(parseDealFile(VUGRAPH_MULTILINE.replace(/\n/g, ' ')), VUGRAPH_HANDS);
});

test('parseDealFile: symbol format with no separators at all between fields (one-liner)', () => {
  const oneLiner =
    'Board:4    Dealer:WEST    Vulnerable:ALL♠87♥J743♦K73♣K762' +
    '♠KJ96♥92♦Q964♣854♠QT2♥KQ65♦AJ852♣TPar: -130' +
    '                    NN    10    3    8    6    7S    :    :    7    5    :' +
    'E    3    10    5    7    5W    :    :    :    :    4' +
    '♠A543♥AT8♦T♣AQJ93';
  assert.deepStrictEqual(parseDealFile(oneLiner), {
    north: '87 J743 K73 K762',
    west: 'KJ96 92 Q964 854',
    east: 'QT2 KQ65 AJ852 T',
    south: 'A543 AT8 T AQJ93',
  });
});

test('parseDealFile: symbol format, "10" as an alternative to "T" for ten', () => {
  const hands = parseDealFile(VUGRAPH_MULTILINE.replace('♠KT9743', '♠K109743'));
  assert.strictEqual(hands.north, 'K109743 QJT A J72');
});

const BRIDGEWINNERS_HANDS = {
  west: '7 KQJ96 A74 J1082',
  north: 'J65 1032 98 A9763',
  east: 'Q43 A75 QJ105 KQ5',
  south: 'AK10982 84 K632 4',
};

test('parseDealFile: full seat names, one symbol/rank per line, not in N/W/E/S order (bridgewinners.com)', () => {
  // Regression: seats are listed West, North, East, South here -- not the
  // usual N,W,E,S -- so this only comes out right if "West"/"North"/etc.
  // are recognized as real labels; falling back to positional order would
  // silently swap North and West (both still 4-card spades, easy to miss).
  const text = [
    'West', '♠', '7', '♥', 'KQJ96', '♦', 'A74', '♣', 'J1082',
    'North', '♠', 'J65', '♥', '1032', '♦', '98', '♣', 'A9763',
    'East', '♠', 'Q43', '♥', 'A75', '♦', 'QJ105', '♣', 'KQ5',
    'South', '♠', 'AK10982', '♥', '84', '♦', 'K632', '♣', '4',
  ].join('\n');
  assert.deepStrictEqual(parseDealFile(text), BRIDGEWINNERS_HANDS);
});

test('parseDealFile: same bridgewinners.com text collapsed to spaces (what a real <input> paste leaves)', () => {
  const text = [
    'West', '♠', '7', '♥', 'KQJ96', '♦', 'A74', '♣', 'J1082',
    'North', '♠', 'J65', '♥', '1032', '♦', '98', '♣', 'A9763',
    'East', '♠', 'Q43', '♥', 'A75', '♦', 'QJ105', '♣', 'KQ5',
    'South', '♠', 'AK10982', '♥', '84', '♦', 'K632', '♣', '4',
  ].join(' ');
  assert.deepStrictEqual(parseDealFile(text), BRIDGEWINNERS_HANDS);
});

test('parseDealFile: one-symbol-per-line format, void suit written as nothing at all', () => {
  const text = [
    'West', '♠', '♥', 'KQJ96', '♦', 'A74', '♣', 'J1082',
    'North', '♠', 'J65', '♥', '1032', '♦', '98', '♣', 'A9763',
    'East', '♠', 'Q43', '♥', 'A75', '♦', 'QJ105', '♣', 'KQ5',
    'South', '♠', 'AK10982', '♥', '84', '♦', 'K632', '♣', '4',
  ].join('\n');
  assert.deepStrictEqual(parseDealFile(text), { ...BRIDGEWINNERS_HANDS, west: '- KQJ96 A74 J1082' });
});

test('parseDealFile: one-symbol-per-line format, void suit written as an explicit "-"', () => {
  const text = [
    'West', '♠', '-', '♥', 'KQJ96', '♦', 'A74', '♣', 'J1082',
    'North', '♠', 'J65', '♥', '1032', '♦', '98', '♣', 'A9763',
    'East', '♠', 'Q43', '♥', 'A75', '♦', 'QJ105', '♣', 'KQ5',
    'South', '♠', 'AK10982', '♥', '84', '♦', 'K632', '♣', '4',
  ].join('\n');
  assert.deepStrictEqual(parseDealFile(text), { ...BRIDGEWINNERS_HANDS, west: '- KQJ96 A74 J1082' });
});

test('parseDealFile: full seat names inline on one line (space-separated), void written as nothing at all', () => {
  const text = 'West ♠ A1042 ♥ Q107 ♦ KJ7 ♣ K106 ' +
    'North ♠ 76 ♥ A92 ♦ AQ92 ♣ AJ84 ' +
    'East ♠ KQ8 ♥ J6 ♦ 1086543 ♣ Q9 ' +
    'South ♠ J953 ♥ K8543 ♦ ♣ 7532';
  assert.deepStrictEqual(parseDealFile(text), {
    west: 'A1042 Q107 KJ7 K106',
    north: '76 A92 AQ92 AJ84',
    east: 'KQ8 J6 1086543 Q9',
    south: 'J953 K8543 - 7532',
  });
});

// --- parseDealFile: positional (no suit symbols) formats ---

const DEAL2_HANDS = {
  north: 'Q73 2 A762 AJ973',
  west: 'J96 3 QJ95 K8642',
  east: 'K84 AJT754 8 QT5',
  south: 'AT52 KQ986 KT43 -',
};

test('parseDealFile: positional groups, multi-line with a shared West/East line (deals/hard/deal.2)', () => {
  assert.deepStrictEqual(parseDealFile(readDeal('deal.2')), DEAL2_HANDS);
});

test('parseDealFile: same positional deal with newlines collapsed to spaces (what a real <input> paste leaves)', () => {
  const collapsed = readDeal('deal.2').replace(/\n/g, ' ');
  assert.deepStrictEqual(parseDealFile(collapsed), DEAL2_HANDS);
});

test('parseDealFile: positional groups, "10" as an alternative to "T" for ten', () => {
  const hands = parseDealFile(readDeal('deal.2').replace('AJ973', 'AJ10973'));
  assert.strictEqual(hands.north, 'Q73 2 A762 AJ10973');
});

test('parseDealFile: a stray 2-digit number elsewhere does not corrupt the positional flat-token fallback', () => {
  // Regression guard for widening the rank charclass to include 0/1 (for
  // "10"): an unrelated 2-digit number sitting in text this fallback would
  // otherwise ignore must not get counted as a 17th token and silently
  // pass as one of the four hands -- it should just fail closed instead.
  const collapsed = readDeal('deal.2').replace(/\n/g, ' ');
  assert.deepStrictEqual(parseDealFile(`Board:12 ${collapsed}`), {});
});

test('parseDealFile: unrecognized text returns no hands', () => {
  assert.deepStrictEqual(parseDealFile('just some random text'), {});
});

// --- parsePastedDeal: tries PBN, then parseDealFile ---

test('parsePastedDeal: recognizes PBN', () => {
  const pbn = 'N:42.AQT63.K52.T84 AJ65.42.J963.KQJ Q973..AT74.A9632 KT8.KJ9875.Q8.75';
  assert.deepStrictEqual(parsePastedDeal(pbn), DEFAULT_HANDS);
});

test('parsePastedDeal: recognizes a deal-file format PBN would not match', () => {
  assert.deepStrictEqual(parsePastedDeal(VUGRAPH_MULTILINE), VUGRAPH_HANDS);
});

test('parsePastedDeal: unrecognized text returns null', () => {
  assert.strictEqual(parsePastedDeal('just some random text'), null);
});

let failed = 0;
for (const { name, fn } of tests) {
  try {
    fn();
    console.log(`ok - ${name}`);
  } catch (err) {
    failed++;
    console.error(`FAIL - ${name}`);
    console.error(err);
  }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
