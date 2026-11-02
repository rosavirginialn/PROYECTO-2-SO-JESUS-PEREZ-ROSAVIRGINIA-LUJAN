// Implementación con setrlimit/getrlimit
#include "../../include/core/Limits.hpp"

#include <sys/resource.h>
#include <cerrno>
#include <cstring>
#include <sstream>

namespace {

std::string errno_msg() {
    return std::string(strerror(errno));
}

bool apply_rlimit_as(long bytes, long& verified, std::string& err) {
    struct rlimit rl;
    if (getrlimit(RLIMIT_AS, &rl) != 0) {
        err = "getrlimit(RLIMIT_AS) fallo: " + errno_msg();
        return false;
    }
    // El tope duro solo puede bajarse, nunca subirse: nos quedamos
    // con el menor entre el duro actual y el pedido.
    rlim_t want = (rlim_t)bytes;
    rlim_t hard = rl.rlim_max;
    if (hard != RLIM_INFINITY && want > hard) want = hard;

    rl.rlim_cur = want;
    rl.rlim_max = want; // blindamos el límite para el hijo
    if (setrlimit(RLIMIT_AS, &rl) != 0) {
        err = "setrlimit(RLIMIT_AS=" + std::to_string(bytes / (1024 * 1024)) +
              "MB) fallo: " + errno_msg();
        return false;
    }
    // Verificación independiente: leemos lo que el kernel realmente fijo.
    struct rlimit chk;
    if (getrlimit(RLIMIT_AS, &chk) != 0 || chk.rlim_cur != want) {
        err = "RLIMIT_AS no quedo fijado al valor pedido";
        return false;
    }
    verified = (chk.rlim_cur == RLIM_INFINITY) ? -1 : (long)chk.rlim_cur;
    return true;
}

bool apply_rlimit_cpu(int seconds, int& verified, std::string& err) {
    struct rlimit rl;
    if (getrlimit(RLIMIT_CPU, &rl) != 0) {
        err = "getrlimit(RLIMIT_CPU) fallo: " + errno_msg();
        return false;
    }
    // soft = segundos pedidos; el kernel manda SIGXCPU al agotarlos.
    // hard = soft + 1 s (SIGKILL si el proceso ignorara SIGXCPU).
    rlim_t want = (rlim_t)seconds;
    if (rl.rlim_max != RLIM_INFINITY && want > rl.rlim_max) want = rl.rlim_max;

    rl.rlim_cur = want;
    rl.rlim_max = (rl.rlim_max == RLIM_INFINITY) ? want + 1 : rl.rlim_max;
    if (setrlimit(RLIMIT_CPU, &rl) != 0) {
        err = "setrlimit(RLIMIT_CPU=" + std::to_string(seconds) +
              "s) fallo: " + errno_msg();
        return false;
    }
    struct rlimit chk;
    if (getrlimit(RLIMIT_CPU, &chk) != 0 || chk.rlim_cur != want) {
        err = "RLIMIT_CPU no quedo fijado al valor pedido";
        return false;
    }
    verified = (int)chk.rlim_cur;
    return true;
}

} // namespace

namespace Limits {

LimitStatus apply(const TestConfig& cfg) {
    LimitStatus st;

    if (cfg.memLimitMB > 0) {
        st.memAttempted = true;
        const long bytes = (long)cfg.memLimitMB * 1024L * 1024L;
        long verified = 0;
        std::string err;
        if (apply_rlimit_as(bytes, verified, err)) {
            st.memApplied = true;
            st.memBytes   = verified;
        } else {
            st.memError = err;
        }
    }

    if (cfg.cpuLimitSec > 0) {
        st.cpuAttempted = true;
        int verified = 0;
        std::string err;
        if (apply_rlimit_cpu(cfg.cpuLimitSec, verified, err)) {
            st.cpuApplied = true;
            st.cpuSeconds = verified;
        } else {
            st.cpuError = err;
        }
    }
    return st;
}

std::string describe(const LimitStatus& st) {
    std::ostringstream oss;
    if (st.memAttempted && st.memApplied) {
        oss << "MEM: " << (st.memBytes / (1024 * 1024)) << "MB aplicado | ";
    } else if (st.memAttempted && !st.memApplied) {
        oss << "MEM: NO APLICADO (" << st.memError << ") - SIN LIMITE | ";
    } else {
        oss << "MEM: sin limite | ";
    }
    if (st.cpuAttempted && st.cpuApplied) {
        oss << "CPU: " << st.cpuSeconds << "s aplicado";
    } else if (st.cpuAttempted && !st.cpuApplied) {
        oss << "CPU: NO APLICADO (" << st.cpuError << ") - SIN LIMITE";
    } else {
        oss << "CPU: sin limite";
    }
    return oss.str();
}

} // namespace Limits
