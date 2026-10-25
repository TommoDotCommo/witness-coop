# Game-free (T0) tests of the character model code. `make test` builds the clean test and one mutant per protection; every mutant must FAIL.
MODELS = ../../assets/models
GLB = $(MODELS)/cesium-man.glb $(MODELS)/fox.glb $(MODELS)/rigged-figure.glb
BUILD = build
SRC = model.cpp tests/model_test.cpp
MUTANTS = IBM_ORDER NO_LOOP NO_NORMALIZE MIRROR NO_TELEPORT_GUARD NO_RUN MAX_JOINTS NO_BLEND NO_RUNREF EYE_IBM EYE_BIND EYE_NEG ONE_SKIN NO_FLAT_COLOR
$(BUILD)/model-test: $(SRC) model.h ../third_party/cgltf.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -o $@ $(SRC)
$(BUILD)/mutant-%: $(SRC) model.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wno-unused-parameter -DWM_MUT_$* -o $@ $(SRC)
test: $(BUILD)/model-test $(addprefix $(BUILD)/mutant-,$(MUTANTS))
	$(BUILD)/model-test $(GLB)
	@for m in $(MUTANTS); do if $(BUILD)/mutant-$$m $(GLB) >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done
clean:
	rm -rf $(BUILD)
.PHONY: test clean
