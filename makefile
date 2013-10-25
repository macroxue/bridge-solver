all: solver
solver: solver.c
	gcc -O3 -o $@ $^
