.PHONY: all sanitizer web clean
# Don't leave a half-built solver.p that later makes look up to date.
.DELETE_ON_ERROR:
all: solver.p solver
sanitizer: solver.m solver.a
web:
	$(MAKE) -C web

CXX ?= g++

# -march=native is unsafe on AArch64 (silently ignored by GCC, hard errors
# or slower codegen on some Clang versions) and unneeded -- NEON is
# baseline-mandatory there regardless of arch flags.
IS_AARCH64 := $(shell echo | $(CXX) -E -dM -x c++ - 2>/dev/null | grep -q __aarch64__ && echo 1)
ARCH_OPTS := $(if $(filter 1,$(IS_AARCH64)),,-march=native)
OPTS=-std=c++17 -Wall $(if $(IS_CLANG),,-Wno-missing-profile) $(ARCH_OPTS)

# CXX may be Clang directly or Apple Clang aliased as g++ on stock macOS.
# Its PGO format (.profraw + llvm-profdata) differs from GCC's (.gcda), and
# GCC's flat-file recipe measures ~0.8% faster than directory-form, so each
# compiler keeps its own recipe below.
IS_CLANG := $(shell echo | $(CXX) -E -dM -x c++ - 2>/dev/null | grep -q __clang__ && echo 1)
PGO_DIR = pgo-data

ifeq ($(IS_CLANG),1)
# Prefer the llvm-profdata next to CXX so profile versions match (e.g. Homebrew
# LLVM alongside Xcode); Apple Clang's lives in the Xcode toolchain, off PATH.
LLVM_PROFDATA := $(shell p=$$($(CXX) -print-prog-name=llvm-profdata); \
	[ -x "$$p" ] && echo "$$p" || xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
solver.p: solver.cc
	@test -n "$(LLVM_PROFDATA)" || { echo "llvm-profdata not found (install Xcode CLT or LLVM)" >&2; exit 1; }
	rm -rf $(PGO_DIR)
	mkdir $(PGO_DIR)
	$(CXX) $(OPTS) -O3 -fprofile-generate=$(PGO_DIR) -o $@ $^
	./$@ -if deals/hard/deal.8 | tail
	"$(LLVM_PROFDATA)" merge -o $(PGO_DIR)/default.profdata $(PGO_DIR)/*.profraw
solver: solver.cc solver.p
	$(CXX) $(OPTS) -O3 -fprofile-use=$(PGO_DIR) -o $@ solver.cc
	./$@ -if deals/hard/deal.8 | tail
else
solver.p: solver.cc
	rm -f solver.gcda
	$(CXX) $(OPTS) -O3 -fprofile-generate -o $@ $^
	./$@ -if deals/hard/deal.8 | tail
	mv solver.p-solver.gcda solver.gcda
solver: solver.cc
	$(CXX) $(OPTS) -O3 -fprofile-use -o $@ $^
	./$@ -if deals/hard/deal.8 | tail
endif
solver.g: solver.cc
	$(CXX) $(OPTS) -D_DEBUG -Og -g -o $@ $^
solver.m: solver.cc
	clang++ -std=c++17 -O3 -fsanitize=memory -o $@ $^
	./$@ -if deals/hard/deal.1
solver.a: solver.cc
	clang++ -std=c++17 -O3 -fsanitize=address -o $@ $^
	./$@ -if deals/hard/deal.1
clean:
	rm -rf solver.p solver solver.g solver.m solver.a solver.gcda $(PGO_DIR)
	$(MAKE) -C web clean
