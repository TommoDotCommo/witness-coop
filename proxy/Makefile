# Builds AnselSDK64.dll (the proxy loaded by the game). Needs x86_64-w64-mingw32-g++ (MinGW-w64, 64-bit).
# One object per source with its header dependencies, so a change to one file recompiles that file only; `make -j` compiles the rest side by side.
MAKEFLAGS += -j8
CXX = x86_64-w64-mingw32-g++
OUT = AnselSDK64.dll
.DEFAULT_GOAL := $(OUT)
SRC = diag.cpp tracer.cpp guide_ui.cpp tv.cpp radio/radio_game.cpp race_game.cpp race/race.cpp voice_audio.cpp voice/voice.cpp ansel_proxy.cpp overlay.cpp gamecam.cpp model_gpu.cpp depth.cpp model/model.cpp coop.cpp game.cpp state.cpp hooks.cpp config.cpp input.cpp ui.cpp devmenu.cpp social.cpp video.cpp savepoints.cpp core/sync_core.cpp mapgen.cpp savemeta_game.cpp progress/progress.cpp progress/savemeta.cpp progress/progress_data.cpp
OBJ = $(patsubst %.cpp,build/%.o,$(SRC))
CXXFLAGS = -O2 -std=c++17 -D_WIN32_WINNT=0x0601 -MMD -MP -Ibuild
LIBS = -ld3d11 -ldxgi -luuid -lws2_32 -lgdi32 -lshell32 -lwindowscodecs -lole32 -loleaut32 -lwinmm
# the version the logs and the diagnostics name: `git describe`, written to build/version.h only when it changes (so only diag.cpp recompiles)
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
build/version.h: FORCE
	@mkdir -p build; printf '#define MOD_VERSION "%s"\n' '$(VERSION)' > build/version.h.new; cmp -s build/version.h.new build/version.h && rm build/version.h.new || mv build/version.h.new build/version.h
FORCE:
build/diag.o: build/version.h
$(OUT): $(OBJ) ansel_proxy.def
	$(CXX) -shared -static-libgcc -static-libstdc++ -Wl,-Map=AnselSDK64.map -o $@ $(OBJ) ansel_proxy.def $(LIBS)
build/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
-include $(OBJ:.o=.d)
clean:
	rm -rf build $(OUT) AnselSDK64.map
.PHONY: clean
