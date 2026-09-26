.PHONY: all clean check-emcc
.DELETE_ON_ERROR:
all: solver.js solver.wasm solver-no-simd.wasm deals.js

# Emscripten 6.0+ ships a clang that emits raw profile v11 against a
# compiler-rt still on v10, so -fprofile-instr-generate crashes at exit.
# 5.0.7 is the last release whose clang and compiler-rt agree on the layout.
EMSCRIPTEN_VERSION ?= 5.0.7
# Prefer an activated emsdk tree (source emsdk_env.sh, or set EMSDK).
EMCC ?= $(or $(wildcard $(EMSDK)/upstream/emscripten/emcc),emcc)
# emcc's own llvm-profdata, in the LLVM tree emcc is configured with.
LLVM_PROFDATA ?= $(shell $(EMCC) --print-prog-name=llvm-profdata 2>/dev/null)
# emsdk's own Node -- the system one here is too old to run wasm builds.
NODE=$(or $(EMSDK_NODE),node)

# Fail fast with install instructions when PATH has the wrong emcc.
# Inlined into recipes (not a prerequisite) so a .PHONY check doesn't
# force every target to rebuild on each make.
define require_emcc
v=$$($(EMCC) --version 2>/dev/null | sed -n 's/.* \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\).*/\1/p' | head -1); \
if [ "$$v" != "$(EMSCRIPTEN_VERSION)" ]; then \
	echo "error: need Emscripten $(EMSCRIPTEN_VERSION) (got '$${v:-none}' from $(EMCC))" >&2; \
	echo "  git clone https://github.com/emscripten-core/emsdk.git ~/emsdk && cd ~/emsdk && \\" >&2; \
	echo "  ./emsdk install $(EMSCRIPTEN_VERSION) && ./emsdk activate $(EMSCRIPTEN_VERSION) && \\" >&2; \
	echo "  source ./emsdk_env.sh" >&2; \
	exit 1; \
fi
endef
check-emcc:
	@$(require_emcc)

# Instrumented wasm run under Node to produce PGO data for the builds
# below. -sNODERAWFS escapes Emscripten's in-memory FS so default.profraw
# (LLVM_PROFILE_FILE isn't honored here) actually lands on disk.
web.profdata: web-profile.cc web-bindings.cc ../solver.cc
	@$(require_emcc)
	$(EMCC) -std=c++17 -O3 -msimd128 -msse4.2 -fprofile-instr-generate \
		-sNODERAWFS=1 -sEXIT_RUNTIME=1 -o web-profile.js web-profile.cc
	rm -f default.profraw
	$(NODE) web-profile.js > /dev/null
	"$(LLVM_PROFDATA)" merge -o $@ default.profraw
	rm -f web-profile.js web-profile.wasm default.profraw
solver.js: web-bindings.cc ../solver.cc solver-no-simd.wasm web.profdata
	@$(require_emcc)
	$(EMCC) -std=c++17 -O3 -msimd128 -msse4.2 \
		-fprofile-instr-use=web.profdata -o $@ web-bindings.cc \
		--bind -s ALLOW_MEMORY_GROWTH
	@# Cache-bust both wasm URLs with each binary's own content hash, to
	@# avoid stale files being served after a new build. -i.bak rather than
	@# -i: BSD sed (macOS) requires a backup suffix.
	simd_hash=$$(md5sum solver.wasm | cut -c1-8); \
	nosimd_hash=$$(md5sum solver-no-simd.wasm | cut -c1-8); \
	sed -i.bak "s/\"solver.wasm\"/simd?\"solver.wasm?v=$$simd_hash\":\"solver-no-simd.wasm?v=$$nosimd_hash\"/" $@
	rm -f $@.bak
solver.wasm: solver.js
solver-no-simd.wasm: web-bindings.cc ../solver.cc web.profdata
	@$(require_emcc)
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
# Embeds deals/hard/ and deals/freak/ text directly into the app so it
# doesn't depend on symlinks or runtime fetches (GitHub Pages deployments
# reject symlinks outright).
deals.js: gen_deals.py $(wildcard ../deals/hard/deal.* ../deals/freak/deal.*)
	python3 gen_deals.py
clean:
	rm -f solver.js solver.wasm solver-no-simd.js solver-no-simd.wasm \
		web-test web.profdata web-profile.js web-profile.wasm web-profile.profraw \
		default.profraw deals.js
