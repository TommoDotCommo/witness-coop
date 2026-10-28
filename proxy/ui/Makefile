CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
build/text-test: text_test.cpp text.h theme.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ text_test.cpp
test: build/text-test
	build/text-test
clean:
	rm -rf build
.PHONY: test clean
