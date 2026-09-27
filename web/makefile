.PHONY: all clean test refresh-emcc-id
.DELETE_ON_ERROR:
all: solver.js solver.wasm solver-no-simd.wasm deals.js

# Prefer an activated emsdk tree (source emsdk_env.sh, or set EMSDK).
EMCC ?= $(or $(wildcard $(EMSDK)/upstream/emscripten/emcc),emcc)
# Absolute path so $(dir ...) works when EMCC is the bare name `emcc`.
EMCC_PATH := $(shell command -v $(EMCC) 2>/dev/null)
# Stamp contents: first line of emcc --version. Changing toolchains (e.g.
# 5.0.7 -> 6.x) updates .emcc-id so web.profdata and wasm rebuild.
EMCC_ID := $(shell $(EMCC) --version 2>/dev/null | head -n1)
# emcc's llvm-profdata. Newer emcc reports it via --print-prog-name; older
# emsdk (incl. 5.0.7) leaves that empty, so fall back to the LLVM bin next to
# emcc / under EMSDK, then Homebrew's em-config LLVM_ROOT.
LLVM_PROFDATA ?= $(firstword \
	$(wildcard $(shell $(EMCC) --print-prog-name=llvm-profdata 2>/dev/null)) \
	$(wildcard $(dir $(EMCC_PATH))../bin/llvm-profdata) \
	$(wildcard $(EMSDK)/upstream/bin/llvm-profdata) \
	$(wildcard $(shell em-config LLVM_ROOT 2>/dev/null)/llvm-profdata))
# emsdk's own Node -- the system one here is too old to run wasm builds.
NODE=$(or $(EMSDK_NODE),node)
# Portable content hash: md5sum when on PATH, else absolute `md5 -q` (stock
# macOS puts both under /sbin; a bare `md5` fails if /sbin is not on PATH).
MD5_CMD := $(shell command -v md5sum 2>/dev/null || \
	{ m=$$(command -v md5 2>/dev/null) && printf '%s -q\n' "$$m"; })

# Refresh stamp every make; only rewrite .emcc-id when the identity changes.
refresh-emcc-id:
	@printf '%s\n' "$(EMCC_ID)" > .emcc-id.new
	@if ! cmp -s .emcc-id.new .emcc-id 2>/dev/null; then mv .emcc-id.new .emcc-id; else rm -f .emcc-id.new; fi

.emcc-id: refresh-emcc-id
	@test -f $@ || printf '%s\n' "$(EMCC_ID)" > $@

# Instrumented wasm run under Node to produce PGO data for the builds
# below. -sNODERAWFS escapes Emscripten's in-memory FS so default.profraw
# (LLVM_PROFILE_FILE isn't honored here) actually lands on disk.
web.profdata: web-profile.cc web-bindings.cc ../solver.cc .emcc-id
	$(EMCC) -std=c++17 -O3 -msimd128 -msse4.2 -fprofile-instr-generate \
		-sNODERAWFS=1 -sEXIT_RUNTIME=1 -o web-profile.js web-profile.cc
	rm -f default.profraw
	@test -n "$(LLVM_PROFDATA)" || { \
		echo "llvm-profdata not found (set EMSDK or ensure emcc's LLVM tools are on PATH)" >&2; \
		exit 1; }; \
	$(NODE) web-profile.js > /dev/null || { \
		printf '\033[1mYou need emscripten v5.0.7 or earlier. See repo root / web/README.md\033[0m\n' >&2; \
		exit 1; }; \
	"$(LLVM_PROFDATA)" merge -o $@ default.profraw || { \
		echo "llvm-profdata merge failed (LLVM_PROFDATA=$(LLVM_PROFDATA))" >&2; \
		exit 1; }
	rm -f web-profile.js web-profile.wasm default.profraw
solver.js: web-bindings.cc ../solver.cc solver-no-simd.wasm web.profdata
	$(EMCC) -std=c++17 -O3 -msimd128 -msse4.2 \
		-fprofile-instr-use=web.profdata -o $@ web-bindings.cc \
		--bind -s ALLOW_MEMORY_GROWTH
	@# Cache-bust both wasm URLs with each binary's own content hash, to
	@# avoid stale files being served after a new build. -i.bak rather than
	@# -i: BSD sed (macOS) requires a backup suffix. MD5_CMD covers stock
	@# macOS, which has `md5 -q` but not GNU `md5sum`.
	simd_hash=$$($(MD5_CMD) solver.wasm | cut -c1-8); \
	nosimd_hash=$$($(MD5_CMD) solver-no-simd.wasm | cut -c1-8); \
	test -n "$$simd_hash" && test -n "$$nosimd_hash" || { \
		echo "cache-bust hash failed (MD5_CMD='$(MD5_CMD)')" >&2; exit 1; }; \
	sed -i.bak "s/\"solver.wasm\"/simd?\"solver.wasm?v=$$simd_hash\":\"solver-no-simd.wasm?v=$$nosimd_hash\"/" $@
	rm -f $@.bak
solver.wasm: solver.js
solver-no-simd.wasm: web-bindings.cc ../solver.cc web.profdata
	$(EMCC) -std=c++17 -O3 -fprofile-instr-use=web.profdata \
		-o solver-no-simd.js web-bindings.cc \
		--bind -s ALLOW_MEMORY_GROWTH
	rm solver-no-simd.js
# -march=native is unsafe on AArch64 -- see ../makefile's IS_AARCH64 comment.
IS_AARCH64 := $(shell echo | g++ -E -dM -x c++ - 2>/dev/null | grep -q __aarch64__ && echo 1)
ARCH_OPTS := $(if $(filter 1,$(IS_AARCH64)),,-march=native)
web-test: web-test.cc web-bindings.cc ../solver.cc
	g++ -std=c++17 -Wall -Wno-missing-profile $(ARCH_OPTS) -O3 -o $@ web-test.cc
	./$@

# The C++ binding tests, then every *-test.js. Those need Node 14+ (for
# par-score.js's private class methods), e.g. emsdk's via EMSDK_NODE.
JS_TESTS = $(sort $(wildcard *-test.js))
test: web-test
	./web-test > /dev/null && echo "web-test: ok"
	@$(NODE) -e 'process.exit(parseInt(process.versions.node) < 14 ? 1 : 0)' || \
		{ echo "Node 14+ is needed for the JS tests; set EMSDK_NODE or put it on PATH." >&2; exit 1; }
	@for t in $(JS_TESTS); do \
		out=$$($(NODE) $$t 2>&1) || { echo "$$out"; echo "$$t: FAILED" >&2; exit 1; }; \
		echo "$$t: $$(echo "$$out" | tail -1)"; \
	done
# Embeds deals/hard/ and deals/freak/ text directly into the app so it
# doesn't depend on symlinks or runtime fetches (GitHub Pages deployments
# reject symlinks outright).
deals.js: gen_deals.py $(wildcard ../deals/hard/deal.* ../deals/freak/deal.*)
	python3 gen_deals.py
clean:
	rm -f solver.js solver.wasm solver-no-simd.js solver-no-simd.wasm \
		web-test web.profdata web-profile.js web-profile.wasm web-profile.profraw \
		default.profraw deals.js .emcc-id .emcc-id.new
