// Unit tests for par-score.js, ported from bidding-practice's test.js.
// Run with: node par-score-test.js (Node 14.6+ for private class methods)
'use strict';

const assert = require('assert');
const { computePar, formatParContract, formatParScore } = require('./par-score.js');

const tests = [];
const test = (name, fn) => tests.push({ name, fn });

const strainName = s => (s === 'N' ? 'NT' : s);

// Same output shape as bidding-practice's testParScore().
function parLines(ddLines, vulnerability) {
  const [nsFirst, ewFirst] = computePar(ddLines, vulnerability);
  if (!nsFirst.length && !ewFirst.length) return ['Par: 0'];
  return [nsFirst, ewFirst].filter(list => list.length).flatMap(list =>
    ['Par: ' + formatParScore(list), ...list.map(c => formatParContract(c, strainName))]);
}

const cases = [
  // Positive scores.
  [['N 1 1 11 11', 'S 2 2 11 11', 'H 0 0 13 13', 'D 5 5 8 8', 'C 0 0 12 12'], 'E-W',
   ['Par: EW +2210', '7H= by EW']],
  [['N 1 1 10 10', 'S 6 6 7 7', 'H 1 1 11 11', 'D 1 1 11 11', 'C 3 3 9 9'], 'None',
   ['Par: EW +450', '4H+1 by EW']],
  [['N 3 3 9 9', 'S 3 3 10 9', 'H 2 2 10 9', 'D 7 7 5 5', 'C 5 5 8 8'], 'None',
   ['Par: EW +420', '4S= by W', '4H= by W']],
  [['N 7 7 6 6', 'S 6 6 6 7', 'H 7 7 6 6', 'D 7 7 6 6', 'C 8 8 5 5'], 'E-W',
   ['Par: NS +90', '1NT= by NS', '2C= by NS']],
  // Negative scores.
  [['N 5 5 8 7', 'S 3 3 10 10', 'H 6 6 5 7', 'D 8 8 5 5', 'C 2 2 10 10'], 'E-W',
   ['Par: NS -500', '5DX-3 by NS']],
  [['N 4 4 8 8', 'S 8 8 5 5', 'H 4 4 8 9', 'D 8 8 5 5', 'C 4 4 9 9'], 'None',
   ['Par: NS -100', '3SX-1 by NS']],
  [['N 5 5 7 7', 'S 4 4 8 8', 'H 8 8 5 5', 'D 3 3 9 9', 'C 8 8 5 5'], 'None',
   ['Par: NS -100', '3HX-1 by NS']],
  [['N 7 7 6 6', 'S 3 3 10 10', 'H 9 9 4 4', 'D 9 9 4 4', 'C 4 4 9 9'], 'All',
   ['Par: NS -500', '5HX-2 by NS', '5DX-2 by NS']],
  [['N 5 5 8 8', 'S 9 9 4 4', 'H 9 9 4 4', 'D 3 3 9 9', 'C 3 3 9 9'], 'N-S',
   ['Par: EW -100', '3NTX-1 by EW', '4DX-1 by EW', '4CX-1 by EW']],
  [['N 8 8 5 5', 'S 5 5 7 7', 'H 9 9 3 3', 'D 4 4 9 8', 'C 8 8 4 4'], 'N-S',
   ['Par: EW -100', '4DX-1 by W']],
  // Special.
  [['N 5 5 3 3', 'S 5 5 5 5', 'H 4 4 6 6', 'D 6 6 3 3', 'C 6 6 3 3'], 'N-S',
   ['Par: 0']],
  [['N 7 7 7 7', 'S 6 6 6 6', 'H 6 6 6 6', 'D 6 6 6 6', 'C 6 6 6 6'], 'All',
   ['Par: NS +90', '1NT= by NS', 'Par: EW +90', '1NT= by EW']],
  [['N 13 0 13 0', 'S 13 12 1 0', 'H 2 1 12 11', 'D 2 1 12 11', 'C 0 0 13 13'], 'N-S',
   ['Par: NS +2220', '7NT= by S', 'Par: EW +1520', '7NT= by W']],
];

for (const [ddLines, vulnerability, expected] of cases) {
  test(`${expected.join(', ')} (vul ${vulnerability})`, () => {
    assert.deepStrictEqual(parLines(ddLines, vulnerability), expected);
  });
}

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
