const errors = [];

Module = {
  'onRuntimeInitialized': function() { postMessage(['ready']); },
  'onAbort': function() { postMessage(['abort', errors.join('\n')]); errors.length = 0; },
  'printErr': function(err) {
    errors.push('[' + (errors.length + 1) + '] ' + err);
    console.log('[solver]', err);
  },
};

// Splits a "SSSS HHHH DDDD CCCC" hand string (single-char ranks, '-' for
// void, as normalized by app.js's resolveWildcards) into 4 arrays of rank
// characters, one per suit.
function parseHandTokens(value) {
  const suits = value.trim().length ? value.trim().split(/\s+/) : [];
  return [0, 1, 2, 3].map(i => (!suits[i] || suits[i] === '-' ? [] : suits[i].split('')));
}

function buildHandString(bySuit) {
  return bySuit.map(ranks => (ranks.length ? ranks.join('') : '-')).join(' ');
}

// Pools the given seats' cards together and redeals them randomly and
// evenly, mirroring Hands::Shuffle in solver.cc.
function shuffleSeats(parsedHands, seats) {
  const pool = [];
  for (const seat of seats) {
    parsedHands[seat].forEach((ranks, suit) => ranks.forEach(rank => pool.push({ suit, rank })));
  }
  for (let i = pool.length - 1; i > 0; --i) {
    const j = Math.floor(Math.random() * (i + 1));
    [pool[i], pool[j]] = [pool[j], pool[i]];
  }
  const perSeat = pool.length / seats.length;
  const shuffled = {};
  seats.forEach((seat, idx) => {
    const bySuit = [[], [], [], []];
    for (const { suit, rank } of pool.slice(idx * perSeat, (idx + 1) * perSeat)) bySuit[suit].push(rank);
    shuffled[seat] = bySuit;
  });
  return shuffled;
}

// Parses solve()'s "<strain> <S> <N> <W> <E> <time> s" lines (see
// DECLARER_COLUMNS in app.js for why the 4 numbers land in that order).
function parseSolveResult(result) {
  return result.trim().split('\n').map(line => {
    const p = line.trim().split(/\s+/);
    return { S: +p[1], N: +p[2], W: +p[3], E: +p[4] };
  });
}

// Runs `rounds` shuffled solves for each of {shuffle E/W, shuffle N/S},
// holding the other pair's hands fixed, and returns per-strain sums and
// making-N-or-more histograms for each declarer -- the same aggregation
// shuffle.py does over many `solver -s ... -d` subprocess calls, just
// in-process and sequential (one solve() call at a time on this worker).
function runShuffle(hands, rounds, minTricks, onProgress) {
  const parsedHands = {};
  for (const seat of ['west', 'north', 'east', 'south']) parsedHands[seat] = parseHandTokens(hands[seat]);

  const directions = [
    { key: 'ew', shuffled: ['west', 'east'] },
    { key: 'ns', shuffled: ['north', 'south'] },
  ];
  const total = rounds * directions.length;
  let done = 0;

  const results = {};
  for (const { key, shuffled } of directions) {
    const sums = { S: [0, 0, 0, 0, 0], N: [0, 0, 0, 0, 0], W: [0, 0, 0, 0, 0], E: [0, 0, 0, 0, 0] };
    const histo = {};
    for (const d of ['S', 'N', 'W', 'E']) {
      histo[d] = [];
      for (let row = 0; row < 5; ++row) {
        histo[d][row] = {};
        for (let t = minTricks; t <= 13; ++t) histo[d][row][t] = 0;
      }
    }
    for (let round = 0; round < rounds; ++round) {
      const merged = { ...parsedHands, ...shuffleSeats(parsedHands, shuffled) };
      const result = Module.solve(buildHandString(merged.west), buildHandString(merged.north),
        buildHandString(merged.east), buildHandString(merged.south));
      parseSolveResult(result).forEach((row, r) => {
        for (const d of ['S', 'N', 'W', 'E']) {
          sums[d][r] += row[d];
          for (let t = minTricks; t <= 13; ++t) if (row[d] >= t) ++histo[d][r][t];
        }
      });
      if (onProgress && (++done % 5 === 0 || done === total)) onProgress(done, total);
    }
    results[key] = { sums, histo };
  }
  return { rounds, minTricks, ew: results.ew, ns: results.ns };
}

onmessage = function(event) {
  const [type, ...args] = event.data;
  try {
    if (type === 'solve') {
      const [hands] = args;
      const start = performance.now();
      const result = Module.solve(hands.west, hands.north, hands.east, hands.south);
      const elapsedMs = performance.now() - start;
      postMessage(['solve', result.trim(), elapsedMs]);
    } else if (type === 'solve_plays') {
      const [hands, level, trump, leadSeat, played, requestId] = args;
      const start = performance.now();
      const result = Module.solve_plays(hands.west, hands.north, hands.east, hands.south,
        level, trump, leadSeat, played);
      const elapsedMs = performance.now() - start;
      postMessage(['solve_plays', result.trim(), elapsedMs, requestId]);
    } else if (type === 'shuffle') {
      const [hands, rounds, minTricks] = args;
      const data = runShuffle(hands, rounds, minTricks,
        (done, total) => postMessage(['shuffle_progress', done, total]));
      postMessage(['shuffle', data]);
    }
  } catch (e) {
    const requestId = type === 'solve_plays' ? args[5] : undefined;
    postMessage(['error', type, errors.join('\n'), requestId]);
    errors.length = 0;
  }
};

// Some browsers don't support WASM SIMD, e.g. Firefox on older devices.
// Detect SIMD support here and load the right WASM binary in solver.js.
var simd = true;

fetch('solver.wasm')
  .then(response => response.arrayBuffer())
  .then(bytes => {
    simd = WebAssembly.validate(bytes);
    importScripts('solver.js');
  });
