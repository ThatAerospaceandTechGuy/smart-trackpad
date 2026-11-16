cmake_minimum_required(VERSION 3.21)

project(SmartTrackpad VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "SmartTrackpad is a Windows-only application.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SMARTTRACKPAD_SOURCES
    src/main.cpp
    src/App.cpp
    src/Actions.cpp
    src/Brightness.cpp
    src/Config.cpp
    src/GestureEngine.cpp
    src/Install.cpp
    src/Log.cpp
    src/Osd.cpp
    src/RawInput.cpp
    src/TrayIcon.cpp
    src/Volume.cpp
)

add_executable(SmartTrackpad WIN32 ${SMARTTRACKPAD_SOURCES})

target_include_directories(SmartTrackpad PRIVATE src)
target_compile_features(SmartTrackpad PRIVATE cxx_std_20)
target_compile_definitions(SmartTrackpad PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_link_libraries(SmartTrackpad PRIVATE
    user32
    hid
    dxva2
    shell32
    wbemuuid
    ole32
    oleaut32
    gdiplus
    dwmapi
    uuid
)

if(MSVC)
    set_property(TARGET SmartTrackpad PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(SmartTrackpad PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
else()
    target_compile_options(SmartTrackpad PRIVATE -Wall -Wextra -Wpedantic)
    if(MINGW)
        target_link_options(SmartTrackpad PRIVATE -municode)
    endif()
endif()

option(SMARTTRACKPAD_BUILD_TESTS "Build the gesture engine unit tests" ON)

if(SMARTTRACKPAD_BUILD_TESTS)
    enable_testing()

    add_executable(SmartTrackpadTests
        tests/GestureEngineTests.cpp
        src/GestureEngine.cpp
    )
    target_include_directories(SmartTrackpadTests PRIVATE src)
    target_compile_features(SmartTrackpadTests PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(SmartTrackpadTests PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    else()
        target_compile_options(SmartTrackpadTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME gesture_engine COMMAND SmartTrackpadTests)
endif()
