all: solver
solver: solver.cc
	g++ -O3 -o $@ $^
