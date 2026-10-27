# Game-free (T0) tests of the sync core. `make test` builds sync_core.cpp once, links every test against it, and runs them side by side (make -j is on by default);
# a test whose binary has not changed since it last passed is not run again (`make retest` forces them all). The fuzz run (tests/run_t0.py) uses the hub's framework
# (HUB is the Co-op Anything clone).
MAKEFLAGS += -j8
HUB ?= $(abspath ../../../coop-anything)
FUZZ = $(HUB)/tools/fuzz
BUILD = build
CXXF = -std=c++17 -O2 -Wall -Wextra
TESTS = pov ping-names social-sync line-sync group-sync apply-retry power-sync reload-sync cable-sync cable-off race-isolation

$(BUILD)/sync_core.o: sync_core.cpp sync_core.h pov.h
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
$(BUILD)/witness_target.o: tests/witness_target.cpp sync_core.h
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
