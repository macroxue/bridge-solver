all: solver.p solver
sanitizer: solver.m solver.a
web: solver.js solver.wasm

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
solver.js: solver.cc
	emcc -D_WEB -std=c++17 -O3 -msimd128 -msse4.2 -o $@ $^ \
		-s ALLOW_MEMORY_GROWTH \
		-s EXPORTED_FUNCTIONS=_solve,_solve_leads \
		-s EXPORTED_RUNTIME_METHODS=ccall
clean:
	rm -f solver.p solver solver.g solver.m solver.a solver.js solver.wasm
