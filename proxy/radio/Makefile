BUILD = build
SRC = radio_test.cpp ../voice/voice.cpp ../core/sync_core.cpp ../model/model.cpp
MUTANTS = WR_MUT_GUIDE WR_MUT_ROSTER WR_MUT_BINDING WR_MUT_RADIO_TX WR_MUT_RADIO_CHANNEL WR_MUT_RADIO_EXPIRE WR_MUT_VALIDATE WR_MUT_HOLD WR_MUT_SNAP WR_MUT_CENTER WR_MUT_CAMERA WR_MUT_POSE WV_MUT_NO_RADIO_FILTER WV_MUT_DSP_CLIP WV_MUT_PLAYER_GAIN WV_MUT_DSP_RANDOM WV_MUT_NO_SQUELCH WV_MUT_NO_HISS
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-misleading-indentation
$(BUILD)/radio-test: $(SRC) radio.h ../voice/voice.h ../model/model.h ../core/sync_core.h ../core/appearance.h ../config.h ../bindings.h
	mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -o $@ $(SRC)
$(BUILD)/mutant-%: $(SRC) radio.h ../voice/voice.h ../model/model.h ../core/sync_core.h ../bindings.h ../config.h
	mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -D$* -o $@ $(SRC)
test: $(BUILD)/radio-test $(addprefix $(BUILD)/mutant-,$(MUTANTS))
	$(BUILD)/radio-test
	@for m in $(MUTANTS); do if $(BUILD)/mutant-$$m >$(BUILD)/$$m.log 2>&1; then echo "MUTANT $$m SURVIVED"; exit 1; else echo "mutant $$m killed"; fi; done
clean:
	rm -rf $(BUILD)
.PHONY: test clean
