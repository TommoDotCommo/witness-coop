# Builds AnselSDK64.dll (the proxy loaded by the game). Needs x86_64-w64-mingw32-g++ (MinGW-w64, 64-bit).
# One object per source with its header dependencies, so a change to one file recompiles that file only; `make -j` compiles the rest side by side.
MAKEFLAGS += -j8
CXX = x86_64-w64-mingw32-g++
OUT = AnselSDK64.dll
SRC = ansel_proxy.cpp overlay.cpp model_gpu.cpp depth.cpp model/model.cpp coop.cpp game.cpp state.cpp hooks.cpp config.cpp input.cpp ui.cpp devmenu.cpp video.cpp savepoints.cpp core/sync_core.cpp
OBJ = $(patsubst %.cpp,build/%.o,$(SRC))
CXXFLAGS = -O2 -std=c++17 -D_WIN32_WINNT=0x0601 -MMD -MP
LIBS = -ld3d11 -ldxgi -luuid -lws2_32 -lgdi32 -lshell32 -lwindowscodecs -lole32 -loleaut32
$(OUT): $(OBJ) ansel_proxy.def
	$(CXX) -shared -static-libgcc -static-libstdc++ -o $@ $(OBJ) ansel_proxy.def $(LIBS)
build/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
-include $(OBJ:.o=.d)
clean:
	rm -rf build $(OUT)
.PHONY: clean
