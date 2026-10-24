# Builds AnselSDK64.dll (the proxy loaded by the game). Needs x86_64-w64-mingw32-g++ (MinGW-w64, 64-bit).
CXX = x86_64-w64-mingw32-g++
OUT = AnselSDK64.dll
SRC = ansel_proxy.cpp overlay.cpp model_gpu.cpp depth.cpp model/model.cpp coop.cpp game.cpp state.cpp hooks.cpp config.cpp input.cpp core/sync_core.cpp
$(OUT): $(SRC) ansel_proxy.def coop.h game.h state.h config.h input.h model_gpu.h depth.h model/model.h core/sync_core.h
	$(CXX) -O2 -std=c++17 -D_WIN32_WINNT=0x0601 -shared -static-libgcc -static-libstdc++ -o $@ $(SRC) ansel_proxy.def -ld3d11 -ldxgi -luuid -lws2_32
clean:
	rm -f $(OUT)
.PHONY: clean
