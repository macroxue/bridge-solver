// Solves deal files with the web build's wasm, printing each deal's name and
// results like ../solver -m0. Run with: node run.js <simd|nosimd> <deal file>...
'use strict';

const fs = require('fs');
const path = require('path');
const { parseDealFile } = require('./deal-parser.js');

const [variant, ...files] = process.argv.slice(2);

// solver.js picks the wasm by the global simd flag, as set in worker.js, and
// asks for it by a cache-busted URL, which Node would read as a file name.
globalThis.simd = variant !== 'nosimd';
const solverJs = path.join(__dirname, 'solver.js');
const source = fs.readFileSync(solverJs, 'utf8').replace(/\?v=[0-9a-f]+/g, '');
const solverModule = { exports: {} };
new Function('require', 'module', '__dirname', '__filename', source)(
  require, solverModule, __dirname, solverJs);

const Module = solverModule.exports;
Module.onRuntimeInitialized = () => {
  for (const file of files) {
    try {
      const hands = parseDealFile(fs.readFileSync(file, 'utf8'));
      const result = Module.solve(hands.west, hands.north, hands.east, hands.south);
      process.stdout.write(`${path.basename(file)}\n${result}`);
    } catch (e) {
      console.error(`${file}: ${e.message}`);
    }
  }
};
