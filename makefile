all: solver.p solver

OPTS=-std=c++17 -Wall
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
	./$@ -f < hard_deals/deal.8 | tail
solver: solver.cc
	g++ $(OPTS) -O3 -fprofile-use -o $@ $^
	./$@ -f < hard_deals/deal.8 | tail
solver.g: solver.cc
	g++ $(OPTS) -D_DEBUG -Og -g -o $@ $^
