cmake_minimum_required(VERSION 3.22)

project(ChainRig VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Bluetooth LE MIDI (pedaliere BT senza chiavetta USB). Richiede le intestazioni WinRT
# del Windows SDK: se la build fallisce con questa opzione, lasciala OFF.
option(CHAINRIG_BLUETOOTH_MIDI "Abilita MIDI Bluetooth LE su Windows (WinRT)" OFF)

# Se hai già JUCE sul disco, passa -DJUCE_PATH=C:/percorso/JUCE per non scaricarlo.
set(JUCE_PATH "" CACHE PATH "Percorso di una copia locale di JUCE (facoltativo)")

if(JUCE_PATH)
    add_subdirectory(${JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.6
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_gui_app(ChainRig
    PRODUCT_NAME "ChainRig"
    COMPANY_NAME "ChainRig"
    BUNDLE_ID "com.chainrig.app"
    # macOS: senza questo permesso l'ingresso della scheda audio arriva muto
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "ChainRig usa l'ingresso della scheda audio per la chitarra.")

target_sources(ChainRig PRIVATE
    Source/Main.cpp
    Source/Engine.cpp
    Source/MainComponent.cpp
    Source/PluginChooser.cpp
    Source/PluginChooser.h
    Source/PluginScanner.h
    Source/Recorder.h)

target_compile_definitions(ChainRig PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_ASIO=1                       # usa le intestazioni ASIO incluse in JUCE (solo Windows)
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ChainRig,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ChainRig,JUCE_VERSION>")

if(WIN32 AND CHAINRIG_BLUETOOTH_MIDI)
    target_compile_definitions(ChainRig PRIVATE JUCE_USE_WINRT_MIDI=1)
endif()

target_link_libraries(ChainRig
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
