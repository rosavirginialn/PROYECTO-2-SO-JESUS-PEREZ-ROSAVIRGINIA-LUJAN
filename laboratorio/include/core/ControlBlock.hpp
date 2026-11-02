// Bloque de control en memoria compartida
#pragma once
#include <atomic>
#include <cstdint>

// Estado del intento de aplicar un iímite (lo escribe el hijo)
enum : uint32_t {
    LIMIT_NOT_TRIED = 0,   // no se solicitó ese límite
    LIMIT_APPLIED   = 1,   // setrlimit OK y verificado con getrlimit
    LIMIT_FAILED    = 2    // setrlimit falló -> la prueba NO queda limitada
};

struct ControlBlock {
    // --- Control: padre -> hijo ---
    std::atomic<bool> stopRequested{false};  // el padre pide detener la carga

    // --- Ciclo de vida: hijo -> padre ---
    std::atomic<bool>    childReady{false};  // el hijo terminó de inicializar
    std::atomic<uint32_t> limitMemState{LIMIT_NOT_TRIED};
    std::atomic<uint32_t> limitCpuState{LIMIT_NOT_TRIED};
    std::atomic<uint64_t> limitMemBytes{0};  // valor real verificado (getrlimit)
    std::atomic<uint64_t> limitCpuSec{0};    // valor real verificado (getrlimit)

    // --- Progreso de la carga: hijo -> padre ---
    std::atomic<uint64_t> iterations{0};     // pasadas completadas
    std::atomic<uint64_t> pagesTouched{0};   // páginas accedidas (escritas)
    std::atomic<uint64_t> checksum{0};       // suma de verificación del trabajo

    // --- Resultado del contador compartido ---
    std::atomic<int64_t> counterExpected{0};
    std::atomic<int64_t> counterFinal{0};

    // --- Mensaje de error del hijo (se escribe una vez, antes de childReady) ---
    char errorMsg[256] = {0};

    void set_error(const char* msg) {
        for (int i = 0; i < 255 && msg[i]; ++i) errorMsg[i] = msg[i];
        errorMsg[255] = '\0';
    }
};
