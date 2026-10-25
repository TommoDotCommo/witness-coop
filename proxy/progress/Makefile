# Game-free (T0) test of the progression data. `make test` builds the clean test and one mutant per protection; every mutant must FAIL.
BUILD = build
SRC = progress.cpp progress_data.cpp progress_test.cpp
MUTANTS = NOPREREQ UNORDERED LASERS_FEW LASERS_FIRST
$(BUILD)/progress-test: $(SRC) progress.h progression.txt
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -o $@ $(SRC)
$(BUILD)/mutant-%: $(SRC) progress.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wno-unused-parameter -DWP_MUT_$* -o $@ $(SRC)
test: $(BUILD)/progress-test $(addprefix $(BUILD)/mutant-,$(MUTANTS))
	$(BUILD)/progress-test
	@for m in $(MUTANTS); do if $(BUILD)/mutant-$$m >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done
clean:
	rm -rf $(BUILD)
.PHONY: test clean
