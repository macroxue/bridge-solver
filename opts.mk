# Native g++/clang++ flags, tuned to the CPU actually running the build.
# Shared by makefile and web/makefile (included via ../opts.mk there).
OPTS=-std=c++17 -Wall -Wno-missing-profile
ifeq (sse4_2, $(shell grep -m1 -o sse4_2 /proc/cpuinfo))
	OPTS+=-msse4.2
endif
ifeq (bmi1, $(shell grep -m1 -o bmi1 /proc/cpuinfo))
	OPTS+=-mbmi
endif
ifeq (bmi2, $(shell grep -m1 -o bmi2 /proc/cpuinfo))
	OPTS+=-mbmi2
endif
