// A separate worker (and thus a separate wasm instance/memory) from
// worker.js, so this feature's discard_suit_bottom-based fast solves --
// and their approximation-tainted common_bounds_cache/cutoff_cache -- can
// never be reused by solve_plays()'s exact-precision cache in worker.js.
const errors = [];

Module = {
  'onRuntimeInitialized': function() { postMessage(['ready']); },
  'onAbort': function() { postMessage(['abort', errors.join('\n')]); errors.length = 0; },
  'printErr': function(err) {
    errors.push('[' + (errors.length + 1) + '] ' + err);
    console.log('[solver]', err);
  },
};

importScripts('declarer-columns.js');
// Maps DECLARER_COLUMNS' full seat names to the single-letter keys sums/histo
// below use.
const SEAT_LETTER = { south: 'S', north: 'N', west: 'W', east: 'E' };

// Parses solve()'s "<strain> <S> <N> <W> <E> <time> s" lines, using the same
// DECLARER_COLUMNS app.js renders the DD table with as the single source of
// truth for which column is which declarer's trick count.
function parseSolveResult(result) {
  return result.trim().split('\n').map(line => {
    const p = line.trim().split(/\s+/);
    const row = {};
    DECLARER_COLUMNS.forEach((seat, i) => { row[SEAT_LETTER[seat]] = +p[i + 1]; });
    return row;
  });
}

// Runs `rounds` shuffled solves for each of {shuffle E/W, shuffle N/S},
// holding the other pair's hands fixed, and returns per-strain sums and
// making-N-or-more histograms for each declarer -- the same aggregation
// shuffle.py does over many `solver -s ... -d` subprocess calls, just
// in-process and sequential (one shuffle_and_solve() call at a time here).
function runShuffle(hands, rounds, minTricks, onProgress) {
  const start = performance.now();
  const directions = [
    { key: 'ew', seats: 'WE' },
    { key: 'ns', seats: 'NS' },
  ];
  const total = rounds * directions.length;
  let done = 0;

  const results = {};
  for (const { key, seats } of directions) {
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
      // A single bad round (rare edge-case hand, transient wasm error)
      // shouldn't discard every round already completed in this batch --
      // skip it and keep going rather than letting it throw out of runShuffle().
      try {
        const result = Module.shuffle_and_solve(
          hands.west, hands.north, hands.east, hands.south, seats, /*discard_suit_bottom=*/true);
        parseSolveResult(result).forEach((row, r) => {
          for (const d of ['S', 'N', 'W', 'E']) {
            sums[d][r] += row[d];
            for (let t = minTricks; t <= 13; ++t) if (row[d] >= t) ++histo[d][r][t];
          }
        });
      } catch (e) {
        console.error('[shuffle] round failed, skipping:', e);
      }
      if (onProgress && (++done % 5 === 0 || done === total)) onProgress(done, total);
    }
    results[key] = { sums, histo };
  }
  const elapsedMs = performance.now() - start;
  return { rounds, minTricks, elapsedMs, ew: results.ew, ns: results.ns };
}

onmessage = function(event) {
  const [type, ...args] = event.data;
  try {
    if (type === 'shuffle') {
      const [hands, rounds, minTricks] = args;
      const data = runShuffle(hands, rounds, minTricks,
        (done, total) => postMessage(['shuffle_progress', done, total]));
      postMessage(['shuffle', data]);
    }
  } catch (e) {
    postMessage(['error', type, errors.join('\n')]);
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
