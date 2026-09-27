# Bridge double dummy solver — web app

This is a browser front end for the [double dummy solver](../README.md),
compiled to WebAssembly (WASM). It runs entirely client-side: once the
page loads, no server is involved in solving.

Try it live at the [web app](https://macroxue.github.io/bridge-solver/web/)
or build it yourself with the following steps.

## Build the app
Requirement: [Emscripten](https://emscripten.org) and Python 3, in addition to
the [solver's own requirement](../README.md#build-the-solver).
```
make
```

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
Type each seat's cards directly, or use one of these controls.

| Control     | Meaning                                                  |
|-------------|----------------------------------------------------------|
| Paste bar   | A deal in PBN, `deals/` or vugraph format, with its Vul. |
| Random deal | A fresh random deal, like `./solver -r`.                 |
| Sample deal | One of the bundled `deals/freak` or `deals/hard` deals.  |

Hands can have fewer than 13 cards, as long as all four have the same number,
to solve, shuffle and play out endings. Par needs full hands.

The page's URL keeps the deal, vulnerability, DD table and any play in
progress, so it can be shared as a link to that position without solving
again. `Solve` recomputes a linked table. A link carries a checksum, so one
that was edited or cut off is rejected rather than half loaded.

Try these links:
- [Freak 3](https://macroxue.github.io/bridge-solver/web/#deal=N:AJ962.KT74..Q853+Q853.AJ962.KT74.+.Q853.AJ962.KT74+KT74..Q853.AJ962&dd=77774477774444777744&sum=1081mzh),
  the most difficult known deal, shows its table at once instead of taking
  a minute or more to solve.
- [5♦ by North](https://macroxue.github.io/bridge-solver/web/#deal=N:A64.K54.AK982.QJ+QT97.A962.T.K652+5.873.QJ643.AT87+KJ832.QJT.75.943&vul=N-S&dd=994444885855ab228944&play=DN&cards=H2&sum=1mne301)
  after East's ♥2 lead, where par is E/W's 5♠X sacrifice.
- [A squeeze](https://macroxue.github.io/bridge-solver/web/#deal=N:.AJ.K86.7+.KT.JT2.K+53.4.A7.J+.Q76.Q95.&dd=55105510332244225511&play=SE&sum=1bwv98m)
  in a 6-card ending: ♠ trump, South to lead and take all 6 tricks.

### 2. Solve
`Solve` generates the full double-dummy table, the same one `./solver -r`
prints on the terminal: one row per strain, tricks for each of the four
declarers.

Click any cell in the table to play out that contract. Each card in hand is
then labeled with how the contract ends (`=`, `+N`, `-N`) if played, same as
`./solver -p`'s interactive play mode. `Undo` and `Undo Trick` step back
through the play; `Edit Hands` returns to the deal entry form.

Below the table is the par score and contract(s) for the chosen
vulnerability, taken from a pasted deal's `Vulnerable` tag if present. When
par depends on which side bids first, both results are shown. `par-score.js`
is ported from [bidding-practice](https://github.com/macroxue/bridge-bidding-practice).

`Solve` itself is single-threaded, same as the native solver.

### 3. Shuffle 10 / Shuffle 100
A single-dummy approximation in the browser, mirroring `./shuffle.py`: holds
one side's hands fixed and reshuffles the other side's cards over the given
number of rounds, reporting average tricks and making-percentage histograms
per strain and declarer.

Below the tables is the single-dummy par: the same par logic, but on
expected scores from each declarer's trick distribution over the shuffles
(N/S declarers from shuffling E/W and vice versa), each par contract
followed by how often it makes. Few rounds make it noisy; Shuffle 100 is
more reliable.

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
- Every JS file is loaded with a `?v=<hash>` suffix so browsers never mix a
  stale cached file with fresh ones. `../.githooks/pre-commit` bumps the
  suffixes for staged files, up through every file that references them;
  enable it once per clone with `git config core.hooksPath .githooks`.
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

Note that the above numbers are measured without CPU binding or huge pages.
In the browser itself, expect a bit more overhead on top of that from
JS-to-WASM marshalling and the browser's own WASM JIT.

The most difficult known deal `deals/freak/deal.3` (Freak 3 in the Sample deal
menu) took 50 seconds natively and 90 seconds with WASM. A 2023
[Pixel 7a](https://en.wikipedia.org/wiki/Pixel_7a) phone with 8GB of RAM solved
it in 150 seconds, only 1.7x slower than WASM on the desktop.

## License

Licensed under either of [Apache License, Version 2.0](../LICENSE-APACHE) or
[MIT license](../LICENSE-MIT) at your option.
