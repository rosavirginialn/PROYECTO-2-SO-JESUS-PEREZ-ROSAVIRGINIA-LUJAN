// Tipos y configuraciones compartidas por toda la app
#pragma once
#include <string>
#include <vector>

enum class TestType {
    MemoryCpuLoad,   // Carga general: hilos + reserva de memoria + ráfagas de CPU
    SharedCounter    // Acceso compartido: contador incrementado por N hilos
};

enum class ThreadKind {
    NativeOs,        // Hilos nativos del SO: std::thread (pthread_create)
    RuntimeManaged   // Tareas administradas por el runtime C++: std::async/std::future
};

enum class SyncMode {
    None,            // Sin sincronización (condición de carrera observable)
    Atomic,          // std::atomic<int64_t>::fetch_add
    Mutex            // std::mutex + lock
};

enum class RunOutcome {
    Unknown,
    Completed,        // Llegó al final de la duración o terminó el contador
    StoppedByUser,    // El usuario la detuvo desde la TUI
    CpuLimitSigXcpu,  // El kernel envió SIGXCPU por agotar RLIMIT_CPU
    Killed,           // Terminada por SIGKILL/SIGTERM de la app (escalamiento)
    ReserveError,     // mmap/malloc falló (típicamente por RLIMIT_AS)
    ThreadError,      // No se pudieron crear los hilos
    Crashed           // Otra señal fatal
};

inline const char* outcome_str(RunOutcome o) {
    switch (o) {
        case RunOutcome::Completed:       return "COMPLETADA";
        case RunOutcome::StoppedByUser:   return "DETENIDA (usuario)";
        case RunOutcome::CpuLimitSigXcpu: return "DETENIDA POR LIMITE CPU (SIGXCPU)";
        case RunOutcome::Killed:          return "TERMINADA (SIGKILL/SIGTERM)";
        case RunOutcome::ReserveError:    return "ERROR DE RESERVA (limite memoria)";
        case RunOutcome::ThreadError:     return "ERROR AL CREAR HILOS";
        case RunOutcome::Crashed:         return "CRASH (senal fatal)";
        default:                          return "DESCONOCIDO";
    }
}

inline const char* testtype_str(TestType t) {
    return t == TestType::MemoryCpuLoad ? "Carga Mem+CPU" : "Contador compartido";
}

inline const char* threadkind_str(ThreadKind k) {
    return k == ThreadKind::NativeOs ? "Nativos del SO (std::thread)"
                                     : "Runtime C++ (std::async)";
}

inline const char* syncmode_str(SyncMode s) {
    switch (s) {
        case SyncMode::None:   return "NINGUNA (sin sincronizar)";
        case SyncMode::Atomic: return "std::atomic";
        case SyncMode::Mutex:  return "std::mutex";
    }
    return "?";
}

// Configuracion de una prueba
struct TestConfig {
    TestType   type           = TestType::MemoryCpuLoad;
    int        threads        = 4;        // hilos de trabajo (1..64)
    ThreadKind threadKind     = ThreadKind::NativeOs;
    int        memoryMB       = 64;       // memoria virtual total a reservar (MB)
    int        touchPercent   = 100;      // % de páginas accedidas por pasada (10..100)
    int        cpuBurstMs     = 20;       // ráfaga de CPU por pasada (0..500 ms)
    int        sleepMs        = 80;       // pausa entre pasadas (0..1000 ms)
    int        memLimitMB     = 0;        // límite de memoria (RLIMIT_AS), 0 = sin límite
    int        cpuLimitSec    = 0;        // límite de CPU (RLIMIT_CPU), 0 = sin límite
    int        durationSec    = 15;       // duración objetivo de la carga (s)
    SyncMode   sync           = SyncMode::Atomic;   // solo contador compartido
    long       counterIters   = 1000000;  // incrementos por hilo (solo contador)
    int        sampleMs       = 500;      // intervalo de muestreo de /proc
};

// Muestra puntual tomada de /proc/<pid>
struct Sample {
    double t       = 0;      // segundos desde el inicio de la prueba
    long   vsizeKB = 0;      // memoria virtual (VmSize)
    long   rssKB   = 0;      // memoria residente (VmRSS)
    long   swapKB  = 0;      // swap del proceso (VmSwap)
    long   minFlt  = 0;      // fallos de página menores (acumulados)
    long   majFlt  = 0;      // fallos de página mayores (acumulados)
    double cpuSec  = 0;      // utime+stime del proceso (segundos)
    char   state   = '-';    // estado del proceso R/S/T/Z
    long   volCtxt = 0;      // cambios de contexto voluntarios
    long   nvolCtxt= 0;      // cambios de contexto no voluntarios
};

// Resultado de corrida (lo que va al historial)
struct RunResult {
    int        id = 0;
    std::string fecha;        // DD/MM/AAAA
    std::string hora;         // HH:MM:SS

    TestConfig  cfg;
    RunOutcome  outcome = RunOutcome::Unknown;
    std::string notas;        // mensajes de limites/errores

    double wallSeconds = 0;   // duracion en tiempo real
    double cpuSeconds  = 0;   // CPU consumida por la carga (última muestra)

    long vsizePeakKB = 0;     // pico de memoria virtual
    long vsizeLastKB = 0;
    long rssPeakKB   = 0;     // pico de memoria residente
    long rssLastKB   = 0;
    long swapKB      = 0;     // swap del proceso al final
    long minFlt      = 0;     // fallos menores acumulados
    long majFlt      = 0;     // fallos mayores acumulados
    long volCtxt     = 0;
    long nvolCtxt    = 0;

    // Resultado del experimento de contador compartido
    long counterExpected = 0;
    long counterFinal    = 0;

    // Progreso de la carga general
    long pagesTouched = 0;
    long iterations   = 0;
};

// Utilidades numericas pequeñas
inline double pct_lost(long expected, long finalv) {
    if (expected <= 0) return 0.0;
    double lost = (double)(expected - finalv);
    if (lost < 0) lost = 0;
    return lost * 100.0 / (double)expected;
}
