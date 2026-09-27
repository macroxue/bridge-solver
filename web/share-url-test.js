// Unit tests for share-url.js: the link format, its checksum, and loading
// links, including while busy. Run with: node share-url-test.js
'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const { loadPage, test, runTests } = require('./test-env.js');

const FULL = {
  west: 'KT8 KJ9875 Q8 75', north: '42 AQT63 K52 T84',
  east: 'AJ65 42 J963 KQJ', south: 'Q973 - AT74 A9632',
};
const FULL_DD = 'N 7 7 6 6\nS 6 6 6 7\nH 7 7 6 6\nD 7 7 6 6\nC 8 8 5 5';
const ENDING = { west: '- Q76 Q95 -', north: '- AJ K86 7', east: '- KT JT2 K', south: '53 4 A7 J' };

// The table stub can't find cells in its HTML string, so look them up
// there for resumeUrlPlay().
function withTableCells(page) {
  const table = page.byId('table');
  table.querySelector = (selector) => {
    const [, strain, declarer] = selector.match(/data-strain="(.)"\]\[data-declarer="(\w+)"/);
    const m = table.innerHTML.match(
      new RegExp(`data-strain="${strain}" data-declarer="${declarer}" data-tricks="(\\d+)"`));
    return m ? { dataset: { tricks: m[1] } } : null;
  };
  return page;
}

// A page showing `hands` as solved, with an optional play, and its link.
function linkFor(hands, { vul = 'None', dd, play } = {}) {
  const page = withTableCells(loadPage());
  page.run(`workerReady = true; vulnerability = ${JSON.stringify(vul)};
    for (const seat of SEATS) setHandValue(seat, ${JSON.stringify(hands)}[seat])`);
  if (dd) {
    page.run(`ddDeal = formatPBN(${JSON.stringify(hands)}); showDdTable(${JSON.stringify(dd)})`);
  }
  if (play) page.run(`startPlay(${play.map(x => JSON.stringify(x)).join(', ')})`);
  page.run('writeUrl()');
  return page.location.hash;
}

test('encodeDd/decodeDd: 20 hex digits, back to solve()\'s lines', () => {
  const page = loadPage();
  const dd = page.run(`encodeDd(${JSON.stringify(FULL_DD.split('\n'))})`);
  assert.strictEqual(dd, '77666667776677668855');
  assert.strictEqual(page.run(`decodeDd('${dd}', 13)`), FULL_DD);
});

test('decodeDd: rejects the wrong length, non-hex and too many tricks', () => {
  const page = loadPage();
  assert.strictEqual(page.run("decodeDd('7766', 13)"), null);
  assert.strictEqual(page.run("decodeDd('7766776677667766885z', 13)"), null);
  assert.strictEqual(page.run("decodeDd('77666667776677668855', 6)"), null, '8 tricks in a 6-card ending');
  assert.strictEqual(page.run("decodeDd('dddddddddddddddddddd', 13)").split('\n')[0], 'N 13 13 13 13');
});

test('urlChecksum: ignores sum and survives re-encoding', () => {
  const page = loadPage();
  const sum = (query) => page.run(`urlChecksum(new URLSearchParams(${JSON.stringify(query)}))`);
  assert.strictEqual(sum('deal=N%3AAK..&vul=N-S'), sum('deal=N:AK..&vul=N-S&sum=whatever'));
  assert.strictEqual(sum('deal=N:AK..+Q'), sum('deal=N:AK..%20Q'));
  assert.notStrictEqual(sum('deal=N:AK..'), sum('deal=N:AQ..'));
  assert.match(sum('deal=x'), /^[0-9a-z]{1,7}$/);
});

test('the README\'s example links have valid checksums', () => {
  const page = loadPage();
  const readme = fs.readFileSync(path.join(__dirname, 'README.md'), 'utf8');
  const links = [...readme.matchAll(/\/bridge-solver\/web\/#([^)]*)\)/g)].map(m => m[1]);
  assert.ok(links.length >= 3, 'examples found');
  for (const hash of links) {
    const params = `new URLSearchParams(${JSON.stringify(hash)})`;
    assert.strictEqual(page.run(`${params}.get('sum')`), page.run(`urlChecksum(${params})`), hash);
  }
});

test('writeUrl: deal, vul, table and play, with a matching sum', () => {
  const hash = linkFor(FULL, { vul: 'N-S', dd: FULL_DD, play: ['D', 'north', 7, 'H2'] });
  const params = new URLSearchParams(hash.slice(1));
  assert.deepStrictEqual([...params.keys()], ['deal', 'vul', 'dd', 'play', 'cards', 'sum']);
  assert.strictEqual(params.get('deal'),
    'N:42.AQT63.K52.T84 AJ65.42.J963.KQJ Q973..AT74.A9632 KT8.KJ9875.Q8.75');
  assert.strictEqual(params.get('dd'), '77666667776677668855');
  assert.strictEqual(params.get('play'), 'DN');
  assert.strictEqual(params.get('cards'), 'H2');
  const page = loadPage();
  assert.strictEqual(params.get('sum'), page.run(`urlChecksum(new URLSearchParams(${JSON.stringify(hash.slice(1))}))`));
});

test('writeUrl: no table for other hands, and no link without a valid deal', () => {
  const page = loadPage();
  page.run(`for (const seat of SEATS) setHandValue(seat, ${JSON.stringify(FULL)}[seat]);
    ddDeal = formatPBN(${JSON.stringify(ENDING)}); ddLines = ['N 1 1 1 1']; writeUrl()`);
  assert.doesNotMatch(page.location.hash, /dd=/);
  page.run("setHandValue('west', 'KT8 KJ9875 Q8 7'); writeUrl()");
  assert.strictEqual(page.location.hash, '');
});

test('updateUrl: debounced, and skipped when nothing changed', () => {
  const page = loadPage();
  page.run(`urlLoaded = true; for (const seat of SEATS) setHandValue(seat, ${JSON.stringify(FULL)}[seat])`);
  for (let i = 0; i < 50; ++i) page.run(`vulnerability = '${i % 2 ? 'All' : 'N-S'}'; updateUrl()`);
  assert.strictEqual(page.replaceStateCalls.length, 0);
  page.timers.flush();
  assert.strictEqual(page.replaceStateCalls.length, 1);
  assert.match(page.location.hash, /vul=All/);
  page.run('updateUrl()');
  page.timers.flush();
  assert.strictEqual(page.replaceStateCalls.length, 1);
});

test('loadFromUrl: a deal-only link fills in the deal and vulnerability', () => {
  const hash = linkFor(FULL, { vul: 'E-W' });
  const page = loadPage({ hash });
  assert.strictEqual(page.run("getHandValue('south')"), FULL.south);
  assert.strictEqual(page.run('vulnerability'), 'E-W');
  assert.strictEqual(page.byId('status').textContent, 'Loaded deal from link.');
  assert.strictEqual(page.byId('table').innerHTML, '');
});

test('loadFromUrl: a link with a table shows it and resumes its play', () => {
  const hash = linkFor(ENDING, { dd: 'N 5 5 1 0\nS 5 5 1 0\nH 3 3 2 2\nD 4 4 2 2\nC 5 5 1 1',
    play: ['S', 'east', 0, 'S5HQ'] });
  const page = withTableCells(loadPage({ hash }));
  page.run('loadFromUrl()');  // again, now that the table stub can find cells
  assert.match(page.byId('table').innerHTML, /<th>Lead<\/th>/);
  assert.strictEqual(page.run('playState.history.map(cardStr).join("")'), 'S5HQ');
  assert.match(page.byId('playStatus').innerHTML, /South to lead, 6 of 6/);
});

test('loadFromUrl: rejects damaged links and puts the URL back', () => {
  const good = linkFor(FULL, { vul: 'N-S', dd: FULL_DD });
  const damaged = [
    good.replace('AQT63', 'AQT62'),      // edited card
    good.replace(/&dd=[0-9a-f]+/, ''),   // cut out
    good.replace(/&sum=[0-9a-z]+$/, ''), // no sum
  ];
  for (const hash of damaged) {
    const page = loadPage();
    page.run(`urlLoaded = true; for (const seat of SEATS) setHandValue(seat, ${JSON.stringify(ENDING)}[seat])`);
    page.changeHash(hash);
    assert.strictEqual(page.byId('status').textContent, 'This link is damaged or incomplete.', hash);
    assert.strictEqual(page.run("getHandValue('south')"), ENDING.south, 'page unchanged');
    page.timers.flush();
    assert.match(decodeURIComponent(page.location.hash), /deal=N:.AJ.K86.7/, 'URL back to the page');
  }
});

test('loadFromUrl: a valid checksum on an invalid deal says what\'s wrong', () => {
  const page = loadPage();
  const hash = page.run(`(() => { const p = new URLSearchParams();
    p.set('deal', 'N:AK.2.2.2 AK.3.3.3 Q.4.4.4 J.5.5.5'); p.set('sum', urlChecksum(p));
    return '#' + p.toString(); })()`);
  page.changeHash(hash);
  assert.strictEqual(page.byId('status').textContent, "Couldn't use the deal in the link: Duplicate card: SA");
});

test('hashchange: a link opened while busy waits, and stays in the address bar', () => {
  const hash = linkFor(FULL, { vul: 'All' });
  const page = loadPage();
  page.run('urlLoaded = true; workerReady = true; setBusyState(true)');
  page.byId('status').textContent = 'Solving…';
  page.changeHash(hash);
  page.changeHash(hash);
  assert.strictEqual(page.byId('status').textContent, 'Solving… The new link opens when this finishes.');
  page.run('updateUrl()');
  page.timers.flush();
  assert.strictEqual(page.location.hash, hash, 'not overwritten while waiting');
  page.run('setBusyState(false)');
  assert.strictEqual(page.run('vulnerability'), 'All');
  assert.strictEqual(page.byId('status').textContent, 'Loaded deal from link.');
});

test('hashchange: clearing the URL puts back the page\'s link', () => {
  const hash = linkFor(FULL, { vul: 'All' });
  const page = loadPage({ hash });
  page.changeHash('');
  page.timers.flush();
  assert.strictEqual(page.location.hash, hash);
});

test('a fresh visit without a link keeps the URL clean', () => {
  const page = loadPage();
  page.timers.flush();
  assert.strictEqual(page.location.hash, '');
  assert.strictEqual(page.replaceStateCalls.length, 0);
});

runTests();
