// Declarer for each of solve()'s 4 trick-count columns, in order; matches
// solve()'s fixed lead_seats iteration in solver.cc, independent of trump.
// Single source of truth for both app.js (DD-table rendering) and
// shuffle-worker.js (shuffle-batch result parsing) -- loaded by app.js via
// <script>, and by shuffle-worker.js via importScripts().
const DECLARER_COLUMNS = ['south', 'north', 'west', 'east'];
