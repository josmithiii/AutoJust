# CMakeLists.txt for AutoJust — master-bus adaptive Just Intonation retuner

cmake_minimum_required(VERSION 3.16)

# Detect whether we are the top-level project (standalone autojust repo)
# or being built as a subdirectory of a parent (e.g. jos-juce-plugins).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(AUTOJUST_STANDALONE TRUE)
else()
    set(AUTOJUST_STANDALONE FALSE)
endif()

if(AUTOJUST_STANDALONE AND APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_SYSROOT "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")
endif()

project(AutoJust
  VERSION 0.0.1
  DESCRIPTION "Master-bus adaptive Just Intonation retuner"
  LANGUAGES C CXX
)

if(AUTOJUST_STANDALONE)
    if(APPLE)
        set(MIN_MACOS_VERSION "12" CACHE STRING "Minimum macOS deployment version")
        set(CMAKE_OSX_DEPLOYMENT_TARGET ${MIN_MACOS_VERSION} CACHE STRING "Minimum macOS deployment version" FORCE)
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE INTERNAL "")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    endif()

    set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
    set_property(GLOBAL PROPERTY USE_FOLDERS YES)
    option(JUCE_ENABLE_MODULE_SOURCE_GROUPS "Enable Module Source Groups" ON)

    add_library(jos_warning_flags INTERFACE)
    target_compile_options(jos_warning_flags INTERFACE
        -Winline
        "-Wno-sign-conversion"
        "-Wno-float-conversion"
        "-Wno-unused-parameter"
        "-Wno-unused-variable"
        "-Wno-zero-as-null-pointer-constant"
        "-Wno-float-equal"
        "-Wno-multichar"
        "-Wno-extra-semi"
        "-Wno-switch-enum"
        "-Wno-shadow"
        "-Wno-deprecated-copy-with-user-provided-dtor"
        "-Wno-deprecated-copy-with-dtor"
        "-Wno-deprecated-declarations"
        "-Wno-sign-compare"
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(jos_warning_flags INTERFACE
            "-Wno-shorten-64-to-32"
            "-Wno-implicit-float-conversion"
            "-Wno-unused-private-field"
            "-Wno-vla-cxx-extension"
            "-Wno-c++98-compat-extra-semi"
            "-Wno-missing-prototypes"
            "-Wno-#pragma-messages"
            "-Wno-shadow-field"
            "-Wno-implicit-int-conversion"
            "-Wno-reorder-ctor"
        )
    endif()

    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/submodules/JUCE
        ${CMAKE_BINARY_DIR}/JUCE_build
        EXCLUDE_FROM_ALL
    )
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/submodules/pgmf/modules
        ${CMAKE_BINARY_DIR}/foleys_gui_magic_build
        EXCLUDE_FROM_ALL
    )
endif()

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
        Source/TuningGrid.cpp
        Source/Retuner.cpp
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

# Silence warnings from JUCE / pgmf headers — paths differ between standalone
# and parent-repo layouts.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    if(AUTOJUST_STANDALONE)
        set_source_files_properties(
            ${CMAKE_CURRENT_SOURCE_DIR}/submodules/JUCE/**/*
            ${CMAKE_CURRENT_SOURCE_DIR}/submodules/pgmf/**/*
            PROPERTIES
            COMPILE_FLAGS "-w"
        )
    else()
        set_source_files_properties(
            ${CMAKE_CURRENT_SOURCE_DIR}/../../submodules/JUCE/**/*
            ${CMAKE_CURRENT_SOURCE_DIR}/../../submodules/pgmf/**/*
            PROPERTIES
            COMPILE_FLAGS "-w"
        )
    endif()
endif()

if(AUTOJUST_STANDALONE)
    message("AutoJust: ./build/<preset>/${BaseTargetName}_artefacts/<Config>/Standalone/${BaseTargetName}.app")
else()
    message("AutoJust: ./build/<preset>/Effects/${BaseTargetName}/${BaseTargetName}_artefacts/<Config>/Standalone/${BaseTargetName}.app")
endif()

add_subdirectory(tests)
