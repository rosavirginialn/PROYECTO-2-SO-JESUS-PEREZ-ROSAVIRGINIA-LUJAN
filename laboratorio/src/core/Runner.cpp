// fork, supervision y muestreo de la prueba
#include "../../include/core/Runner.hpp"
#include "../../include/core/Load.hpp"
#include "../../include/platform/ProcFs.hpp"

#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <chrono>

using Clock = std::chrono::steady_clock;

namespace {

constexpr int   kPollMs          = 50;    // resolución del bucle supervisor
constexpr double kTermAfterSec   = 2.0;   // stop -> SIGTERM tras 2 s
constexpr double kKillAfterSec   = 5.0;   // stop -> SIGKILL tras 5 s
constexpr size_t kMaxSamples     = 6000;  // tope de memoria del histórico

void finalize(RunState& rs, int status, double wall, const Sample& last);

// Reconstruye el LimitStatus desde lo que el hijo escribió en el bloque
LimitStatus limits_from_cb(const ControlBlock* cb) {
    LimitStatus st;
    if (!cb) return st;
    const uint32_t m = cb->limitMemState.load();
    if (m == LIMIT_APPLIED || m == LIMIT_FAILED) {
        st.memAttempted = true;
        st.memApplied   = (m == LIMIT_APPLIED);
        st.memBytes     = (long)cb->limitMemBytes.load();
        if (!st.memApplied) st.memError = "setrlimit(RLIMIT_AS) fallo";
    }
    const uint32_t c = cb->limitCpuState.load();
    if (c == LIMIT_APPLIED || c == LIMIT_FAILED) {
        st.cpuAttempted = true;
        st.cpuApplied   = (c == LIMIT_APPLIED);
        st.cpuSeconds   = (int)cb->limitCpuSec.load();
        if (!st.cpuApplied) st.cpuError = "setrlimit(RLIMIT_CPU) fallo";
    }
    return st;
}

void monitor_loop(std::shared_ptr<RunState> rs) {
    const Clock::time_point t0 = Clock::now();

    // Espera razonable a que el hijo inicialice (aplica límites).
    // La bandera vive en el BLOQUE COMPARTIDO (mmap MAP_SHARED),
    // no en el RunState: tras fork() ese objeto es privado de cada proceso.
    for (int i = 0; i < 100 && !(rs->cb && rs->cb->childReady.load()); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    rs->childReady.store(rs->cb && rs->cb->childReady.load());
    rs->limits = limits_from_cb(rs->cb);
    rs->childError = rs->cb ? std::string(rs->cb->errorMsg) : std::string();

    const pid_t pid = rs->pid;
    Sample last;

    // Primera muestra inmediata (importante para corridas muy cortas,
    // como el contador con std::atomic, que puede terminar en milisegundos)
    {
        ProcMetrics m = ProcFs::read_process(pid);
        if (m.ok) {
            Sample s;
            s.t = 0;
            s.vsizeKB = m.vsizeKB; s.rssKB = m.rssKB; s.swapKB = m.swapKB;
            s.minFlt = m.minFlt;   s.majFlt = m.majFlt;
            s.cpuSec = m.cpuSec;   s.state = m.state;
            s.volCtxt = m.volCtxt; s.nvolCtxt = m.nvolCtxt;
            last = s;
            std::lock_guard<std::mutex> lk(rs->mx);
            rs->samples.push_back(s);
        }
    }

    auto elapsed = [&]() {
        return std::chrono::duration<double>(Clock::now() - t0).count();
    };

    double lastSampleT = -1e9;
    double stopSentAt  = -1.0;
    bool   termSent = false, killSent = false;

    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        const double t = elapsed();

        // ---- Muestreo de /proc/<pid> ----
        if (t - lastSampleT >= (double)rs->cfg.sampleMs / 1000.0) {
            ProcMetrics m = ProcFs::read_process(pid);
            if (m.ok) {
                Sample s;
                s.t = t;
                s.vsizeKB = m.vsizeKB;  s.rssKB = m.rssKB;  s.swapKB = m.swapKB;
                s.minFlt  = m.minFlt;   s.majFlt = m.majFlt;
                s.cpuSec  = m.cpuSec;   s.state = m.state;
                s.volCtxt = m.volCtxt;  s.nvolCtxt = m.nvolCtxt;
                last = s;
                std::lock_guard<std::mutex> lk(rs->mx);
                rs->samples.push_back(s);
                if (rs->samples.size() > kMaxSamples) {
                    // adelgazamos el histórico a la mitad para no crecer sin control
                    std::vector<Sample> kept;
                    kept.reserve(rs->samples.size() / 2 + 1);
                    for (size_t i = 0; i < rs->samples.size(); i += 2) kept.push_back(rs->samples[i]);
                    rs->samples.swap(kept);
                }
            }
            lastSampleT = t;
        }

        // ---- Detención pedida por el usuario (con escalamiento) ----
        if (rs->userStopRequested.load()) {
            rs->cb->stopRequested.store(true);
            if (stopSentAt < 0.0) stopSentAt = t;
            if (stopSentAt >= 0.0 && t - stopSentAt > kTermAfterSec && !termSent) {
                kill(pid, SIGTERM); termSent = true;
            }
            if (stopSentAt >= 0.0 && t - stopSentAt > kKillAfterSec && !killSent) {
                kill(pid, SIGKILL); killSent = true;
            }
        }

        // ---- Fin natural de la carga (llegó a la duración objetivo) ----
        if (rs->cfg.type == TestType::MemoryCpuLoad &&
            rs->cfg.durationSec > 0 && t >= (double)rs->cfg.durationSec) {
            rs->cb->stopRequested.store(true);
        }

        // ---- ¿Terminó el hijo? ----
        int status = 0;
        const pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            // último intento de muestra antes de que desaparezca el /proc
            ProcMetrics m = ProcFs::read_process(pid);
            if (m.ok) {
                Sample s = last;
                s.t = t;
                s.vsizeKB = m.vsizeKB; s.rssKB = m.rssKB; s.swapKB = m.swapKB;
                s.minFlt = m.minFlt;   s.majFlt = m.majFlt;
                s.cpuSec = m.cpuSec;   s.state = m.state;
                s.volCtxt = m.volCtxt; s.nvolCtxt = m.nvolCtxt;
                last = s;
                std::lock_guard<std::mutex> lk(rs->mx);
                rs->samples.push_back(s);
            }
            finalize(*rs, status, t, last);
            break;
        }
        if (r < 0) { // error inesperado de waitpid
            finalize(*rs, 0, t, last);
            break;
        }
    }
}

void finalize(RunState& rs, int status, double wall, const Sample& last) {
    RunResult& R = rs.result;
    R.cfg          = rs.cfg;
    R.wallSeconds  = wall;
    R.cpuSeconds   = last.cpuSec;
    R.vsizeLastKB  = last.vsizeKB;
    R.rssLastKB    = last.rssKB;
    R.swapKB       = last.swapKB;
    R.minFlt       = last.minFlt;
    R.majFlt       = last.majFlt;
    R.volCtxt      = last.volCtxt;
    R.nvolCtxt     = last.nvolCtxt;
    R.counterExpected = rs.cb ? rs.cb->counterExpected.load() : 0;
    R.counterFinal    = rs.cb ? rs.cb->counterFinal.load() : 0;
    R.pagesTouched    = rs.cb ? (long)rs.cb->pagesTouched.load() : 0;
    R.iterations      = rs.cb ? (long)rs.cb->iterations.load() : 0;

    // Picos a partir de las muestras
    {
        std::lock_guard<std::mutex> lk(rs.mx);
        for (const Sample& s : rs.samples) {
            if (s.vsizeKB > R.vsizePeakKB) R.vsizePeakKB = s.vsizeKB;
            if (s.rssKB   > R.rssPeakKB)   R.rssPeakKB   = s.rssKB;
        }
    }

    // Resultado según cómo terminó el hijo
    const bool userStop = rs.userStopRequested.load();
    if (WIFEXITED(status)) {
        const int code = WEXITSTATUS(status);
        if (code == CHILD_OK)            R.outcome = userStop ? RunOutcome::StoppedByUser : RunOutcome::Completed;
        else if (code == CHILD_ERR_RESERVE) R.outcome = RunOutcome::ReserveError;
        else if (code == CHILD_ERR_THREADS) R.outcome = RunOutcome::ThreadError;
        else                                R.outcome = RunOutcome::Crashed;
    } else if (WIFSIGNALED(status)) {
        const int sig = WTERMSIG(status);
        if (sig == SIGXCPU)              R.outcome = RunOutcome::CpuLimitSigXcpu;
        else if (sig == SIGTERM || sig == SIGKILL)
                                         R.outcome = userStop ? RunOutcome::StoppedByUser : RunOutcome::Killed;
        else                             R.outcome = RunOutcome::Crashed;
    } else {
        R.outcome = RunOutcome::Crashed;
    }

    R.notas = Limits::describe(rs.limits);
    const std::string err = rs.cb ? std::string(rs.cb->errorMsg) : std::string();
    if (!err.empty()) R.notas += " | " + err;

    rs.finished.store(true);

    // Liberar la memoria compartida (nadie más la usa tras finished)
    if (rs.cb) {
        munmap((void*)rs.cb, sizeof(ControlBlock));
        rs.cb = nullptr;
    }
}

} // namespace

// ============================================================
// Runner
// ============================================================
std::shared_ptr<RunState> Runner::start(const TestConfig& cfg) {
    auto rs = std::make_shared<RunState>();
    rs->cfg = cfg;

    // Memoria compartida padre<->hijo para el bloque de control
    void* mem = mmap(nullptr, sizeof(ControlBlock),
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        rs->finished = true;
        rs->result.outcome = RunOutcome::Crashed;
        rs->result.notas = "mmap del bloque de control fallo";
        return rs;
    }
    rs->cb = new (mem) ControlBlock();

    const pid_t pid = fork();
    if (pid < 0) {
        rs->finished = true;
        rs->result.outcome = RunOutcome::Crashed;
        rs->result.notas = "fork() fallo";
        munmap(mem, sizeof(ControlBlock));
        rs->cb = nullptr;
        return rs;
    }

    if (pid == 0) {
        // ================= HIJO =================
        // Nunca debe regresar a la TUI: aplica límites y corre la carga.
        LimitStatus st = Limits::apply(cfg);
        rs->cb->limitMemState.store(st.memAttempted
            ? (st.memApplied ? LIMIT_APPLIED : LIMIT_FAILED) : LIMIT_NOT_TRIED);
        rs->cb->limitMemBytes.store((uint64_t)st.memBytes);
        rs->cb->limitCpuState.store(st.cpuAttempted
            ? (st.cpuApplied ? LIMIT_APPLIED : LIMIT_FAILED) : LIMIT_NOT_TRIED);
        rs->cb->limitCpuSec.store((uint64_t)st.cpuSeconds);
        rs->cb->childReady.store(true);

        Load::run_in_child(cfg, rs->cb);   // termina con _exit()
        std::_Exit(CHILD_OK);              // por si acaso
    }

    // ================= PADRE =================
    rs->pid = pid;
    std::thread(monitor_loop, rs).detach();
    return rs;
}

void Runner::request_stop(std::shared_ptr<RunState> rs) {
    if (!rs || rs->finished.load()) return;
    rs->userStopRequested.store(true);
    if (rs->cb) rs->cb->stopRequested.store(true);
}

void Runner::toggle_pause(std::shared_ptr<RunState> rs) {
    if (!rs || rs->finished.load() || rs->pid <= 0) return;
    if (rs->paused.load()) {
        kill(rs->pid, SIGCONT);
        rs->paused.store(false);
    } else {
        kill(rs->pid, SIGSTOP);   // el kernel congela todos los hilos
        rs->paused.store(true);
    }
}
