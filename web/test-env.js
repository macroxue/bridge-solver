// Shared by the web app's unit tests: loads the browser scripts into a Node
// vm context with just enough of a fake browser to run them, and a tiny test
// runner in the style of deal-parser-test.js. Run a test with e.g.
// `node app-test.js` (Node 14.6+ for par-score.js's private class methods).
'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

// The page's scripts, in index.html's order.
const PAGE_SCRIPTS = ['deals.js', 'deal-parser.js', 'declarer-columns.js', 'par-score.js',
  'app.js', 'shuffle.js', 'play.js', 'share-url.js', 'startup.js'];

// An element that accepts whatever the scripts do to it. querySelector()
// returns a fresh element rather than null, so rendering code can run; tests
// that depend on a lookup failing override it.
function makeElement(tag = 'div') {
  const classes = new Set();
  return {
    tagName: tag.toUpperCase(), id: '', value: '', textContent: '', innerHTML: '', label: '',
    disabled: false, style: {}, dataset: {}, children: [], listeners: {},
    scrollWidth: 0, clientWidth: 0,
    classList: {
      add: (c) => classes.add(c), remove: (c) => classes.delete(c), contains: (c) => classes.has(c),
      toggle: (c, on) => ((on ?? !classes.has(c)) ? classes.add(c) : classes.delete(c)),
    },
    addEventListener(type, fn) { (this.listeners[type] = this.listeners[type] || []).push(fn); },
    appendChild(child) { this.children.push(child); return child; },
    querySelector: () => makeElement(), querySelectorAll: () => [],
    closest: () => null, blur() {}, focus() {},
    getBoundingClientRect: () => ({ left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 }),
  };
}

// Timers only run when a test flushes them, so debounced work is explicit.
function makeTimers() {
  let next = 1;
  const pending = new Map();
  return {
    setTimeout: (fn, ms) => { pending.set(next, fn); return next++; },
    clearTimeout: (id) => pending.delete(id),
    flush() {
      while (pending.size) {
        const [id, fn] = pending.entries().next().value;
        pending.delete(id);
        fn();
      }
    },
  };
}

class FakeWorker {
  constructor(url) { this.url = url; this.messages = []; this.onmessage = null; this.terminated = false; }
  postMessage(message) { this.messages.push(message); }
  terminate() { this.terminated = true; }
  // Delivers a message from the worker to the page.
  send(...data) { this.onmessage({ data }); }
}

// Loads the page into a fresh context. `search` is location.search, e.g.
// '?workers=3', and `hash` the initial link.
function loadPage({ search = '', hash = '', hardwareConcurrency = 8 } = {}) {
  const elements = new Map();
  const byId = (id) => {
    if (!elements.has(id)) elements.set(id, Object.assign(makeElement(), { id }));
    return elements.get(id);
  };
  // Start each element with index.html's id, value and disabled attributes,
  // e.g. the default deal in the hand inputs and Solve disabled.
  const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
  for (const [tag] of html.matchAll(/<(?:input|button|select)\b[^>]*>/g)) {
    const id = (tag.match(/\bid="([^"]*)"/) || [])[1];
    if (!id) continue;
    const el = byId(id);
    el.value = (tag.match(/\bvalue="([^"]*)"/) || [, ''])[1];
    el.disabled = /\sdisabled[\s>]/.test(tag);
  }
  // playHint's text is read at load as the full-deal hint.
  byId('playHint').textContent = 'Each card shows how the contract ends (=, +N, –N) if played.';
  byId('status').textContent = 'Loading solver…';
  const timers = makeTimers();
  const location = { search, hash, pathname: '/index.html', reloads: 0 };
  location.reload = () => { ++location.reloads; };
  const replaceStateCalls = [];
  const windowListeners = {};
  const sandbox = {
    console, performance, Math, JSON, Date, URLSearchParams, Promise,
    setTimeout: timers.setTimeout, clearTimeout: timers.clearTimeout,
    requestAnimationFrame: (fn) => fn(),
    getComputedStyle: () => ({ fontSize: '16px' }),
    navigator: { hardwareConcurrency },
    location,
    history: {
      replaceState(state, title, url) {
        replaceStateCalls.push(url);
        location.hash = url.startsWith('#') ? url : '';
      },
    },
    document: {
      getElementById: byId,
      createElement: (tag) => makeElement(tag),
      querySelector: () => makeElement(),
      documentElement: { style: { setProperty() {} } },
    },
    Worker: FakeWorker,
  };
  sandbox.window = {
    matchMedia: () => ({ matches: false }),
    addEventListener: (type, fn) => { (windowListeners[type] = windowListeners[type] || []).push(fn); },
  };
  const context = vm.createContext(sandbox);
  for (const file of PAGE_SCRIPTS) {
    vm.runInContext(fs.readFileSync(path.join(__dirname, file), 'utf8'), context, { filename: file });
  }
  return {
    context, byId, timers, location, replaceStateCalls,
    // Evaluates an expression in the page, including its let/const globals.
    run: (code) => vm.runInContext(code, context),
    // Simulates the user changing the URL's hash.
    changeHash(newHash) {
      location.hash = newHash;
      for (const fn of windowListeners.hashchange || []) fn();
    },
    // Fires a window event, e.g. pagehide.
    fireWindow(type, event = {}) {
      for (const fn of windowListeners[type] || []) fn(event);
    },
    // Fires a listener registered on an element, e.g. click on #solve.
    fire(id, type, event = {}) {
      for (const fn of byId(id).listeners[type] || []) fn(event);
    },
  };
}

// Loads worker.js or shuffle-worker.js with a fake importScripts() and a
// fetch() that never resolves, so the real wasm is never loaded; tests put
// fake solver functions on the Module object the script creates.
function loadWorker(file) {
  const posted = [];
  // Quiet the solver's stderr echo (console.log); errors still show.
  const quietConsole = { log() {}, warn: console.warn, error: console.error };
  const sandbox = { console: quietConsole, performance, Math, JSON, Promise };
  sandbox.postMessage = (message) => posted.push(message);
  sandbox.fetch = () => new Promise(() => {});
  sandbox.importScripts = (url) => {
    const name = url.replace(/\?v=.*$/, '');
    vm.runInContext(fs.readFileSync(path.join(__dirname, name), 'utf8'), context, { filename: name });
  };
  const context = vm.createContext(sandbox);
  vm.runInContext(fs.readFileSync(path.join(__dirname, file), 'utf8'), context, { filename: file });
  return {
    context, posted,
    run: (code) => vm.runInContext(code, context),
    // Delivers a message from the page to the worker.
    send: (...data) => context.onmessage({ data }),
  };
}

const tests = [];
const test = (name, fn) => tests.push({ name, fn });

function runTests() {
  let failed = 0;
  for (const { name, fn } of tests) {
    try {
      fn();
      console.log(`ok - ${name}`);
    } catch (err) {
      failed++;
      console.error(`FAIL - ${name}`);
      console.error(err);
    }
  }
  console.log(`\n${tests.length - failed}/${tests.length} passed`);
  process.exit(failed ? 1 : 0);
}

module.exports = { loadPage, loadWorker, test, runTests };
