# psolve: the panel solver for the sweep (game-free). `make -C tools/psolve` builds it, `make -C tools/psolve test` runs its tests (T0).
CXX ?= g++
psolve: psolve.cpp
	$(CXX) -O2 -std=c++17 -o psolve psolve.cpp
test: psolve tests.py
	python3 tests.py
	python3 tests.py --mutants
clean:
	rm -f psolve
