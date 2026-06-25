cmake_minimum_required(VERSION 3.20)
project(ili9342_serial_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(serial_image_gui WIN32
    client.cpp
)

target_link_libraries(serial_image_gui PRIVATE comctl32 shell32 windowscodecs ole32)

if (MSVC)
    target_compile_options(serial_image_gui PRIVATE /EHsc /W3)
endif()
