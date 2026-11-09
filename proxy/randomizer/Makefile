# Game-free (T0) tests of the built-in randomizers. `make test` builds the vendored generator natively (g++, with a stand-in windows.h) and runs:
#   backend-test     the memory layer (overlay, copy on write, fingerprint, apply), with its mutants
#   generate-test    whole runs of every randomizer over a game recorded by the `randrecord` command (local/randomizer/*.world); skipped, and
#                    said so, when no recording is there (it is game data and stays out of the repository)
# Every mutant must FAIL.
MAKEFLAGS += -j8
BUILD = build
VENDOR = ../../third_party/witness-randomizer
VSRC = Generate MultiGenerate Panel PuzzleList Quaternion Randomizer Special Watchdog
CXXF = -std=gnu++2a -O2 -DWITNESS_COOP -Itests/winstub -include $(VENDOR)/shim/compat.h -I$(VENDOR)/shim -I$(VENDOR)/src -I.
VOBJ = $(patsubst %,$(BUILD)/v/%.o,$(VSRC))
WORLDS = $(wildcard ../../local/randomizer/*.world)

$(BUILD)/v/%.o: $(VENDOR)/src/%.cpp $(VENDOR)/shim/Memory.h backend.h
	@mkdir -p $(BUILD)/v
	g++ $(CXXF) -w -c -o $@ $<
$(BUILD)/backend.o: backend.cpp backend.h $(VENDOR)/shim/Memory.h
	@mkdir -p $(BUILD)
	g++ $(CXXF) -Wall -Wextra -c -o $@ $<
$(BUILD)/backend-%.o: backend.cpp backend.h $(VENDOR)/shim/Memory.h
	@mkdir -p $(BUILD)
	g++ $(CXXF) -DWR_MUT_$* -c -o $@ $<
$(BUILD)/generate.o: generate.cpp generate.h backend.h ../../common/randomizers.h
	@mkdir -p $(BUILD)
	g++ $(CXXF) -Wall -c -o $@ $<
$(BUILD)/generate-%.o: generate.cpp generate.h backend.h ../../common/randomizers.h
	@mkdir -p $(BUILD)
	g++ $(CXXF) -DWR_MUT_$* -c -o $@ $<

$(BUILD)/backend-test: tests/backend_test.cpp tests/fake_raw.h $(BUILD)/backend.o $(BUILD)/generate.o $(VOBJ)
	g++ $(CXXF) -Wall -o $@ tests/backend_test.cpp $(BUILD)/backend.o $(BUILD)/generate.o $(VOBJ)
$(BUILD)/backend-mut-%: tests/backend_test.cpp tests/fake_raw.h $(BUILD)/backend-%.o $(BUILD)/generate.o $(VOBJ)
	g++ $(CXXF) -o $@ tests/backend_test.cpp $(BUILD)/backend-$*.o $(BUILD)/generate.o $(VOBJ)
$(BUILD)/generate-test: tests/generate_test.cpp world.h $(BUILD)/backend.o $(BUILD)/generate.o $(VOBJ)
	g++ $(CXXF) -Wall -o $@ tests/generate_test.cpp $(BUILD)/backend.o $(BUILD)/generate.o $(VOBJ)
$(BUILD)/generate-mut-%: tests/generate_test.cpp world.h $(BUILD)/backend.o $(BUILD)/generate-%.o $(VOBJ)
	g++ $(CXXF) -o $@ tests/generate_test.cpp $(BUILD)/backend.o $(BUILD)/generate-$*.o $(VOBJ)

BACKEND_MUTANTS = COW_NO_COPY FP_ADDRESSES
GENERATE_MUTANTS = NO_RESET WATCHDOG_THREAD
test: $(BUILD)/backend-test $(addprefix $(BUILD)/backend-mut-,$(BACKEND_MUTANTS)) $(BUILD)/generate-test $(addprefix $(BUILD)/generate-mut-,$(GENERATE_MUTANTS))
	$(BUILD)/backend-test
	@for m in $(BACKEND_MUTANTS); do if $(BUILD)/backend-mut-$$m >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done
	@if [ -z "$(WORLDS)" ]; then echo "generate-test: SKIPPED, no recorded game in local/randomizer/ (run tools/qa.sh randomizer once, or the randrecord command)"; else \
	  $(BUILD)/generate-test $(WORLDS) && \
	  for m in $(GENERATE_MUTANTS); do if $(BUILD)/generate-mut-$$m $(WORLDS) >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done; fi
clean:
	rm -rf $(BUILD)
.PHONY: test clean
