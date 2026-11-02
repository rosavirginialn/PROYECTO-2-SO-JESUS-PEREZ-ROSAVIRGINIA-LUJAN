# Proyecto 2 — Laboratorio de concurrencia y memoria

**Sistemas Operativos — Integrantes:** Jesús Pérez · Rosa Virginia Luján
**Plataforma:** Linux (probado en Debian/Ubuntu con g++ ≥ 9 y CMake ≥ 3.10)

Aplicación con **interfaz TUI** que configura y ejecuta experimentos controlados
de **concurrencia** y **uso de memoria** (virtual, residente, SWAP y fallos de
página), aplicando **límites de memoria y de CPU en Linux** a la carga de prueba
y mostrando cómo responde el sistema operativo en tiempo real.

---

## 1. Compilación y ejecución

Con CMake (vía recomendada):

```bash
cd laboratorio
mkdir -p build && cd build
cmake ..
make
./laboratorio
```

Sin CMake (alternativa con Make):

```bash
cd laboratorio
make            # genera ./laboratorio
./laboratorio   # abre la TUI
```

Verificación rápida de los mecanismos internos (útil antes de una revisión):

```bash
./laboratorio --selftest
```

Ejecución headless de una prueba sin TUI (para scripts o demo rápida):

```bash
./laboratorio --run --threads 4 --mem 200 --duration 8 --mem-limit 800
./laboratorio --run --type counter --threads 4 --iters 5000000 --sync none
./laboratorio --help      # lista completa de opciones
```

Recomendación: usar un terminal de al menos **100×30 caracteres** (el terminal
por defecto de Ubuntu, Konsole, GNOME Terminal y VS Code cumplen).

## 2. Uso de la TUI

| Tecla | Acción |
|---|---|
| `↑` `↓` | moverse entre campos |
| `←` `→`, `+` `-` | cambiar el valor del campo |
| `Enter` | editar un campo escribiendo dígitos / confirmar |
| `i` | **iniciar** la prueba |
| `1` `2` `3` `4` | **presets** de los 4 experimentos requeridos |
| `p` (en ejecución) | pausa / reanudar con `SIGSTOP` / `SIGCONT` |
| `x` (en ejecución) | detener la prueba |
| `h` | historial de pruebas y comparación |
| `c` (en historial) | comparar las 2 filas marcadas (o las 2 últimas del tipo) |
| `ESPACIO` (en historial) | marcar/desmarcar una fila para comparar |
| `l` (en historial) | limpiar historial (pedir confirmación) |
| `?` | ayuda con el glosario de conceptos |
| `q` | salir |

Cada corrida se registra en `resultados_lab.csv` (en el directorio desde donde
se ejecuta) para **repetir pruebas con distintas configuraciones y compararlas**,
incluso entre sesiones.

## 3. Qué hace por dentro (mapa del código)

```
laboratorio/
├── CMakeLists.txt / Makefile      compilación
├── include/
│   ├── common/Types.hpp           TestConfig, Sample, RunResult, enums
│   ├── core/ControlBlock.hpp      bloque de control en memoria compartida
│   │                              (stop/pausa/progreso/límites padre<->hijo)
│   ├── core/Limits.hpp            setrlimit(RLIMIT_AS y RLIMIT_CPU) + verificación
│   ├── core/Load.hpp              la carga de prueba que corre en el HIJO
│   ├── core/Runner.hpp            fork + hilo supervisor + muestreo de /proc
│   ├── core/History.hpp           historial y CSV
│   ├── platform/ProcFs.hpp        lectura de /proc (memoria, fallos, swap, CPU)
│   └── tui/App.hpp                interfaz
└── src/                           (implementación .cpp espejo de los .hpp)
```

Flujo de una prueba:

1. La TUI toma la configuración y `Runner::start()` hace **`fork()`**.
2. En el **hijo** se aplican los límites con `setrlimit()` y se **verifican**
   con `getrlimit()`; luego corre la carga (`Load::run_in_child`).
3. En el **padre** un hilo supervisor muestrea `/proc/<pid>` (memoria virtual
   `VmSize`, residente `VmRSS`, `VmSwap`, fallos `minflt/majflt`, tiempo de CPU,
   cambios de contexto) cada N ms, controla pausa/detención y arma el resultado.
4. Padre e hijo se comunican por un bloque en `mmap(MAP_SHARED)`:
   banderas de stop, progreso de la carga y mensajes de los límites.

### Detalles que el enunciado pide y dónde están

| Requisito | Dónde / cómo |
|---|---|
| TUI para configurar, iniciar, detener y repetir | `src/tui/App.cpp` (pantallas Config / Ejecución / Historial) |
| Cambiar nº de hilos e indicar su tipo | campo “Hilos” y campo “Tipo de hilos”: **nativos del SO** (`std::thread` → `pthread`) o **tareas del runtime C++** (`std::async`) |
| Carga que reserva memoria virtual y accede a sus páginas | `Load.cpp`: `mmap()` anónimo + escritura por página → fallos menores y RSS |
| Límites de memoria y CPU en Linux | `Limits.cpp`: `RLIMIT_AS` (ENOMEM en `mmap` si se supera) y `RLIMIT_CPU` (SIGXCPU al agotar los segundos de procesador; **no** cambia el nº de núcleos). Si `setrlimit` falla, la TUI muestra “NO APLICADO → PRUEBA SIN LÍMITE” |
| Mostrar residente, virtual, SWAP, fallos, duración y resultado | pantalla de ejecución + `RunResult` (+ `VmSwap` y SWAP del sistema en `/proc/meminfo`; si no hay SWAP se indica) |
| Repetir y comparar | historial en memoria + `resultados_lab.csv` + pantalla de comparación con diferencias |
| Carga controlada y no destructiva | tope de reserva = 70 % de la RAM disponible (máx. 2 GiB), detención con `stopRequested` + escalamiento SIGTERM/SIGKILL, pausa SIGSTOP |

## 4. Los 4 experimentos requeridos (presets 1–4)

1. **Concurrencia** — misma carga con 1, 2, 4 y 8 hilos; comparar pasadas
   completadas, CPU total y pico residente.
2. **Límite de CPU** — carga intensiva sin límite vs `RLIMIT_CPU` de 3 s y 1 s;
   observar que el kernel envía **SIGXCPU** y la prueba termina antes.
3. **Presión de memoria** — con `RLIMIT_AS` fijo (p. ej. 800 MB), variar la
   reserva (200/400/600 MB) y observar virtual, residente y fallos; y una
   reserva mayor que el límite → **ERROR DE RESERVA** controlado.
4. **Acceso compartido y sincronización** — contador incrementado por 4 hilos
   **sin** sincronizar (actualizaciones perdidas por condición de carrera) vs
   `std::atomic` y `std::mutex` (valor exacto, con costo en tiempo).

## 5. Notas técnicas

- La carga pide al kernel **no usar páginas enormes** (`MADV_NOHUGEPAGE`) para
  que los fallos de página menores de 4 KiB sean visibles en las mediciones.
- El tiempo de CPU se obtiene de `utime+stime` en `/proc/<pid>/stat` ( ticks
  convertidos con `_SC_CLK_TCK`); los picos de memoria, del muestreo.
- El historial usa `;` como separador para evitar conflictos con la coma
  decimal en configuraciones regionales.

## 6. Limpieza

```bash
make clean          # borra binario, objetos y resultados_lab.csv
```
