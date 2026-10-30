# Game-free (T0) tests of the character model code. `make test` builds the clean test and one mutant per protection; every mutant must FAIL.
MODELS = ../../assets/models
GLB = $(MODELS)/adventurer.glb
BUILD = build
SRC = model.cpp tests/model_test.cpp
LIFESRC = model.cpp tests/life_test.cpp
HDR = model.h gestures.h life.h ../core/emotes.h
MUTANTS = IBM_ORDER NO_LOOP NO_NORMALIZE MIRROR NO_TELEPORT_GUARD NO_RUN MAX_JOINTS NO_BLEND NO_RUNREF EYE_IBM EYE_BIND EYE_NEG ONE_SKIN NO_FLAT_COLOR EMOTE_SUBSTRING
# the life layer (life.h) and the fidget scheduler: each mutant breaks one protection the life test must catch
LIFE_MUTANTS = NO_BLINK NO_LOOK LOOK_UNCLAMPED NO_BREATH NO_CLOTH CLOTH_UNDAMPED NO_FIDGET FIDGET_MOVING NO_BODY_LAG NO_BACKWALK
$(BUILD)/model-test: $(SRC) $(HDR) ../third_party/cgltf.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -o $@ $(SRC)
$(BUILD)/mutant-%: $(SRC) $(HDR)
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wno-unused-parameter -DWM_MUT_$* -o $@ $(SRC)
$(BUILD)/life-test: $(LIFESRC) $(HDR) ../third_party/cgltf.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -o $@ $(LIFESRC)
$(BUILD)/life-mutant-%: $(LIFESRC) $(HDR)
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wno-unused-parameter -DWM_MUT_$* -o $@ $(LIFESRC)
test: $(BUILD)/model-test $(addprefix $(BUILD)/mutant-,$(MUTANTS)) $(BUILD)/life-test $(addprefix $(BUILD)/life-mutant-,$(LIFE_MUTANTS))
	$(BUILD)/model-test $(GLB)
	@for m in $(MUTANTS); do if $(BUILD)/mutant-$$m $(GLB) >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done
	$(BUILD)/life-test $(GLB)
	@for m in $(LIFE_MUTANTS); do if $(BUILD)/life-mutant-$$m $(GLB) >/dev/null; then echo "LIFE MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "life mutant $$m killed"; fi; done
clean:
	rm -rf $(BUILD)
.PHONY: test clean
