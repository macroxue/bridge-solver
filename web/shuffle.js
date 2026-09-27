// Single-dummy shuffles: the shuffle worker pool's results, merged across
// batches, and the two shuffle tables. Loaded after app.js.

// Tables show the top 7 trick counts: 7+ to 13+ for a full deal, matching
// shuffle.py's CLI default, and e.g. 1+ to 6+ for a 6-card ending.
const SHUFFLE_COLUMNS = 7;

// Shuffle results accumulate across button presses (10 then 100 -> 110
// total) as long as the hands being shuffled haven't changed since;
// `shuffleHandsKey` is what detects that they have.
let shuffleAccumulator = null;
let shuffleHandsKey = null;

// Cards per hand in the shuffled deal; shuffleHandsKey starts with West's.
function shuffleHandLength() {
  return shuffleHandsKey ? parseHandCards(shuffleHandsKey.split('|')[0]).length : 13;
}

function clearShuffleResults() {
  shuffleAccumulator = null;
  shuffleHandsKey = null;
  shuffleFolded.ew = false;
  shuffleFolded.ns = false;
  shuffleTableEl.innerHTML = '';
}

function mergeShuffleData(acc, batch) {
  if (!acc) return batch;
  const merged = {
    rounds: acc.rounds + batch.rounds,
    elapsedMs: acc.elapsedMs + batch.elapsedMs,
    ew: {},
    ns: {},
  };
  for (const key of ['ew', 'ns']) {
    const counts = {};
    for (const d of ['S', 'N', 'W', 'E'])
      counts[d] = acc[key].counts[d].map((row, r) => row.map((v, t) => v + batch[key].counts[d][r][t]));
    merged[key] = { counts };
  }
  return merged;
}

// A shuffle batch currently in flight across the pool (null when idle).
// Each pool worker gets its own slice of the requested round count (see
// runShuffleBatch()) and reports back independently; this folds their
// partial results together the same way mergeShuffleData() already folds
// separate button-press batches, and tracks combined progress/timing.
let pendingBatch = null;

function workersLabel(n) {
  return ` (${n} worker${n === 1 ? '' : 's'})`;
}

// Dispatch always uses a prefix shuffleWorkers[0..workersToUse-1] of the
// pool (see runShuffleBatch()), so a message's workerIndex here is always
// a valid index into pendingBatch.doneByWorker.
// Messages from shuffle pool worker workerIndex (see startup.js).
function onShuffleWorkerMessage(workerIndex, event) {
  const [type, ...rest] = event.data;
  switch (type) {
    case 'ready':
      ++shuffleWorkersReadyCount;
      updateShuffleBtns();
      maybeReportReady();
      break;
    case 'abort':
      // As for the solver: no more Shuffle, and no waiting link.
      shuffleWorkersReadyCount = 0;
      deferredUrlHash = null;
      statusEl.textContent = 'Solver crashed: ' + rest[0];
      pendingBatch = null;
      setBusyState(false);
      break;
    case 'error': {
      const [, message] = rest;
      statusEl.textContent = 'Solver error: ' + message;
      pendingBatch = null;
      setBusyState(false);
      break;
    }
    case 'shuffle_progress': {
      if (!pendingBatch) break;
      const [done] = rest;
      pendingBatch.doneByWorker[workerIndex] = done;
      const totalDone = pendingBatch.doneByWorker.reduce((a, b) => a + b, 0);
      statusEl.textContent = `Shuffling… ${totalDone}/${pendingBatch.totalUnits}${workersLabel(pendingBatch.workers)}` +
        (deferredUrlHash !== null ? LINK_WAITING_NOTE : '');
      break;
    }
    case 'shuffle': {
      if (!pendingBatch) break;
      const [data] = rest;
      pendingBatch.merged = mergeShuffleData(pendingBatch.merged, data);
      if (--pendingBatch.workersRemaining > 0) break;
      // Wall-clock time for the whole parallel batch, not the sum of each
      // worker's own elapsedMs (which ran concurrently, not back to back).
      pendingBatch.merged.elapsedMs = performance.now() - pendingBatch.startTime;
      shuffleAccumulator = mergeShuffleData(shuffleAccumulator, pendingBatch.merged);
      renderShuffleTable(shuffleAccumulator);
      growTablesColumnMinWidth();
      const { workers } = pendingBatch;
      pendingBatch = null;
      const { rounds, elapsedMs } = shuffleAccumulator;
      const elapsedS = (elapsedMs / 1000).toFixed(1);
      statusEl.textContent = `Shuffled ${rounds} times each way in ${elapsedS} s${workersLabel(workers)}.`;
      setBusyState(false);
      break;
    }
  }
}

// Stats shown in the shuffle tables, from counts[t] = shuffles taking exactly
// t tricks: the average, and pct[t] = % taking t or more, rounded down.
function trickStats(counts) {
  const total = counts.reduce((a, b) => a + b, 0);
  const avg = counts.reduce((a, n, t) => a + n * t, 0) / total;
  const pct = [];
  let atLeast = 0;
  for (let t = 13; t >= 0; --t) {
    atLeast += counts[t];
    pct[t] = Math.floor(atLeast * 100 / total);
  }
  return { avg, pct };
}

// Per-declarer trick counts by strain, as counts[d][strain]; N/S declarers'
// come from shuffling E/W, and vice versa.
function singleDummyCounts(data) {
  const strains = ['N', 'S', 'H', 'D', 'C'];
  const counts = {};
  for (const [d, key] of [['S', 'ew'], ['N', 'ew'], ['W', 'ns'], ['E', 'ns']]) {
    counts[d] = {};
    strains.forEach((strain, row) => { counts[d][strain] = data[key].counts[d][row]; });
  }
  return counts;
}

// Which shuffle sections are folded, keyed by the same 'ew'/'ns' dir used
// as each <h3>'s data-dir -- persists across re-renders (accumulating a
// batch, or toggling a fold) since renderShuffleTable() always rebuilds
// from scratch and reads this each time.
const shuffleFolded = { ew: false, ns: false };

// Renders the two shuffle.py-style tables: shuffling E/W (with N/S fixed)
// shows how South/North's actual hands fare against random opponents, and
// vice versa for shuffling N/S. Each title is clickable to fold/unfold its
// own table (see the click listener below).
function renderShuffleTable(data) {
  const strains = ['N', 'S', 'H', 'D', 'C'];
  const tricksList = [];
  const n = shuffleHandLength();
  const ending = n < 13;
  for (let t = Math.max(1, n - SHUFFLE_COLUMNS + 1); t <= n; ++t) {
    tricksList.push(t);
  }
  const counts = ending ? null : singleDummyCounts(data);
  // As in the DD table, an ending's columns are the player on lead, with the
  // side on lead's tricks, taken from its declarer (the leader's right-hand
  // opponent) in the same shuffles.
  const columnCounts = (dir, seat, strain) => {
    if (!ending) return counts[seat][strain];
    const declarer = prevSeat(SEAT_NAME_BY_LETTER[seat])[0].toUpperCase();
    const c = data[dir].counts[declarer][strains.indexOf(strain)];
    return c.map((_, t) => (t <= n ? c[n - t] : 0));
  };

  // Both declarers stay in one table, side by side in each cell as
  // "left/right", so they're still directly comparable at a glance -- but
  // collapsed to one number when they don't differ (common at the high/low
  // ends of the trick range), which also keeps cells narrow enough to fit
  // phone widths without horizontal scrolling.
  const pair = (left, right) => (left === right ? `${left}` : `${left}/${right}`);

  function section(dir, title, leftKey, rightKey) {
    const folded = shuffleFolded[dir];
    const arrow = FOLD_ARROW(folded);
    let html = `<h3 data-dir="${dir}">${arrow} ${title}</h3>`;
    if (folded) return html;

    html += `<table><tr><th>${leftKey}/${rightKey}${ending ? ' lead' : ''}</th><th>avg</th>`;
    for (const t of tricksList) html += `<th>${t}+</th>`;
    html += '</tr>';
    for (const strain of strains) {
      const left = trickStats(columnCounts(dir, leftKey, strain));
      const right = trickStats(columnCounts(dir, rightKey, strain));
      html += `<tr><td>${STRAIN_LABELS[strain]}</td>` +
        `<td>${pair(left.avg.toFixed(1), right.avg.toFixed(1))}</td>`;
      for (const t of tricksList) {
        const leftPct = left.pct[t], rightPct = right.pct[t];
        const cell = leftPct === 0 && rightPct === 0 ? '' : pair(leftPct, rightPct);
        html += `<td>${cell}</td>`;
      }
      html += '</tr>';
    }
    return html + '</table>';
  }

  shuffleTableEl.innerHTML =
    section('ew', 'Single-dummy table: Shuffling East/West.', 'S', 'N') +
    section('ns', 'Single-dummy table: Shuffling North/South.', 'W', 'E') +
    // Par only makes sense for a full deal.
    (!ending ? '<p class="par" id="sdPar"></p>' : '');
  renderPar();
}

// Delegated (not attached per-<h3>) since renderShuffleTable() replaces
// those elements outright on every render.
shuffleTableEl.addEventListener('click', (event) => {
  const h3 = event.target.closest('h3');
  if (!h3 || !shuffleAccumulator) return;
  shuffleFolded[h3.dataset.dir] = !shuffleFolded[h3.dataset.dir];
  renderShuffleTable(shuffleAccumulator);
});

function runShuffleBatch(rounds) {
  const hands = {};
  for (const seat of SEATS) hands[seat] = getHandValue(seat);

  const error = validateHands(hands);
  if (error) {
    statusEl.textContent = error;
    return;
  }
  // solve()/shuffle_and_solve()'s hand parsing assumes single-char ranks
  // (no "10", no "X" wildcards); resolveWildcards() normalizes both
  // unconditionally.
  const resolvedHands = resolveWildcards(hands);

  // Results accumulate across presses, but only for the same hands -- reset
  // if they've changed (new deal, edited card, etc.) since the last batch.
  const handsKey = SEATS.map(seat => resolvedHands[seat]).join('|');
  if (handsKey !== shuffleHandsKey) {
    clearShuffleResults();
    shuffleHandsKey = handsKey;
  }

  exitPlay();
  setBusyState(true);

  // Split rounds as evenly as possible across the pool -- capped to
  // `rounds` itself so a small batch (e.g. Shuffle 10 on a many-core
  // machine) doesn't spin up workers that would get 0 rounds.
  const workersToUse = Math.min(shuffleWorkers.length, rounds);
  const baseRounds = Math.floor(rounds / workersToUse);
  const extraRounds = rounds % workersToUse;

  pendingBatch = {
    totalUnits: rounds * 2, // *2 for the ew/ns directions, like shuffle-worker.js's own `total`
    doneByWorker: new Array(workersToUse).fill(0),
    merged: null,
    workers: workersToUse,
    workersRemaining: workersToUse,
    startTime: performance.now(),
  };

  statusEl.textContent = `Shuffling… 0/${pendingBatch.totalUnits}${workersLabel(workersToUse)}`;
  for (let i = 0; i < workersToUse; ++i) {
    const workerRounds = baseRounds + (i < extraRounds ? 1 : 0);
    shuffleWorkers[i].postMessage(['shuffle', resolvedHands, workerRounds]);
  }
}

shuffle10Btn.addEventListener('click', () => runShuffleBatch(10));
shuffle100Btn.addEventListener('click', () => runShuffleBatch(100));
