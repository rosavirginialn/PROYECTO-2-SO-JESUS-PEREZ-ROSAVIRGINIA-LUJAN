cmake_minimum_required(VERSION 3.10)
project(laboratorio CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)        # ← AÑADIR (antes de add_executable)

set(LAB_SOURCES
    src/main.cpp
    src/platform/ProcFs.cpp
    src/core/Limits.cpp
    src/core/Load.cpp
    src/core/Runner.cpp
    src/core/History.cpp
    src/gui/MainWindow.cpp
    src/gui/MemChart.cpp
    include/gui/MainWindow.hpp      # ← AÑADIR
    include/gui/MemChart.hpp
)

add_executable(laboratorio ${LAB_SOURCES})
target_link_libraries(laboratorio PRIVATE Threads::Threads Qt6::Widgets)
target_compile_options(laboratorio PRIVATE -Wall -Wextra)

# IMPORTANTE: decirle a AUTOMOC qué cabeceras procesar
target_include_directories(laboratorio PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/common
    ${CMAKE_SOURCE_DIR}/include/core
    ${CMAKE_SOURCE_DIR}/include/gui
    ${CMAKE_SOURCE_DIR}/include/platform
)
