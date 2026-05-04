# AutoJust unit tests — standalone console executables.
# Currently just the STFT round-trip test (v0.2).

juce_add_console_app(AutoJust_StftTest
    PRODUCT_NAME "AutoJust_StftTest")

juce_generate_juce_header(AutoJust_StftTest)

target_sources(AutoJust_StftTest PRIVATE
    StftRoundTripTest.cpp
    ../Source/Stft.cpp)

target_include_directories(AutoJust_StftTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source)

target_compile_definitions(AutoJust_StftTest PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1)

target_compile_options(AutoJust_StftTest PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fno-strict-aliasing>)

target_link_libraries(AutoJust_StftTest PRIVATE
    juce_audio_basics
    juce_dsp
    juce_core
    juce_recommended_warning_flags
    juce_recommended_config_flags
    juce_recommended_lto_flags
    jos_warning_flags)
