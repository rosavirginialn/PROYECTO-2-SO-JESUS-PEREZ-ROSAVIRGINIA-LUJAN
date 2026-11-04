// ============================================================
// Proyecto 2 - Laboratorio de concurrencia y memoria
// Runner.hpp - Lanza la carga en un proceso hijo (fork) y la supervisa
//
//  * En el HIJO: aplica los límites (setrlimit) y ejecuta la carga.
//  * En el PADRE (aquí vive la interfaz): muestrea /proc/<pid> cada
//    intervalo, controla pausa/detención y arma el RunResult final.
// ============================================================
#pragma once
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include "../common/Types.hpp"
#include "../core/ControlBlock.hpp"
#include "../core/Limits.hpp"

// Estado compartido de la prueba en curso (la interfaz lo consulta)
struct RunState {
    TestConfig  cfg;
    pid_t       pid = -1;
    ControlBlock* cb = nullptr;      // memoria compartida padre<->hijo

    std::mutex           mx;
    std::vector<Sample>  samples;    // histórico de muestras de /proc

    std::atomic<bool> finished{false};
    std::atomic<bool> userStopRequested{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> childReady{false};

    LimitStatus limits;              // válido tras childReady/finished
    std::string childError;          // mensaje de error del hijo (si hubo)
    RunResult   result;              // válido cuando finished == true
};

class Runner {
public:
    // fork() + hilo supervisor. Regresa inmediatamente.
    static std::shared_ptr<RunState> start(const TestConfig& cfg);

    // Detener la prueba (con escalamiento stop -> SIGTERM -> SIGKILL)
    static void request_stop(std::shared_ptr<RunState> rs);

    // Pausa/reanudar con SIGSTOP/SIGCONT (el kernel congela los hilos)
    static void toggle_pause(std::shared_ptr<RunState> rs);
};
