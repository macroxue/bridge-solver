.PHONY: all sanitizer web clean test
all: solver.p solver
sanitizer: solver.m solver.a
web:
	$(MAKE) -C web
test: parallel_strains_test
	./parallel_strains_test

include opts.mk

parallel_strains_test: parallel_strains_test.cc solver.cc
	g++ $(OPTS) -O3 -D_TEST -o $@ parallel_strains_test.cc

solver.p: solver.cc
	rm -f solver.gcda
	g++ $(OPTS) -O3 -fprofile-generate -o $@ $^
	./$@ -if deals/hard/deal.8 | tail
	mv solver.p-solver.gcda solver.gcda
solver: solver.cc
	g++ $(OPTS) -O3 -fprofile-use -o $@ $^
	./$@ -if deals/hard/deal.8 | tail
solver.g: solver.cc
	g++ $(OPTS) -D_DEBUG -Og -g -o $@ $^
solver.m: solver.cc
	clang++ -std=c++17 -pthread -O3 -fsanitize=memory -o $@ $^
	./$@ -if deals/hard/deal.1
solver.a: solver.cc
	clang++ -std=c++17 -pthread -O3 -fsanitize=address -o $@ $^
	./$@ -if deals/hard/deal.1
clean:
	rm -f solver.p solver solver.g solver.m solver.a parallel_strains_test
	$(MAKE) -C web clean
