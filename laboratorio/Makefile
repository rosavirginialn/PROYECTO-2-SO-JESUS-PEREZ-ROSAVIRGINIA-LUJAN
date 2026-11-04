# ============================================================
# Proyecto 2 - Laboratorio de concurrencia y memoria
# Makefile alternativo (compila la GUI Qt6, por si el equipo
# prefiere make en lugar de CMake)
#
#   make            -> compila ./laboratorio (con GUI Qt6)
#   make run        -> compila y abre la GUI
#   make selftest   -> compila y corre la verificación interna
#   make clean
#
# Qt6 es obligatorio (la GUI es la interfaz del proyecto; el
# enunciado admite TUI o GUI y elegimos GUI). Los flags del
# compilador salen de pkg-config: si falta Qt6, make aborta con
# el mensaje de instalación. No hace falta moc: nuestras clases
# no usan Q_OBJECT (solo conexiones con lambdas).
# ============================================================

# ---------- Flags de Qt6 vía pkg-config ----------
QT_CFLAGS := $(shell pkg-config --cflags Qt6Widgets 2>/dev/null)
QT_LIBS   := $(shell pkg-config --libs Qt6Widgets 2>/dev/null)
ifeq ($(strip $(QT_LIBS)),)
$(error Qt6 no encontrado. Instale: sudo apt install -y build-essential pkg-config qt6-base-dev)
endif

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra $(QT_CFLAGS)
LDLIBS   ?= -pthread $(QT_LIBS)

SRCS := src/main.cpp \
        src/platform/ProcFs.cpp \
        src/core/Limits.cpp \
        src/core/Load.cpp \
        src/core/Runner.cpp \
        src/core/History.cpp \
        src/gui/MainWindow.cpp \
        src/gui/MemChart.cpp

OBJS := $(SRCS:.cpp=.o)
BIN  := laboratorio

.PHONY: all run selftest clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

run: $(BIN)
	./$(BIN)

selftest: $(BIN)
	./$(BIN) --selftest

clean:
	rm -f $(BIN) $(OBJS) resultados_lab.csv
