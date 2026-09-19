.PHONY: all clean
all: solver.js solver.wasm solver-no-simd.wasm deals.js

# emcc's own llvm-profdata, found next to it on PATH.
EMSDK_BIN=$(dir $(shell which emcc))../bin
# emsdk's own Node -- the system one here is too old to run wasm builds.
NODE=$(or $(EMSDK_NODE),node)

# Instrumented wasm run under Node to produce PGO data for the builds
# below. -sNODERAWFS escapes Emscripten's in-memory FS so default.profraw
# (LLVM_PROFILE_FILE isn't honored here) actually lands on disk.
web.profdata: web-profile.cc web-bindings.cc ../solver.cc
	emcc -std=c++17 -O3 -msimd128 -msse4.2 -fprofile-instr-generate \
		-sNODERAWFS=1 -sEXIT_RUNTIME=1 -o web-profile.js web-profile.cc
	rm -f default.profraw
	$(NODE) web-profile.js > /dev/null
	$(EMSDK_BIN)/llvm-profdata merge -o $@ default.profraw
	rm -f web-profile.js web-profile.wasm default.profraw
solver.js: web-bindings.cc ../solver.cc solver-no-simd.wasm web.profdata
	emcc -std=c++17 -O3 -msimd128 -msse4.2 \
		-fprofile-instr-use=web.profdata -o $@ web-bindings.cc \
		--bind -s ALLOW_MEMORY_GROWTH
	@# Cache-bust both wasm URLs with each binary's own content hash, to
	@# avoid stale files being served after a new build.
	simd_hash=$$(md5sum solver.wasm | cut -c1-8); \
	nosimd_hash=$$(md5sum solver-no-simd.wasm | cut -c1-8); \
	sed -i "s/\"solver.wasm\"/simd?\"solver.wasm?v=$$simd_hash\":\"solver-no-simd.wasm?v=$$nosimd_hash\"/" $@
solver.wasm: solver.js
solver-no-simd.wasm: web-bindings.cc ../solver.cc web.profdata
	emcc -std=c++17 -O3 -fprofile-instr-use=web.profdata \
		-o solver-no-simd.js web-bindings.cc \
		--bind -s ALLOW_MEMORY_GROWTH
	rm solver-no-simd.js
# -march=native is unsafe on AArch64 -- see ../makefile's IS_AARCH64 comment.
IS_AARCH64 := $(shell echo | g++ -E -dM -x c++ - 2>/dev/null | grep -q __aarch64__ && echo 1)
ARCH_OPTS := $(if $(filter 1,$(IS_AARCH64)),,-march=native)
web-test: web-test.cc web-bindings.cc ../solver.cc
	g++ -std=c++17 -Wall -Wno-missing-profile $(ARCH_OPTS) -O3 -o $@ web-test.cc
	./$@
# Embeds deals/hard/ and deals/freak/ text directly into the app so it
# doesn't depend on symlinks or runtime fetches (GitHub Pages deployments
# reject symlinks outright).
deals.js: gen_deals.py $(wildcard ../deals/hard/deal.* ../deals/freak/deal.*)
	python3 gen_deals.py
clean:
	rm -f solver.js solver.wasm solver-no-simd.js solver-no-simd.wasm \
		web-test web.profdata web-profile.js web-profile.wasm web-profile.profraw \
		deals.js
