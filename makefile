all: solver solver.g solver.p solver.q

OPTS=-std=c++0x -Wall

solver: solver.cc
	g++ $(OPTS) -O3 -o $@ $^
solver.g: solver.cc
	g++ $(OPTS) -D_DEBUG -O0 -g -o $@ $^
solver.p: solver.cc
	g++ $(OPTS) -O3 -fprofile-generate -o $@ $^
	./$@ -Dct < fu | tail
solver.q: solver.cc
	g++ $(OPTS) -O3 -fprofile-use -o $@ $^
	./$@ -Dct < fu | tail
