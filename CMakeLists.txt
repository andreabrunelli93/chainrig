cmake_minimum_required(VERSION 3.22)

project(ChainRig VERSION 0.2.4)

# Windows: runtime C++ statico, così sul computer di destinazione non serve installare il Visual C++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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

# Neural Amp Modeler (MIT): il motore delle catture NAM per il posto "NAM" integrato.
# Con -DNAM_PATH=... usi una copia locale di NeuralAmpModelerCore (con i sottomoduli).
set(NAM_PATH "" CACHE PATH "Percorso di una copia locale di NeuralAmpModelerCore (facoltativo)")

if(NOT NAM_PATH)
    include(FetchContent)
    FetchContent_Declare(nam_core
        GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
        GIT_TAG v0.6.0
        GIT_SHALLOW TRUE
        GIT_SUBMODULES "Dependencies/eigen" "Dependencies/AudioDSPTools"
        SOURCE_SUBDIR _non_compilare_gli_strumenti)   # solo i sorgenti: il loro CMakeLists compila anche i tool
    FetchContent_MakeAvailable(nam_core)
    set(NAM_PATH ${nam_core_SOURCE_DIR})
endif()

file(GLOB NAM_SOURCES "${NAM_PATH}/NAM/*.cpp" "${NAM_PATH}/NAM/*/*.cpp")
# OBJECT e non STATIC: le architetture si registrano con oggetti statici che il linker scarterebbe
add_library(nam_core OBJECT ${NAM_SOURCES})
target_include_directories(nam_core SYSTEM PUBLIC
    "${NAM_PATH}"
    "${NAM_PATH}/Dependencies/eigen"
    "${NAM_PATH}/Dependencies/nlohmann"
    "${NAM_PATH}/Dependencies/AudioDSPTools/dsp")
target_compile_definitions(nam_core PUBLIC NAM_ENABLE_A2_FAST)
if(WIN32)
    target_compile_definitions(nam_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    target_compile_options(nam_core PRIVATE /bigobj /W0)
else()
    target_compile_options(nam_core PRIVATE -w)
endif()

juce_add_gui_app(ChainRig
    PRODUCT_NAME "ChainRig"
    COMPANY_NAME "ChainRig"
    BUNDLE_ID "com.chainrig.app"
    # Icona dell'app (JUCE genera .icns per macOS e .ico per Windows)
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/logo-1024.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/logo-1024.png"
    # macOS: senza questo permesso l'ingresso della scheda audio arriva muto
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "ChainRig usa l'ingresso della scheda audio per la chitarra.")

target_sources(ChainRig PRIVATE
    Source/Main.cpp
    Source/Engine.cpp
    Source/MainComponent.cpp
    Source/Look.cpp
    Source/Look.h
    Source/NamProcessor.cpp
    Source/NamProcessor.h
    Source/PluginChooser.cpp
    Source/PluginChooser.h
    Source/PluginScanner.h
    Source/SetlistEditor.cpp
    Source/SetlistEditor.h
    Source/Recorder.h
    Source/UpdateChecker.h)

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

juce_add_binary_data(ChainRigData SOURCES assets/buymeacoffee.png)

target_link_libraries(ChainRig
    PRIVATE
        ChainRigData
        nam_core
        juce::juce_dsp
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
