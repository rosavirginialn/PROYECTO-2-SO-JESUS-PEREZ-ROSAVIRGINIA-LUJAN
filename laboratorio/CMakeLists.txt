# ============================================================
# Proyecto 2 - Laboratorio de concurrencia y memoria
# CMakeLists.txt - Compilación principal (GUI Qt6)
#
#   sudo apt install qt6-base-dev      (una sola vez)
#   mkdir -p build && cd build && cmake .. && make
#   ./laboratorio                      (abre la GUI)
#
# Qt6 es obligatorio: es la interfaz del proyecto (el enunciado
# admite TUI o GUI y elegimos GUI). Si cmake falla con "Qt6 no
# encontrado", instale qt6-base-dev y vuelva a ejecutar cmake.
# ============================================================
cmake_minimum_required(VERSION 3.10)
project(laboratorio CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# ---------- Qt6 (obligatorio: la GUI vive en src/gui/) ----------
find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(LAB_SOURCES
    src/main.cpp
    src/platform/ProcFs.cpp
    src/core/Limits.cpp
    src/core/Load.cpp
    src/core/Runner.cpp
    src/core/History.cpp
    src/gui/MainWindow.cpp
    src/gui/MemChart.cpp
)

add_executable(laboratorio ${LAB_SOURCES})

target_link_libraries(laboratorio PRIVATE Threads::Threads Qt6::Widgets)

target_compile_options(laboratorio PRIVATE -Wall -Wextra)

# No usamos Q_OBJECT en nuestras clases (solo conexiones con lambdas),
# así que no hace falta ejecutar moc ni usar AUTOMOC.
