// ============================================================
// Proyecto 2 - Laboratorio de concurrencia y memoria
// Limits.hpp - Aplicación de límites de memoria y CPU en Linux
//
// Se aplican en el PROCESO HIJO (donde corre la carga) mediante
// setrlimit():
//   * RLIMIT_AS : dirección virtual máxima que el proceso puede
//                 reservar. Si mmap/malloc la supera, falla.
//   * RLIMIT_CPU: segundos de PROCESADOR (suma de todos los hilos).
//                 Al agotarlos el kernel envía SIGXCPU. No reduce
//                 la cantidad de núcleos del equipo.
// El enunciado exige: si un límite no puede aplicarse, la app debe
// indicarlo claramente y NO presentar la prueba como limitada.
// ============================================================
#pragma once
#include "../common/Types.hpp"

struct LimitStatus {
    bool memAttempted = false;   // ¿se pidió límite de memoria?
    bool memApplied   = false;   // ¿se aplicó y verificó con getrlimit?
    long memBytes     = 0;       // valor real verificado
    std::string memError;        // motivo del fallo (si falló)

    bool cpuAttempted = false;
    bool cpuApplied   = false;
    int  cpuSeconds   = 0;
    std::string cpuError;
};

namespace Limits {

// Aplica los límites de la configuración al proceso llamante.
// Debe invocarse en el HIJO, antes de lanzar la carga.
LimitStatus apply(const TestConfig& cfg);

// Texto legible para la interfaz / el historial
std::string describe(const LimitStatus& st);

} // namespace Limits
