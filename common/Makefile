CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
MUTANTS = NO_MIGRATE TOGGLE ALLOW_COLLISION MISSING_KEY
build/guide-test: guide_test.cpp guide.h bindings.def ../proxy/config.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $<
build/mutant-%: guide_test.cpp guide.h bindings.def ../proxy/config.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DGUIDE_MUT_$* -o $@ $<
test: build/guide-test $(addprefix build/mutant-,$(MUTANTS))
	build/guide-test
	@for m in $(MUTANTS); do if build/mutant-$$m >/dev/null 2>&1; then echo "MUTANT $$m SURVIVED"; exit 1; else echo "mutant $$m killed"; fi; done
clean:
	rm -rf build
.PHONY: test clean
