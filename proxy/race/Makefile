# Game-free (T0) test of the race. `make test` builds the clean test and one mutant per protection; every mutant must FAIL.
BUILD = build
SRC = race.cpp race_test.cpp
MUTANTS = NO_GATE NO_BASELINE NO_GRACE NO_GOAL_CHECK NO_CHECKSUM NO_HOST
$(BUILD)/race-test: $(SRC) race.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -o $@ $(SRC)
$(BUILD)/mutant-%: $(SRC) race.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wno-unused-parameter -DWR_MUT_$* -o $@ $(SRC)
test: $(BUILD)/race-test $(addprefix $(BUILD)/mutant-,$(MUTANTS))
	$(BUILD)/race-test
	@for m in $(MUTANTS); do if $(BUILD)/mutant-$$m >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done
clean:
	rm -rf $(BUILD)
.PHONY: test clean
