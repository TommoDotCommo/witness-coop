# Game-free (T0) test of the see-through geometry. `make test` builds the clean test and one mutant per protection; every mutant must FAIL.
BUILD = build
MUTANTS = BROAD_NEAR PANEL NOVERTICAL NOFADE NOSMOOTH
$(BUILD)/ghost-test: ghost_test.cpp ghost.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -o $@ ghost_test.cpp
$(BUILD)/mutant-%: ghost_test.cpp ghost.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wno-unused-parameter -DWG_MUT_$* -o $@ ghost_test.cpp
test: $(BUILD)/ghost-test $(addprefix $(BUILD)/mutant-,$(MUTANTS))
	$(BUILD)/ghost-test
	@for m in $(MUTANTS); do if $(BUILD)/mutant-$$m >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done
clean:
	rm -rf $(BUILD)
.PHONY: test clean
