// Startup, loaded last: the workers are created only once every script has
// loaded, as their messages can arrive between two scripts and their
// handlers use code from all of them. Then the page loads any link.

// Solve/solve_plays run in `worker`; Shuffle runs across a pool of
// `shuffleWorkers` (each its own wasm instance/memory, so their
// discard_suit_bottom fast-solve caches can never be reused by
// solve_plays()'s exact-precision cache), so a batch's rounds run across
// the machine's cores in parallel instead of one at a time.
const worker = new Worker('worker.js?v=36be9e64');
// hardwareConcurrency counts logical (SMT) threads; solving is CPU/cache-
// bound, and README.md's own multi-core benchmark shows SMT buys almost
// nothing for it (8 physical cores -> 16 SMT threads only takes the
// speed-up from 5.2x to 6.3x), so target physical cores (/2, the common
// case on x86) rather than one worker per logical thread -- that
// benchmark says nothing about scaling across *more* physical cores, so
// this isn't otherwise capped. A browser without hardwareConcurrency at
// all is likely on old, low-core hardware, hence the low (not 8) fallback.
// ?workers=N overrides the default, for timing pool sizes per device.
const workersParam = parseInt(new URLSearchParams(location.search).get('workers'), 10);
const SHUFFLE_WORKER_COUNT = workersParam > 0 ? workersParam
  : Math.max(1, Math.floor((navigator.hardwareConcurrency || 4) / 2));
const shuffleWorkers =
  Array.from({ length: SHUFFLE_WORKER_COUNT }, () => new Worker('shuffle-worker.js?v=1ceb2a95'));

worker.onmessage = onSolverMessage;
shuffleWorkers.forEach((shuffleWorker, workerIndex) => {
  shuffleWorker.onmessage = (event) => onShuffleWorkerMessage(workerIndex, event);
});

// Leaving the page frees the workers' wasm memories now. Firefox otherwise
// holds a left page's memories for a while, and a few quick visits ran a
// 32-bit tablet out of address space for new ones.
window.addEventListener('pagehide', () => {
  worker.terminate();
  shuffleWorkers.forEach(shuffleWorker => shuffleWorker.terminate());
});
// Back to a page kept in the back/forward cache: its workers are gone, so
// load it afresh (from its link).
window.addEventListener('pageshow', (event) => {
  if (event.persisted) location.reload();
});

loadFromUrl();
