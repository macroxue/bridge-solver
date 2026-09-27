// Playing out a contract or ending from a DD table cell, with each card's
// result from solve_plays(). Loaded after app.js.

// --- CARD PLAY ---
// Trump encoding expected by solve_plays (see solver.cc:
// enum { SPADE, HEART, DIAMOND, CLUB, NUM_SUITS, NOTRUMP = NUM_SUITS }).
const TRUMP_NUMBERS = { S: 0, H: 1, D: 2, C: 3, N: 4 };
// Rank value for comparing cards within a suit (A high).
const RANK_VALUE = Object.fromEntries(RANKS.map((r, i) => [r, RANKS.length - i]));

const handsEl = document.getElementById('hands');
const centerEl = document.getElementById('center');
const seatCardsEls = Object.fromEntries(SEATS.map(seat => [seat, document.getElementById(seat + '-cards')]));
const seatLabelEls = Object.fromEntries(SEATS.map(seat => [seat, document.querySelector(`.seat.${seat} label`)]));
const playBarEl = document.getElementById('playBar');
const playStatusEl = document.getElementById('playStatus');
const entryHintEl = document.getElementById('entryHint');
const playHintEl = document.getElementById('playHint');
const PLAY_HINT_TEXT = playHintEl.textContent;
const ENDING_PLAY_HINT_TEXT = 'Each card shows how the side on lead ends up against its target ' +
  '(=, +N, \u2013N) if played and followed by best play.';
const controlsEl = document.getElementById('controls');
const undoBtn = document.getElementById('undo');
const undoTrickBtn = document.getElementById('undoTrick');
const editHandsBtn = document.getElementById('editHands');

// Active play session, or null when not playing. `history` entries are
// tagged `auto: true` when solve_plays left no real choice. `pendingPlays`
// is the card->diff map for the seat on lead, once the worker answers.
let playState = null;
let playRequestId = 0;

function nextSeat(seat) {
  return SEATS[(SEATS.indexOf(seat) + 1) % 4];
}

function prevSeat(seat) {
  return SEATS[(SEATS.indexOf(seat) + 3) % 4];
}

function displayRank(rank) {
  return rank === 'T' ? '10' : rank;
}

// "10" is two characters where every other rank is one; tighten it (.ten,
// see CSS) rather than widening every card box to fit it.
function rankHtml(rank) {
  return rank === 'T' ? '<span class="ten">10</span>' : rank;
}

function cardStr(card) {
  return card.suit + card.rank;
}

// Splits an already-validated hand string into its {suit, rank} cards.
function parseHandCards(value) {
  const suits = value.trim().length ? value.trim().split(/\s+/) : [];
  const cards = [];
  for (let i = 0; i < 4 && i < suits.length; ++i) {
    const token = suits[i];
    if (token === '-') continue;
    for (const rank of parseRanks(token)) cards.push({ suit: SUIT_LETTERS[i], rank });
  }
  return cards;
}

// Mirrors ParseHand() in solver.cc: each X becomes the lowest unclaimed
// rank in its suit, walking seats West/North/East/South and suits
// Spades/Hearts/Diamonds/Clubs, left to right.
function resolveWildcards(hands) {
  const used = new Set();
  const resolved = {};
  for (const seat of SEATS) {
    const suits = hands[seat].trim().length ? hands[seat].trim().split(/\s+/) : [];
    const outSuits = [];
    for (let i = 0; i < 4; ++i) {
      const token = suits[i];
      if (!token || token === '-') {
        outSuits.push('-');
        continue;
      }
      const outRanks = parseRanks(token).map(rank => {
        if (rank !== 'X') {
          used.add(SUIT_LETTERS[i] + rank);
          return rank;
        }
        for (let r = RANKS.length - 1; r >= 0; --r) {
          const candidate = SUIT_LETTERS[i] + RANKS[r];
          if (!used.has(candidate)) {
            used.add(candidate);
            return RANKS[r];
          }
        }
        return rank;  // unreachable: suit already holds all 13 ranks
      });
      outSuits.push(outRanks.join(''));
    }
    resolved[seat] = outSuits.join(' ');
  }
  return resolved;
}

function winningCard(trick, trumpLetter) {
  let best = trick[0];
  for (const card of trick.slice(1)) {
    const beats = card.suit === best.suit
      ? RANK_VALUE[card.rank] > RANK_VALUE[best.rank]
      : card.suit === trumpLetter;
    if (beats) best = card;
  }
  return best;
}

// Replays history to derive whose turn it is, the score, and the current
// trick. Recomputing keeps undo trivial (just shorten history and replay).
function replayState() {
  let seat = playState.leadSeat;
  let nsTricks = 0, ewTricks = 0;
  let trick = [];
  let lastTrick = [];
  for (const play of playState.history) {
    trick.push(play);
    if (trick.length === 4) {
      const winner = winningCard(trick, playState.trumpLetter).seat;
      if (winner === 'north' || winner === 'south') ++nsTricks; else ++ewTricks;
      seat = winner;
      lastTrick = trick;
      trick = [];
    } else {
      seat = nextSeat(seat);
    }
  }
  // Keep showing the just-finished trick until the next one's first card.
  return { seat, nsTricks, ewTricks, trick: trick.length ? trick : lastTrick };
}

function remainingCards(seat) {
  const played = new Set(playState.history.filter(p => p.seat === seat).map(cardStr));
  return playState.hands[seat].filter(c => !played.has(cardStr(c)));
}

// level is clamped to >= 1 in startPlay (tricks-6 can be <1, but there's
// no such bid); shortfall shows how far below that clamped level tricks falls.
function contractLabel(level, tricks, strainHtml) {
  const shortfall = (6 + level) - tricks;
  if (shortfall <= 0) return `${level}${strainHtml}`;
  return `${level}${strainHtml}-${shortfall}`;
}

function renderDiffLabel(diff) {
  const n = Number(diff);
  if (n === 0) return '<span class="diff zero">=</span>';
  if (n > 0) return `<span class="diff plus">+${n}</span>`;
  return `<span class="diff minus">–${-n}</span>`;
}

// `plays` is this seat's card->diff map if it's their turn and answered,
// else null/undefined -> cards render plain and unclickable.
function renderSeatCards(seat, plays) {
  const container = seatCardsEls[seat];
  container.innerHTML = '';
  const bySuit = { S: [], H: [], D: [], C: [] };
  for (const card of remainingCards(seat)) bySuit[card.suit].push(card);
  for (const suit of SUIT_LETTERS) bySuit[suit].sort((a, b) => RANK_VALUE[b.rank] - RANK_VALUE[a.rank]);

  // Pad every row to the same slot count (a void suit's "-" counts as one,
  // and 4 is the floor even if every suit is shorter) so East's
  // right-anchored rows still line up their suit symbols.
  const maxSlots = Math.max(4, ...SUIT_LETTERS.map(suit => bySuit[suit].length || 1));

  for (const suit of SUIT_LETTERS) {
    const cards = bySuit[suit];
    const row = document.createElement('div');
    row.className = 'suit-row';
    const label = document.createElement('span');
    label.className = 'suit-symbol';
    label.innerHTML = STRAIN_LABELS[suit];
    row.appendChild(label);

    if (cards.length === 0) {
      const span = document.createElement('span');
      span.className = 'card';
      span.textContent = '-';
      row.appendChild(span);
    } else if (plays && cardStr(cards[0]) in plays) {
      // This suit is the one the seat may legally play from. Every card is
      // clickable; cards within an equivalent run share the same diff as
      // the run's top card (the only one solve_plays reports directly).
      let prevDiff;
      for (const card of cards) {
        const key = cardStr(card);
        const diff = key in plays ? plays[key] : prevDiff;
        const span = document.createElement('span');
        span.className = 'card playable';
        span.innerHTML = rankHtml(card.rank) + renderDiffLabel(diff);
        span.onclick = () => playCard(seat, card);
        row.appendChild(span);
        prevDiff = diff;
      }
    } else {
      for (const card of cards) {
        const span = document.createElement('span');
        span.className = 'card';
        span.innerHTML = rankHtml(card.rank);
        row.appendChild(span);
      }
    }
    for (let i = (cards.length || 1); i < maxSlots; i++) {
      const span = document.createElement('span');
      span.className = 'card pad';
      row.appendChild(span);
    }
    container.appendChild(row);
    // Only fade the edge when the row actually overflows.
    if (row.scrollWidth > row.clientWidth) row.classList.add('scrollable');
  }
}

function renderTrickCenter(trick) {
  centerEl.classList.add('trick-grid');
  centerEl.innerHTML = '<div class="logo"><img class="center-logo" src="favicon.svg" alt=""></div>';
  for (const seat of SEATS) {
    const div = document.createElement('div');
    const playIndex = trick.findIndex(p => p.seat === seat);
    const play = playIndex >= 0 ? trick[playIndex] : null;
    div.className = seat[0] + (play ? ' played' : '');
    div.innerHTML = play
      ? STRAIN_LABELS[play.suit] + '<span class="gap"></span>' + rankHtml(play.rank)
      : '';
    // Grid position is fixed by seat (n/w/e/s), not play order, so when
    // adjacent cards overlap at narrow widths, stack later plays on top.
    if (play) div.style.zIndex = playIndex + 1;
    centerEl.appendChild(div);
  }
}

// West/East's cards are anchored to their column's left/right edge, at a
// position that shifts with the longest suit in the hand (see maxSlots in
// renderSeatCards); nudge the label to sit (plus 1em) past the 2nd card of
// the top suit row so it tracks that shift instead of the label's own
// centering.
function positionSideLabel(seat) {
  const label = seatLabelEls[seat];
  label.style.transform = '';
  const slots = seatCardsEls[seat].querySelector('.suit-row').querySelectorAll('.card');
  if (slots.length < 2) return;
  const labelRect = label.getBoundingClientRect();
  const targetRect = slots[1].getBoundingClientRect();
  const em = parseFloat(getComputedStyle(label).fontSize);
  const shift = (targetRect.left + targetRect.width / 2) - (labelRect.left + labelRect.width / 2) + em;
  label.style.transform = `translateX(${shift}px)`;
}

function renderPlay() {
  updateUrl();
  const state = replayState();
  for (const seat of SEATS) {
    renderSeatCards(seat, seat === state.seat ? playState.pendingPlays : null);
  }
  positionSideLabel('west');
  positionSideLabel('east');
  renderTrickCenter(state.trick);

  const declarerLabel = playState.declarer[0].toUpperCase() + playState.declarer.slice(1);
  const done = playState.history.length === 4 * playState.numTricks;
  // An ending has no contract; state it like a problem instead: trumps, who
  // leads, and the tricks the side on lead can take.
  const leadLabel = playState.leadSeat[0].toUpperCase() + playState.leadSeat.slice(1);
  const trumpLabel = playState.strain === 'N' ? 'NT' : `${STRAIN_LABELS[playState.strain]} trump`;
  const contract = playState.numTricks === 13
    ? `${contractLabel(playState.level, playState.tricks, STRAIN_LABELS[playState.strain])} by ${declarerLabel}`
    : `${trumpLabel}, ${leadLabel} to lead, ${playState.numTricks - playState.tricks} of ${playState.numTricks}`;
  playStatusEl.innerHTML =
    `<div>${contract}${done ? ' (final)' : ''}</div>` +
    `<div>NS ${state.nsTricks} &ndash; EW ${state.ewTricks}</div>`;

  undoBtn.disabled = !playState.history.some(p => !p.auto);
  undoTrickBtn.disabled = playState.history.length === 0;
}

// Set when play starts (e.g. from a link) before the solver has loaded.
let playsOnReady = false;

function requestPlays() {
  playsOnReady = !workerReady;
  if (playsOnReady) return;
  ++playRequestId;
  playState.requestId = playRequestId;
  const playedStr = playState.history.map(cardStr).join('');
  worker.postMessage(['solve_plays', playState.handStrings, playState.targetTricks,
    TRUMP_NUMBERS[playState.strain], SEATS.indexOf(playState.leadSeat), playedStr,
    playState.requestId]);
}

function playCardInternal(seat, card, auto) {
  playState.history.push({ seat, suit: card.suit, rank: card.rank, auto });
  playState.pendingPlays = null;
  playState.autoPlay = true;
  renderPlay();
  if (playState.history.length < 4 * playState.numTricks) requestPlays();
}

function playCard(seat, card) {
  if (!playState) return;
  playCardInternal(seat, card, false);
}

function onSolvePlays(result, requestId) {
  if (!playState || requestId !== playState.requestId) return;  // stale response
  const plays = {};
  for (const token of result.trim().split(/\s+/)) {
    if (!token) continue;
    const [card, diff] = token.split(':');
    // An ending is scored for the side on lead, i.e. against declarer.
    plays[card] = playState.numTricks === 13 ? diff : String(-Number(diff));
  }
  const cardStrs = Object.keys(plays);
  if (cardStrs.length === 0) return;
  const state = replayState();
  if (cardStrs.length === 1 && playState.autoPlay) {
    // Only one meaningful choice -- play it automatically, except right
    // after an undo, which would otherwise just replay it.
    const c = cardStrs[0];
    playCardInternal(state.seat, { suit: c[0], rank: c[1] }, true);
  } else {
    playState.pendingPlays = plays;
    renderPlay();
  }
}

function undo() {
  if (!playState) return;
  // Undo trailing auto-plays too, so one click lands back at a real choice.
  while (playState.history.length > 0 && playState.history[playState.history.length - 1].auto) {
    playState.history.pop();
  }
  if (playState.history.length > 0) playState.history.pop();
  playState.pendingPlays = null;
  playState.autoPlay = false;
  renderPlay();
  requestPlays();
}

// Rewinds to the start of the trick currently in progress, or the last
// completed trick if none is (undo() rewinds one card at a time; this is
// the fast, trick-at-a-time version).
function undoTrick() {
  if (!playState || playState.history.length === 0) return;
  const trickStart = Math.floor((playState.history.length - 1) / 4) * 4;
  playState.history.length = trickStart;
  playState.pendingPlays = null;
  playState.autoPlay = false;
  renderPlay();
  requestPlays();
}

function setPlayModeUI(active) {
  playBarEl.style.display = active ? 'flex' : 'none';
  playStatusEl.style.display = active ? 'block' : 'none';
  entryHintEl.style.display = active ? 'none' : '';
  document.getElementById('pasteBar').style.display = active ? 'none' : '';
  playHintEl.style.display = active ? 'block' : 'none';
  controlsEl.style.display = active ? 'none' : 'flex';
  // Deal entry also stays locked while a solve or shuffle is running.
  setEntryDisabled(active || busy);
  updateSolveBtn();
}

function exitPlay() {
  if (!playState) return;
  playState = null;
  handsEl.classList.remove('playing');
  centerEl.classList.remove('trick-grid');
  centerEl.innerHTML = '<img class="center-logo" src="favicon.svg" alt="">';
  seatLabelEls.west.style.transform = '';
  seatLabelEls.east.style.transform = '';
  setPlayModeUI(false);
  updateUrl();
}

// `cards` (e.g. "CASQ", from a link) are played first, stopping at the first
// one that isn't legal, so the solver is only asked about the position after.
function startPlay(strain, declarer, tricks, cards = '') {
  const hands = {};
  for (const seat of SEATS) hands[seat] = getHandValue(seat);

  const error = validateHands(hands);
  if (error) {
    statusEl.textContent = error;
    return;
  }
  // The table's tricks (and, for an ending, its columns) belong to the deal
  // it was solved for.
  if (formatPBN(hands) !== ddDeal) {
    statusEl.textContent = 'The hands changed since the table was solved. Solve again to play.';
    return;
  }
  // Card play needs concrete cards, so resolve any X wildcards first.
  const resolvedHands = SEATS.some(seat => /x/i.test(hands[seat])) ? resolveWildcards(hands) : hands;

  const parsedHands = {};
  for (const seat of SEATS) parsedHands[seat] = parseHandCards(resolvedHands[seat]);

  const numTricks = handLength(resolvedHands);
  playState = {
    handStrings: resolvedHands,
    hands: parsedHands,
    strain,
    trumpLetter: strain === 'N' ? null : strain,
    level: Math.max(1, tricks - 6),
    tricks,
    // Declarer's target for solve_plays(): the contract (at least 1-level,
    // even when declarer falls short), or for an ending, its DD tricks.
    targetTricks: numTricks === 13 ? Math.max(7, tricks) : tricks,
    numTricks,
    declarer,
    leadSeat: nextSeat(declarer),
    history: [],
    pendingPlays: null,
    // Off for the first answer after an undo, so it doesn't just replay.
    autoPlay: true,
    requestId: 0,
  };

  for (const card of cards.toUpperCase().match(/[SHDC][2-9TJQKA]/g) || []) {
    const history = playState.history;
    const { seat } = replayState();
    const remaining = remainingCards(seat);
    if (!remaining.some(c => cardStr(c) === card)) break;
    const led = history.length % 4 ? history[history.length - history.length % 4].suit : null;
    if (led && card[0] !== led && remaining.some(c => c.suit === led)) break;
    history.push({ seat, suit: card[0], rank: card[1], auto: false });
  }

  handsEl.classList.add('playing');
  playHintEl.textContent = playState.numTricks === 13 ? PLAY_HINT_TEXT : ENDING_PLAY_HINT_TEXT;
  setPlayModeUI(true);

  renderPlay();
  if (playState.history.length < 4 * playState.numTricks) requestPlays();
}

undoBtn.addEventListener('click', undo);
undoTrickBtn.addEventListener('click', undoTrick);
editHandsBtn.addEventListener('click', exitPlay);
