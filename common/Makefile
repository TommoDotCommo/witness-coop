CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
MUTANTS = NO_MIGRATE TOGGLE ALLOW_COLLISION MISSING_KEY MARKER_BOUND
build/guide-test: guide_test.cpp guide.h bindings.def ../proxy/config.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $<
build/mutant-%: guide_test.cpp guide.h bindings.def ../proxy/config.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DGUIDE_MUT_$* -o $@ $<
build/buildid-test: buildid_test.cpp buildid.h sha256.h buildid_fixture.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -o $@ $<
build/buildid-mutant: buildid_test.cpp buildid.h sha256.h buildid_fixture.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DBI_MUT_SKIP_TEXT -o $@ $<
test: build/guide-test $(addprefix build/mutant-,$(MUTANTS)) build/buildid-test build/buildid-mutant
	build/buildid-test $(BUILDID_EXE)
	@if build/buildid-mutant >/dev/null 2>&1; then echo "MUTANT buildid SKIP_TEXT SURVIVED"; exit 1; else echo "mutant buildid SKIP_TEXT killed"; fi
	build/guide-test
	@for m in $(MUTANTS); do if build/mutant-$$m >/dev/null 2>&1; then echo "MUTANT $$m SURVIVED"; exit 1; else echo "mutant $$m killed"; fi; done
clean:
	rm -rf build
.PHONY: test clean
