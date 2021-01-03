all: solver.p solver

OPTS=-std=c++0x -Wall

solver.p: solver.cc
	rm -f solver.gcda
	g++ $(OPTS) -O3 -fprofile-generate -o $@ $^
	./$@ < fu | tail
solver: solver.cc
	g++ $(OPTS) -O3 -fprofile-use -o $@ $^
	./$@ < fu | tail
solver.g: solver.cc
	g++ $(OPTS) -D_DEBUG -O0 -g -o $@ $^
