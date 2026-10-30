# Game-free (T0) tests of the sync core. `make test` builds sync_core.cpp once, links every test against it, and runs them side by side (make -j is on by default);
# a test whose binary has not changed since it last passed is not run again (`make retest` forces them all). The fuzz run (tests/run_t0.py) uses the hub's framework
# (HUB is the Co-op Anything clone).
MAKEFLAGS += -j8
HUB ?= $(abspath ../../../coop-anything)
FUZZ = $(HUB)/tools/fuzz
BUILD = build
CXXF = -std=c++17 -O2 -Wall -Wextra
TESTS = pov ping-names social-sync line-sync group-sync apply-retry power-sync reload-sync cable-sync cable-off race-isolation resend-count unsolve-sync

$(BUILD)/sync_core.o: sync_core.cpp sync_core.h line_precision.h appearance.h emotes.h pov.h ../radio/radio.h
	@mkdir -p $(BUILD)
	g++ $(CXXF) -c -o $@ sync_core.cpp

# one binary and one run stamp per test: build/<name>, build/<name>.ok (the log is build/<name>.log)
define TEST
$$(BUILD)/$(1): tests/$(subst -,_,$(1)).cpp $$(BUILD)/sync_core.o sync_core.h pov.h
	@mkdir -p $$(BUILD)
	g++ $$(CXXF) -o $$@ tests/$(subst -,_,$(1)).cpp $$(BUILD)/sync_core.o
$$(BUILD)/$(1).ok: $$(BUILD)/$(1)
	@if $$(BUILD)/$(1) > $$(BUILD)/$(1).log 2>&1; then touch $$@; else cat $$(BUILD)/$(1).log; rm -f $$@; exit 1; fi
endef
$(foreach t,$(TESTS),$(eval $(call TEST,$(t))))

# the hub's fuzzer, with our target: its sources are compiled once into objects
FUZZSRC = main net scenario oracles runner targets
FUZZOBJ = $(patsubst %,$(BUILD)/fuzz/%.o,$(FUZZSRC))
$(BUILD)/fuzz/%.o: $(FUZZ)/src/%.cpp
	@mkdir -p $(BUILD)/fuzz
	g++ $(CXXF) -I$(FUZZ)/include -c -o $@ $<
$(BUILD)/witness_target.o: tests/witness_target.cpp sync_core.h appearance.h
	@mkdir -p $(BUILD)
	g++ $(CXXF) -I$(FUZZ)/include -c -o $@ tests/witness_target.cpp
$(BUILD)/coopfuzz-witness: $(FUZZOBJ) $(BUILD)/witness_target.o $(BUILD)/sync_core.o
	g++ $(CXXF) -o $@ $^
$(BUILD)/t0.ok: $(BUILD)/coopfuzz-witness tests/run_t0.py
	@if python3 tests/run_t0.py --hub $(HUB) > $(BUILD)/t0.log 2>&1; then touch $@; else cat $(BUILD)/t0.log; rm -f $@; exit 1; fi

STAMPS = $(foreach t,$(TESTS),$(BUILD)/$(t).ok) $(BUILD)/t0.ok
test: $(STAMPS)
	@for t in $(TESTS) t0; do echo "== $$t"; cat $(BUILD)/$$t.log; done
	$(MAKE) -C ../model test
retest:
	rm -f $(BUILD)/*.ok
	$(MAKE) test
clean:
	rm -rf $(BUILD)
.PHONY: test retest clean
$(BUILD)/group-transport.exe: tests/group_transport.cpp ../coop.cpp ../coop.h sync_core.cpp sync_core.h
	mkdir -p $(BUILD)
	x86_64-w64-mingw32-g++ -std=c++17 -O2 -D_WIN32_WINNT=0x0601 -static -o $@ tests/group_transport.cpp ../coop.cpp sync_core.cpp -lws2_32
transport-test: $(BUILD)/group-transport.exe
	python3 tests/run_group_transport.py
.PHONY: transport-test

$(BUILD)/fake-pose: tests/fake_pose.cpp remote_pose.h appearance.h
	@mkdir -p $(BUILD)
	g++ $(CXXF) -o $@ tests/fake_pose.cpp
$(BUILD)/fake-pose-mutant: tests/fake_pose.cpp remote_pose.h appearance.h
	@mkdir -p $(BUILD)
	g++ $(CXXF) -DWM_MUT_FAKE_METADATA -o $@ tests/fake_pose.cpp
fake-pose-test: $(BUILD)/fake-pose $(BUILD)/fake-pose-mutant
	$(BUILD)/fake-pose
	@if $(BUILD)/fake-pose-mutant; then echo 'fake metadata mutant survived'; exit 1; else echo 'fake metadata mutant rejected'; fi
test: fake-pose-test
.PHONY: fake-pose-test

$(BUILD)/protocol.ok: tests/protocol.cpp tests/run_protocol.py sync_core.cpp sync_core.h appearance.h
	@mkdir -p $(BUILD)
	@if python3 tests/run_protocol.py > $(BUILD)/protocol.log 2>&1; then cat $(BUILD)/protocol.log; touch $@; else cat $(BUILD)/protocol.log; exit 1; fi
test: $(BUILD)/protocol.ok

$(BUILD)/appearance.ok: appearance.h sync_core.cpp sync_core.h tests/appearance.cpp tests/protocol.cpp tests/run_appearance.py
	@mkdir -p $(BUILD)
	@if python3 tests/run_appearance.py > $(BUILD)/appearance.log 2>&1; then cat $(BUILD)/appearance.log; touch $@; else cat $(BUILD)/appearance.log; exit 1; fi
test: $(BUILD)/appearance.ok

test: appearance-palette-test
appearance-palette-test:
	python3 ../../tools/test_appearance.py
.PHONY: appearance-palette-test

test: appearance-label-test
appearance-label-test:
	python3 ../../tools/test_appearance_labels.py
.PHONY: appearance-label-test

test: appearance-asset-validator-test
appearance-asset-validator-test:
	python3 ../../tools/test_appearance_asset.py
.PHONY: appearance-asset-validator-test

test: gesture-test
gesture-test:
	python3 ../../tools/test_gestures.py
.PHONY: gesture-test

test: radio-test
radio-test:
	$(MAKE) -C ../radio test
.PHONY: radio-test

# Optional back TV: wire fuzz, bounded animation/layout and mutation oracles.
test: tv-test
tv-test:
	python3 ../../tools/test_tv.py
.PHONY: tv-test

$(BUILD)/line-precision: tests/line_precision.cpp tests/line_sync.cpp line_precision.h ../frame/panelframe.h ../frame/ribbon.h $(BUILD)/sync_core.o
	g++ $(CXXF) -o $@ tests/line_precision.cpp $(BUILD)/sync_core.o
$(BUILD)/line-precision-coarse: tests/line_precision.cpp tests/line_sync.cpp line_precision.h ../frame/panelframe.h ../frame/ribbon.h $(BUILD)/sync_core.o
	g++ $(CXXF) -DWL_MUT_COARSE -o $@ tests/line_precision.cpp $(BUILD)/sync_core.o
line-precision-test: $(BUILD)/line-precision $(BUILD)/line-precision-coarse
	$(BUILD)/line-precision
	@ulimit -c 0; if $(BUILD)/line-precision-coarse >/dev/null 2>&1; then echo 'coarse precision mutant survived'; exit 1; else echo 'coarse precision mutant rejected'; fi
test: line-precision-test
.PHONY: line-precision-test
line-precision-mutants:
	python3 tests/run_line_precision.py
test: line-precision-mutants
.PHONY: line-precision-mutants
line-capture-test:
	python3 ../../tools/test_line_precision_capture.py
	python3 ../../tools/test_line_capture_pixels.py
test: line-capture-test
.PHONY: line-capture-test
