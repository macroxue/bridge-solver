# Bridge double dummy solver — web demo

This is a browser front end for the [double dummy solver](../README.md),
compiled to WebAssembly (WASM). It runs entirely client-side: once the
page loads, no server is involved in solving.

Try it live at the [web demo](https://macroxue.github.io/bridge-solver/web/)
or build it yourself with the following steps.

## Build the demo
Requirement: Python 3 and the [solver's own requirement](../README.md#build-the-solver).
Emscripten is installed automatically into a repo-local `.emsdk/` (gitignored).

Emscripten 6.0+ cannot build this tree: its clang and compiler-rt disagree on
the LLVM profile data layout, so the PGO instrumentation step crashes. The
`web/makefile` pins **5.0.7**, the last release whose clang and compiler-rt
agree. From this directory (`web/`):
```
make setup   # once: target in web/makefile; installs into ../.emsdk
make
```
From the repo root (same targets, via `-C web`):
```
make -C web setup && make web
```
There is no `setup` target in the top-level makefile.

Serve the directory over HTTP locally:
```
python3 -m http.server
```

Then point your browser to `localhost:8000`.

Among the produced objects, `solver.js`, `solver.wasm` and `solver-no-simd.wasm`
can be used by another web app, as long as it follows the protocol demonstrated
by `worker.js` and `shuffle-worker.js` to communicate with the WASM solver.

## Features

### 1. Enter or generate a deal
Type each seat's cards directly, or use one of the controls next to Solve.

| Control     | Meaning                                  |
|-------------|------------------------------------------|
| Random deal | A fresh random deal, like `./solver -r`. |
| Freak deal  | One of the bundled `deals/freak` deals.  |
| Hard deal   | One of the bundled `deals/hard` deals.   |

### 2. Solve
`Solve` generates the full double-dummy table, the same one `./solver -r`
prints on the terminal: one row per strain, tricks for each of the four
declarers.

Click any cell in the table to play out that contract. Each card in hand is
then labeled with how the contract ends (`=`, `+N`, `-N`) if played, same as
`./solver -p`'s interactive play mode. `Undo` and `Undo Trick` step back
through the play; `Edit Hands` returns to the deal entry form.

`Solve` itself is single-threaded, same as the native solver.

### 3. Shuffle 10 / Shuffle 100
A single-dummy approximation in the browser, mirroring `./shuffle.py`: holds
one side's hands fixed and reshuffles the other side's cards over the given
number of rounds, reporting average tricks and making-percentage histograms
per strain and declarer.

Shuffle rounds run across a pool of Web Workers, up to half of the
`navigator.hardwareConcurrency` reported by the browser.

## How it's put together
- `web-bindings.cc` is the Emscripten-facing glue around `../solver.cc`
  (`solve`, `solve_plays`, `shuffle_and_solve`); it's what actually gets
  compiled, not `solver.cc` directly.
- The DD table (`solve`/`solve_plays`) and the shuffle feature
  (`shuffle_and_solve`) each run in their own Web Worker with its own WASM
  instance — `worker.js` and `shuffle-worker.js` — so the shuffle's
  approximation caches can never leak into the DD table's exact-precision
  ones.
- Some browsers (e.g. older Firefox) lack WASM SIMD support. `worker.js`
  feature-detects it against `solver.wasm` itself and falls back to
  `solver-no-simd.wasm` when it fails to validate.

## Performance

The WASM build is expected to run slower than the native solver, as it's capped
at SSE4.2-equivalent SIMD (`-msimd128 -msse4.2`), while the native build
compiles with `-march=native` and gets to use whatever the host CPU actually
has (e.g. BMI2 and AVX2).

On the same [AMD Ryzen 7 5800H](https://www.amd.com/en/support/downloads/drivers.html/processors/ryzen/ryzen-5000-series/amd-ryzen-7-5800h.html)
used for the main README's benchmarks, solving single-threaded in Node came out
**~1.5x slower** than the native CLI, as shown on the full 1000-deal
`deals/1k` set:

|        | 100 deals | 300 deals | 1000 deals |
|--------|-----------|-----------|------------|
| Native | 9.6 s     | 28.1 s    | 100.9 s    |
| Wasm   | 14.0 s    | 41.2 s    | 151.9 s    |
| Ratio  | 1.46x     | 1.47x     | 1.51x      |

Note that the above numbers are measured without CPU binding with `taskset`.
In the browser itself, expect a bit more overhead on top of that from
JS-to-WASM marshalling and the browser's own WASM JIT.

## License

Licensed under either of [Apache License, Version 2.0](../LICENSE-APACHE) or
[MIT license](../LICENSE-MIT) at your option.
