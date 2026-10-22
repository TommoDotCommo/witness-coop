# Game-free (T0) tests of the sync core: builds the hub's fuzzer with our target, natively, no CMake needed.
# HUB is the Co-op Anything clone (the fuzzer framework lives there).
HUB ?= $(HOME)/source/coop-anything
FUZZ = $(HUB)/tools/fuzz
BUILD = build
FUZZSRC = $(FUZZ)/src/main.cpp $(FUZZ)/src/net.cpp $(FUZZ)/src/scenario.cpp $(FUZZ)/src/oracles.cpp $(FUZZ)/src/runner.cpp $(FUZZ)/src/targets.cpp
$(BUILD)/coopfuzz-witness: tests/witness_target.cpp sync_core.cpp sync_core.h $(FUZZSRC)
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -I$(FUZZ)/include -o $@ $(FUZZSRC) tests/witness_target.cpp sync_core.cpp
$(BUILD)/apply-retry: tests/apply_retry.cpp sync_core.cpp sync_core.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -o $@ tests/apply_retry.cpp sync_core.cpp
test: $(BUILD)/coopfuzz-witness $(BUILD)/apply-retry
	$(BUILD)/apply-retry
	python3 tests/run_t0.py --hub $(HUB)
clean:
	rm -rf $(BUILD)
.PHONY: test clean
