CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
MUTANTS = RAW_KEY_NAMES NO_PAD NO_MIGRATE TOGGLE REARM ALLOW_COLLISION MISSING_KEY MARKER_BOUND KEEP_OLD OVERWRITE_CUSTOM SHARED_FREE NO_RESOLVE WRITE_ALWAYS TV_KEY_WHILE_OFF TV_IN_GUIDE RADIO_KEY_WHILE_OFF RADIO_IN_GUIDE DEV_IN_GUIDE
build/guide-test: guide_test.cpp guide.h padlayer.h movedkeys.h bindings.def ../proxy/config.h ../proxy/bindings.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $<
build/mutant-%: guide_test.cpp guide.h padlayer.h movedkeys.h bindings.def ../proxy/config.h ../proxy/bindings.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DGUIDE_MUT_$* -o $@ $<
PAD_MUTANTS = LEAK STUCK_GLYPHS
build/padlayer-test: padlayer_test.cpp padlayer.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $<
build/padlayer-mutant-%: padlayer_test.cpp padlayer.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DPAD_MUT_$* -o $@ $<
pad-test: build/padlayer-test $(addprefix build/padlayer-mutant-,$(PAD_MUTANTS))
	build/padlayer-test
	@for m in $(PAD_MUTANTS); do if build/padlayer-mutant-$$m >/dev/null 2>&1; then echo "MUTANT pad $$m SURVIVED"; exit 1; else echo "mutant pad $$m killed"; fi; done
NOTICE_MUTANTS = WORDS REPEAT NEVER_LEFT
build/notices-test: notices_test.cpp notices.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $<
build/notices-mutant-%: notices_test.cpp notices.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DNOTICE_MUT_$* -o $@ $<
# the game's own font read from the player's archive (gamefont.h): a zip built in memory; mutant: the inverted member is not undone
build/gamefont-test: gamefont_test.cpp gamefont.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -o $@ $<
build/gamefont-mutant: gamefont_test.cpp gamefont.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DGF_MUT_NO_XOR -o $@ $<
gamefont-test: build/gamefont-test build/gamefont-mutant
	build/gamefont-test
	@if build/gamefont-mutant >/dev/null 2>&1; then echo "MUTANT gamefont NO_XOR SURVIVED"; exit 1; else echo "mutant gamefont NO_XOR killed"; fi
build/buildid-test: buildid_test.cpp buildid.h sha256.h buildid_fixture.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -o $@ $<
build/buildid-mutant: buildid_test.cpp buildid.h sha256.h buildid_fixture.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DBI_MUT_SKIP_TEXT -o $@ $<
# where the settings and logs live (userdata.h): a Windows program (WSL interop or Windows); mutants: every cfg is an override / the old cfg is kept after the move
WINCXX ?= x86_64-w64-mingw32-g++
USERDATA_MUTANTS = ANY_CFG_IS_INSTANCE KEEP_OLD
build/userdata-test.exe: userdata_test.cpp userdata.h
	mkdir -p build
	$(WINCXX) -std=c++17 -O2 -Wall -Wextra -static -o $@ $<
build/userdata-mutant-%.exe: userdata_test.cpp userdata.h
	mkdir -p build
	$(WINCXX) -std=c++17 -O2 -Wall -static -DUSERDATA_MUT_$* -o $@ $<
userdata-test: build/userdata-test.exe $(addsuffix .exe,$(addprefix build/userdata-mutant-,$(USERDATA_MUTANTS)))
	build/userdata-test.exe
	@for m in $(USERDATA_MUTANTS); do if build/userdata-mutant-$$m.exe >/dev/null; then echo "MUTANT userdata $$m SURVIVED"; exit 1; else echo "mutant userdata $$m killed"; fi; done
test: pad-test userdata-test gamefont-test build/guide-test $(addprefix build/mutant-,$(MUTANTS)) build/buildid-test build/buildid-mutant build/notices-test $(addprefix build/notices-mutant-,$(NOTICE_MUTANTS))
	build/notices-test
	@for m in $(NOTICE_MUTANTS); do if build/notices-mutant-$$m >/dev/null 2>&1; then echo "MUTANT notices $$m SURVIVED"; exit 1; else echo "mutant notices $$m killed"; fi; done
	build/buildid-test $(BUILDID_EXE)
	@if build/buildid-mutant >/dev/null 2>&1; then echo "MUTANT buildid SKIP_TEXT SURVIVED"; exit 1; else echo "mutant buildid SKIP_TEXT killed"; fi
	build/guide-test
	@for m in $(MUTANTS); do if build/mutant-$$m >/dev/null 2>&1; then echo "MUTANT $$m SURVIVED"; exit 1; else echo "mutant $$m killed"; fi; done
clean:
	rm -rf build
.PHONY: test clean userdata-test pad-test gamefont-test
