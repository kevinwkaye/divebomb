cmake_minimum_required(VERSION 3.22)

project(Divebomb VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE: use a local copy if you have one (-DDIVEBOMB_JUCE_PATH=...),
# otherwise download JUCE 8 automatically.
# ---------------------------------------------------------------------------
set(DIVEBOMB_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")
option(DIVEBOMB_BUILD_PLUGIN "Build the plugin (needs JUCE)" ON)
option(DIVEBOMB_BUILD_TESTS "Build the offline render test" ON)

if(DIVEBOMB_BUILD_PLUGIN)
    if(DIVEBOMB_JUCE_PATH)
        add_subdirectory(${DIVEBOMB_JUCE_PATH} JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(DIVEBOMB_FORMATS VST3 Standalone)
    if(UNIX AND NOT APPLE)
        # LV2 as well on Linux, for Pi pedalboard hosts
        list(APPEND DIVEBOMB_FORMATS LV2)
    endif()

    juce_add_plugin(Divebomb
        COMPANY_NAME "Kevin Kaye"
        BUNDLE_ID "uk.kevinkaye.divebomb"
        PLUGIN_MANUFACTURER_CODE Kkay
        PLUGIN_CODE Dvbm
        FORMATS ${DIVEBOMB_FORMATS}
        PRODUCT_NAME "Divebomb"
        DESCRIPTION "Treadle bends, drops, hammer-ons and glides"
        LV2URI "https://github.com/kevinwkaye/divebomb"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Pitch Shift)

    juce_add_binary_data(DivebombData SOURCES
        Resources/RobotoCondensed-Bold.ttf
        Resources/PermanentMarker-Regular.ttf)

    target_sources(Divebomb PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PedalControl.cpp
        Source/Keeper.cpp)

    target_include_directories(Divebomb PRIVATE Source Source/engine)

    target_compile_definitions(Divebomb PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(Divebomb
        PRIVATE
            DivebombData
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# Panel snapshots for checking the layout (not part of the release build)
option(DIVEBOMB_BUILD_SNAPSHOT "Build the panel snapshot tool" OFF)
if(DIVEBOMB_BUILD_PLUGIN AND DIVEBOMB_BUILD_SNAPSHOT)
    juce_add_console_app(DivebombSnapshot PRODUCT_NAME "Divebomb Snapshot")
    target_sources(DivebombSnapshot PRIVATE Tests/Snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/PedalControl.cpp Source/Keeper.cpp)
    target_include_directories(DivebombSnapshot PRIVATE Source Source/engine)
    target_compile_definitions(DivebombSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(DivebombSnapshot PRIVATE DivebombData juce::juce_audio_utils juce::juce_dsp)
endif()

# ---------------------------------------------------------------------------
# Offline engine test (no JUCE needed): bass tracking, levels, footswitches,
# latency and CPU, written to test-results.md
# ---------------------------------------------------------------------------
if(DIVEBOMB_BUILD_TESTS)
    add_executable(divebomb_render_test Tests/RenderTest.cpp)
    target_include_directories(divebomb_render_test PRIVATE Source/engine)
    if(NOT MSVC)
        target_compile_options(divebomb_render_test PRIVATE -O2)
    endif()
endif()
