all: solver.p solver

OPTS=-std=c++0x -Wall
ifeq (sse4_2, $(shell grep -m1 -o sse4_2 /proc/cpuinfo))
	OPTS+=-msse4.2
endif

solver.p: solver.cc
	rm -f solver.gcda
	g++ $(OPTS) -O3 -fprofile-generate -o $@ $^
	./$@ -f < old_deals/fu8 | tail
solver: solver.cc
	g++ $(OPTS) -O3 -fprofile-use -o $@ $^
	./$@ -f < old_deals/fu8 | tail
solver.g: solver.cc
	g++ $(OPTS) -D_DEBUG -Og -g -o $@ $^
