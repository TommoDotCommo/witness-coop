# Builds AnselSDK64.dll (the proxy loaded by the game). Needs x86_64-w64-mingw32-g++ (MinGW-w64, 64-bit).
CXX = x86_64-w64-mingw32-g++
OUT = AnselSDK64.dll
SRC = ansel_proxy.cpp overlay.cpp coop.cpp game.cpp state.cpp
$(OUT): $(SRC) ansel_proxy.def coop.h game.h state.h
	$(CXX) -O2 -std=c++17 -shared -static-libgcc -static-libstdc++ -o $@ $(SRC) ansel_proxy.def -ld3d11 -ldxgi -luuid -lws2_32
clean:
	rm -f $(OUT)
.PHONY: clean
