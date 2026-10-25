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
test: $(BUILD)/voice-test $(addprefix $(BUILD)/mutant-,$(MUTANTS))
	$(BUILD)/voice-test
	@for m in $(MUTANTS); do if $(BUILD)/mutant-$$m >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done
clean:
	rm -rf $(BUILD)
.PHONY: test clean
