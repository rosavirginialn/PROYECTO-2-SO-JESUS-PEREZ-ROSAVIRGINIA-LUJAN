// ============================================================
// Proyecto 2 - Laboratorio de concurrencia y memoria
// History.cpp - Implementación del historial CSV
// ============================================================
#include "../../include/core/History.hpp"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdio>
#include <chrono>

namespace {

const char* CSV_FILE = "resultados_lab.csv";

// Columnas del CSV (separadas por ';')
// id;fecha;hora;tipo;hilos;tipo_hilos;memMB;pct_paginas;rafaga_ms;pausa_ms;
// lim_mem_MB;lim_cpu_s;duracion_s;sync;iter_contador;duracion_real;cpu;
// vm_pico_kb;rss_pico_kb;swap_kb;fmenores;fmayores;ctxt_vol;ctxt_novol;
// resultado;esperado;obtenido;notas

std::string tipo_code(TestType t)  { return t == TestType::MemoryCpuLoad ? "Carga" : "Contador"; }
std::string hilo_code(ThreadKind k){ return k == ThreadKind::NativeOs ? "Nativos" : "Runtime"; }
std::string sync_code(SyncMode s)  {
    switch (s) { case SyncMode::None: return "Ninguna";
                 case SyncMode::Atomic: return "atomic";
                 default: return "mutex"; }
}
TestType   tipo_parse(const std::string& s)   { return s == "Carga" ? TestType::MemoryCpuLoad : TestType::SharedCounter; }
ThreadKind hilo_parse(const std::string& s)   { return s == "Nativos" ? ThreadKind::NativeOs : ThreadKind::RuntimeManaged; }
SyncMode   sync_parse(const std::string& s)   { return s == "Ninguna" ? SyncMode::None : s == "atomic" ? SyncMode::Atomic : SyncMode::Mutex; }

RunOutcome outcome_parse(const std::string& s) {
    if (s == "COMPLETADA")                       return RunOutcome::Completed;
    if (s == "DETENIDA (usuario)")               return RunOutcome::StoppedByUser;
    if (s == "DETENIDA POR LIMITE CPU (SIGXCPU)")return RunOutcome::CpuLimitSigXcpu;
    if (s == "TERMINADA (SIGKILL/SIGTERM)")      return RunOutcome::Killed;
    if (s == "ERROR DE RESERVA (limite memoria)")return RunOutcome::ReserveError;
    if (s == "ERROR AL CREAR HILOS")             return RunOutcome::ThreadError;
    if (s == "CRASH (senal fatal)")              return RunOutcome::Crashed;
    return RunOutcome::Unknown;
}

std::vector<std::string> split(const std::string& line, char sep) {
    std::vector<std::string> out;
    std::string cur;
    std::stringstream ss(line);
    while (std::getline(ss, cur, sep)) out.push_back(cur);
    // si la línea termina en separador, getline no genera el último campo vacío
    if (!line.empty() && line.back() == sep) out.push_back("");
    return out;
}

long to_l(const std::string& s, long def = 0) {
    try { return std::stol(s); } catch (...) { return def; }
}
double to_d(const std::string& s, double def = 0.0) {
    try { return std::stod(s); } catch (...) { return def; }
}

} // namespace

namespace History {

std::string csv_path() { return std::string(CSV_FILE); }

std::string to_csv(const RunResult& r) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    std::string notas = r.notas;
    for (char& c : notas) if (c == ';' || c == '\n' || c == '\r') c = ',';
    o << tipo_code(r.cfg.type) << ';'
      << r.cfg.threads << ';'
      << hilo_code(r.cfg.threadKind) << ';'
      << r.cfg.memoryMB << ';'
      << r.cfg.touchPercent << ';'
      << r.cfg.cpuBurstMs << ';'
      << r.cfg.sleepMs << ';'
      << r.cfg.memLimitMB << ';'
      << r.cfg.cpuLimitSec << ';'
      << r.cfg.durationSec << ';'
      << sync_code(r.cfg.sync) << ';'
      << r.cfg.counterIters << ';'
      << r.wallSeconds << ';'
      << r.cpuSeconds << ';'
      << r.vsizePeakKB << ';'
      << r.rssPeakKB << ';'
      << r.swapKB << ';'
      << r.minFlt << ';'
      << r.majFlt << ';'
      << r.volCtxt << ';'
      << r.nvolCtxt << ';'
      << outcome_str(r.outcome) << ';'
      << r.counterExpected << ';'
      << r.counterFinal << ';'
      << r.pagesTouched << ';'
      << r.iterations << ';'
      << notas;
    return o.str();
}

bool from_csv(const std::string& line, RunResult& out) {
    if (line.empty() || line[0] == '#') return false;
    const auto f = split(line, ';');
    if (f.size() < 29) return false;
    out.id              = to_l(f[0], -1);
    out.fecha           = f[1];
    out.hora            = f[2];
    out.cfg.type        = tipo_parse(f[3]);
    out.cfg.threads     = (int)to_l(f[4], 1);
    out.cfg.threadKind  = hilo_parse(f[5]);
    out.cfg.memoryMB    = (int)to_l(f[6]);
    out.cfg.touchPercent= (int)to_l(f[7], 100);
    out.cfg.cpuBurstMs  = (int)to_l(f[8]);
    out.cfg.sleepMs     = (int)to_l(f[9]);
    out.cfg.memLimitMB  = (int)to_l(f[10]);
    out.cfg.cpuLimitSec = (int)to_l(f[11]);
    out.cfg.durationSec = (int)to_l(f[12], 15);
    out.cfg.sync        = sync_parse(f[13]);
    out.cfg.counterIters= to_l(f[14], 1000000);
    out.wallSeconds     = to_d(f[15]);
    out.cpuSeconds      = to_d(f[16]);
    out.vsizePeakKB     = to_l(f[17]);
    out.rssPeakKB       = to_l(f[18]);
    out.swapKB          = to_l(f[19]);
    out.minFlt          = to_l(f[20]);
    out.majFlt          = to_l(f[21]);
    out.volCtxt         = to_l(f[22]);
    out.nvolCtxt        = to_l(f[23]);
    out.outcome         = outcome_parse(f[24]);
    out.counterExpected = to_l(f[25]);
    out.counterFinal    = to_l(f[26]);
    out.pagesTouched    = to_l(f[27]);
    out.iterations      = to_l(f[28]);
    if (f.size() > 29) out.notas = f[29];
    return out.id >= 0;
}

std::vector<RunResult> load() {
    std::vector<RunResult> out;
    std::ifstream f(CSV_FILE);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        RunResult r;
        if (from_csv(line, r)) out.push_back(r);
    }
    // Renumerar de forma consecutiva
    for (size_t i = 0; i < out.size(); ++i) out[i].id = (int)(i + 1);
    return out;
}

int append(const RunResult& r) {
    // id = último id + 1
    int nextId = 1;
    {
        std::ifstream f(CSV_FILE);
        std::string line;
        while (std::getline(f, line)) {
            const auto p = split(line, ';');
            if (!p.empty()) {
                const int id = (int)to_l(p[0], -1);
                if (id >= nextId) nextId = id + 1;
            }
        }
    }
    std::ofstream o(CSV_FILE, std::ios::app);
    if (!o) return nextId;

    // Fecha y hora local
    const auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
    localtime_r(&tt, &tmv);
    char fbuf[40], hbuf[16];
    std::snprintf(fbuf, sizeof fbuf, "%02d/%02d/%04d", tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900);
    std::snprintf(hbuf, sizeof hbuf, "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    o << nextId << ';' << fbuf << ';' << hbuf << ';' << to_csv(r) << '\n';
    return nextId;
}

bool clear() {
    return std::remove(CSV_FILE) == 0;
}

} // namespace History
