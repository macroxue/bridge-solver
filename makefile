.PHONY: all clean clobber distclean expunge perf sanitizer test
# Don't leave a half-built solver.p that later makes look up to date.
.DELETE_ON_ERROR:
all: solver.p solver
sanitizer: solver.m solver.a

# Fast correctness checks of the solver and its CLI (see tests/test.sh), ~5 s.
test: solver
	tests/test.sh

# Benchmarks, pinned to PERF_CPU with huge pages (see tests/perf.sh); minutes.
# PERF_SETS overrides the deal sets, e.g. PERF_SETS=deals/1k.
PERF_CPU ?= 0
PERF_SETS ?=
perf: solver
	tests/perf.sh $(PERF_CPU) $(PERF_SETS)

CXX ?= g++

# -march=native is unsafe on AArch64 (silently ignored by GCC, hard errors
# or slower codegen on some Clang versions) and unneeded -- NEON is
# baseline-mandatory there regardless of arch flags.
IS_AARCH64 := $(shell echo | $(CXX) -E -dM -x c++ - 2>/dev/null | grep -q __aarch64__ && echo 1)
ARCH_OPTS := $(if $(filter 1,$(IS_AARCH64)),,-march=native)
# Distro compilers (e.g. Ubuntu GCC) enable the stack protector by default; its
# canary check in every search prologue costs ~0.7% on deals/hard.
OPTS=-std=c++17 -Wall $(if $(IS_CLANG),,-Wno-missing-profile) $(ARCH_OPTS) -fno-stack-protector
# Static (non-PIE) linking runs searches 1-2% faster and starts 0.5 ms
# sooner. Used only if a tiny C++ program links statically: macOS doesn't
# support it, and e.g. Fedora needs glibc-static and libstdc++-static.
LDOPTS := $(shell t=$$(mktemp) && \
	printf '\043include <iostream>\nint main() { std::cout << 1; }\n' | \
	$(CXX) -static -x c++ -o $$t - >/dev/null 2>&1 && echo -static; rm -f $$t)

# CXX may be Clang directly or Apple Clang aliased as g++ on stock macOS.
# Its PGO format (.profraw + llvm-profdata) differs from GCC's (.gcda), and
# GCC's flat-file recipe measures ~0.8% faster than directory-form, so each
# compiler keeps its own recipe below.
SOURCES = solver.cc solver-cli.cc
HEADERS = solver.h

IS_CLANG := $(shell echo | $(CXX) -E -dM -x c++ - 2>/dev/null | grep -q __clang__ && echo 1)
PGO_DIR = pgo-data

ifeq ($(IS_CLANG),1)
# Prefer the llvm-profdata next to CXX so profile versions match (e.g. Homebrew
# LLVM alongside Xcode); Apple Clang's lives in the Xcode toolchain, off PATH.
LLVM_PROFDATA := $(shell p=$$($(CXX) -print-prog-name=llvm-profdata); \
	[ -x "$$p" ] && echo "$$p" || xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
solver.p: $(SOURCES) $(HEADERS)
	@test -n "$(LLVM_PROFDATA)" || { echo "llvm-profdata not found (install Xcode CLT or LLVM)" >&2; exit 1; }
	rm -rf $(PGO_DIR)
	mkdir $(PGO_DIR)
	$(CXX) $(OPTS) -O3 -fprofile-generate=$(PGO_DIR) $(LDOPTS) -o $@ $(SOURCES)
	./$@ -if deals/hard/deal.8 | tail
	"$(LLVM_PROFDATA)" merge -o $(PGO_DIR)/default.profdata $(PGO_DIR)/*.profraw
solver: $(SOURCES) $(HEADERS) solver.p
	$(CXX) $(OPTS) -O3 -fprofile-use=$(PGO_DIR) $(LDOPTS) -o $@ $(SOURCES)
	./$@ -if deals/hard/deal.8 | tail
else
# GCC names each source's profile after the binary: solver.p-solver.gcda
# from the run, read back as solver-solver.gcda.
solver.p: $(SOURCES) $(HEADERS)
	rm -f solver-*.gcda
	$(CXX) $(OPTS) -O3 -fprofile-generate $(LDOPTS) -o $@ $(SOURCES)
	./$@ -if deals/hard/deal.8 | tail
	for f in $(SOURCES:.cc=); do mv solver.p-$$f.gcda solver-$$f.gcda; done
solver: $(SOURCES) $(HEADERS) solver.p
	$(CXX) $(OPTS) -O3 -fprofile-use $(LDOPTS) -o $@ $(SOURCES)
	./$@ -if deals/hard/deal.8 | tail
endif
solver.g: $(SOURCES) $(HEADERS)
	$(CXX) $(OPTS) -D_DEBUG -Og -g -o $@ $(SOURCES)
solver.m: $(SOURCES) $(HEADERS)
	clang++ -std=c++17 -O3 -fsanitize=memory -o $@ $(SOURCES)
	./$@ -if deals/hard/deal.1
solver.a: $(SOURCES) $(HEADERS)
	clang++ -std=c++17 -O3 -fsanitize=address -o $@ $(SOURCES)
	./$@ -if deals/hard/deal.1
clean:
	rm -rf solver.p solver solver.g solver.m solver.a $(PGO_DIR)
	rm -f *.gcda *.gcno

# Also drop local run logs from run.sh / web/run.sh / parallel_run*.sh.
distclean: clean
	rm -f results.* web/results.*
expunge clobber: distclean
