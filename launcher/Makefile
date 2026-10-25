# Game-free (T0) test of the launcher's logic (wine is not needed: the test only uses file calls, so it runs on Windows or under WSL interop).
CXX = x86_64-w64-mingw32-g++
BUILD = build
SRC = core.cpp core_test.cpp ../proxy/progress/savemeta.cpp ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp
MUTANTS = NO_ROUNDTRIP INT_AS_FLOAT KEEP_DUPLICATES NO_RANGE LOOKALIKE
$(BUILD)/core-test.exe: $(SRC) core.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -static -o $@ $(SRC)
$(BUILD)/mutant-%.exe: $(SRC) core.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wno-unused-parameter -static -DWL_MUT_$* -o $@ $(SRC)
test: $(BUILD)/core-test.exe $(addsuffix .exe,$(addprefix $(BUILD)/mutant-,$(MUTANTS)))
	$(BUILD)/core-test.exe
	@for m in $(MUTANTS); do if $(BUILD)/mutant-$$m.exe >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done
clean:
	rm -rf $(BUILD)
.PHONY: test clean

# the window (not part of the test): witness_launcher.exe goes next to the game's exe
launcher: $(BUILD)/witness_launcher.exe
$(BUILD)/launcher_res.o: launcher.rc launcher.manifest
	mkdir -p $(BUILD)
	x86_64-w64-mingw32-windres launcher.rc -o $@
$(BUILD)/witness_launcher.exe: main.cpp core.cpp core.h $(BUILD)/launcher_res.o ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp ../proxy/progress/savemeta.cpp
	$(CXX) -std=c++17 -O2 -Wall -mwindows -municode -static -o $@ main.cpp core.cpp ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp ../proxy/progress/savemeta.cpp $(BUILD)/launcher_res.o -lcomctl32 -lshell32 -lole32 -luuid
.PHONY: launcher
