CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
build/text-test: text_test.cpp text.h theme.h guide_layout.h ../../common/guide.h ../../common/bindings.def ../config.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ text_test.cpp
MUTANTS = WIDTH WRAP SCALE
DETAIL_MUTANTS = NOFIT FLOW
PANEL_MUTANTS = NO_FIT ROW_OFFSET
TAG_MUTANTS = OVERLAP
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
test: build/text-test $(addprefix build/mutant-,$(MUTANTS)) build/detail-test $(addprefix build/detail-mutant-,$(DETAIL_MUTANTS)) build/panel-test $(addprefix build/panel-mutant-,$(PANEL_MUTANTS)) build/tag-test $(addprefix build/tag-mutant-,$(TAG_MUTANTS))
	build/text-test
	build/panel-test
	@for m in $(PANEL_MUTANTS); do if build/panel-mutant-$$m >/dev/null 2>&1; then echo "MUTANT panel $$m SURVIVED"; exit 1; else echo "mutant panel $$m killed"; fi; done
	build/tag-test
	@for m in $(TAG_MUTANTS); do if build/tag-mutant-$$m >/dev/null 2>&1; then echo "MUTANT tag $$m SURVIVED"; exit 1; else echo "mutant tag $$m killed"; fi; done
	build/detail-test
	@for m in $(MUTANTS); do if build/mutant-$$m >/dev/null; then echo "MUTANT $$m SURVIVED"; exit 1; else echo "mutant $$m killed"; fi; done
	@for m in $(DETAIL_MUTANTS); do if build/detail-mutant-$$m >/dev/null; then echo "MUTANT detail $$m SURVIVED"; exit 1; else echo "mutant detail $$m killed"; fi; done
clean:
	rm -rf build
.PHONY: test clean
