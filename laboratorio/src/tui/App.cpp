// TUI completa: configuracion, ejecucion, historial, comparacion y ayuda. ANSI + modo raw (como el procmon).
// Pantallas:
//   [1] CONFIG    - formulario de la prueba + presets de los 4 experimentos
//   [2] EJECUCION - métricas en vivo de /proc/<pid> + control (pausa/detener)
//   [3] HISTORIAL - tabla de corridas, marcado y comparación
//   [4] AYUDA     - glosario de conceptos
#include "../../include/tui/App.hpp"

#include "../../include/core/Runner.hpp"
#include "../../include/core/History.hpp"
#include "../../include/platform/ProcFs.hpp"

#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ------------------- Colores ANSI -------------------
#define C_R  "\033[0m"
#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YEL     "\033[33m"
#define C_BLUE    "\033[34m"
#define C_MAG     "\033[35m"
#define C_CYAN    "\033[36m"
#define C_BOLD    "\033[1m"
#define C_DIM     "\033[2m"
#define C_INV     "\033[7m"

namespace {

// ------------------- Terminal en modo raw (técnica del Proyecto 1) -------------------
struct TerminalRawMode {
    termios old_t{};
    bool ok = false;
    TerminalRawMode() {
        if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old_t) == 0) {
            termios nt = old_t;
            nt.c_lflag &= ~(ICANON | ECHO);
            tcsetattr(STDIN_FILENO, TCSANOW, &nt);
            ok = true;
        }
    }
    ~TerminalRawMode() {
        if (ok) tcsetattr(STDIN_FILENO, TCSANOW, &old_t);
    }
};

int kbhit() {
    timeval tv{0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
}

int getch_nb() {   // lectura no bloqueante (llamar tras kbhit)
    char c = 0;
    if (::read(STDIN_FILENO, &c, 1) == 1) return (unsigned char)c;
    return -1;
}

bool is_load(const TestConfig& c) { return c.type == TestType::MemoryCpuLoad; }

// ------------------- Utilidades de formato -------------------
std::string mb_str(long kb) {           // kB -> MB legible
    char b[32];
    std::snprintf(b, sizeof b, "%.1f MB", (double)kb / 1024.0);
    return b;
}

std::string sec_str(double s) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f s", s);
    return b;
}

// Barra ASCII [####----]
std::string bar(double frac, int width) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    const int k = (int)(frac * width + 0.5);
    std::string s = "[";
    for (int i = 0; i < width; ++i) s += (i < k) ? '#' : '-';
    s += "]";
    return s;
}

// Minigráfica ASCII de una serie (últimos n valores)
std::string spark(const std::vector<double>& v, int n) {
    const std::string levels = " .:-=+*#@";
    std::string out;
    const int sz = (int)v.size();
    const int start = sz > n ? sz - n : 0;
    double mx = 1;
    for (int i = start; i < sz; ++i) mx = std::max(mx, v[i]);
    for (int i = start; i < sz; ++i) {
        const int lv = (int)((v[i] / mx) * (double)(levels.size() - 1));
        out += levels[std::min(lv, (int)levels.size() - 1)];
    }
    return out;
}

std::string fmt_long(long v) {
    char b[32];
    std::snprintf(b, sizeof b, "%ld", v);
    return b;
}

std::string resultado_corto(RunOutcome o) {
    switch (o) {
        case RunOutcome::Completed:       return "COMPLETADA";
        case RunOutcome::StoppedByUser:   return "DET-USUARIO";
        case RunOutcome::CpuLimitSigXcpu: return "LIM-CPU (SIGXCPU)";
        case RunOutcome::Killed:          return "TERMINADA";
        case RunOutcome::ReserveError:    return "ERR-RESERVA";
        case RunOutcome::ThreadError:     return "ERR-HILOS";
        case RunOutcome::Crashed:         return "CRASH";
        default:                          return "?";
    }
}

} // namespace

// ============================================================
// Aplicación
// ============================================================
namespace Tui {

class App {
public:
    int exec();

private:
    enum Screen { S_CONFIG, S_RUN, S_HISTORY, S_COMPARE, S_HELP };
    enum Field {
        F_TYPE, F_THREADS, F_THREADKIND, F_MEM, F_TOUCH, F_BURST, F_SLEEP,
        F_DURATION, F_MEMLIMIT, F_CPULIMIT, F_SYNC, F_ITERS, F_START,
        F_COUNT
    };

    // ---------- estado ----------
    Screen scr_ = S_CONFIG;
    TestConfig cfg_{};
    int  sel_ = 0;
    bool editing_ = false;
    std::string editBuf_;
    std::string msg_ = "Bienvenido. Configure la prueba y presione [i] para iniciar.";
    bool msgErr_ = false;

    SystemInfo sys_{};
    long capMB_ = 1024;

    std::vector<RunResult> history_;
    std::shared_ptr<RunState> run_;

    int histSel_ = 0, histScroll_ = 0;
    int markA_ = -1, markB_ = -1;
    bool confirmClear_ = false;
    int compareA_ = -1, compareB_ = -1;

    // ---------- ciclo principal ----------
    void handle_key(int c);
    void draw();

    // ---------- configuración ----------
    bool field_visible(int f) const;
    void move_sel(int dir);
    void field_cycle(int dir);
    void field_edit_commit();
    void apply_preset(int p);
    void clamp_config();
    void start_run();

    // ---------- pantallas ----------
    void draw_header();
    void draw_config();
    void draw_run();
    void draw_history();
    void draw_compare();
    void draw_help();
    void draw_footer(const std::string& keys);

    // ---------- historial ----------
    void toggle_mark();
    void open_compare();
};

// ------------------------------------------------------------
int App::exec() {
    TerminalRawMode term;   // RAII: restaura el terminal al salir
    if (!term.ok) {
        std::fprintf(stderr,
            "laboratorio: la TUI necesita un terminal interactivo.\n"
            "Use './laboratorio --run ...' para el modo headless.\n");
        return 2;
    }

    sys_     = ProcFs::read_system();
    capMB_   = ProcFs::safe_memory_cap_mb();
    history_ = History::load();

    std::fputs("\033[2J\033[?25l", stdout);   // limpiar y ocultar cursor
    std::fflush(stdout);

    bool running = true;
    while (running) {
        // --- entrada no bloqueante ---
        while (kbhit()) {
            const int c = getch_nb();
            if (c < 0) break;
            const bool canQuit =
                (scr_ != S_RUN) || !run_ || run_->finished.load();
            if (c == 'q' && canQuit) { running = false; break; }
            handle_key(c);
        }
        if (!running) break;

        draw();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }

    // Si sale mientras corre una prueba: reanudarla (si estaba en pausa) y detenerla
    if (run_ && !run_->finished.load()) {
        if (run_->paused.load()) kill(run_->pid, SIGCONT);
        Runner::request_stop(run_);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    std::fputs("\033[?25h\033[2J\033[H", stdout);
    std::fflush(stdout);
    return 0;
}

// ------------------------------------------------------------
// Manejo de teclas por pantalla
// ------------------------------------------------------------
void App::handle_key(int c) {
    // Secuencias de flecha: ESC [ A/B/C/D
    static int arrowState = 0; // 0 normal, 1 tras ESC, 2 tras '['
    int arrow = 0;             // 1=arriba 2=abajo 3=der 4=izq
    if (arrowState == 1) {
        if (c == '[') { arrowState = 2; return; }
        arrowState = 0;
    } else if (arrowState == 2) {
        arrowState = 0;
        switch (c) {
            case 'A': arrow = 1; break;
            case 'B': arrow = 2; break;
            case 'C': arrow = 3; break;
            case 'D': arrow = 4; break;
            default: return;
        }
    } else if (c == 27) {
        arrowState = 1;
        return;
    }

    switch (scr_) {
        case S_CONFIG: {
            if (editing_) {
                if (c == '\r' || c == '\n') { field_edit_commit(); return; }
                if (c == 27) { editing_ = false; editBuf_.clear(); return; }
                if (c >= '0' && c <= '9') {
                    if (editBuf_.size() < 9) editBuf_ += (char)c;
                    return;
                }
                if (c == 127 || c == 8) {   // backspace
                    if (!editBuf_.empty()) editBuf_.pop_back();
                    return;
                }
                return;
            }
            if (arrow == 1) { move_sel(-1); return; }
            if (arrow == 2) { move_sel(+1); return; }
            if (arrow == 3 || c == '+' || c == '=') { field_cycle(+1); return; }
            if (arrow == 4 || c == '-')             { field_cycle(-1); return; }
            if (c == '\r' || c == '\n') {
                if (sel_ == F_START) { start_run(); return; }
                if (field_visible(sel_)) {
                    switch (sel_) {
                        case F_THREADS: case F_MEM: case F_TOUCH: case F_BURST:
                        case F_SLEEP: case F_DURATION: case F_MEMLIMIT:
                        case F_CPULIMIT: case F_ITERS:
                            editing_ = true; editBuf_.clear(); return;
                        default:
                            field_cycle(+1); return;   // enums: Enter cicla
                    }
                }
                return;
            }
            if (c == 'i' || c == 'I') { start_run(); return; }
            if (c == 'h' || c == 'H') { scr_ = S_HISTORY; histSel_ = 0; return; }
            if (c == '?') { scr_ = S_HELP; return; }
            if (c >= '1' && c <= '4') { apply_preset(c - '0'); return; }
            return;
        }

        case S_RUN: {
            if (!run_) { scr_ = S_CONFIG; return; }
            const bool fin = run_->finished.load();
            if (c == 'p' || c == 'P') { if (!fin) Runner::toggle_pause(run_); return; }
            if (c == 'x' || c == 'X') { if (!fin) Runner::request_stop(run_); return; }
            if (fin) {
                if (c == '\r' || c == '\n' || c == 'b' || c == 'B') {
                    run_.reset();
                    scr_ = S_CONFIG;
                    msg_ = "Listo para otra prueba. Repita con otra configuracion para comparar.";
                    msgErr_ = false;
                } else if (c == 'h' || c == 'H') {
                    run_.reset();
                    histSel_ = 0;
                    scr_ = S_HISTORY;
                }
            }
            return;
        }

        case S_HISTORY: {
            const int n = (int)history_.size();
            if (arrow == 1 && histSel_ > 0) histSel_--;
            else if (arrow == 2 && histSel_ < n - 1) histSel_++;
            else if (c == ' ') toggle_mark();
            else if (c == 'c' || c == 'C') open_compare();
            else if (c == 'l' || c == 'L') {
                if (confirmClear_) {
                    History::clear();
                    history_.clear();
                    markA_ = markB_ = -1;
                    confirmClear_ = false;
                    msg_ = "Historial borrado.";
                    msgErr_ = false;
                } else {
                    confirmClear_ = true;
                    msg_ = "Presione [l] otra vez para CONFIRMAR el borrado del historial.";
                    msgErr_ = true;
                }
                return;
            } else if (c == 'b' || c == 'B' || c == '\r' || c == '\n') {
                confirmClear_ = false;
                scr_ = S_CONFIG;
                return;
            }
            // ajustar scroll para que la fila seleccionada sea visible
            const int rows = 12;
            if (histSel_ < histScroll_) histScroll_ = histSel_;
            if (histSel_ >= histScroll_ + rows) histScroll_ = histSel_ - rows + 1;
            if (histScroll_ < 0) histScroll_ = 0;
            return;
        }

        case S_COMPARE:
            if (c == 'b' || c == 'B' || c == '\r' || c == '\n') {
                scr_ = S_HISTORY;
                return;
            }
            return;

        case S_HELP:
            if (c == 'b' || c == 'B' || c == '\r' || c == '\n' || c == '?') {
                scr_ = S_CONFIG;
                return;
            }
            return;
    }
}

// ------------------------------------------------------------
// Campos de configuración
// ------------------------------------------------------------
bool App::field_visible(int f) const {
    const bool load = is_load(cfg_);
    switch (f) {
        case F_TOUCH: case F_BURST: case F_SLEEP: case F_DURATION:
            return load;
        case F_SYNC: case F_ITERS:
            return !load;
        default:
            return true;
    }
}

void App::move_sel(int dir) {
    int s = sel_;
    for (int guard = 0; guard < F_COUNT * 2; ++guard) {
        s += dir;
        if (s < 0) { s = 0; break; }
        if (s >= F_COUNT) { s = F_COUNT - 1; break; }
        if (field_visible(s)) { sel_ = s; return; }
    }
    sel_ = s;
}

void App::field_cycle(int dir) {
    clamp_config();
    switch (sel_) {
        case F_TYPE:
            cfg_.type = is_load(cfg_)
                      ? TestType::SharedCounter : TestType::MemoryCpuLoad;
            if (!is_load(cfg_)) cfg_.counterIters = 5000000;
            break;
        case F_THREADS:
            cfg_.threads = std::max(1, std::min(64, cfg_.threads + dir));
            break;
        case F_THREADKIND:
            cfg_.threadKind = (cfg_.threadKind == ThreadKind::NativeOs)
                            ? ThreadKind::RuntimeManaged : ThreadKind::NativeOs;
            break;
        case F_MEM:
            cfg_.memoryMB = std::max(8, std::min((int)capMB_, cfg_.memoryMB + dir * 16));
            break;
        case F_TOUCH:
            cfg_.touchPercent = std::max(10, std::min(100, cfg_.touchPercent + dir * 10));
            break;
        case F_BURST:
            cfg_.cpuBurstMs = std::max(0, std::min(500, cfg_.cpuBurstMs + dir * 10));
            break;
        case F_SLEEP:
            cfg_.sleepMs = std::max(0, std::min(1000, cfg_.sleepMs + dir * 10));
            break;
        case F_DURATION:
            cfg_.durationSec = std::max(5, std::min(120, cfg_.durationSec + dir * 5));
            break;
        case F_MEMLIMIT:
            cfg_.memLimitMB = std::max(0, std::min((int)capMB_, cfg_.memLimitMB + dir * 32));
            break;
        case F_CPULIMIT:
            cfg_.cpuLimitSec = std::max(0, std::min(120, cfg_.cpuLimitSec + dir));
            break;
        case F_SYNC:
            switch (cfg_.sync) {
                case SyncMode::None:   cfg_.sync = dir > 0 ? SyncMode::Atomic : SyncMode::Mutex; break;
                case SyncMode::Atomic: cfg_.sync = dir > 0 ? SyncMode::Mutex : SyncMode::None; break;
                case SyncMode::Mutex:  cfg_.sync = dir > 0 ? SyncMode::None : SyncMode::Atomic; break;
            }
            break;
        case F_ITERS: {
            long v = cfg_.counterIters;
            if (dir > 0) v = std::min(100000000L, v * 2);
            else         v = std::max(1000L,     v / 2);
            cfg_.counterIters = v;
            break;
        }
        default: break;
    }
    clamp_config();
}

void App::field_edit_commit() {
    editing_ = false;
    if (editBuf_.empty()) return;
    const long v = std::atol(editBuf_.c_str());
    editBuf_.clear();
    switch (sel_) {
        case F_THREADS:  cfg_.threads     = (int)std::max(1L, std::min(64L, v)); break;
        case F_MEM:      cfg_.memoryMB    = (int)std::max(8L,  std::min(capMB_, v)); break;
        case F_TOUCH:    cfg_.touchPercent= (int)std::max(10L, std::min(100L, v)); break;
        case F_BURST:    cfg_.cpuBurstMs  = (int)std::max(0L,  std::min(500L, v)); break;
        case F_SLEEP:    cfg_.sleepMs     = (int)std::max(0L,  std::min(1000L, v)); break;
        case F_DURATION: cfg_.durationSec = (int)std::max(5L,  std::min(120L, v)); break;
        case F_MEMLIMIT: cfg_.memLimitMB  = (int)std::max(0L,  std::min(capMB_, v)); break;
        case F_CPULIMIT: cfg_.cpuLimitSec = (int)std::max(0L,  std::min(120L, v)); break;
        case F_ITERS:    cfg_.counterIters= std::max(1000L, std::min(100000000L, v)); break;
        default: break;
    }
    clamp_config();
}

void App::clamp_config() {
    cfg_.threads      = std::max(1, std::min(64, cfg_.threads));
    cfg_.memoryMB     = std::max(8, std::min((int)capMB_, cfg_.memoryMB));
    cfg_.touchPercent = std::max(10, std::min(100, cfg_.touchPercent));
    cfg_.cpuBurstMs   = std::max(0, std::min(500, cfg_.cpuBurstMs));
    cfg_.sleepMs      = std::max(0, std::min(1000, cfg_.sleepMs));
    cfg_.durationSec  = std::max(5, std::min(120, cfg_.durationSec));
    cfg_.memLimitMB   = std::max(0, std::min((int)capMB_, cfg_.memLimitMB));
    cfg_.cpuLimitSec  = std::max(0, std::min(120, cfg_.cpuLimitSec));
    cfg_.counterIters = std::max(1000L, std::min(100000000L, cfg_.counterIters));
}

void App::apply_preset(int p) {
    cfg_.memLimitMB  = 0;
    cfg_.cpuLimitSec = 0;
    switch (p) {
        case 1: // Concurrencia: misma carga, variar hilos
            cfg_.type = TestType::MemoryCpuLoad;
            cfg_.threads = 1; cfg_.threadKind = ThreadKind::NativeOs;
            cfg_.memoryMB = 64; cfg_.touchPercent = 100;
            cfg_.cpuBurstMs = 50; cfg_.sleepMs = 50; cfg_.durationSec = 10;
            msg_ = "PRESET 1 Concurrencia: ejecute con 1 hilo y repita con 2, 4 y 8 (solo cambie HILOS).";
            break;
        case 2: // Límite de CPU
            cfg_.type = TestType::MemoryCpuLoad;
            cfg_.threads = 2; cfg_.threadKind = ThreadKind::NativeOs;
            cfg_.memoryMB = 64; cfg_.touchPercent = 100;
            cfg_.cpuBurstMs = 200; cfg_.sleepMs = 0; cfg_.durationSec = 30;
            cfg_.cpuLimitSec = 0;
            msg_ = "PRESET 2 Limite CPU: ejecute SIN limite y repita con limite CPU = 3 s y 1 s.";
            break;
        case 3: // Presión de memoria bajo límite
            cfg_.type = TestType::MemoryCpuLoad;
            cfg_.threads = 4; cfg_.threadKind = ThreadKind::NativeOs;
            cfg_.memoryMB = (int)std::min(200L, capMB_); cfg_.touchPercent = 100;
            cfg_.cpuBurstMs = 10; cfg_.sleepMs = 200; cfg_.durationSec = 15;
            cfg_.memLimitMB = (int)std::min(800L, capMB_);
            msg_ = "PRESET 3 Presion de memoria: repita variando MEMORIA (200/400/600 MB) con el mismo limite.";
            break;
        case 4: // Contador compartido con y sin sincronización
            cfg_.type = TestType::SharedCounter;
            cfg_.threads = 4; cfg_.threadKind = ThreadKind::NativeOs;
            cfg_.sync = SyncMode::None; cfg_.counterIters = 5000000;
            msg_ = "PRESET 4 Sincronizacion: ejecute con NINGUNA y repita con atomic y mutex.";
            break;
    }
    clamp_config();
    sel_ = 0;
}

void App::start_run() {
    clamp_config();
    run_ = Runner::start(cfg_);
    scr_ = S_RUN;
    msgErr_ = false;
}

// ------------------------------------------------------------
// Dibujo
// ------------------------------------------------------------
void App::draw() {
    std::fputs("\033[H", stdout);   // home (sin clear: evita parpadeo)
    switch (scr_) {
        case S_CONFIG:  draw_header(); draw_config();  break;
        case S_RUN:     draw_header(); draw_run();     break;
        case S_HISTORY: draw_header(); draw_history(); break;
        case S_COMPARE: draw_header(); draw_compare(); break;
        case S_HELP:    draw_help(); break;
    }
    std::fflush(stdout);
}

void App::draw_header() {
    std::printf("%s%s=== LABORATORIO DE CONCURRENCIA Y MEMORIA - Proyecto 2 SO ===%s\033[K\n",
                C_BOLD, C_CYAN, C_R);
    std::printf("RAM disp: %s de %s | Nucleos: %d | SWAP: %s | Tope seguro de reserva: %ld MB\033[K\n",
                mb_str(sys_.memAvailableKB).c_str(), mb_str(sys_.memTotalKB).c_str(),
                sys_.cores,
                sys_.swapEnabled() ? "habilitada" : "NO disponible",
                capMB_);
    std::printf("\033[K\n");
}

void App::draw_config() {
    static const char* labels[F_COUNT] = {
        "Tipo de prueba",
        "Hilos de trabajo",
        "Tipo de hilos",
        "Memoria a reservar (MB)",
        "Paginas a tocar por pasada (%)",
        "Rafaga de CPU por pasada (ms)",
        "Pausa entre pasadas (ms)",
        "Duracion de la prueba (s)",
        "Limite de memoria MB (0=off)",
        "Limite de CPU s (0=off)",
        "Sincronizacion del contador",
        "Iteraciones por hilo (contador)",
        ">>> INICIAR PRUEBA <<<"
    };

    // advertencias de seguridad
    if (cfg_.memoryMB > capMB_ * 7 / 10)
        std::printf("%s! AVISO: reserva grande (%d MB). El tope seguro es %ld MB.%s\033[K\n",
                    C_YEL, cfg_.memoryMB, capMB_, C_R);
    if (cfg_.memLimitMB > 0 && cfg_.memLimitMB < cfg_.memoryMB + cfg_.threads * 8 + 60)
        std::printf("%s! AVISO: el limite de memoria (%d MB) puede no dejar espacio para hilos+runtime.%s\033[K\n",
                    C_YEL, cfg_.memLimitMB, C_R);

    const int sel = sel_;
    for (int f = 0; f < F_COUNT; ++f) {
        if (!field_visible(f)) continue;
        const bool on = (f == sel);
        std::string val;
        const char* extra = "";
        const char* valColor = on ? C_BOLD : "";
        switch (f) {
            case F_TYPE:      val = is_load(cfg_)
                                     ? "< Carga de memoria y CPU >" : "< Contador compartido >"; break;
            case F_THREADS:   val = editing_ ? editBuf_ + "_" : std::to_string(cfg_.threads); break;
            case F_THREADKIND:val = cfg_.threadKind == ThreadKind::NativeOs
                                     ? "< Nativos del SO (std::thread) >"
                                     : "< Runtime C++ (std::async) >"; break;
            case F_MEM:       val = editing_ ? editBuf_ + "_" : std::to_string(cfg_.memoryMB); extra = " MB"; break;
            case F_TOUCH:     val = editing_ ? editBuf_ + "_" : std::to_string(cfg_.touchPercent); extra = " %"; break;
            case F_BURST:     val = editing_ ? editBuf_ + "_" : std::to_string(cfg_.cpuBurstMs); extra = " ms"; break;
            case F_SLEEP:     val = editing_ ? editBuf_ + "_" : std::to_string(cfg_.sleepMs); extra = " ms"; break;
            case F_DURATION:  val = editing_ ? editBuf_ + "_" : std::to_string(cfg_.durationSec); extra = " s"; break;
            case F_MEMLIMIT:  val = editing_ ? editBuf_ + "_" : std::to_string(cfg_.memLimitMB); extra = " MB"; break;
            case F_CPULIMIT:  val = editing_ ? editBuf_ + "_" : std::to_string(cfg_.cpuLimitSec); extra = " s"; break;
            case F_SYNC:      val = std::string("< ") + syncmode_str(cfg_.sync) + " >"; break;
            case F_ITERS:     val = editing_ ? editBuf_ + "_" : fmt_long(cfg_.counterIters); break;
            case F_START:     val = "presione [i] o Enter"; valColor = C_GREEN C_BOLD; break;
        }
        std::printf("%s%s %-32s%s %s%s%s%s\033[K\n",
                    on ? C_CYAN : " ",
                    on ? ">" : " ",
                    labels[f],
                    C_R,
                    valColor, val.c_str(), extra, C_R);
    }

    if (cfg_.memLimitMB > 0)
        std::printf("%sVA estimada: reserva %d MB + hilos*8 MB + ~40 MB de runtime debe caber en el limite.%s\033[K\n",
                    C_DIM, cfg_.memoryMB, C_R);

    std::printf("%s%s\033[K\n%s", msgErr_ ? C_YEL : C_GREEN, msg_.c_str(), C_R);

    draw_footer("[^v]mover [<> +/-]cambiar [Enter]editar/iniciar [1-4]presets [h]historial [?]ayuda [q]salir");

    // relleno para limpiar restos de frames anteriores
    for (int i = 0; i < 8; ++i) std::printf("\033[K\n");
}

void App::draw_run() {
    if (!run_) { scr_ = S_CONFIG; return; }
    RunState& rs = *run_;
    const bool fin = rs.finished.load();

    Sample last;
    long vsizePeak = 0, rssPeak = 0;
    double fltRate = 0, cpuPct = 0;
    {
        std::lock_guard<std::mutex> lk(rs.mx);
        if (!rs.samples.empty()) last = rs.samples.back();
        for (const Sample& s : rs.samples) {
            vsizePeak = std::max(vsizePeak, s.vsizeKB);
            rssPeak   = std::max(rssPeak,   s.rssKB);
        }
        const int n = (int)rs.samples.size();
        if (n >= 2) {
            const Sample& a = rs.samples[n - 2];
            const Sample& b = rs.samples[n - 1];
            const double dt = b.t - a.t;
            if (dt > 0.05) {
                fltRate = (double)(b.minFlt - a.minFlt) / dt;
                cpuPct  = 100.0 * (b.cpuSec - a.cpuSec) / dt / (double)std::max(1, sys_.cores);
                cpuPct  = std::min(cpuPct, 100.0 * sys_.cores);
            }
        }
    }

    // --- estado y límites ---
    std::printf("%sEJECUCION %s | PID: %d | Estado del proceso: %c%s\033[K\n",
                C_BOLD,
                fin ? "FINALIZADA" : "EN CURSO",
                rs.pid, last.state, C_R);
    if (rs.paused.load())
        std::printf("%s>>> PRUEBA EN PAUSA (SIGSTOP): el kernel congelo todos los hilos <<<%s\033[K\n", C_MAG, C_R);

    auto limit_line = [](const char* name, bool attempted, bool applied,
                         const std::string& val, const std::string& err) {
        if (!attempted)
            std::printf("  %s: %sno configurado%s\033[K\n", name, C_DIM, C_R);
        else if (applied)
            std::printf("  %s: %s%s APLICADO Y VERIFICADO%s\033[K\n", name, C_GREEN, val.c_str(), C_R);
        else
            std::printf("  %s: %sNO APLICADO (%s) -> PRUEBA SIN LIMITE%s\033[K\n",
                        name, C_RED, err.c_str(), C_R);
    };
    limit_line("Limite de memoria", rs.limits.memAttempted, rs.limits.memApplied,
               mb_str(rs.limits.memBytes), rs.limits.memError);
    limit_line("Limite de CPU",     rs.limits.cpuAttempted, rs.limits.cpuApplied,
               std::to_string(rs.limits.cpuSeconds) + " s de procesador", rs.limits.cpuError);
    std::printf("\033[K\n");

    // --- progreso ---
    if (is_load(rs.cfg)) {
        const double frac = rs.cfg.durationSec > 0
                          ? std::min(1.0, last.t / (double)rs.cfg.durationSec) : 0.0;
        std::printf("Progreso: %s %.1f / %d s\033[K\n",
                    bar(frac, 40).c_str(), last.t, rs.cfg.durationSec);
    } else {
        long itersNow = 0;
        if (fin) itersNow = rs.result.iterations;
        else if (rs.cb) itersNow = (long)rs.cb->iterations.load();
        std::printf("Progreso: incrementos realizados: %s de %s\033[K\n",
                    fmt_long(itersNow).c_str(),
                    fmt_long(rs.cfg.counterIters * rs.cfg.threads).c_str());
    }
    std::printf("\033[K\n");

    // --- MEMORIA (el corazón del proyecto) ---
    const long refKB = rs.cfg.memLimitMB > 0
                     ? (long)rs.cfg.memLimitMB * 1024
                     : (long)rs.cfg.memoryMB * 1024;
    std::printf("%sMEMORIA VIRTUAL :%s %-12s (pico %s)\033[K\n",
                C_BOLD, C_R, mb_str(last.vsizeKB).c_str(), mb_str(vsizePeak).c_str());
    std::printf("%sRAM RESIDENTE   :%s %-12s (pico %s) %s%s%s\033[K\n",
                C_BOLD, C_R, mb_str(last.rssKB).c_str(), mb_str(rssPeak).c_str(),
                C_CYAN, bar(refKB > 0 ? (double)last.rssKB / (double)refKB : 0.0, 30).c_str(), C_R);
    std::printf("%sSWAP (proceso)  :%s %s\033[K\n",
                C_BOLD, C_R, mb_str(last.swapKB).c_str());
    if (sys_.swapEnabled())
        std::printf("SWAP sistema    : libre %s de %s\033[K\n",
                    mb_str(sys_.swapFreeKB).c_str(), mb_str(sys_.swapTotalKB).c_str());
    else
        std::printf("SWAP sistema    : %sNO disponible en este equipo%s\033[K\n", C_DIM, C_R);
    std::printf("\033[K\n");

    // --- fallos de página y CPU ---
    std::printf("Fallos de pagina MENORES: %s (%.0f/s) | MAYORES: %s\033[K\n",
                fmt_long(last.minFlt).c_str(), fltRate, fmt_long(last.majFlt).c_str());
    std::printf("CPU: %.2f s de procesador | uso reciente: %.0f%% | ctxt vol/novol: %s/%s\033[K\n",
                last.cpuSec, cpuPct, fmt_long(last.volCtxt).c_str(), fmt_long(last.nvolCtxt).c_str());

    // --- sparkline RSS ---
    {
        std::vector<double> series;
        {
            std::lock_guard<std::mutex> lk(rs.mx);
            series.reserve(rs.samples.size());
            for (const Sample& s : rs.samples) series.push_back((double)s.rssKB);
        }
        if (series.size() >= 2)
            std::printf("RSS historial : %s%s%s\033[K\n", C_CYAN,
                        spark(series, 70).c_str(), C_R);
        else std::printf("\033[K\n");
    }
    std::printf("\033[K\n");

    // --- mensajes del hijo ---
    if (!rs.childReady.load())
        std::printf("%sEsperando inicializacion del proceso de carga...%s\033[K\n", C_DIM, C_R);
    if (!rs.childError.empty())
        std::printf("%sMensaje del hijo: %s%s\033[K\n", C_YEL, rs.childError.c_str(), C_R);

    // --- resumen final ---
    if (fin) {
        const RunResult& R = rs.result;
        std::printf("\033[K\n");
        std::printf("%s===== RESULTADO: %s%s%s =====\033[K\n",
                    C_BOLD, C_GREEN, outcome_str(R.outcome), C_R);
        std::printf("Duracion: %s | CPU consumida: %s | Hilos: %d (%s)\033[K\n",
                    sec_str(R.wallSeconds).c_str(), sec_str(R.cpuSeconds).c_str(),
                    R.cfg.threads, R.cfg.threadKind == ThreadKind::NativeOs ? "nativos" : "runtime");
        std::printf("Virtual pico: %s | Residente pico: %s | SWAP: %s | f.min: %s | f.may: %s\033[K\n",
                    mb_str(R.vsizePeakKB).c_str(), mb_str(R.rssPeakKB).c_str(),
                    mb_str(R.swapKB).c_str(), fmt_long(R.minFlt).c_str(), fmt_long(R.majFlt).c_str());
        if (!is_load(R.cfg))
            std::printf("Contador esperado: %s | obtenido: %s | %s%.1f%% de actualizaciones perdidas%s\033[K\n",
                        fmt_long(R.counterExpected).c_str(), fmt_long(R.counterFinal).c_str(),
                        R.counterFinal != R.counterExpected ? C_RED : C_GREEN,
                        pct_lost(R.counterExpected, R.counterFinal), C_R);
        else
            std::printf("Pasadas: %s | paginas accedidas: %s\033[K\n",
                        fmt_long(R.iterations).c_str(), fmt_long(R.pagesTouched).c_str());
        std::printf("Limites: %s\033[K\n", R.notas.c_str());
        std::printf("%s[Enter] volver  [h] historial/comparar%s\033[K\n", C_BOLD, C_R);
    } else {
        draw_footer("[p] pausa/reanudar (SIGSTOP/SIGCONT)   [x] detener la prueba");
    }
    for (int i = 0; i < 6; ++i) std::printf("\033[K\n");
}

void App::draw_history() {
    std::printf("%sHISTORIAL DE PRUEBAS (archivo: %s)%s\033[K\n",
                C_BOLD, History::csv_path().c_str(), C_R);
    if (history_.empty()) {
        std::printf("Aun no hay pruebas. Ejecute una y aparecera aqui.\033[K\n");
        draw_footer("[b] volver  [q] salir");
        for (int i = 0; i < 16; ++i) std::printf("\033[K\n");
        return;
    }

    std::printf("%s%3s %-9s %3s %-5s %6s %6s %4s %8s %8s %8s %8s %7s %-18s%s\033[K\n",
                C_BOLD, "#", "TIPO", "HIL", "T-HIL", "MEM", "LIMM", "LIMC",
                "DUR(s)", "CPU(s)", "VMpico", "RSSpico", "SWAP", "RESULTADO", C_R);
    std::printf("--------------------------------------------------------------------------------------------------\033[K\n");

    const int rows = 12;
    const int n = (int)history_.size();
    const int end = std::min(n, histScroll_ + rows);
    for (int i = histScroll_; i < end; ++i) {
        const RunResult& r = history_[i];
        const bool selRow = (i == histSel_);
        const bool marked = (i == markA_ || i == markB_);
        std::printf("%s%s%3d %-9s %3d %-5s %5dM %5dM %3ds %8.2f %8.2f %7.1fM %7.1fM %6ldK %-18s%s\033[K\n",
                    marked ? C_INV : "",
                    selRow ? ">" : " ",
                    r.id,
                    is_load(r.cfg) ? "Carga" : "Contador",
                    r.cfg.threads,
                    r.cfg.threadKind == ThreadKind::NativeOs ? "Nat" : "Rtm",
                    r.cfg.memoryMB, r.cfg.memLimitMB, r.cfg.cpuLimitSec,
                    r.wallSeconds, r.cpuSeconds,
                    (double)r.vsizePeakKB / 1024.0, (double)r.rssPeakKB / 1024.0,
                    r.swapKB,
                    resultado_corto(r.outcome).c_str(),
                    C_R);
    }
    std::printf("(seleccion: %d | marcadas: %d y %d)\033[K\n",
                histSel_ + 1, markA_ + 1, markB_ + 1);
    draw_footer("[^v]mover [ESPACIO]marcar 2 [c]comparar [l]limpiar historial [b]volver");
    for (int i = rows - (end - histScroll_) + 3; i > 0; --i)
        std::printf("\033[K\n");
}

void App::toggle_mark() {
    if (history_.empty()) return;
    if (histSel_ == markA_) { markA_ = -1; return; }
    if (histSel_ == markB_) { markB_ = -1; return; }
    if (markA_ < 0) markA_ = histSel_;
    else if (markB_ < 0) markB_ = histSel_;
    else { markA_ = markB_; markB_ = histSel_; }
}

void App::open_compare() {
    if (history_.empty()) {
        msg_ = "No hay pruebas que comparar.";
        msgErr_ = true;
        return;
    }
    int a = markA_, b = markB_;
    if (a < 0 || b < 0 || a == b) {
        // comparar las dos últimas del mismo tipo que la fila seleccionada
        const TestType ref = history_[std::max(0, std::min(histSel_, (int)history_.size() - 1))].cfg.type;
        a = b = -1;
        for (int i = (int)history_.size() - 1; i >= 0 && a < 0; --i)
            if (history_[i].cfg.type == ref) a = i;
        for (int i = a - 1; i >= 0 && b < 0; --i)
            if (history_[i].cfg.type == ref) b = i;
    }
    if (a < 0 || b < 0 || a == b) {
        msg_ = "Marque 2 filas con [ESPACIO] para comparar.";
        msgErr_ = true;
        return;
    }
    compareA_ = std::min(a, b);
    compareB_ = std::max(a, b);
    scr_ = S_COMPARE;
}

void App::draw_compare() {
    if (compareA_ < 0 || compareB_ < 0 ||
        compareA_ >= (int)history_.size() || compareB_ >= (int)history_.size()) {
        scr_ = S_HISTORY;
        return;
    }
    const RunResult& A = history_[compareA_];
    const RunResult& B = history_[compareB_];

    std::printf("%sCOMPARACION DE PRUEBAS%s\033[K\n", C_BOLD, C_R);
    std::printf("\033[K\n");
    auto row = [&](const char* label, const std::string& a, const std::string& b,
                   const std::string& d = "", bool highlight = false) {
        std::printf("%-24s %-26s %-26s %s%s%s\033[K\n",
                    label, a.c_str(), b.c_str(),
                    highlight ? C_YEL : C_DIM, d.c_str(), C_R);
    };
    std::printf("%s%-24s %-26s %-26s %-14s%s\033[K\n",
                C_BOLD, "Campo", ("Prueba #" + std::to_string(A.id)).c_str(),
                ("Prueba #" + std::to_string(B.id)).c_str(), "Diferencia", C_R);
    std::printf("-------------------------------------------------------------------------------\033[K\n");

    row("Tipo de prueba", testtype_str(A.cfg.type), testtype_str(B.cfg.type));
    row("Hilos", std::to_string(A.cfg.threads), std::to_string(B.cfg.threads),
        std::to_string(B.cfg.threads - A.cfg.threads));
    row("Tipo de hilos", threadkind_str(A.cfg.threadKind), threadkind_str(B.cfg.threadKind));
    row("Memoria (MB)", std::to_string(A.cfg.memoryMB), std::to_string(B.cfg.memoryMB),
        std::to_string(B.cfg.memoryMB - A.cfg.memoryMB));
    row("Limite memoria (MB)", std::to_string(A.cfg.memLimitMB), std::to_string(B.cfg.memLimitMB));
    row("Limite CPU (s)", std::to_string(A.cfg.cpuLimitSec), std::to_string(B.cfg.cpuLimitSec));
    if (!is_load(A.cfg))
        row("Sincronizacion", syncmode_str(A.cfg.sync), syncmode_str(B.cfg.sync));
    std::printf("\033[K\n");

    row("Resultado", outcome_str(A.outcome), outcome_str(B.outcome),
        "", A.outcome != B.outcome);
    row("Duracion (s)", sec_str(A.wallSeconds), sec_str(B.wallSeconds),
        sec_str(B.wallSeconds - A.wallSeconds), true);
    row("CPU consumida (s)", sec_str(A.cpuSeconds), sec_str(B.cpuSeconds),
        sec_str(B.cpuSeconds - A.cpuSeconds), true);
    row("Memoria virtual pico", mb_str(A.vsizePeakKB), mb_str(B.vsizePeakKB),
        mb_str(B.vsizePeakKB - A.vsizePeakKB), true);
    row("RAM residente pico", mb_str(A.rssPeakKB), mb_str(B.rssPeakKB),
        mb_str(B.rssPeakKB - A.rssPeakKB), true);
    row("SWAP (KB)", fmt_long(A.swapKB), fmt_long(B.swapKB),
        fmt_long(B.swapKB - A.swapKB), true);
    row("Fallos menores", fmt_long(A.minFlt), fmt_long(B.minFlt),
        fmt_long(B.minFlt - A.minFlt), true);
    row("Fallos mayores", fmt_long(A.majFlt), fmt_long(B.majFlt),
        fmt_long(B.majFlt - A.majFlt), true);
    row("ctxt vol / no vol", (fmt_long(A.volCtxt) + "/" + fmt_long(A.nvolCtxt)),
        (fmt_long(B.volCtxt) + "/" + fmt_long(B.nvolCtxt)));
    if (!is_load(A.cfg)) {
        row("Contador esperado", fmt_long(A.counterExpected), fmt_long(B.counterExpected));
        row("Contador obtenido", fmt_long(A.counterFinal), fmt_long(B.counterFinal));
        char buf[64], buf2[64];
        std::snprintf(buf,  sizeof buf, "%.2f%% perdidas", pct_lost(A.counterExpected, A.counterFinal));
        std::snprintf(buf2, sizeof buf2, "%.2f%% perdidas", pct_lost(B.counterExpected, B.counterFinal));
        row("Fiabilidad", buf, buf2, "", A.counterFinal != B.counterFinal);
    } else {
        row("Pasadas", fmt_long(A.iterations), fmt_long(B.iterations));
        row("Paginas accedidas", fmt_long(A.pagesTouched), fmt_long(B.pagesTouched));
    }
    std::printf("\033[K\nNotas A: %s\033[K\n", A.notas.c_str());
    std::printf("Notas B: %s\033[K\n", B.notas.c_str());
    draw_footer("[b] volver al historial");
    for (int i = 0; i < 6; ++i) std::printf("\033[K\n");
}

void App::draw_help() {
    std::printf("%sAYUDA - CONCEPTOS QUE ESTE LABORATORIO LE MUESTRA%s\033[K\n\n", C_BOLD, C_R);
    const char* lines[] = {
        "Memoria VIRTUAL (VmSize): espacio de direcciones que el proceso reserva.",
        "  Reservar no es usar: mmap() crea el mapa pero las paginas no ocupan RAM.",
        "RAM RESIDENTE (VmRSS): paginas que estan en memoria fisica en este momento.",
        "  Sube cuando el proceso ESCRIBE/LEE las paginas por primera vez.",
        "SWAP (VmSwap): paginas del proceso que el kernel movio al disco.",
        "  Solo aparece con presion de memoria y con la SWAP del sistema habilitada.",
        "Fallo de pagina MENOR: se resuelve sin ir al disco (p.ej. primera escritura).",
        "  Es lo que dispara el paso de virtual -> residente. Vera miles al tocar paginas.",
        "Fallo de pagina MAYOR: hay que traer la pagina desde el disco (swap o archivo).",
        "RLIMIT_AS (setrlimit): tope de direccion virtual por proceso. Si mmap lo supera,",
        "  mmap falla con ENOMEM y la carga se detiene de forma controlada.",
        "RLIMIT_CPU (setrlimit): tope de SEGUNDOS DE PROCESADOR (suma de los hilos).",
        "  NO reduce nucleos: al agotarlo el kernel envia SIGXCPU y el proceso muere.",
        "Hilos nativos (std::thread -> pthread): cada uno es una tarea del planificador.",
        "Runtime C++ (std::async): el runtime administra tareas y devuelve std::future.",
        "Condicion de carrera: 'contador++' son 3 pasos (leer, sumar, escribir); con varios",
        "  hilos se pierden actualizaciones. atomic/mutex hacen la operacion indivisible.",
        "",
        "PRESETS [1..4] = los 4 experimentos pedidos en el enunciado del proyecto.",
    };
    for (const char* l : lines) std::printf("%s\033[K\n", l);
    draw_footer("[b] volver");
    for (int i = 0; i < 3; ++i) std::printf("\033[K\n");
}

void App::draw_footer(const std::string& keys) {
    std::printf("\033[K%s%s%s\n", C_DIM, keys.c_str(), C_R);
}

// ------------------------------------------------------------
int run() {
    App app;
    return app.exec();
}

} // namespace Tui
