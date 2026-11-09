# Game-free (T0) test of the launcher's logic (wine is not needed: the test only uses file calls, so it runs on Windows or under WSL interop).
CXX = x86_64-w64-mingw32-g++
BUILD = build
SRC = core.cpp core_test.cpp ../proxy/progress/savemeta.cpp ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp
MUTANTS = BUILD_ALWAYS_OK NO_ROUNDTRIP INT_AS_FLOAT KEEP_DUPLICATES NO_RANGE LOOKALIKE ANY_TEXT ART_NO_FALLBACK CONTINUE_REPICKS
$(BUILD)/core-test.exe: $(SRC) core.h ../common/buildid.h ../common/buildid_fixture.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -static -o $@ $(SRC)
$(BUILD)/mutant-%.exe: $(SRC) core.h ../common/buildid.h ../common/buildid_fixture.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wno-unused-parameter -static -DWL_MUT_$* -o $@ $(SRC)
test: $(BUILD)/core-test.exe $(addsuffix .exe,$(addprefix $(BUILD)/mutant-,$(MUTANTS))) diag-test crash-test editor-test
	$(BUILD)/core-test.exe
	@for m in $(MUTANTS); do if $(BUILD)/mutant-$$m.exe >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done

# the puzzle editor's logic (editor_core.h), game-free with g++ (no Windows calls); mutants: a start not mirrored, a drawn line crossing gaps
ED_MUTANTS = NO_MIRROR TRACE_THROUGH_GAP
$(BUILD)/editor-test: editor_test.cpp editor_core.h ../common/wpuzzle.h
	mkdir -p $(BUILD); g++ -std=c++17 -O2 -Wall -Wextra -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -o $@ editor_test.cpp
$(BUILD)/editor-mut-%: editor_test.cpp editor_core.h ../common/wpuzzle.h
	mkdir -p $(BUILD); g++ -std=c++17 -O2 -DED_MUT_$* -o $@ editor_test.cpp
editor-test: $(BUILD)/editor-test $(addprefix $(BUILD)/editor-mut-,$(ED_MUTANTS))
	$(BUILD)/editor-test
	@for m in $(ED_MUTANTS); do if $(BUILD)/editor-mut-$$m >/dev/null; then echo "MUTANT editor $$m SURVIVED"; exit 1; else echo "mutant editor $$m killed"; fi; done
.PHONY: editor-test

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

# launcher text in the game's face through DirectWrite (theme.cpp th::Text): needs the player's data-pc.zip, so give the game folder (Windows path):
#   make text-test GAME_DIR='C:\path\to\The Witness'    (without it: SKIP). The mutant keeps GDI and must fail.
THEME_LIBS = -lcomctl32 -luxtheme -lgdi32 -lmsimg32 -ld2d1 -ldwrite -lole32
$(BUILD)/text-test.exe: text_test.cpp theme.cpp theme.h trace.h ../common/gamefont.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wall -D_WIN32_WINNT=0x0601 -municode -static -o $@ text_test.cpp theme.cpp $(THEME_LIBS)
$(BUILD)/text-mutant-GDI_GAME_FACE.exe: text_test.cpp theme.cpp theme.h trace.h ../common/gamefont.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -D_WIN32_WINNT=0x0601 -municode -static -DWL_MUT_GDI_GAME_FACE -o $@ text_test.cpp theme.cpp $(THEME_LIBS)
$(BUILD)/text-mutant-GDI_SHAPES.exe: text_test.cpp theme.cpp theme.h trace.h ../common/gamefont.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -D_WIN32_WINNT=0x0601 -municode -static -DWL_MUT_GDI_SHAPES -o $@ text_test.cpp theme.cpp $(THEME_LIBS)
text-test: $(BUILD)/text-test.exe $(BUILD)/text-mutant-GDI_GAME_FACE.exe $(BUILD)/text-mutant-GDI_SHAPES.exe
	$(BUILD)/text-test.exe '$(GAME_DIR)'
	@if [ -n "$(GAME_DIR)" ]; then for m in GDI_GAME_FACE GDI_SHAPES; do if $(BUILD)/text-mutant-$$m.exe '$(GAME_DIR)' >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "text mutant $$m killed"; fi; done; fi
.PHONY: text-test
clean:
	rm -rf $(BUILD)
.PHONY: test clean

# the window (not part of the test): witness_launcher.exe goes next to the game's exe
launcher: $(BUILD)/witness_launcher.exe
# the script behind "Send diagnostics" is embedded in the launcher (dist/diag.ps1), and the version comes from git describe (written only when it changes)
$(BUILD)/diag_ps1.h: ../dist/diag.ps1
	mkdir -p $(BUILD); { printf 'static const char kDiagScript[] = R"PS1('; cat ../dist/diag.ps1; printf ')PS1";\n'; } > $@
# the notices of the other projects in the mod (the Randomizers tab shows them in full, so every installed copy carries them)
$(BUILD)/notices_txt.h: ../dist/THIRD-PARTY-NOTICES.txt
	mkdir -p $(BUILD); { printf 'static const char kNotices[] = R"TXT('; cat ../dist/THIRD-PARTY-NOTICES.txt; printf ')TXT";\n'; } > $@
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
$(BUILD)/version.h: FORCE
	@mkdir -p $(BUILD); printf '#define MOD_VERSION "%s"\n' '$(VERSION)' > $(BUILD)/version.h.new; cmp -s $(BUILD)/version.h.new $(BUILD)/version.h && rm $(BUILD)/version.h.new || mv $(BUILD)/version.h.new $(BUILD)/version.h
FORCE:
LMUTANTS = WINDOWS_CAPTION GOAL_ALWAYS STATUS_STAYS FOCUS_TRACE_ALWAYS DARK_BANNER NO_BACKDROP LOUD_BACKDROP UPD_CHOICE_UNSAVED NF_MUT_IGNORE_REPLIES NO_PROBE_ANSWER NO_MONITOR NO_VIDEO_WRITE NO_SAVE_WRITE CFG_DROP LOBBY_NO_CFG LOBBY_NO_RESULT LOBBY_NO_RESTORE LOBBY_NO_VOICE WIDE_COMBOS TAB_REPAINT_ALWAYS LONG_LABELS LOBBY_VERSION_ANY NO_SAVE_ON_CHANGE NO_STEAM_AT_START GUIDE_REARM FJ_NO_RELAYOUT PRIMARY_BY_STYLE BUTTON_SELF_PAINT
launcher-mutants: $(addsuffix .exe,$(addprefix $(BUILD)/witness_launcher_,$(LMUTANTS)))
# finding each other and the hosting checks (C: the shared Windows headers vendored from MDK2 are C)
NF_HDRS = netfind.h ../common/coop_witness.h ../common/coop_invite.h ../common/coop_discover.h ../common/coop_discover_win.h ../common/launcher_join.h ../common/launcher_host_core.h ../common/launcher_host_win.h joinglue.h
$(BUILD)/netfind.o: netfind.c $(NF_HDRS)
	mkdir -p $(BUILD)
	x86_64-w64-mingw32-gcc -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter -D_WIN32_WINNT=0x0601 -c -o $@ netfind.c
$(BUILD)/netfind_%.o: netfind.c $(NF_HDRS)
	mkdir -p $(BUILD)
	x86_64-w64-mingw32-gcc -std=c99 -O2 -Wno-unused-parameter -D_WIN32_WINNT=0x0601 -D$* -c -o $@ netfind.c
$(BUILD)/witness_launcher_NF_MUT_IGNORE_REPLIES.exe: $(BUILD)/netfind_NF_MUT_IGNORE_REPLIES.o      # (the Join list mutant is in netfind.c)
$(BUILD)/witness_launcher_%.exe: $(NF_HDRS) $(BUILD)/netfind.o ../common/guide.h ../common/bindings.def ../proxy/config.h main.cpp editor.cpp editor.h editor_core.h ../common/wpuzzle.h ../common/wpuzzle_json.h ../common/wpuzzle_panels.h shell.cpp shell.h ../common/handover.h update_win.h update.h ../dist/install-policy.h core.cpp core.h ../proxy/core/appearance.h theme.cpp theme.h trace.h lobby.cpp lobby.h lobby_win.cpp lobby_win.h lobby_steam.cpp lobby_steam.h ../common/steam_abi.h ../common/steam_ids.h ../proxy/voice/voice.cpp $(BUILD)/launcher_res.o ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp ../proxy/progress/savemeta.cpp $(BUILD)/diag_ps1.h $(BUILD)/notices_txt.h $(BUILD)/version.h ../common/diag_log.h ../common/crash_handler.h
	$(CXX) -std=c++17 -O2 -I$(BUILD) -D_WIN32_WINNT=0x0601 -mwindows -municode -static -DWL_MUT_$* -o $@ main.cpp editor.cpp shell.cpp core.cpp theme.cpp lobby.cpp lobby_win.cpp lobby_steam.cpp ../proxy/voice/voice.cpp ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp ../proxy/progress/savemeta.cpp $(if $(filter NF_MUT_%,$*),$(BUILD)/netfind_$*.o,$(BUILD)/netfind.o) $(BUILD)/launcher_res.o -lcomctl32 -lshell32 -lole32 -luuid -luxtheme -lws2_32 -lwinmm -lgdi32 -lmsimg32 -lwininet -ld2d1 -ldwrite
# the manifest plus a VERSIONINFO (ProductName "The Witness Co-op", the mod version): tools/versioninfo.py writes the version part
$(BUILD)/launcher_res.o: launcher.rc launcher.manifest ../tools/versioninfo.py $(BUILD)/version.h
	mkdir -p $(BUILD)
	python3 -I ../tools/versioninfo.py '$(VERSION)' witness_launcher.exe 'The Witness Co-op launcher' $(BUILD)/launcher_version.rc
	{ cat launcher.rc; cat $(BUILD)/launcher_version.rc; } > $(BUILD)/launcher_all.rc
	x86_64-w64-mingw32-windres -I . $(BUILD)/launcher_all.rc -o $@
$(BUILD)/witness_launcher.exe: $(NF_HDRS) $(BUILD)/netfind.o ../common/guide.h ../common/bindings.def ../proxy/config.h main.cpp editor.cpp editor.h editor_core.h ../common/wpuzzle.h ../common/wpuzzle_json.h ../common/wpuzzle_panels.h shell.cpp shell.h ../common/handover.h update_win.h update.h ../dist/install-policy.h core.cpp core.h ../proxy/core/appearance.h theme.cpp theme.h trace.h lobby.cpp lobby.h lobby_win.cpp lobby_win.h lobby_steam.cpp lobby_steam.h ../common/steam_abi.h ../common/steam_ids.h ../proxy/voice/voice.cpp $(BUILD)/launcher_res.o ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp ../proxy/progress/savemeta.cpp $(BUILD)/diag_ps1.h $(BUILD)/notices_txt.h $(BUILD)/version.h ../common/diag_log.h ../common/crash_handler.h
	$(CXX) -std=c++17 -O2 -Wall -I$(BUILD) -D_WIN32_WINNT=0x0601 -mwindows -municode -static -o $@ main.cpp editor.cpp shell.cpp core.cpp theme.cpp lobby.cpp lobby_win.cpp lobby_steam.cpp ../proxy/voice/voice.cpp ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp ../proxy/progress/savemeta.cpp $(BUILD)/netfind.o $(BUILD)/launcher_res.o -lcomctl32 -lshell32 -lole32 -luuid -luxtheme -lws2_32 -lwinmm -lgdi32 -lmsimg32 -lwininet -ld2d1 -ldwrite
.PHONY: launcher

# the lobby (game-free): the same kind of test and mutants
LSRC = lobby.cpp lobby_test.cpp core.cpp ../proxy/voice/voice.cpp ../proxy/progress/savemeta.cpp ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp
LOBBY_MUTANTS = NO_CHECKSUM GATE_OPEN VERSION_NOT_CHECKED REPLY_NO_VERSIONS LIST_NO_VERSION INVITE_ONLY_OPEN NO_RANDOMIZER START_WITHOUT_PUZZLES SET_UNCHECKED
$(BUILD)/lobby-test.exe: $(LSRC) lobby.h core.h ../proxy/voice/voice.h ../common/steam_ids.h ../common/wpuzzle.h ../common/randomizers.h
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

# the launcher's shape (shell.h: tabs, settings rows, key capture, pad navigation, hints) and the Steam start decision (common/handover.h): game-free, with mutants
SHSRC = shell.cpp shell_test.cpp core.cpp ../proxy/progress/savemeta.cpp ../proxy/progress/progress.cpp ../proxy/progress/progress_data.cpp
SHELL_MUTANTS = SHELL_MUT_ALLOW_F1 SHELL_MUT_PAD_LEVEL HANDOVER_MUT_IGNORE_SESSION SHELL_MUT_TYPED_VOLUME SHELL_MUT_PLAYER_NUMBER SHELL_MUT_TV_DEFAULT_ON SHELL_MUT_RADIO_DEFAULT_ON
$(BUILD)/shell-test.exe: $(SHSRC) shell.h core.h ../common/handover.h ../common/guide.h ../common/bindings.def ../proxy/config.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -static -o $@ $(SHSRC)
$(BUILD)/shell-mutant-%.exe: $(SHSRC) shell.h core.h ../common/handover.h ../common/guide.h ../common/bindings.def ../proxy/config.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wno-unused-parameter -static -D$* -o $@ $(SHSRC)
shell-test: $(BUILD)/shell-test.exe $(addsuffix .exe,$(addprefix $(BUILD)/shell-mutant-,$(SHELL_MUTANTS)))
	$(BUILD)/shell-test.exe
	@for m in $(SHELL_MUTANTS); do if $(BUILD)/shell-mutant-$$m.exe >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "shell mutant $$m killed"; fi; done
.PHONY: shell-test
test: shell-test

# the moving puzzle line (trace.h): solution paths, the drawn part, a button's ring, the timing; game-free, with mutants
TRACE_MUTANTS = TR_MUT_NO_SEED TR_MUT_DIAGONAL TR_MUT_REVISIT TR_MUT_PREFIX_NODES TR_MUT_RING_CCW TR_MUT_NO_FADE
$(BUILD)/trace-test.exe: trace_test.cpp trace.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -static -o $@ trace_test.cpp
$(BUILD)/trace-mutant-%.exe: trace_test.cpp trace.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wno-unused-parameter -Wno-unused-variable -static -D$* -o $@ trace_test.cpp
trace-test: $(BUILD)/trace-test.exe $(addsuffix .exe,$(addprefix $(BUILD)/trace-mutant-,$(TRACE_MUTANTS)))
	$(BUILD)/trace-test.exe
	@for m in $(TRACE_MUTANTS); do if $(BUILD)/trace-mutant-$$m.exe >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "trace mutant $$m killed"; fi; done
.PHONY: trace-test
test: trace-test

# the self-update (update.h, ported from the MDK2 launcher): game-free, with source mutants (tools/test_launcher_update.py)
update-test:
	python3 -I ../tools/test_launcher_update.py
.PHONY: update-test
test: update-test

# finding each other and the invite (joinglue.h around the shared code in common/, VENDORED.md): game-free, with mutants
JG_HDRS = joinglue.h ../common/coop_witness.h ../common/coop_invite.h ../common/coop_discover.h ../common/launcher_join.h ../common/launcher_host_core.h
JOINGLUE_MUTANTS = NO_STEAM_INVITE OLD_FORM_DROPPED WRONG_GAME_ID KEEP_INVITE_ONLY_ROW
$(BUILD)/joinglue-test.exe: joinglue_test.cpp $(JG_HDRS)
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I../common -static -o $@ joinglue_test.cpp
$(BUILD)/joinglue-mutant-%.exe: joinglue_test.cpp $(JG_HDRS)
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -I../common -static -DJG_MUT_$* -o $@ joinglue_test.cpp
joinglue-test: $(BUILD)/joinglue-test.exe $(addsuffix .exe,$(addprefix $(BUILD)/joinglue-mutant-,$(JOINGLUE_MUTANTS)))
	$(BUILD)/joinglue-test.exe
	@for m in $(JOINGLUE_MUTANTS); do if $(BUILD)/joinglue-mutant-$$m.exe >/dev/null; then echo "MUTANT joinglue $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "joinglue mutant $$m killed"; fi; done
.PHONY: joinglue-test
test: joinglue-test
