cmake_minimum_required(VERSION 3.20)
project(Recorder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Recorder WIN32
    src/main.cpp
    src/recorder.cpp
    src/recorder.h
)

target_compile_definitions(Recorder PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

target_link_libraries(Recorder PRIVATE user32 gdi32 shell32)
