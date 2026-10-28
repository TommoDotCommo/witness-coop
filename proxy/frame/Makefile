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

$(BUILD)/ribbon-test: ribbon_test.cpp ribbon.h panelframe.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -o $@ ribbon_test.cpp
$(BUILD)/ribbon-mutant-%: ribbon_test.cpp ribbon.h panelframe.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -DWR_MUT_$* -o $@ ribbon_test.cpp
test-ribbon: $(BUILD)/ribbon-test $(BUILD)/ribbon-mutant-VIEW_FACING $(BUILD)/ribbon-mutant-EXTEND
	$(BUILD)/ribbon-test
	@ulimit -c 0; for m in VIEW_FACING EXTEND; do if $(BUILD)/ribbon-mutant-$$m >/dev/null 2>&1; then echo "MUTANT $$m SURVIVED"; exit 1; else echo "mutant $$m killed"; fi; done
test: test-ribbon
.PHONY: test-ribbon
