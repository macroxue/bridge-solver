all: solver.p solver
sanitizer: solver.m solver.a
web: solver.js solver.wasm solver-no-simd.wasm

OPTS=-std=c++17 -Wall -Wno-missing-profile
ifeq (sse4_2, $(shell grep -m1 -o sse4_2 /proc/cpuinfo))
	OPTS+=-msse4.2
endif
ifeq (bmi1, $(shell grep -m1 -o bmi1 /proc/cpuinfo))
	OPTS+=-mbmi
endif
ifeq (bmi2, $(shell grep -m1 -o bmi2 /proc/cpuinfo))
	OPTS+=-mbmi2
endif

# emcc's own llvm-profdata, found next to it on PATH.
EMSDK_BIN=$(dir $(shell which emcc))../bin
# emsdk's own Node -- the system one here is too old to run wasm builds.
NODE=$(or $(EMSDK_NODE),node)

solver.p: solver.cc
	rm -f solver.gcda
	g++ $(OPTS) -O3 -fprofile-generate -o $@ $^
	./$@ -if hard_deals/deal.8 | tail
	mv solver.p-solver.gcda solver.gcda
solver: solver.cc
	g++ $(OPTS) -O3 -fprofile-use -o $@ $^
	./$@ -if hard_deals/deal.8 | tail
solver.g: solver.cc
	g++ $(OPTS) -D_DEBUG -Og -g -o $@ $^
solver.m: solver.cc
	clang++ -std=c++17 -O3 -fsanitize=memory -o $@ $^
	./$@ -if hard_deals/deal.1
solver.a: solver.cc
	clang++ -std=c++17 -O3 -fsanitize=address -o $@ $^
	./$@ -if hard_deals/deal.1
# Instrumented wasm run under Node to produce PGO data for the builds
# below. -sNODERAWFS escapes Emscripten's in-memory FS so default.profraw
# (LLVM_PROFILE_FILE isn't honored here) actually lands on disk.
web.profdata: web-profile.cc solver.cc
	emcc -std=c++17 -O3 -msimd128 -msse4.2 -fprofile-instr-generate \
		-sNODERAWFS=1 -sEXIT_RUNTIME=1 -o web-profile.js web-profile.cc
	rm -f default.profraw
	$(NODE) web-profile.js > /dev/null
	$(EMSDK_BIN)/llvm-profdata merge -o $@ default.profraw
	rm -f web-profile.js web-profile.wasm default.profraw
solver.js: solver.cc solver-no-simd.wasm web.profdata
	emcc -D_WEB -std=c++17 -O3 -msimd128 -msse4.2 \
		-fprofile-instr-use=web.profdata -o $@ $< \
		--bind -s ALLOW_MEMORY_GROWTH
	@# Cache-bust both wasm URLs with each binary's own content hash, to
	@# avoid stale files being served after a new build.
	simd_hash=$$(md5sum solver.wasm | cut -c1-8); \
	nosimd_hash=$$(md5sum solver-no-simd.wasm | cut -c1-8); \
	sed -i "s/\"solver.wasm\"/simd?\"solver.wasm?v=$$simd_hash\":\"solver-no-simd.wasm?v=$$nosimd_hash\"/" $@
solver-no-simd.wasm: solver.cc web.profdata
	emcc -D_WEB -std=c++17 -O3 -fprofile-instr-use=web.profdata \
		-o solver-no-simd.js $< \
		--bind -s ALLOW_MEMORY_GROWTH
	rm solver-no-simd.js
web-test: web-test.cc solver.cc
	g++ $(OPTS) -O3 -o $@ web-test.cc
	./$@
clean:
	rm -f solver.p solver solver.g solver.m solver.a \
		solver.js solver.wasm solver-no-simd.js solver-no-simd.wasm \
		web-test web.profdata web-profile.js web-profile.wasm web-profile.profraw
