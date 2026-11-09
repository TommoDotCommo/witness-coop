# The vendored randomizers (variant.h), shared by this Makefile and ../Makefile: <v>_DIR (under $(TP), the third_party folder, which each Makefile sets) and the .cpp files built.
VARIANTS = nsv
nsv_DIR = $(TP)/witness-randomizer/src
nsv_SRC = Generate MultiGenerate Panel PuzzleList Quaternion Randomizer Special Watchdog
VARIANTS += variety despair shuffler star
# the older forks were built with MSVC's min/max macros (their code calls min() and max() bare): <v>_FLAGS turns them on for that variant
variety_DIR = $(TP)/variety-randomizer/src
variety_FLAGS = -DWRAND_MSVC_MINMAX -DWC_MAX_TRIES=$(VARIETY_MAX_TRIES)
VARIETY_MAX_TRIES ?= 2000
variety_SRC = Generate MultiGenerate Panel PuzzleList Quaternion Randomizer Special Watchdog
despair_DIR = $(TP)/despair-randomizer/src
despair_FLAGS = -DWRAND_MSVC_MINMAX
despair_SRC = Generate MultiGenerate Panel PuzzleList Quaternion Randomizer Special Watchdog
shuffler_DIR = $(TP)/symbol-shuffler/src
shuffler_FLAGS = -DWRAND_MSVC_MINMAX
shuffler_SRC = Generate MultiGenerate Panel PuzzleList Randomizer Special Watchdog
star_DIR = $(TP)/star-randomizer/src
star_FLAGS = -DWRAND_MSVC_MINMAX
star_SRC = Generate MultiGenerate Panel PuzzleList Quaternion Randomizer Special Watchdog
VARIANTS += tutorializer
# hatkirby's Tutorializer has its own memory layer: tutorializer_INSIDE is included inside its namespace before each of its files
tutorializer_DIR = $(TP)/tutorializer/src
tutorializer_SRC = Randomizer
tutorializer_FLAGS := -I$(dir $(lastword $(MAKEFILE_LIST)))variants/tutorializer_include
tutorializer_INSIDE = variants/tutorializer_inside.h
