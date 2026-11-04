// ============================================================
// Proyecto 2 - Laboratorio de concurrencia y memoria
// History.hpp - Historial de resultados y exportación CSV
//
// Cada corrida se guarda en resultados_lab.csv (en el directorio
// desde donde se ejecuta la app) para poder repetir pruebas con
// distintas configuraciones y compararlas entre sesiones.
// ============================================================
#pragma once
#include <string>
#include <vector>
#include "../common/Types.hpp"

namespace History {

// Ruta del CSV que se está usando
std::string csv_path();

// Carga el historial desde el CSV (si existe) y renumera los ids
std::vector<RunResult> load();

// Agrega una corrida al CSV. Devuelve el id asignado.
int append(const RunResult& r);

// Elimina el archivo de historial
bool clear();

// Convierte una corrida a la línea CSV (sin id ni salto de línea;
// el id lo asigna append/load)
std::string to_csv(const RunResult& r);

// Parsea una línea CSV a RunResult (false si la línea no es válida)
bool from_csv(const std::string& line, RunResult& out);

} // namespace History
