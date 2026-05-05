# AutoJust — convenience wrappers around CMake.
#
# Naming convention (mirrors the jos-juce-plugins Makefile):
#   [aj][type][modifier]
#     type:     d (debug), r (release)
#     modifier: au (AU plugin), aui (AU build + install), o (open/run)
# Long-form aliases (debug, release, run, ...) also work.
#
# Underlying invocations are plain `cmake --preset {release,debug}` followed
# by `cmake --build build/{release,debug} --target ... --parallel`.

# ---- Output paths --------------------------------------------------------
APP_REL    = ./build/release/AutoJust_artefacts/Release/Standalone/AutoJust.app
APP_DBG    = ./build/debug/AutoJust_artefacts/Debug/Standalone/AutoJust.app
BIN_REL    = $(APP_REL)/Contents/MacOS/AutoJust
BIN_DBG    = $(APP_DBG)/Contents/MacOS/AutoJust
AU_REL     = ./build/release/AutoJust_artefacts/Release/AU/AutoJust.component
VST3_REL   = ./build/release/AutoJust_artefacts/Release/VST3/AutoJust.vst3

AU_DEST    = $(HOME)/Library/Audio/Plug-Ins/Components
VST3_DEST  = $(HOME)/Library/Audio/Plug-Ins/VST3

# ---- Help (default target) -----------------------------------------------
.PHONY: help
help:
	@echo "AutoJust Makefile Targets"
	@echo "========================="
	@echo ""
	@echo "Naming convention: [aj][type][modifier]"
	@echo "  Types: r (release), d (debug)"
	@echo "  Modifiers: au (AU plugin), aui (AU build + install), o (open/run)"
	@echo ""
	@echo "AUTOJUST BUILDS:"
	@echo "  ajr            - Release build (standalone)"
	@echo "  ajd            - Debug build (standalone)"
	@echo "  ajau           - AU plugin build (release)"
	@echo "  ajaui          - AU plugin build and install"
	@echo "  ajvst3         - VST3 plugin build (release)"
	@echo "  ajvst3i        - VST3 plugin build and install"
	@echo "  all            - Release build (standalone + AU + VST3)"
	@echo ""
	@echo "AUTOJUST RUN:"
	@echo "  ajro           - Open/run release standalone (kills running first)"
	@echo "  ajdo           - Open/run debug standalone (kills running first)"
	@echo ""
	@echo "INSTALL:"
	@echo "  install        - Install AU + VST3 into ~/Library/Audio/Plug-Ins/"
	@echo "  install-au     - Install AU only"
	@echo "  install-vst3   - Install VST3 only"
	@echo ""
	@echo "TESTS / HOUSEKEEPING:"
	@echo "  tests          - Build and run all unit tests"
	@echo "  submodules     - git submodule update --init --recursive"
	@echo "  clean          - Remove build/ entirely"
	@echo ""
	@echo "Long-form aliases also work: release, debug, standalone, au, vst3,"
	@echo "run, run-debug."

# ---- Configure -----------------------------------------------------------
.PHONY: configure-release configure-debug
configure-release:
	cmake --preset release

configure-debug:
	cmake --preset debug

# ---- Build ---------------------------------------------------------------
.PHONY: all release standalone au vst3 debug ajr ajd ajau ajaui ajvst3 ajvst3i
all: release

release: configure-release
	cmake --build build/release --target AutoJust_Standalone AutoJust_AU AutoJust_VST3 --parallel

standalone: configure-release
	cmake --build build/release --target AutoJust_Standalone --parallel

au: configure-release
	cmake --build build/release --target AutoJust_AU --parallel

vst3: configure-release
	cmake --build build/release --target AutoJust_VST3 --parallel

debug: configure-debug
	cmake --build build/debug --target AutoJust_Standalone --parallel

# Short-prefix aliases (mirror jos-juce-plugins Makefile naming)
ajr: standalone
ajd: debug
ajau: au
ajaui: install-au
ajvst3: vst3
ajvst3i: install-vst3

# ---- Run -----------------------------------------------------------------
.PHONY: run run-debug ajro ajdo
run: standalone
	@test -f "$(BIN_REL)" || { echo "release standalone not built"; exit 1; }
	-@killall AutoJust 2>/dev/null; true
	open $(APP_REL)

run-debug: debug
	@test -f "$(BIN_DBG)" || { echo "debug standalone not built"; exit 1; }
	-@killall AutoJust 2>/dev/null; true
	open $(APP_DBG)

ajro: run
ajdo: run-debug

# ---- Install plugins -----------------------------------------------------
.PHONY: install install-au install-vst3
install: install-au install-vst3

install-au: au
	@mkdir -p $(AU_DEST)
	@rm -rf $(AU_DEST)/AutoJust.component
	cp -R $(AU_REL) $(AU_DEST)/
	@echo "Installed AU. You may need to restart your DAW or run:"
	@echo "    killall -9 AudioComponentRegistrar"

install-vst3: vst3
	@mkdir -p $(VST3_DEST)
	@rm -rf $(VST3_DEST)/AutoJust.vst3
	cp -R $(VST3_REL) $(VST3_DEST)/
	@echo "Installed VST3 to $(VST3_DEST)"

# ---- Tests ---------------------------------------------------------------
TEST_TARGETS = AutoJust_StftTest AutoJust_PeakAnalyzerTest \
               AutoJust_TonicEstimatorTest AutoJust_RetunerTest \
               AutoJust_TuningGridTest AutoJust_RetunerJITest

TEST_DIR = ./build/release/tests

.PHONY: tests build-tests
build-tests: configure-release
	cmake --build build/release --target $(TEST_TARGETS) --parallel

tests: build-tests
	@for t in $(TEST_TARGETS); do \
	    echo "=== $$t ==="; \
	    "$(TEST_DIR)/$${t}_artefacts/Release/$${t}" || exit 1; \
	done

# ---- Submodules ----------------------------------------------------------
.PHONY: submodules
submodules:
	git submodule update --init --recursive

# ---- Clean ---------------------------------------------------------------
.PHONY: clean distclean
clean:
	@rm -rf build
	@echo "build/ removed"

distclean: clean
