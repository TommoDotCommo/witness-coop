# Game-free (T0) tests of the built-in randomizers. `make test` builds every vendored randomizer natively (g++, with a stand-in windows.h), each in
# its own namespace (variant.h: a generated wrapper per vendored file, build/variants/<v>/<file>.cpp), and runs:
#   backend-test     the memory layer (overlay, copy on write, fingerprint, apply), with its mutants
#   generate-test    whole runs of every randomizer over a game recorded by tools/randomizer_solo.sh --record (local/randomizer/*.world); skipped,
#                    and said so, when no recording is there (it is game data and stays out of the repository)
# Every mutant must FAIL.
MAKEFLAGS += -j8
BUILD = build
TP = ../../third_party
SHIM = $(TP)/witness-randomizer/shim
CXXF = -std=gnu++2a -O2 -DWITNESS_COOP -Itests/winstub -I$(SHIM) -I.
VCXX = $(CXXF) -include variant_prelude.h
WORLDS = $(wildcard ../../local/randomizer/*.world)
HDRS = backend.h variant.h variant_prelude.h generate.h ../../common/randomizers.h $(wildcard $(SHIM)/*.h)

# ---- the variants (variant.h): <v>_DIR holds the vendored .cpp files, <v>_SRC the ones built (the same list is in ../Makefile)
include variants.mk

define VARIANT
$(BUILD)/variants/$(1)/%.cpp: $$($(1)_DIR)/%.cpp
	@mkdir -p $$(dir $$@); printf '#include "variant_prelude.h"\nnamespace v_$(1) {\n%b#include "%s"\n}\n' "$$(if $$($(1)_INSIDE),#include \"$$(abspath $$($(1)_INSIDE))\"\n)" "$$(abspath $$<)" > $$@
$(BUILD)/variants/$(1)/%.o: $(BUILD)/variants/$(1)/%.cpp $(HDRS)
	g++ $(VCXX) $$($(1)_FLAGS) -w -c -o $$@ $$<
$(BUILD)/variants/$(1)_glue.o: variants/$(1).cpp $(HDRS)
	@mkdir -p $(BUILD)/variants
	g++ $(VCXX) $$($(1)_FLAGS) -Wall -c -o $$@ $$<
$(BUILD)/variants/$(1)_glue-%.o: variants/$(1).cpp $(HDRS)
	@mkdir -p $(BUILD)/variants
	g++ $(VCXX) $$($(1)_FLAGS) -DWR_MUT_$$* -c -o $$@ $$<
$(1)_OBJ = $$(patsubst %,$(BUILD)/variants/$(1)/%.o,$$($(1)_SRC))
endef
$(foreach v,$(VARIANTS),$(eval $(call VARIANT,$(v))))
VOBJ = $(foreach v,$(VARIANTS),$($(v)_OBJ))
GLUE = $(foreach v,$(VARIANTS),$(BUILD)/variants/$(v)_glue.o)
glue-mut = $(foreach v,$(VARIANTS),$(BUILD)/variants/$(v)_glue-$(1).o)      # every variant's glue built with mutant $(1)

$(BUILD)/backend.o: backend.cpp $(HDRS)
	@mkdir -p $(BUILD)
	g++ $(CXXF) -include compat.h -Wall -Wextra -c -o $@ $<
$(BUILD)/backend-%.o: backend.cpp $(HDRS)
	@mkdir -p $(BUILD)
	g++ $(CXXF) -include compat.h -DWR_MUT_$* -c -o $@ $<
$(BUILD)/generate.o: generate.cpp $(HDRS)
	@mkdir -p $(BUILD)
	g++ $(CXXF) -include compat.h -Wall -c -o $@ $<
LIBS = $(BUILD)/generate.o $(VOBJ)

$(BUILD)/backend-test: tests/backend_test.cpp tests/fake_raw.h world.h $(BUILD)/backend.o $(GLUE) $(LIBS)
	g++ $(CXXF) -include compat.h -Wall -o $@ tests/backend_test.cpp $(BUILD)/backend.o $(GLUE) $(LIBS)
$(BUILD)/backend-mut-%: tests/backend_test.cpp tests/fake_raw.h world.h $(BUILD)/backend-%.o $(GLUE) $(LIBS)
	g++ $(CXXF) -include compat.h -o $@ tests/backend_test.cpp $(BUILD)/backend-$*.o $(GLUE) $(LIBS)
$(BUILD)/generate-test: tests/generate_test.cpp world.h $(BUILD)/backend.o $(GLUE) $(LIBS)
	g++ $(CXXF) -include compat.h -Wall -o $@ tests/generate_test.cpp $(BUILD)/backend.o $(GLUE) $(LIBS)
.SECONDEXPANSION:
$(BUILD)/generate-mut-%: tests/generate_test.cpp world.h $(BUILD)/backend.o $$(call glue-mut,$$*) $(LIBS)
	g++ $(CXXF) -include compat.h -o $@ tests/generate_test.cpp $(BUILD)/backend.o $(call glue-mut,$*) $(LIBS)

# the player's own puzzles written into a recorded game and read back (tests/custom_test.cpp); mutants: polyominoes upside down, panels that are not grids read as they are
$(BUILD)/custom-test: tests/custom_test.cpp world.h $(BUILD)/backend.o $(GLUE) $(LIBS)
	g++ $(CXXF) -include compat.h -Wall -o $@ tests/custom_test.cpp $(BUILD)/backend.o $(GLUE) $(LIBS)
$(BUILD)/custom-mut-%: tests/custom_test.cpp world.h $(BUILD)/backend.o $$(call glue-mut,$$*) $(LIBS)
	g++ $(CXXF) -include compat.h -o $@ tests/custom_test.cpp $(BUILD)/backend.o $(call glue-mut,$*) $(LIBS)
CUSTOM_MUTANTS = SHAPE_FLIPPED READ_ANY_PANEL

# one run of one spec over a recorded game (tools/randomizer_sweep.py: every mode over many seeds, each in a process of its own)
$(BUILD)/one-run: tests/one_run.cpp world.h $(BUILD)/backend.o $(GLUE) $(LIBS)
	g++ $(CXXF) -include compat.h -o $@ tests/one_run.cpp $(BUILD)/backend.o $(GLUE) $(LIBS)

BACKEND_MUTANTS = COW_NO_COPY FP_ADDRESSES
GENERATE_MUTANTS = NO_RESET WATCHDOG_THREAD
GEN_SHARDS ?= 4      # the replay as this many parallel processes (each recorded run is independent); its mutants run beside them (tests/generate_par.sh)
$(BUILD)/generate-par.sh: tests/generate_par.sh
	mkdir -p $(BUILD); cp tests/generate_par.sh $@; chmod +x $@
test: $(BUILD)/generate-par.sh $(BUILD)/backend-test $(addprefix $(BUILD)/backend-mut-,$(BACKEND_MUTANTS)) $(BUILD)/generate-test $(addprefix $(BUILD)/generate-mut-,$(GENERATE_MUTANTS)) $(BUILD)/custom-test $(addprefix $(BUILD)/custom-mut-,$(CUSTOM_MUTANTS))
	$(BUILD)/backend-test
	@for m in $(BACKEND_MUTANTS); do if $(BUILD)/backend-mut-$$m >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done
	@if [ -z "$(WORLDS)" ]; then echo "generate-test: SKIPPED, no recorded game in local/randomizer/ (tools/randomizer_solo.sh --record)"; else \
	  $(BUILD)/generate-par.sh $(GEN_SHARDS) "$(GENERATE_MUTANTS)" $(WORLDS) && \
	  $(BUILD)/custom-test $(firstword $(WORLDS)) && \
	  for m in $(CUSTOM_MUTANTS); do if $(BUILD)/custom-mut-$$m $(firstword $(WORLDS)) >/dev/null; then echo "MUTANT $$m SURVIVED (the test cannot fail for it)"; exit 1; else echo "mutant $$m killed"; fi; done; fi
clean:
	rm -rf $(BUILD)
.PHONY: test clean
