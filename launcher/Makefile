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
LMUTANTS = NO_VIDEO_WRITE NO_SAVE_WRITE CFG_DROP LOBBY_NO_CFG LOBBY_NO_RESULT LOBBY_NO_RESTORE LOBBY_NO_VOICE WIDE_COMBOS TAB_REPAINT_ALWAYS
launcher-mutants: $(addsuffix .exe,$(addprefix $(BUILD)/witness_launcher_,$(LMUTANTS)))
$(BUILD)/witness_launcher_%.exe: main.cpp core.cpp core.h theme.cpp theme.h lobby.cpp lobby.h lobby_win.cpp lobby_win.h ../proxy/voice/voice.cpp $(BUILD)/launcher_res.o ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp ../proxy/progress/savemeta.cpp
	$(CXX) -std=c++17 -O2 -D_WIN32_WINNT=0x0601 -mwindows -municode -static -DWL_MUT_$* -o $@ main.cpp core.cpp theme.cpp lobby.cpp lobby_win.cpp ../proxy/voice/voice.cpp ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp ../proxy/progress/savemeta.cpp $(BUILD)/launcher_res.o -lcomctl32 -lshell32 -lole32 -luuid -luxtheme -lws2_32 -lwinmm -lgdi32 -lmsimg32
$(BUILD)/launcher_res.o: launcher.rc launcher.manifest
	mkdir -p $(BUILD)
	x86_64-w64-mingw32-windres launcher.rc -o $@
$(BUILD)/witness_launcher.exe: main.cpp core.cpp core.h theme.cpp theme.h lobby.cpp lobby.h lobby_win.cpp lobby_win.h ../proxy/voice/voice.cpp $(BUILD)/launcher_res.o ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp ../proxy/progress/savemeta.cpp
	$(CXX) -std=c++17 -O2 -Wall -D_WIN32_WINNT=0x0601 -mwindows -municode -static -o $@ main.cpp core.cpp theme.cpp lobby.cpp lobby_win.cpp ../proxy/voice/voice.cpp ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp ../proxy/progress/savemeta.cpp $(BUILD)/launcher_res.o -lcomctl32 -lshell32 -lole32 -luuid -luxtheme -lws2_32 -lwinmm -lgdi32 -lmsimg32
.PHONY: launcher

# the lobby (game-free): the same kind of test and mutants
LSRC = lobby.cpp lobby_test.cpp core.cpp ../proxy/voice/voice.cpp ../proxy/progress/savemeta.cpp ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp
LOBBY_MUTANTS = NO_CHECKSUM
$(BUILD)/lobby-test.exe: $(LSRC) lobby.h core.h ../proxy/voice/voice.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -static -o $@ $(LSRC)
$(BUILD)/lobby-mutant-%.exe: $(LSRC) lobby.h core.h ../proxy/voice/voice.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wno-unused-parameter -static -DWLB_MUT_$* -o $@ $(LSRC)
lobby-test: $(BUILD)/lobby-test.exe $(addsuffix .exe,$(addprefix $(BUILD)/lobby-mutant-,$(LOBBY_MUTANTS)))
	$(BUILD)/lobby-test.exe
	@for m in $(LOBBY_MUTANTS); do if $(BUILD)/lobby-mutant-$$m.exe >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "lobby mutant $$m killed"; fi; done
.PHONY: lobby-test
