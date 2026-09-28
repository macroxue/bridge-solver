// Loaded after app.js and play.js; startup.js loads any link as the page opens.

// --- Shareable URL ---
// The URL hash mirrors the deal, vulnerability, DD table and any play in
// progress, e.g.
//   #deal=<PBN>&vul=N-S&dd=<20 hex digits>&play=NE&cards=CASQ&sum=<checksum>
// (notrump by East, with CA and SQ played), so a copied link reopens the same
// position. dd, the table's trick counts, lets a link show it without
// re-solving; play is only ever written with it. Links are made by the page,
// not by hand: sum rejects edited or truncated ones.
let urlLoaded = false;
const DD_STRAINS = ['N', 'S', 'H', 'D', 'C'];

function encodeDd(lines) {
  return lines.map(line => line.trim().split(/\s+/).slice(1, 5)
    .map(n => Number(n).toString(16)).join('')).join('');
}

// Back to solve()'s "<strain> <S> <N> <W> <E>" lines, or null if malformed.
function decodeDd(dd, numTricks) {
  if (!/^[0-9a-d]{20}$/i.test(dd)) return null;
  const tricks = [...dd].map(c => parseInt(c, 16));
  if (tricks.some(t => t > numTricks)) return null;
  return DD_STRAINS.map((strain, i) => `${strain} ${tricks.slice(i * 4, i * 4 + 4).join(' ')}`).join('\n');
}

// Debounced, as some actions call it many times (e.g. holding Up/Down on the
// Vul select, or typing a hand) and browsers throttle replaceState.
let urlTimer = null;
function updateUrl() {
  if (!urlLoaded) return;
  clearTimeout(urlTimer);
  urlTimer = setTimeout(writeUrl, 200);
}

function writeUrl() {
  // Leave a link waiting on a solve or shuffle in the address bar.
  if (deferredUrlHash !== null) return;
  const hands = {};
  for (const seat of SEATS) hands[seat] = getHandValue(seat);
  const params = new URLSearchParams();
  // Without a valid deal there's nothing to link to.
  const deal = validateHands(hands) ? null : formatPBN(hands);
  if (deal) {
    params.set('deal', deal);
    if (vulnerability !== 'None') params.set('vul', vulnerability);
    if (ddLines.length && ddDeal === deal) params.set('dd', encodeDd(ddLines));
    if (playState) {
      params.set('play', playState.strain + playState.declarer[0].toUpperCase());
      if (playState.history.length) params.set('cards', playState.history.map(cardStr).join(''));
    }
    params.set('sum', urlChecksum(params));
  }
  const hash = deal ? '#' + params.toString() : '';
  if (hash === location.hash) return;
  // Safari throws past ~100 calls per 30 s; the next update carries the full
  // state anyway.
  try {
    history.replaceState(null, '', hash || location.pathname + location.search);
  } catch (e) {
    console.warn('URL not updated:', e);
  }
}

// Checks a link's sum, so a damaged or hand-edited link is rejected instead
// of loading part of it (e.g. a play without the DD table it needs).
function loadFromUrl(hash = location.hash) {
  urlLoaded = true;
  // A plain URL gets the link to the page's default deal.
  if (!hash) {
    updateUrl();
    return;
  }
  // A rejected link leaves the page as it was, so the URL goes back to it.
  const reject = (message) => {
    statusEl.textContent = message;
    updateUrl();
  };
  const params = new URLSearchParams(hash.slice(1));
  const deal = params.get('deal');
  const hands = deal && parsePBN(deal);
  if (params.get('sum') !== urlChecksum(params) || !hands || SEATS.some(seat => !hands[seat])) {
    reject('This link is damaged or incomplete.');
    return;
  }
  const error = validateHands(hands);
  if (error) {
    reject(`Couldn't use the deal in the link: ${error}`);
    return;
  }
  const vul = params.get('vul');
  vulnerability = VULNERABILITIES.includes(vul) ? vul : 'None';
  const pbn = formatPBN(hands);
  applyPastedHands(hands);  // Also shows the vulnerability.
  pasteBoxEl.value = pbn;
  const dd = decodeDd(params.get('dd') || '', handLength(hands));
  if (!dd) {
    statusEl.textContent = 'Loaded deal from link.';
    return;
  }
  // Shown as given; Solve recomputes it.
  ddDeal = pbn;
  showDdTable(dd);
  statusEl.textContent = 'Loaded deal and DD table from link.';
  const play = params.get('play') || '';
  if (/^[NSHDC][NSEW]$/.test(play)) resumeUrlPlay(play, params.get('cards') || '');
}

// 32-bit FNV-1a in base 36 of the params other than sum: catches typos and
// truncation, not tampering. Over decoded values, so a link survives apps
// that re-encode it (e.g. %3A back to ':').
function urlChecksum(params) {
  const text = [...params].filter(([key]) => key !== 'sum').map(([key, value]) => `${key}=${value}`).join('&');
  let h = 0x811c9dc5;
  for (let i = 0; i < text.length; ++i) {
    h ^= text.charCodeAt(i);
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  return h.toString(36);
}

// Starts the link's contract (e.g. play "DN" for diamonds by North) from its
// DD table, with its cards played.
function resumeUrlPlay(play, cards) {
  const strain = play[0], declarer = SEAT_NAME_BY_LETTER[play[1]];
  const cell = tableEl.querySelector(`td.pick[data-strain="${strain}"][data-declarer="${declarer}"]`);
  if (!cell) return;
  startPlay(strain, declarer, Number(cell.dataset.tricks), cards);
}

window.addEventListener('hashchange', () => {
  if (!location.hash) {
    // Cleared by hand: put back the link to what the page shows.
    updateUrl();
  } else if (busy) {
    deferredUrlHash = location.hash;
    if (!statusEl.textContent.endsWith(LINK_WAITING_NOTE)) statusEl.textContent += LINK_WAITING_NOTE;
  } else {
    loadFromUrl();
  }
});

// Typed edits don't go through updateUrl() otherwise.
for (const seat of SEATS) {
  for (const suit of SUIT_LETTERS) {
    document.getElementById(seat + '-' + suit).addEventListener('input', updateUrl);
  }
}
