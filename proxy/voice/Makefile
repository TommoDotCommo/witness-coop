# Game-free (T0) test of the voice code. `make test` builds the clean test and one mutant per protection; every mutant must FAIL.
BUILD = build
SRC = voice.cpp voice_test.cpp
MUTANTS = CODEC_STATELESS NO_CHECKSUM RADIO_HARD NO_TAPER NO_PAN NO_RADIO_FILTER NO_ORDER CH_NOCHANNEL CH_IGNORED CH_RADIO_CLEAN CH_GLOBAL_DIST CH_DIRECT_ALL
$(BUILD)/voice-test: $(SRC) voice.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -o $@ $(SRC)
$(BUILD)/mutant-%: $(SRC) voice.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wno-unused-parameter -DWV_MUT_$* -o $@ $(SRC)
FOOT_MUTANTS = FS_NO_STEPS FS_NO_DISTANCE FS_NO_PAN FS_NO_DECAY
$(BUILD)/footsteps-test: voice.cpp footsteps_test.cpp footsteps.h voice.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -o $@ voice.cpp footsteps_test.cpp
$(BUILD)/footmutant-%: voice.cpp footsteps_test.cpp footsteps.h voice.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wno-unused-parameter -DWV_MUT_$* -o $@ voice.cpp footsteps_test.cpp
test: $(BUILD)/voice-test $(addprefix $(BUILD)/mutant-,$(MUTANTS)) $(BUILD)/footsteps-test $(addprefix $(BUILD)/footmutant-,$(FOOT_MUTANTS))
	$(BUILD)/voice-test
	@for m in $(MUTANTS); do if $(BUILD)/mutant-$$m >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done
	$(BUILD)/footsteps-test
	@for m in $(FOOT_MUTANTS); do if $(BUILD)/footmutant-$$m >/dev/null; then echo "FOOTSTEP MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "footstep mutant $$m killed"; fi; done
clean:
	rm -rf $(BUILD)
.PHONY: test clean
