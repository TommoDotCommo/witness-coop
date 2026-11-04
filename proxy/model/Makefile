# Game-free (T0) tests of the character model code. `make test` builds the clean test and one mutant per protection; every mutant must FAIL.
MODELS = ../../assets/models
GLB = $(MODELS)/adventurer.glb
BUILD = build
SRC = model.cpp tests/model_test.cpp
LIFESRC = model.cpp tests/life_test.cpp
HDR = model.h gestures.h life.h emote_clip.h emote_data.h ../core/emotes.h
MUTANTS = IBM_ORDER NO_LOOP NO_NORMALIZE MIRROR NO_TELEPORT_GUARD NO_RUN MAX_JOINTS NO_BLEND NO_RUNREF EYE_IBM EYE_BIND EYE_NEG ONE_SKIN NO_FLAT_COLOR EMOTE_SUBSTRING
# the life layer (life.h) and the fidget scheduler: each mutant breaks one protection the life test must catch
# the emote quality gate (tests/emote_quality.cpp, docs/ANIMATION.md): every clip in assets/emotes passes; each code mutant and each
# bad clip in tests/emote_bad (its first line names the check it must trip) must fail
EMOTESRC = model.cpp tests/emote_quality.cpp
EMOTE_MUTANTS = WM_MUT_NO_IK WM_MUT_NO_HIPS WM_MUT_NO_STEP WE_MUT_NO_SPRING
LIFE_MUTANTS = NO_BLINK NO_LOOK LOOK_UNCLAMPED NO_BREATH NO_CLOTH CLOTH_UNDAMPED NO_FIDGET FIDGET_MOVING NO_BODY_LAG NO_BACKWALK NO_BROWS NO_HEM NO_LIPS NO_REACH
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
$(BUILD)/emote-quality: $(EMOTESRC) $(HDR) emote_sim.h ../third_party/cgltf.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -o $@ $(EMOTESRC)
$(BUILD)/emote-mutant-%: $(EMOTESRC) $(HDR) emote_sim.h
	mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Wno-unused-parameter -D$* -o $@ $(EMOTESRC)
emotes: $(BUILD)/emote-quality $(addprefix $(BUILD)/emote-mutant-,$(EMOTE_MUTANTS))
	python3 ../../tools/emote_embed.py --check
	$(BUILD)/emote-quality $(GLB) --json $(BUILD)/emote_report.json
	@for f in tests/emote_bad/*.emote; do c=$$(sed -n 's/^# expect: //p' $$f); if $(BUILD)/emote-quality $(GLB) $$f --expect $$c >/dev/null; then echo "bad clip $$f caught ($$c)"; else echo "BAD CLIP $$f NOT CAUGHT as $$c"; exit 1; fi; done
	@for m in $(EMOTE_MUTANTS); do if $(BUILD)/emote-mutant-$$m $(GLB) >/dev/null; then echo "EMOTE MUTANT $$m SURVIVED (the gate cannot fail for it)"; exit 1; else echo "emote mutant $$m killed"; fi; done
test: emotes $(BUILD)/model-test $(addprefix $(BUILD)/mutant-,$(MUTANTS)) $(BUILD)/life-test $(addprefix $(BUILD)/life-mutant-,$(LIFE_MUTANTS))
	$(BUILD)/model-test $(GLB)
	@for m in $(MUTANTS); do if $(BUILD)/mutant-$$m $(GLB) >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done
	$(BUILD)/life-test $(GLB)
	@for m in $(LIFE_MUTANTS); do if $(BUILD)/life-mutant-$$m $(GLB) >/dev/null; then echo "LIFE MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "life mutant $$m killed"; fi; done
clean:
	rm -rf $(BUILD)
.PHONY: test emotes clean
