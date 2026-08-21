cmake_minimum_required(VERSION 3.20)
project(Recorder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(Recorder WIN32
    src/main.cpp
    src/recorder.cpp
    src/recorder.h
)

target_compile_definitions(Recorder PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)

target_compile_options(Recorder PRIVATE /O2 /Oi /Ot /GL /Gy /Gw /EHsc /W4 /permissive-)
target_link_options(Recorder PRIVATE /LTCG /OPT:REF /OPT:ICF)

target_link_libraries(Recorder PRIVATE user32 shell32 shlwapi)

set_target_properties(Recorder PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "Recorder"
)
