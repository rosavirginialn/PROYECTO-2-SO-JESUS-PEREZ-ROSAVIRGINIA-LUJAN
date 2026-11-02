# Proyecto 2 - Laboratorio de concurrencia y memoria
#   mkdir -p build && cd build && cmake .. && make
#   ./laboratorio
cmake_minimum_required(VERSION 3.10)
project(laboratorio CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_executable(laboratorio
    src/main.cpp
    src/platform/ProcFs.cpp
    src/core/Limits.cpp
    src/core/Load.cpp
    src/core/Runner.cpp
    src/core/History.cpp
    src/tui/App.cpp
)

target_link_libraries(laboratorio PRIVATE Threads::Threads)
target_compile_options(laboratorio PRIVATE -Wall -Wextra)
