all: solver
solver: solver.cc
	g++ -Wall -O3 -o $@ $^
