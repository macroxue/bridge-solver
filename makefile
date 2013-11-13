all: solver.p solver solver.g

OPTS=-std=c++0x -Wall

solver.p: solver.cc
	g++ $(OPTS) -O3 -fprofile-generate -o $@ $^
	./$@ -Dct < fu | tail
solver: solver.cc
	g++ $(OPTS) -O3 -fprofile-use -o $@ $^
	./$@ -Dct < fu | tail
solver.g: solver.cc
	g++ $(OPTS) -O0 -g -o $@ $^
