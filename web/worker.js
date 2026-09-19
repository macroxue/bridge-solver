const errors = [];

Module = {
  'onRuntimeInitialized': function() { postMessage(['ready']); },
  'onAbort': function() { postMessage(['abort', errors.join('\n')]); errors.length = 0; },
  'printErr': function(err) {
    errors.push('[' + (errors.length + 1) + '] ' + err);
    console.log('[solver]', err);
  },
};

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
