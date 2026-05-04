# CMakeLists.txt for AutoJust — master-bus adaptive Just Intonation retuner
# Effect plugin (not a synth). v0: identity passthrough scaffolding.
# See AutoJust_PLAN.md for the full design.

project(AutoJust
  VERSION 0.0.1
  DESCRIPTION "Master-bus adaptive Just Intonation retuner"
  LANGUAGES C CXX
)

set(BaseTargetName AutoJust)

if(POLICY CMP0175)
    cmake_policy(SET CMP0175 NEW)
endif()

juce_add_plugin("${BaseTargetName}"
        COMPANY_NAME "JOS"
        COMPANY_WEBSITE "https://ccrma.stanford.edu/~jos/"
        COMPANY_EMAIL "julius.smith@gmail.com"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD TRUE
        FORMATS Standalone AU VST3
        AAX_CATEGORY "AAX_ePlugInCategory_PitchShift"
        AU_MAIN_TYPE "kAudioUnitType_Effect"
        PLUGIN_MANUFACTURER_CODE JOSj
        BUNDLE_ID "org.jos.AutoJust"
        PLUGIN_CODE Aju1
        PLUGIN_NAME "${BaseTargetName}"
        PRODUCT_NAME "${BaseTargetName}")

juce_generate_juce_header(${BaseTargetName})

target_sources(${BaseTargetName} PRIVATE
        Source/PluginProcessor.cpp
        Source/Stft.cpp
        Source/PeakAnalyzer.cpp
        Source/TonicEstimator.cpp
        )

target_compile_definitions(${BaseTargetName}
    PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    FOLEYS_ENABLE_BINARY_DATA=1
    FOLEYS_SHOW_GUI_EDITOR_PALLETTE=1
    FOLEYS_SAVE_EDITED_GUI_IN_PLUGIN_STATE=0
    OVERRIDE_CREATE_EDITOR=1
    )

target_compile_options(${BaseTargetName} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fno-strict-aliasing>
)

# Binary data: PGM layout XML(s) embedded so the standalone/AU/VST3 all carry the GUI.
set(XMLTarget "${BaseTargetName}-XML")

file(GLOB LAYOUT_XML_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Layouts/*.xml")

juce_add_binary_data(${XMLTarget} SOURCES
    ${LAYOUT_XML_FILES}
)

target_link_libraries(${BaseTargetName}
        PRIVATE
            ${XMLTarget}
            foleys_gui_magic
            juce_audio_utils
            juce_dsp
            juce_gui_extra
            juce_recommended_warning_flags
            juce_recommended_config_flags
            juce_recommended_lto_flags
            jos_warning_flags)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../submodules/JUCE/**/*
        ${CMAKE_CURRENT_SOURCE_DIR}/../../submodules/pgmf/**/*
        PROPERTIES
        COMPILE_FLAGS "-w"
    )
endif()

message("AutoJust: ./build/<preset>/Effects/${BaseTargetName}/${BaseTargetName}_artefacts/<Config>/Standalone/${BaseTargetName}.app")

add_subdirectory(tests)
