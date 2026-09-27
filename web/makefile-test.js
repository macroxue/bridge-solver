// Structural and env-honor tests for web/makefile. No framework — run with:
//   node makefile-test.js
'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFileSync } = require('child_process');

const MAKEFILE = path.join(__dirname, 'makefile');

const tests = [];
const test = (name, fn) => tests.push({ name, fn });

function makePrint(varName, env = {}, scrubEnv = []) {
  const wrapper = path.join(os.tmpdir(), `makefile-print-${process.pid}.mk`);
  fs.writeFileSync(
    wrapper,
    [
      '.DEFAULT_GOAL := __print',
      `include ${MAKEFILE}`,
      '__print:',
      `\t@printf '%s\\n' '$(${varName})'`,
      '',
    ].join('\n'),
  );
  const merged = { ...process.env, ...env };
  for (const key of scrubEnv) delete merged[key];
  try {
    return execFileSync('make', ['-f', wrapper, '__print'], {
      cwd: __dirname,
      env: merged,
      encoding: 'utf8',
    }).trim();
  } finally {
    fs.unlinkSync(wrapper);
  }
}

test('LLVM_PROFDATA resolves next to PATH emcc when EMCC is a bare command name', () => {
  // Arrange: a PATH-only fake emcc whose --print-prog-name is empty (like
  // emsdk 5.x), with llvm-profdata in the sibling ../bin layout. Shadow
  // em-config so Homebrew cannot mask a broken dir($(EMCC)) fallback.
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'path-emcc-'));
  const emccDir = path.join(root, 'upstream', 'emscripten');
  const binDir = path.join(root, 'upstream', 'bin');
  const emccPath = path.join(emccDir, 'emcc');
  const profPath = path.join(binDir, 'llvm-profdata');
  const emConfig = path.join(emccDir, 'em-config');
  fs.mkdirSync(emccDir, { recursive: true });
  fs.mkdirSync(binDir, { recursive: true });
  fs.writeFileSync(
    emccPath,
    '#!/bin/sh\n' +
      'for a in "$@"; do [ "$a" = --print-prog-name=llvm-profdata ] && exit 0; done\n' +
      'exit 0\n',
  );
  fs.chmodSync(emccPath, 0o755);
  fs.writeFileSync(profPath, '#!/bin/sh\nexit 0\n');
  fs.chmodSync(profPath, 0o755);
  fs.writeFileSync(emConfig, '#!/bin/sh\nexit 1\n');
  fs.chmodSync(emConfig, 0o755);

  // Act: EMCC stays the bare name; no EMSDK to fall back on.
  const pathEnv = `${emccDir}${path.delimiter}${process.env.PATH || ''}`;
  let got;
  try {
    got = makePrint(
      'LLVM_PROFDATA',
      { PATH: pathEnv, EMCC: 'emcc' },
      ['EMSDK', 'EMSDK_NODE'],
    );
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }

  // Assert: must find the PATH-adjacent binary, not web/../bin/llvm-profdata.
  assert.ok(got, 'LLVM_PROFDATA should be non-empty');
  assert.strictEqual(path.resolve(got), path.resolve(profPath));
});

test('LLVM_PROFDATA finds llvm-profdata under EMSDK even when --print-prog-name is empty', () => {
  const fake = fs.mkdtempSync(path.join(os.tmpdir(), 'emsdk-'));
  const emccPath = path.join(fake, 'upstream', 'emscripten', 'emcc');
  const profPath = path.join(fake, 'upstream', 'bin', 'llvm-profdata');
  fs.mkdirSync(path.dirname(emccPath), { recursive: true });
  fs.mkdirSync(path.dirname(profPath), { recursive: true });
  fs.writeFileSync(
    emccPath,
    '#!/bin/sh\n' +
      'for a in "$@"; do [ "$a" = --print-prog-name=llvm-profdata ] && exit 0; done\n' +
      'exit 0\n',
  );
  fs.chmodSync(emccPath, 0o755);
  fs.writeFileSync(profPath, '#!/bin/sh\nexit 0\n');
  fs.chmodSync(profPath, 0o755);
  try {
    const got = makePrint('LLVM_PROFDATA', { EMSDK: fake }, ['EMCC']);
    assert.strictEqual(path.resolve(got), path.resolve(profPath));
  } finally {
    fs.rmSync(fake, { recursive: true, force: true });
  }
});

let failed = 0;
for (const { name, fn } of tests) {
  try {
    fn();
    console.log(`ok - ${name}`);
  } catch (err) {
    failed++;
    console.error(`not ok - ${name}`);
    console.error(err);
  }
}
if (failed) process.exit(1);
console.log(`\n${tests.length - failed}/${tests.length} passed`);
