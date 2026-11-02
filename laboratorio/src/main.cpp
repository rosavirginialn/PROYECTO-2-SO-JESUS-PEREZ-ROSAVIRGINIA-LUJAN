#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

#include "../include/core/History.hpp"
#include "../include/core/Runner.hpp"
#include "../include/platform/ProcFs.hpp"
#include "../include/tui/App.hpp"

namespace {

TestConfig g_cfg;
bool g_noCsv = false;

void print_help() {
    std::printf(
        "Laboratorio de concurrencia y memoria - Proyecto 2 Sistemas Operativos\n"
        "Integrantes: Jesus Perez - Rosa Virginia Lujan\n\n"
        "USO:\n"
        "  ./laboratorio                    Interfaz TUI interactiva (modo principal)\n"
        "  ./laboratorio --selftest         Verificacion interna de mecanismos\n"
        "  ./laboratorio --run [opciones]   Ejecuta una prueba sin TUI (headless)\n\n"
        "OPCIONES DE --run:\n"
        "  --type load|counter      Tipo de prueba (por defecto load)\n"
        "  --threads N              Hilos de trabajo (defecto 4)\n"
        "  --tkind native|runtime   Nativos del SO o tareas del runtime C++\n"
        "  --mem MB                 Memoria virtual a reservar (defecto 64)\n"
        "  --touch PCT              Porcentaje de paginas accedidas por pasada\n"
        "  --burst MS               Rafaga de CPU por pasada\n"
        "  --sleep MS               Pausa entre pasadas\n"
        "  --duration S             Duracion de la carga (defecto 15)\n"
        "  --mem-limit MB           Limite de memoria (RLIMIT_AS), 0=sin limite\n"
        "  --cpu-limit S            Limite de CPU (RLIMIT_CPU), 0=sin limite\n"
        "  --sync none|atomic|mutex Sincronizacion (solo --type counter)\n"
        "  --iters N                Incrementos por hilo (solo --type counter)\n"
        "  --sample MS              Intervalo de muestreo (defecto 500)\n"
        "  --no-csv                 No agregar al historial resultados_lab.csv\n\n"
        "Cada prueba (TUI o --run) se registra en resultados_lab.csv para\n"
        "repetir configuraciones y comparar resultados.\n");
}

bool parse_args(int argc, char** argv, bool& doRun, bool& doSelftest) {
    doRun = false;
    doSelftest = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next_long = [&](long def) -> long {
            if (i + 1 < argc) return std::atol(argv[++i]);
            return def;
        };
        auto next_str = [&](const char* def) -> std::string {
            if (i + 1 < argc) return std::string(argv[++i]);
            return std::string(def);
        };
        if (a == "--help" || a == "-h") { print_help(); return false; }
        else if (a == "--selftest") { doSelftest = true; }
        else if (a == "--run") { doRun = true; }
        else if (a == "--type") {
            g_cfg.type = next_str("load") == "counter" ? TestType::SharedCounter
                                                       : TestType::MemoryCpuLoad;
        }
        else if (a == "--threads")     g_cfg.threads      = (int)next_long(4);
        else if (a == "--tkind")       g_cfg.threadKind   = next_str("native") == "runtime"
                                                          ? ThreadKind::RuntimeManaged
                                                          : ThreadKind::NativeOs;
        else if (a == "--mem")         g_cfg.memoryMB     = (int)next_long(64);
        else if (a == "--touch")       g_cfg.touchPercent = (int)next_long(100);
        else if (a == "--burst")       g_cfg.cpuBurstMs   = (int)next_long(20);
        else if (a == "--sleep")       g_cfg.sleepMs      = (int)next_long(80);
        else if (a == "--duration")    g_cfg.durationSec  = (int)next_long(15);
        else if (a == "--mem-limit")   g_cfg.memLimitMB   = (int)next_long(0);
        else if (a == "--cpu-limit")   g_cfg.cpuLimitSec  = (int)next_long(0);
        else if (a == "--sync") {
            const std::string s = next_str("atomic");
            g_cfg.sync = s == "none" ? SyncMode::None
                       : s == "mutex" ? SyncMode::Mutex : SyncMode::Atomic;
        }
        else if (a == "--iters")       g_cfg.counterIters = next_long(1000000);
        else if (a == "--sample")      g_cfg.sampleMs     = (int)next_long(500);
        else if (a == "--no-csv")      g_noCsv = true;
        else {
            std::fprintf(stderr, "Opcion desconocida: %s (use --help)\n", a.c_str());
            return false;
        }
    }
    return true;
}

// ------------------------------------------------------------
// Modo headless: corre una prueba y reporta en texto plano
// ------------------------------------------------------------
int run_headless() {
    const SystemInfo sys = ProcFs::read_system();
    std::printf("== PRUEBA HEADLESS ==\n");
    std::printf("Sistema: %ld MB disponibles | %d nucleos | SWAP %s\n",
                sys.memAvailableKB / 1024, sys.cores,
                sys.swapEnabled() ? "habilitada" : "NO disponible");
    std::printf("Config: tipo=%s hilos=%d (%s) mem=%dMB touch=%d%% burst=%dms sleep=%dms"
                " duracion=%ds limMem=%dMB limCPU=%ds sync=%s iters=%ld\n",
                testtype_str(g_cfg.type), g_cfg.threads, threadkind_str(g_cfg.threadKind),
                g_cfg.memoryMB, g_cfg.touchPercent, g_cfg.cpuBurstMs, g_cfg.sleepMs,
                g_cfg.durationSec, g_cfg.memLimitMB, g_cfg.cpuLimitSec,
                syncmode_str(g_cfg.sync), g_cfg.counterIters);
    std::printf("---- muestras (t_s | virtual | residente | swap | f.min | f.may | cpu_s | estado) ----\n");

    auto rs = Runner::start(g_cfg);
    size_t shown = 0;
    while (!rs->finished.load()) {
        Sample last;
        size_t sz = 0;
        {
            std::lock_guard<std::mutex> lk(rs->mx);
            sz = rs->samples.size();
            if (sz > 0) last = rs->samples.back();
        }
        if (sz > shown) {
            shown = sz;
            std::printf("%6.1f | %8ld kB | %8ld kB | %6ld kB | %8ld | %6ld | %6.2f | %c\n",
                        last.t, last.vsizeKB, last.rssKB, last.swapKB,
                        last.minFlt, last.majFlt, last.cpuSec, last.state);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(g_cfg.sampleMs));
    }

    const RunResult& R = rs->result;
    std::printf("---- resultado ----\n");
    std::printf("resultado      : %s\n", outcome_str(R.outcome));
    std::printf("duracion       : %.2f s (wall) | cpu: %.2f s\n", R.wallSeconds, R.cpuSeconds);
    std::printf("virtual pico   : %ld kB | residente pico: %ld kB | swap final: %ld kB\n",
                R.vsizePeakKB, R.rssPeakKB, R.swapKB);
    std::printf("fallos pagina  : menores=%ld mayores=%ld\n", R.minFlt, R.majFlt);
    std::printf("ctxt switches  : voluntarios=%ld no-voluntarios=%ld\n", R.volCtxt, R.nvolCtxt);
    if (R.cfg.type == TestType::SharedCounter)
        std::printf("contador       : esperado=%ld obtenido=%ld perdidos=%.2f%%\n",
                    R.counterExpected, R.counterFinal,
                    pct_lost(R.counterExpected, R.counterFinal));
    else
        std::printf("progreso       : pasadas=%ld paginas=%ld\n", R.iterations, R.pagesTouched);
    std::printf("limites        : %s\n", R.notas.c_str());

    if (!g_noCsv) {
        RunResult rr = R;
        const int id = History::append(rr);
        std::printf("historial      : agregada como corrida #%d en %s\n",
                    id, History::csv_path().c_str());
    }
    return 0;
}

// ------------------------------------------------------------
// Selftest: verifica los mecanismos clave en segundos
// ------------------------------------------------------------
int selftest() {
    int fails = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("[%s] %s\n", ok ? "OK " : "FALLO", name);
        if (!ok) ++fails;
    };

    // 1) Lectura de /proc de nosotros mismos
    {
        const ProcMetrics m = ProcFs::read_process(getpid());
        check(m.ok && m.rssKB > 0 && m.vsizeKB > 0, "/proc: rss y vsize del propio proceso > 0");
    }
    // 2) Sistema
    {
        const SystemInfo s = ProcFs::read_system();
        check(s.memTotalKB > 0 && s.cores >= 1, "meminfo/nucleos legibles");
    }
    // 3) Corrida corta de carga con 2 hilos y 16 MB
    {
        TestConfig c; c.threads = 2; c.memoryMB = 16; c.durationSec = 2;
        c.cpuBurstMs = 10; c.sleepMs = 20; c.sampleMs = 200;
        auto rs = Runner::start(c);
        while (!rs->finished.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const RunResult& R = rs->result;
        check(R.outcome == RunOutcome::Completed && R.rssPeakKB > 0,
              "carga 2 hilos/16MB: COMPLETADA y rss pico > 0");
    }
    // 4) RLIMIT_AS: reserva mayor que el limite -> error controlado
    {
        TestConfig c; c.threads = 1; c.memoryMB = 256; c.durationSec = 5;
        c.memLimitMB = 128; c.sampleMs = 100;
        auto rs = Runner::start(c);
        while (!rs->finished.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const RunResult& R = rs->result;
        check(R.outcome == RunOutcome::ReserveError,
              "RLIMIT_AS 128MB + reserva 256MB -> ERROR DE RESERVA controlado");
    }
    // 5) RLIMIT_CPU: carga que quema CPU con limite de 1 s -> SIGXCPU
    {
        TestConfig c; c.threads = 1; c.memoryMB = 8; c.durationSec = 30;
        c.cpuBurstMs = 500; c.sleepMs = 0; c.cpuLimitSec = 1; c.sampleMs = 100;
        auto rs = Runner::start(c);
        while (!rs->finished.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const RunResult& R = rs->result;
        check(R.outcome == RunOutcome::CpuLimitSigXcpu,
              "RLIMIT_CPU 1s + carga intensiva -> SIGXCPU del kernel");
    }
    // 6) Contador: atomic exacto, none con perdidas
    {
        TestConfig c; c.type = TestType::SharedCounter; c.threads = 4;
        c.counterIters = 1000000; c.sync = SyncMode::Atomic; c.sampleMs = 100;
        auto rs = Runner::start(c);
        while (!rs->finished.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        check(rs->result.counterFinal == rs->result.counterExpected,
              "contador con std::atomic: sin perdidas");

        c.sync = SyncMode::None;
        rs = Runner::start(c);
        while (!rs->finished.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        check(rs->result.counterFinal < rs->result.counterExpected,
              "contador SIN sincronizacion: hay actualizaciones perdidas (carrera)");
    }
    // 7) CSV round-trip
    {
        RunResult r;
        r.cfg.threads = 3; r.cfg.memoryMB = 32; r.wallSeconds = 1.5; r.cpuSeconds = 0.7;
        r.outcome = RunOutcome::Completed; r.minFlt = 1234; r.notas = "prueba;csv";
        const int id = History::append(r);
        const auto hist = History::load();
        bool ok = !hist.empty() && hist.back().id == id && hist.back().cfg.threads == 3;
        History::clear();
        check(ok, "historial CSV: append+load+clear");
    }

    std::printf("\nSELFTEST %s (%d fallos)\n", fails == 0 ? "PASADO" : "CON FALLOS", fails);
    return fails == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    bool doRun = false, doSelftest = false;
    if (!parse_args(argc, argv, doRun, doSelftest)) return 0;

    if (doSelftest) return selftest();
    if (doRun)      return run_headless();

    if (!isatty(STDIN_FILENO)) {
        std::fprintf(stderr,
            "laboratorio: no hay terminal interactivo.\n"
            "Use './laboratorio --run ...' o './laboratorio --help'.\n");
        return 2;
    }
    return Tui::run();
}
