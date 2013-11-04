all: solver
solver: solver.cc
	g++ -std=c++0x -Wall -O3 -o $@ $^
