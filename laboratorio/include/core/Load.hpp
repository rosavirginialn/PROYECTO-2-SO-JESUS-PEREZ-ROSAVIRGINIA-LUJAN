// La carga de prueba que corre en el proceso hijo
#pragma once
#include "../common/Types.hpp"
#include "../core/ControlBlock.hpp"

// Códigos de salida del hijo (el padre los interpreta en waitpid)
enum ChildExitCode : int {
    CHILD_OK            = 0,   // terminó normalmente
    CHILD_ERR_RESERVE   = 42,  // no se pudo reservar la memoria (¿RLIMIT_AS?)
    CHILD_ERR_THREADS   = 43   // no se pudieron crear los hilos
};

namespace Load {

// Punto de entrada del HIJO tras fork().
// Ejecuta la prueba según cfg y escribe progreso/resultado en cb.
// NUNCA regresa: termina con _exit() para no ejecutar código del padre.
void run_in_child(const TestConfig& cfg, ControlBlock* cb);

} // namespace Load
