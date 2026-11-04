// ============================================================
// Proyecto 2 - Laboratorio de concurrencia y memoria
// Load.cpp - Implementación de la carga de prueba (lado del hijo)
//
// 1) Carga de memoria + CPU:
//    - reserva con mmap() un bloque de "memoryMB" (memoria VIRTUAL),
//    - N hilos acceden (escriben) a páginas de su porción, lo que
//      genera fallos de página menores y sube la memoria RESIDENTE,
//    - entre accesos hacen ráfagas de CPU configurables,
//    - todo es detenido en cualquier momento vía cb->stopRequested.
//
// 2) Contador compartido:
//    - N hilos incrementan el mismo dato "counterIters" veces,
//    - con sincronización (atomic/mutex) o SIN ella para provocar
//      y observar la condición de carrera (actualizaciones perdidas).
// ============================================================
#include "../../include/core/Load.hpp"

#include <sys/mman.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

namespace {

// Contadores del experimento de contador compartido
volatile int64_t  g_counter_plain = 0;   // SIN sincronizar (volátil: el compilador no lo elimina)
std::atomic<int64_t> g_counter_atomic{0};
std::mutex        g_counter_mutex;
int64_t           g_counter_guarded = 0;

inline double elapsed_s(const Clock::time_point& t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

// ------------------------------------------------------------
// Ráfaga de CPU: suma bytes de la porción hasta consumir "ms".
// (la suma se acumula en el checksum para que el compilador
//  no pueda eliminar el trabajo)
// ------------------------------------------------------------
void cpu_burst(uint8_t* p, size_t bytes, int ms, std::atomic<uint64_t>& checksum) {
    if (ms <= 0 || bytes == 0) return;
    const auto t0 = Clock::now();
    uint64_t acc = 0;
    for (;;) {
        for (size_t i = 0; i < bytes; i += 64) acc += (uint64_t)p[i] * 2654435761u;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - t0).count() >= ms) break;
    }
    checksum.fetch_add(acc & 0xFF, std::memory_order_relaxed);
}

// ------------------------------------------------------------
// Hilo trabajador de la carga de memoria + CPU
// ------------------------------------------------------------
void memory_worker(int id, uint8_t* base, size_t bytes,
                   const TestConfig& cfg, ControlBlock* cb,
                   const Clock::time_point& t0) {
    const long pageSize = sysconf(_SC_PAGESIZE);
    const size_t pages  = (bytes > 0) ? bytes / (size_t)pageSize : 0;
    uint64_t myChecksum = 0;

    while (!cb->stopRequested.load(std::memory_order_relaxed)) {
        const double t = elapsed_s(t0);
        if (cfg.durationSec > 0 && t >= (double)cfg.durationSec) break;

        // --- 1) Acceder a las páginas: provoca fallos menores y RSS ---
        if (pages > 0 && cfg.touchPercent > 0) {
            const size_t want = std::max<size_t>(
                1, (size_t)((double)pages * (double)cfg.touchPercent / 100.0));
            const size_t stride = std::max<size_t>(1, pages / want);
            for (size_t pg = 0; pg < pages; pg += stride) {
                base[pg * (size_t)pageSize] = (uint8_t)(pg + (uint64_t)(id + 1));
                myChecksum += base[pg * (size_t)pageSize];
            }
            cb->pagesTouched.fetch_add(want, std::memory_order_relaxed);
        }

        // --- 2) Ráfaga de CPU sobre la porción ---
        cpu_burst(base, bytes, cfg.cpuBurstMs, cb->checksum);

        // --- 3) Pausa (deja que el planificador use los núcleos) ---
        if (cfg.sleepMs > 0) std::this_thread::sleep_for(
            std::chrono::milliseconds(cfg.sleepMs));

        cb->iterations.fetch_add(1, std::memory_order_relaxed);
    }
    cb->checksum.fetch_add(myChecksum, std::memory_order_relaxed);
}

// ------------------------------------------------------------
// Tarea del contador compartido según el modo de sincronización
// ------------------------------------------------------------
void counter_worker(long iters, SyncMode mode) {
    switch (mode) {
        case SyncMode::None:
            for (long i = 0; i < iters; ++i)
                g_counter_plain = g_counter_plain + 1;   // carrera intencional
            break;
        case SyncMode::Atomic:
            for (long i = 0; i < iters; ++i)
                g_counter_atomic.fetch_add(1, std::memory_order_relaxed);
            break;
        case SyncMode::Mutex:
            for (long i = 0; i < iters; ++i) {
                std::lock_guard<std::mutex> lk(g_counter_mutex);
                ++g_counter_guarded;
            }
            break;
    }
}

// Lanza N tareas con el tipo de hilo seleccionado y espera a que terminen.
// Devuelve false si no se pudo crear alguna tarea (p.ej. RLIMIT_AS agotado).
// Si falla a mitad de camino, pide detener la carga y espera a las tareas
// ya creadas para no dejar hilos huérfanos (std::terminate).
template <typename Fn>
bool spawn_workers(int n, ThreadKind kind, Fn&& body, ControlBlock* cb,
                   std::string& err) {
    if (kind == ThreadKind::NativeOs) {
        // Hilos NATIVOS del SO: std::thread -> pthread_create (clone)
        std::vector<std::thread> ts;
        ts.reserve((size_t)n);
        try {
            for (int i = 0; i < n; ++i) ts.emplace_back(body, i);
        } catch (const std::exception& e) {
            err = e.what();
            cb->stopRequested.store(true);
            for (auto& t : ts) if (t.joinable()) t.join();
            return false;
        }
        for (auto& t : ts) t.join();
        return true;
    }

    // Tareas administradas por el RUNTIME C++: std::async/std::future
    std::vector<std::future<void>> fs;
    fs.reserve((size_t)n);
    try {
        for (int i = 0; i < n; ++i)
            fs.push_back(std::async(std::launch::async, body, i));
    } catch (const std::exception& e) {
        err = e.what();
        cb->stopRequested.store(true);
        for (auto& f : fs) f.wait();
        return false;
    }
    for (auto& f : fs) f.get();
    return true;
}

} // namespace

// ============================================================
// Punto de entrada del hijo
// ============================================================
void Load::run_in_child(const TestConfig& cfg, ControlBlock* cb) {
    const Clock::time_point t0 = Clock::now();

    // ---------------- Experimento de contador compartido ----------------
    if (cfg.type == TestType::SharedCounter) {
        g_counter_plain = 0;
        g_counter_atomic.store(0);
        g_counter_guarded = 0;

        cb->counterExpected.store((int64_t)cfg.threads * (int64_t)cfg.counterIters);

        std::string err;
        const bool ok = spawn_workers(cfg.threads, cfg.threadKind,
                                      [&](int) { counter_worker(cfg.counterIters, cfg.sync); },
                                      cb, err);
        if (!ok) {
            cb->set_error(("no se pudieron crear los hilos: " + err).c_str());
            std::_Exit(CHILD_ERR_THREADS);
        }

        int64_t finalv = 0;
        switch (cfg.sync) {
            case SyncMode::None:   finalv = g_counter_plain; break;
            case SyncMode::Atomic: finalv = g_counter_atomic.load(); break;
            case SyncMode::Mutex:  finalv = g_counter_guarded; break;
        }
        cb->counterFinal.store(finalv);
        cb->iterations.store((uint64_t)cfg.threads * (uint64_t)cfg.counterIters);
        cb->pagesTouched.store(0);
        (void)t0;
        std::_Exit(CHILD_OK);
    }

    // ---------------- Carga de memoria + CPU ----------------
    // 1) Reservar la memoria VIRTUAL del bloque completo.
    //    MAP_NORESERVE: no compromete swap de entrada; las páginas solo
    //    se vuelven residentes cuando el hilo las escribe.
    size_t totalBytes = (size_t)cfg.memoryMB * (size_t)1024 * (size_t)1024;
    if (totalBytes < (size_t)cfg.threads * 4096) totalBytes = (size_t)cfg.threads * 4096;

    void* block = mmap(nullptr, totalBytes,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                       -1, 0);
    if (block == MAP_FAILED) {
        const std::string msg = "mmap de " + std::to_string(cfg.memoryMB) +
                                "MB fallo (errno " + std::to_string(errno) +
                                "): tipicamente RLIMIT_AS agotado";
        cb->set_error(msg.c_str());
        std::_Exit(CHILD_ERR_RESERVE);
    }

    // Pedir al kernel NO usar páginas enormes (THP) en el bloque:
    // así cada 4 KiB se gestiona por separado y los fallos de página
    // menores que provoca la carga son visibles en las mediciones.
    // (si el kernel no lo soporta, se ignora y el experimento sigue)
    madvise(block, totalBytes, MADV_NOHUGEPAGE);

    // 2) Repartir el bloque en porciones alineadas a página, una por hilo.
    const long   pageSize   = sysconf(_SC_PAGESIZE);
    const size_t chunkBytes = (totalBytes / (size_t)cfg.threads) /
                              (size_t)pageSize * (size_t)pageSize;

    // 3) Lanzar los trabajadores con el tipo de hilo elegido.
    std::string err;
    const bool ok = spawn_workers(cfg.threads, cfg.threadKind,
        [&](int id) {
            uint8_t* base = (uint8_t*)block + (size_t)id * chunkBytes;
            memory_worker(id, base, chunkBytes, cfg, cb, t0);
        }, cb, err);
    if (!ok) {
        munmap(block, totalBytes);
        cb->set_error(("no se pudieron crear los hilos: " + err).c_str());
        std::_Exit(CHILD_ERR_THREADS);
    }

    munmap(block, totalBytes);
    std::_Exit(CHILD_OK);
}
