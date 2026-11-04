// ============================================================
// Proyecto 2 - Laboratorio de concurrencia y memoria
// ProcFs.hpp - Lectura de /proc (extensión del ProcFs del Proyecto 1)
//
// Reutiliza la idea del procmon: parsear /proc/<pid>/stat,
// /proc/<pid>/status y /proc/meminfo. Aquí se amplía con las
// medidas que pide el enunciado: memoria virtual, residente,
// SWAP, fallos de página y cambios de contexto.
// ============================================================
#pragma once
#include <string>
#include <vector>
#include "../common/Types.hpp"

// ---------- Información de un proceso (para la carga en ejecución) ----------
struct ProcMetrics {
    bool   ok       = false;   // false si el proceso ya no existe
    long   vsizeKB  = 0;       // VmSize  (memoria virtual)
    long   rssKB    = 0;       // VmRSS   (memoria residente en RAM)
    long   swapKB   = 0;       // VmSwap  (part del proceso en SWAP)
    long   minFlt   = 0;       // fallos de página menores (acumulados)
    long   majFlt   = 0;       // fallos de página mayores (acumulados)
    double cpuSec   = 0;       // utime+stime en segundos
    char   state    = '-';     // R S D T Z ...
    long   threads  = 0;       // número de hilos del proceso
    long   volCtxt  = 0;       // cambios de contexto voluntarios
    long   nvolCtxt = 0;       // cambios de contexto no voluntarios
};

// ---------- Información global del sistema ----------
struct SystemInfo {
    long memTotalKB     = 0;
    long memAvailableKB = 0;
    long swapTotalKB    = 0;
    long swapFreeKB     = 0;
    int  cores          = 1;
    long pageSize       = 4096;
    bool swapEnabled() const { return swapTotalKB > 0; }
};

namespace ProcFs {

// Métricas del proceso indicado (una lectura puntual de /proc)
ProcMetrics read_process(pid_t pid);

// Información global del sistema (meminfo + sysconf)
SystemInfo read_system();

// Máximo de memoria que podemos pedir con seguridad (70% de lo disponible,
// con tope duro) - para que la carga nunca agote la memoria del equipo.
long safe_memory_cap_mb();

} // namespace ProcFs
