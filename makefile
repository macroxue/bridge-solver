all: solver solver.p solver.g

OPTS=-std=c++0x -Wall

solver: solver.cc
	g++ $(OPTS) -O3 -fprofile-use -o $@ $^
solver.p: solver.cc
	g++ $(OPTS) -O3 -fprofile-generate -o $@ $^
solver.g: solver.cc
	g++ $(OPTS) -O0 -g -o $@ $^
