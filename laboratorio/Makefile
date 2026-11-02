# Makefile
#   make            -> compila ./laboratorio
#   make run        -> compila y abre la TUI
#   make selftest   -> compila y corre la verificaciosn interna
#   make clean
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
LDLIBS   ?= -pthread

SRCS := src/main.cpp \
        src/platform/ProcFs.cpp \
        src/core/Limits.cpp \
        src/core/Load.cpp \
        src/core/Runner.cpp \
        src/core/History.cpp \
        src/tui/App.cpp

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
