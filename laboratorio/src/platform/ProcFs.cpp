// Implementacion de la lectura de /proc
//
// Basado en el ProcFs.cpp del Proyecto 1 (procmon) de los mismos
// autores, ampliado con: /proc/<pid>/status (VmSize, VmRSS,
// VmSwap, cambios de contexto) y fallos de página de stat.
#include "../../include/platform/ProcFs.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {

// Lee "Clave: valor kB" de /proc/<pid>/status
bool status_kb(const std::string& path, const std::string& key, long& out) {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind(key, 0) == 0) {
            std::istringstream ss(line.substr(key.size()));
            long v = 0;
            if (ss >> v) { out = v; return true; }
        }
    }
    return false;
}

bool status_long(const std::string& path, const std::string& key, long& out) {
    return status_kb(path, key, out); // mismo formato, sin unidades
}

// Lee de /proc/meminfo la clave indicada (en kB)
long meminfo_kb(const std::string& key) {
    std::ifstream f("/proc/meminfo");
    std::string k;
    long v = 0;
    while (f >> k >> v) {
        if (k == key) return v;
        // descartar el resto de la línea (unidad "kB" / "kB\n")
        std::string rest;
        std::getline(f, rest);
    }
    return 0;
}

} // namespace

// ============================================================
// Métricas de un proceso concreto
// ============================================================
namespace ProcFs {

ProcMetrics read_process(pid_t pid) {
    ProcMetrics m;
    const std::string base = "/proc/" + std::to_string(pid);

    // ---- /proc/<pid>/stat: estado, fallos de página, tiempo de CPU ----
    std::ifstream stat(base + "/stat");
    if (!stat) return m; // el proceso ya no existe

    std::string line;
    std::getline(stat, line);

    // El nombre del programa (entre paréntesis) puede contener espacios:
    // se salta hasta el último ')' y después vienen los campos numéricos.
    const size_t p_ini = line.find('(');
    const size_t p_fin = line.rfind(')');
    if (p_ini == std::string::npos || p_fin == std::string::npos) return m;

    std::istringstream iss(line.substr(p_fin + 2));
    // Tras el estado (campo 3), el resto empieza en el campo 4 (ppid):
    //   val[k] == campo (k+4)
    //   campo 10 minflt -> val[6]   | campo 12 majflt -> val[8]
    //   campo 14 utime  -> val[10]  | campo 15 stime  -> val[11]
    //   campo 20 nthreads -> val[16]
    long val[21] = {0};
    iss >> m.state;
    for (int i = 0; i < 21; ++i) iss >> val[i];
    if (!iss) return m; // línea incompleta

    m.minFlt  = val[6];
    m.majFlt  = val[8];
    long utime = val[10], stime = val[11];
    m.threads = val[16];

    const long ticks = sysconf(_SC_CLK_TCK); // normalmente 100
    m.cpuSec = (ticks > 0) ? (double)(utime + stime) / (double)ticks : 0.0;

    // ---- /proc/<pid>/status: memoria en kB y cambios de contexto ----
    status_kb(base + "/status", "VmSize:", m.vsizeKB);
    status_kb(base + "/status", "VmRSS:",  m.rssKB);
    status_kb(base + "/status", "VmSwap:", m.swapKB);
    status_long(base + "/status", "voluntary_ctxt_switches:",    m.volCtxt);
    status_long(base + "/status", "nonvoluntary_ctxt_switches:", m.nvolCtxt);

    m.ok = true;
    return m;
}

// ============================================================
// Información global del sistema
// ============================================================
SystemInfo read_system() {
    SystemInfo si;
    si.memTotalKB     = meminfo_kb("MemTotal:");
    si.memAvailableKB = meminfo_kb("MemAvailable:");
    si.swapTotalKB    = meminfo_kb("SwapTotal:");
    si.swapFreeKB     = meminfo_kb("SwapFree:");
    si.cores          = (int)sysconf(_SC_NPROCESSORS_ONLN);
    si.pageSize       = sysconf(_SC_PAGESIZE);
    if (si.memTotalKB <= 0)     si.memTotalKB = 1;
    if (si.memAvailableKB <= 0) si.memAvailableKB = si.memTotalKB;
    return si;
}

// ============================================================
// Techo de seguridad para la reserva de memoria de la carga:
// nunca más del 70% de la RAM disponible ni más de 2 GiB.
// Así la app cumple "no diseñada para agotar la memoria".
// ============================================================
long safe_memory_cap_mb() {
    SystemInfo si = read_system();
    long cap = (long)((double)si.memAvailableKB * 0.70) / 1024; // MB
    if (cap > 2048) cap = 2048;
    if (cap < 16) cap = 16; // piso mínimo para poder probar en equipos chicos
    return cap;
}

} // namespace ProcFs
