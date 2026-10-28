CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
build/text-test: text_test.cpp text.h theme.h guide_layout.h ../../common/guide.h ../../common/bindings.def ../config.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ text_test.cpp
MUTANTS = WIDTH WRAP SCALE
build/mutant-%: text_test.cpp text.h theme.h guide_layout.h ../../common/guide.h ../../common/bindings.def ../config.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DUI_MUT_$* -o $@ text_test.cpp
test: build/text-test $(addprefix build/mutant-,$(MUTANTS))
	build/text-test
	@for m in $(MUTANTS); do if build/mutant-$$m >/dev/null; then echo "MUTANT $$m SURVIVED"; exit 1; else echo "mutant $$m killed"; fi; done
clean:
	rm -rf build
.PHONY: test clean
