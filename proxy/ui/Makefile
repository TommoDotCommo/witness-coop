CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
build/text-test: text_test.cpp text.h theme.h guide_layout.h ../../common/guide.h ../../common/bindings.def ../config.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ text_test.cpp
MUTANTS = WIDTH WRAP SCALE CAPSHIFT
DETAIL_MUTANTS = NOFIT FLOW
PANEL_MUTANTS = NO_FIT ROW_OFFSET OLD_PIP_KEYS
TAG_MUTANTS = OVERLAP
PROMPT_MUTANTS = OLD
KEY_MUTANTS = NO_IGNORE WHILE_TYPING
build/key-test: key_repeat_test.cpp key_repeat.h ../../common/guide.h ../../common/bindings.def ../config.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ key_repeat_test.cpp
build/key-mutant-%: key_repeat_test.cpp key_repeat.h ../../common/guide.h ../../common/bindings.def ../config.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DWK_MUT_$* -o $@ key_repeat_test.cpp
build/prompt-test: prompts_test.cpp prompts.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ prompts_test.cpp
build/prompt-mutant-%: prompts_test.cpp prompts.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DPR_MUT_$* -o $@ prompts_test.cpp
build/tag-test: tag_test.cpp tag_layout.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ tag_test.cpp
build/tag-mutant-%: tag_test.cpp tag_layout.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DWT_MUT_$* -o $@ tag_test.cpp
build/panel-test: player_panel_test.cpp player_panel.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ player_panel_test.cpp
build/panel-mutant-%: player_panel_test.cpp player_panel.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DWP_MUT_$* -o $@ player_panel_test.cpp
build/detail-test: detail_test.cpp detail_layout.h text.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ detail_test.cpp
build/detail-mutant-%: detail_test.cpp detail_layout.h text.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DUI_MUT_$* -o $@ detail_test.cpp
build/mutant-%: text_test.cpp text.h theme.h guide_layout.h ../../common/guide.h ../../common/bindings.def ../config.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DUI_MUT_$* -o $@ text_test.cpp
test: build/text-test $(addprefix build/mutant-,$(MUTANTS)) build/detail-test $(addprefix build/detail-mutant-,$(DETAIL_MUTANTS)) build/panel-test $(addprefix build/panel-mutant-,$(PANEL_MUTANTS)) build/tag-test $(addprefix build/tag-mutant-,$(TAG_MUTANTS)) build/prompt-test $(addprefix build/prompt-mutant-,$(PROMPT_MUTANTS)) build/key-test $(addprefix build/key-mutant-,$(KEY_MUTANTS))
	build/text-test
	build/panel-test
	@for m in $(PANEL_MUTANTS); do if build/panel-mutant-$$m >/dev/null 2>&1; then echo "MUTANT panel $$m SURVIVED"; exit 1; else echo "mutant panel $$m killed"; fi; done
	build/tag-test
	build/prompt-test
	@for m in $(PROMPT_MUTANTS); do if build/prompt-mutant-$$m >/dev/null 2>&1; then echo "MUTANT prompt $$m SURVIVED"; exit 1; else echo "mutant prompt $$m killed"; fi; done
	@for m in $(TAG_MUTANTS); do if build/tag-mutant-$$m >/dev/null 2>&1; then echo "MUTANT tag $$m SURVIVED"; exit 1; else echo "mutant tag $$m killed"; fi; done
	build/key-test
	@for m in $(KEY_MUTANTS); do if build/key-mutant-$$m >/dev/null 2>&1; then echo "MUTANT key $$m SURVIVED"; exit 1; else echo "mutant key $$m killed"; fi; done
	build/detail-test
	@for m in $(MUTANTS); do if build/mutant-$$m >/dev/null; then echo "MUTANT $$m SURVIVED"; exit 1; else echo "mutant $$m killed"; fi; done
	@for m in $(DETAIL_MUTANTS); do if build/detail-mutant-$$m >/dev/null; then echo "MUTANT detail $$m SURVIVED"; exit 1; else echo "mutant detail $$m killed"; fi; done
clean:
	rm -rf build
.PHONY: test clean
