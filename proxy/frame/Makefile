# Game-free (T0) test of the panel frame. `make test` builds the clean test and one mutant per protection; every mutant must FAIL.
BUILD = build
MUTANTS = NO_ROTATION WHOLE_EDGE
$(BUILD)/frame-test: frame_test.cpp panelframe.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -o $@ frame_test.cpp
$(BUILD)/mutant-%: frame_test.cpp panelframe.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wno-unused-parameter -Wno-missing-field-initializers -DWF_MUT_$* -o $@ frame_test.cpp
test: $(BUILD)/frame-test $(addprefix $(BUILD)/mutant-,$(MUTANTS))
	$(BUILD)/frame-test
	@for m in $(MUTANTS); do if $(BUILD)/mutant-$$m >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done
clean:
	rm -rf $(BUILD)
.PHONY: test clean
