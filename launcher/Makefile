# Game-free (T0) test of the launcher's logic (wine is not needed: the test only uses file calls, so it runs on Windows or under WSL interop).
CXX = x86_64-w64-mingw32-g++
BUILD = build
SRC = core.cpp core_test.cpp ../proxy/progress/savemeta.cpp ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp
MUTANTS = BUILD_ALWAYS_OK NO_ROUNDTRIP INT_AS_FLOAT KEEP_DUPLICATES NO_RANGE LOOKALIKE
$(BUILD)/core-test.exe: $(SRC) core.h ../common/buildid.h ../common/buildid_fixture.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -static -o $@ $(SRC)
$(BUILD)/mutant-%.exe: $(SRC) core.h ../common/buildid.h ../common/buildid_fixture.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wno-unused-parameter -static -DWL_MUT_$* -o $@ $(SRC)
test: $(BUILD)/core-test.exe $(addsuffix .exe,$(addprefix $(BUILD)/mutant-,$(MUTANTS))) diag-test crash-test
	$(BUILD)/core-test.exe
	@for m in $(MUTANTS); do if $(BUILD)/mutant-$$m.exe >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done

# the session logs shared with the proxy (common/diag_log.h), game-free, with mutants
DIAG_MUTANTS = WEAK_CHAIN NO_CHAIN PRUNE_CRASHED PRUNE_ALL
$(BUILD)/diag-test.exe: ../common/diag_test.cpp ../common/diag_log.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -static -o $@ ../common/diag_test.cpp
$(BUILD)/diag-mutant-%.exe: ../common/diag_test.cpp ../common/diag_log.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -DDIAG_MUT_$* -static -o $@ ../common/diag_test.cpp
diag-test: $(BUILD)/diag-test.exe $(addsuffix .exe,$(addprefix $(BUILD)/diag-mutant-,$(DIAG_MUTANTS)))
	$(BUILD)/diag-test.exe
	@for m in $(DIAG_MUTANTS); do if $(BUILD)/diag-mutant-$$m.exe >/dev/null; then echo "MUTANT diag $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "diag mutant $$m killed"; fi; done
.PHONY: diag-test
clean:
	rm -rf $(BUILD)
.PHONY: test clean

# the window (not part of the test): witness_launcher.exe goes next to the game's exe
launcher: $(BUILD)/witness_launcher.exe
# the script behind "Send diagnostics" is embedded in the launcher (dist/diag.ps1), and the version comes from git describe (written only when it changes)
$(BUILD)/diag_ps1.h: ../dist/diag.ps1
	mkdir -p $(BUILD); { printf 'static const char kDiagScript[] = R"PS1('; cat ../dist/diag.ps1; printf ')PS1";\n'; } > $@
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
$(BUILD)/version.h: FORCE
	@mkdir -p $(BUILD); printf '#define MOD_VERSION "%s"\n' '$(VERSION)' > $(BUILD)/version.h.new; cmp -s $(BUILD)/version.h.new $(BUILD)/version.h && rm $(BUILD)/version.h.new || mv $(BUILD)/version.h.new $(BUILD)/version.h
FORCE:
LMUTANTS = NO_MONITOR NO_VIDEO_WRITE NO_SAVE_WRITE CFG_DROP LOBBY_NO_CFG LOBBY_NO_RESULT LOBBY_NO_RESTORE LOBBY_NO_VOICE WIDE_COMBOS TAB_REPAINT_ALWAYS LONG_LABELS LOBBY_VERSION_ANY
launcher-mutants: $(addsuffix .exe,$(addprefix $(BUILD)/witness_launcher_,$(LMUTANTS)))
$(BUILD)/witness_launcher_%.exe: ../common/guide.h ../common/bindings.def ../proxy/config.h main.cpp update_win.h update.h ../dist/install-policy.h core.cpp core.h ../proxy/core/appearance.h theme.cpp theme.h lobby.cpp lobby.h lobby_win.cpp lobby_win.h lobby_steam.cpp lobby_steam.h ../common/steam_abi.h ../common/steam_ids.h ../proxy/voice/voice.cpp $(BUILD)/launcher_res.o ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp ../proxy/progress/savemeta.cpp $(BUILD)/diag_ps1.h $(BUILD)/version.h ../common/diag_log.h ../common/crash_handler.h
	$(CXX) -std=c++17 -O2 -I$(BUILD) -D_WIN32_WINNT=0x0601 -mwindows -municode -static -DWL_MUT_$* -o $@ main.cpp core.cpp theme.cpp lobby.cpp lobby_win.cpp lobby_steam.cpp ../proxy/voice/voice.cpp ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp ../proxy/progress/savemeta.cpp $(BUILD)/launcher_res.o -lcomctl32 -lshell32 -lole32 -luuid -luxtheme -lws2_32 -lwinmm -lgdi32 -lmsimg32 -lwininet
# the manifest plus a VERSIONINFO (ProductName "The Witness Co-op", the mod version): tools/versioninfo.py writes the version part
$(BUILD)/launcher_res.o: launcher.rc launcher.manifest ../tools/versioninfo.py $(BUILD)/version.h
	mkdir -p $(BUILD)
	python3 -I ../tools/versioninfo.py '$(VERSION)' witness_launcher.exe 'The Witness Co-op launcher' $(BUILD)/launcher_version.rc
	{ cat launcher.rc; cat $(BUILD)/launcher_version.rc; } > $(BUILD)/launcher_all.rc
	x86_64-w64-mingw32-windres -I . $(BUILD)/launcher_all.rc -o $@
$(BUILD)/witness_launcher.exe: ../common/guide.h ../common/bindings.def ../proxy/config.h main.cpp update_win.h update.h ../dist/install-policy.h core.cpp core.h ../proxy/core/appearance.h theme.cpp theme.h lobby.cpp lobby.h lobby_win.cpp lobby_win.h lobby_steam.cpp lobby_steam.h ../common/steam_abi.h ../common/steam_ids.h ../proxy/voice/voice.cpp $(BUILD)/launcher_res.o ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp ../proxy/progress/savemeta.cpp $(BUILD)/diag_ps1.h $(BUILD)/version.h ../common/diag_log.h ../common/crash_handler.h
	$(CXX) -std=c++17 -O2 -Wall -I$(BUILD) -D_WIN32_WINNT=0x0601 -mwindows -municode -static -o $@ main.cpp core.cpp theme.cpp lobby.cpp lobby_win.cpp lobby_steam.cpp ../proxy/voice/voice.cpp ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp ../proxy/progress/savemeta.cpp $(BUILD)/launcher_res.o -lcomctl32 -lshell32 -lole32 -luuid -luxtheme -lws2_32 -lwinmm -lgdi32 -lmsimg32 -lwininet
.PHONY: launcher

# the lobby (game-free): the same kind of test and mutants
LSRC = lobby.cpp lobby_test.cpp core.cpp ../proxy/voice/voice.cpp ../proxy/progress/savemeta.cpp ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp
LOBBY_MUTANTS = NO_CHECKSUM GATE_OPEN VERSION_NOT_CHECKED REPLY_NO_VERSIONS LIST_NO_VERSION
$(BUILD)/lobby-test.exe: $(LSRC) lobby.h core.h ../proxy/voice/voice.h ../common/steam_ids.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -static -o $@ $(LSRC)
$(BUILD)/lobby-mutant-%.exe: $(LSRC) lobby.h core.h ../proxy/voice/voice.h ../common/steam_ids.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wno-unused-parameter -static -DWLB_MUT_$* -o $@ $(LSRC)
lobby-test: $(BUILD)/lobby-test.exe $(addsuffix .exe,$(addprefix $(BUILD)/lobby-mutant-,$(LOBBY_MUTANTS)))
	$(BUILD)/lobby-test.exe
	@for m in $(LOBBY_MUTANTS); do if $(BUILD)/lobby-mutant-$$m.exe >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "lobby mutant $$m killed"; fi; done
.PHONY: lobby-test

# the crash handler shared with the proxy (common/crash_handler.h): a child process crashes on purpose; mutants: handler off, dump from a first-chance handler
CRASH_MUTANTS = DISABLED FIRSTCHANCE
$(BUILD)/crash-test.exe: ../common/crash_test.cpp ../common/crash_handler.h ../common/diag_log.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O0 -Wall -Wextra -static -o $@ ../common/crash_test.cpp -lshell32
$(BUILD)/crash-mutant-%.exe: ../common/crash_test.cpp ../common/crash_handler.h ../common/diag_log.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O0 -DCRASH_MUT_$* -static -o $@ ../common/crash_test.cpp -lshell32
crash-test: $(BUILD)/crash-test.exe $(addsuffix .exe,$(addprefix $(BUILD)/crash-mutant-,$(CRASH_MUTANTS)))
	rm -rf $(BUILD)/crash-out; mkdir -p $(BUILD)/crash-out
	cd $(BUILD) && ./crash-test.exe crash-out
	@for m in $(CRASH_MUTANTS); do rm -rf $(BUILD)/crash-mut-$$m; mkdir -p $(BUILD)/crash-mut-$$m; if (cd $(BUILD) && ./crash-mutant-$$m.exe crash-mut-$$m) >/dev/null 2>&1; then echo "MUTANT crash $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "crash mutant $$m killed"; fi; done
.PHONY: crash-test

# the self-update (update.h, ported from the MDK2 launcher): game-free, with source mutants (tools/test_launcher_update.py)
update-test:
	python3 -I ../tools/test_launcher_update.py
.PHONY: update-test
test: update-test
