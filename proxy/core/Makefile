# Game-free (T0) tests of the sync core: builds the hub's fuzzer with our target, natively, no CMake needed.
# HUB is the Co-op Anything clone (the fuzzer framework lives there).
HUB ?= $(abspath ../../../coop-anything)
FUZZ = $(HUB)/tools/fuzz
BUILD = build
FUZZSRC = $(FUZZ)/src/main.cpp $(FUZZ)/src/net.cpp $(FUZZ)/src/scenario.cpp $(FUZZ)/src/oracles.cpp $(FUZZ)/src/runner.cpp $(FUZZ)/src/targets.cpp
$(BUILD)/coopfuzz-witness: tests/witness_target.cpp sync_core.cpp sync_core.h $(FUZZSRC)
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -I$(FUZZ)/include -o $@ $(FUZZSRC) tests/witness_target.cpp sync_core.cpp
$(BUILD)/apply-retry: tests/apply_retry.cpp sync_core.cpp sync_core.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -o $@ tests/apply_retry.cpp sync_core.cpp
$(BUILD)/power-sync: tests/power_sync.cpp sync_core.cpp sync_core.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -o $@ tests/power_sync.cpp sync_core.cpp
$(BUILD)/reload-sync: tests/reload_sync.cpp sync_core.cpp sync_core.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -o $@ tests/reload_sync.cpp sync_core.cpp
$(BUILD)/cable-sync: tests/cable_sync.cpp sync_core.cpp sync_core.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -o $@ tests/cable_sync.cpp sync_core.cpp
$(BUILD)/cable-off: tests/cable_off.cpp sync_core.cpp sync_core.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -o $@ tests/cable_off.cpp sync_core.cpp
$(BUILD)/group-sync: tests/group_sync.cpp sync_core.cpp sync_core.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -o $@ tests/group_sync.cpp sync_core.cpp
test: $(BUILD)/group-sync $(BUILD)/coopfuzz-witness $(BUILD)/apply-retry $(BUILD)/power-sync $(BUILD)/reload-sync $(BUILD)/cable-sync $(BUILD)/cable-off
	$(BUILD)/group-sync
	$(BUILD)/apply-retry
	$(BUILD)/power-sync
	$(BUILD)/reload-sync
	$(BUILD)/cable-sync
	$(BUILD)/cable-off
	$(MAKE) -C ../model test
	python3 tests/run_t0.py --hub $(HUB)
clean:
	rm -rf $(BUILD)
.PHONY: test clean

$(BUILD)/group-transport.exe: tests/group_transport.cpp ../coop.cpp ../coop.h sync_core.cpp sync_core.h
	mkdir -p $(BUILD)
	x86_64-w64-mingw32-g++ -std=c++17 -O2 -D_WIN32_WINNT=0x0601 -static -o $@ tests/group_transport.cpp ../coop.cpp sync_core.cpp -lws2_32
transport-test: $(BUILD)/group-transport.exe
	python3 tests/run_group_transport.py
.PHONY: transport-test
