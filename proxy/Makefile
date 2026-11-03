# Builds dsound.dll (the proxy loaded by the game from its own folder; it forwards to the system dsound.dll). Needs x86_64-w64-mingw32-g++ (MinGW-w64, 64-bit).
# One object per source with its header dependencies, so a change to one file recompiles that file only; `make -j` compiles the rest side by side.
MAKEFLAGS += -j8
CXX = x86_64-w64-mingw32-g++
OUT = dsound.dll
.DEFAULT_GOAL := $(OUT)
SRC = diag.cpp thirdperson.cpp tracer.cpp guide_ui.cpp tv.cpp radio/radio_game.cpp race_game.cpp race/race.cpp voice_audio.cpp voice/voice.cpp dsound_proxy.cpp overlay.cpp gamecam.cpp model_gpu.cpp depth.cpp model/model.cpp coop.cpp steam_net.cpp game.cpp state.cpp hooks.cpp config.cpp input.cpp ui.cpp devmenu.cpp social.cpp video.cpp savepoints.cpp core/sync_core.cpp mapgen.cpp savemeta_game.cpp progress/progress.cpp progress/savemeta.cpp progress/progress_data.cpp
OBJ = $(patsubst %.cpp,build/%.o,$(SRC))
CXXFLAGS = -O2 -std=c++17 -D_WIN32_WINNT=0x0601 -MMD -MP -Ibuild
LIBS = -ld3d11 -ldxgi -luuid -lws2_32 -lgdi32 -lshell32 -lwindowscodecs -lole32 -loleaut32 -lwinmm
# the version the logs and the diagnostics name: `git describe`, written to build/version.h only when it changes (so only diag.cpp recompiles)
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
build/version.h: FORCE
	@mkdir -p build; printf '#define MOD_VERSION "%s"\n' '$(VERSION)' > build/version.h.new; cmp -s build/version.h.new build/version.h && rm build/version.h.new || mv build/version.h.new build/version.h
FORCE:
build/diag.o build/coop.o: build/version.h
RES = build/proxy_res.o
$(RES): proxy.rc build/version.h
	x86_64-w64-mingw32-windres -Ibuild proxy.rc -o $@
$(OUT): $(OBJ) $(RES) dsound.def
	$(CXX) -shared -static-libgcc -static-libstdc++ -Wl,-Map=dsound.map -o $@ $(OBJ) $(RES) dsound.def $(LIBS)
build/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
# Test build for tools/unknown_build_slot.sh: the same proxy with the build check ignored (a mutant that must be caught). Only dsound_proxy.cpp differs.
build/dsound_proxy_mut_gate.o: dsound_proxy.cpp Makefile
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -DCOOP_MUT_ALWAYS_KNOWN -c -o $@ $<
dsound_mut_gate.dll: $(filter-out build/dsound_proxy.o,$(OBJ)) build/dsound_proxy_mut_gate.o $(RES) dsound.def
	$(CXX) -shared -static-libgcc -static-libstdc++ -o $@ $(filter-out build/dsound_proxy.o,$(OBJ)) build/dsound_proxy_mut_gate.o $(RES) dsound.def $(LIBS)
# Test builds for tools/qa_dropin.sh: dsound_mut_NO_CHAIN.dll (the chain file ignored), dsound_mut_NO_OTHER_COPY.dll (the alpha's proxy not looked for).
build/dsound_proxy_mut_%.o: dsound_proxy.cpp Makefile
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -DCOOP_MUT_$* -c -o $@ $<
dsound_mut_%.dll: $(filter-out build/dsound_proxy.o,$(OBJ)) build/dsound_proxy_mut_%.o $(RES) dsound.def
	$(CXX) -shared -static-libgcc -static-libstdc++ -o $@ $(filter-out build/dsound_proxy.o,$(OBJ)) build/dsound_proxy_mut_$*.o $(RES) dsound.def $(LIBS)
-include $(OBJ:.o=.d)
clean:
	rm -rf build $(OUT) dsound.map dsound_mut_*.dll
.PHONY: clean

# a mutant for tools/qa.sh steam: the loop with the Steam overlay's Present hook is not broken (the game must be seen to crash)
build/overlay_PRESENT_LOOP.o: overlay.cpp Makefile
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -DWP_MUT_PRESENT_LOOP -c -o $@ $<
build/dsound_PRESENT_LOOP.dll: $(filter-out build/overlay.o,$(OBJ)) build/overlay_PRESENT_LOOP.o $(RES) dsound.def
	$(CXX) -shared -static-libgcc -static-libstdc++ -o $@ $^ $(LIBS)
# Test build for tools/version_notice_live.sh: the same proxy whose version notice is silent (a mutant that must be caught). Only coop.cpp differs.
build/coop_mut_silent.o: coop.cpp Makefile build/version.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -DWN_MUT_SILENT -c -o $@ coop.cpp
dsound_mut_silent.dll: $(filter-out build/coop.o,$(OBJ)) build/coop_mut_silent.o $(RES) dsound.def
	$(CXX) -shared -static-libgcc -static-libstdc++ -o $@ $(filter-out build/coop.o,$(OBJ)) build/coop_mut_silent.o $(RES) dsound.def $(LIBS)
