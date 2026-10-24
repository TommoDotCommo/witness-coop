# Builds AnselSDK64.dll (the proxy loaded by the game). Needs x86_64-w64-mingw32-g++ (MinGW-w64, 64-bit).
CXX = x86_64-w64-mingw32-g++
OUT = AnselSDK64.dll
SRC = ansel_proxy.cpp overlay.cpp coop.cpp game.cpp state.cpp hooks.cpp config.cpp input.cpp ui.cpp devmenu.cpp video.cpp savepoints.cpp core/sync_core.cpp
$(OUT): $(SRC) ansel_proxy.def coop.h game.h state.h config.h input.h ui.h devmenu.h video.h savepoints.h core/sync_core.h
	$(CXX) -O2 -std=c++17 -D_WIN32_WINNT=0x0601 -shared -static-libgcc -static-libstdc++ -o $@ $(SRC) ansel_proxy.def -ld3d11 -ldxgi -luuid -lws2_32 -lgdi32 -lshell32 -lwindowscodecs -lole32 -loleaut32
clean:
	rm -f $(OUT)
.PHONY: clean
