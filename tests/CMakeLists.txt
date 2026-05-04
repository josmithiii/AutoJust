# AutoJust unit tests — standalone console executables.

function(autojust_add_test_target name source)
    juce_add_console_app(${name} PRODUCT_NAME "${name}")
    juce_generate_juce_header(${name})
    target_sources(${name} PRIVATE ${source} ${ARGN})
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../Source)
    target_compile_definitions(${name} PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_STANDALONE_APPLICATION=1)
    target_compile_options(${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fno-strict-aliasing>)
    target_link_libraries(${name} PRIVATE
        juce_audio_basics
        juce_dsp
        juce_core
        juce_recommended_warning_flags
        juce_recommended_config_flags
        juce_recommended_lto_flags
        jos_warning_flags)
endfunction()

autojust_add_test_target(AutoJust_StftTest
    StftRoundTripTest.cpp
    ../Source/Stft.cpp)

autojust_add_test_target(AutoJust_PeakAnalyzerTest
    PeakAnalyzerTest.cpp
    ../Source/Stft.cpp
    ../Source/PeakAnalyzer.cpp
    ../Source/TonicEstimator.cpp)

autojust_add_test_target(AutoJust_TonicEstimatorTest
    TonicEstimatorTest.cpp
    ../Source/Stft.cpp
    ../Source/PeakAnalyzer.cpp
    ../Source/TonicEstimator.cpp)
