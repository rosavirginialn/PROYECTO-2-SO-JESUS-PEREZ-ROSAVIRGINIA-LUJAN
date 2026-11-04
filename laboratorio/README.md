# Proyecto 2 — Laboratorio de concurrencia y memoria

**Sistemas Operativos — Integrantes:** Jesús Pérez · Rosa Virginia Luján
**Plataforma:** Linux (probado en Debian/Ubuntu con g++ ≥ 9 y CMake ≥ 3.10)

Aplicación con **interfaz gráfica (GUI, Qt6)** que configura y ejecuta
experimentos controlados de **concurrencia** y **uso de memoria** (virtual,
residente, SWAP y fallos de página), aplicando **límites de memoria y de CPU
en Linux** a la carga de prueba y mostrando cómo responde el sistema operativo
en tiempo real (el enunciado admite TUI **o** GUI; este proyecto implementa
la **GUI**).

---

## 1. Compilación y ejecución

**Requisito (una sola vez):** Qt6 y las herramientas básicas:

```bash
sudo apt update && sudo apt install -y build-essential pkg-config qt6-base-dev
```

### Opción A: CMake (vía recomendada)

```bash
cd laboratorio
mkdir -p build && cd build
cmake ..
make
./laboratorio            # abre la GUI
```

### Opción B: Make (misma GUI, sin CMake)

```bash
cd laboratorio
make            # genera ./laboratorio (GUI Qt6, flags vía pkg-config)
./laboratorio
```

En ambas vías el ejecutable es el mismo y abre la ventana gráfica.
Si `cmake` o `make` avisan que no encuentran Qt6, instale `qt6-base-dev`
y vuelva a compilar.

### Verificación y modo headless (sin interfaz)

```bash
./laboratorio --selftest          # verifica los mecanismos internos
./laboratorio --run --threads 4 --mem 200 --duration 8 --mem-limit 800
./laboratorio --run --type counter --threads 4 --iters 5000000 --sync none
./laboratorio --help              # lista completa de opciones
```

### Ver la GUI desde WSL (Windows)

- **Windows 11**: WSLg ya incluye soporte gráfico; `./laboratorio` abre la
  ventana directamente.
- **Windows 10**: instale un servidor X (VcXsrv o X410) y exporte
  `export DISPLAY=:0` antes de ejecutar.

## 2. Uso de la GUI

**Panel izquierdo — Configuración** (todos los campos son modificables antes
de iniciar; la prueba usa exactamente esos valores):

| Campo | Significado |
|---|---|
| Tipo de carga | “Carga de memoria y CPU” o “Contador compartido” |
| Hilos de trabajo | 1–64 hilos |
| Tipo de hilos | **nativos del SO** (`std::thread`→pthread) o **runtime C++** (`std::async`) |
| Memoria virtual a reservar | MB a reservar con `mmap()` y tocar por página |
| Páginas / ráfaga / pausa / duración | parámetros de la carga general |
| Sincronización + iteraciones | solo para el contador compartido |
| Límite de memoria (RLIMIT_AS) | 0 = sin límite |
| Límite de CPU (RLIMIT_CPU) | 0 = sin límite |
| Intervalo de muestreo | frecuencia de lectura de `/proc` |

**Botones y presets**: `Iniciar`, `Pausar/Reanudar` (SIGSTOP/SIGCONT),
`Detener`; y los 4 **presets** del enunciado (1 Concurrencia, 2 Límite CPU,
3 Presión de memoria, 4 Sincronización) que dejan la configuración lista con
un mensaje indicando qué variar en cada repetición.

**Panel derecho**: tarjetas con RAM residente (y pico), memoria virtual (y
pico), SWAP y fallos de página (con tasa por segundo); línea de estado con PID,
pausa y verificación de límites (`APLICADO Y VERIFICADO` / `NO APLICADO →
PRUEBA SIN LÍMITE`); gráfica en vivo virtual vs residente con la línea del
límite; barra de progreso; e historial de corridas (Ctrl+clic para seleccionar
2 filas y `Comparar seleccionadas`, o sin selección compara las 2 últimas del
mismo tipo).

Cada corrida se registra en `resultados_lab.csv` (en el directorio desde donde
se ejecuta) para **repetir pruebas con distintas configuraciones y compararlas**,
incluso entre sesiones.

## 3. Qué hace por dentro (mapa del código)

```
laboratorio/
├── CMakeLists.txt / Makefile      compilación (ambas vías compilan la GUI Qt6)
├── include/
│   ├── common/Types.hpp           TestConfig, Sample, RunResult, enums
│   ├── core/ControlBlock.hpp      bloque de control en memoria compartida
│   │                              (stop/pausa/progreso/límites padre<->hijo)
│   ├── core/Limits.hpp            setrlimit(RLIMIT_AS y RLIMIT_CPU) + verificación
│   ├── core/Load.hpp              la carga de prueba que corre en el HIJO
│   ├── core/Runner.hpp            fork + hilo supervisor + muestreo de /proc
│   ├── core/History.hpp           historial y CSV
│   ├── platform/ProcFs.hpp        lectura de /proc (memoria, fallos, swap, CPU)
│   ├── gui/MainWindow.hpp         ventana principal Qt6 (GUI)
│   └── gui/MemChart.hpp           gráfica en vivo (QPainter, sin QtCharts)
└── src/                           (implementación .cpp espejo de los .hpp)
```

**La GUI usa directamente el motor** (`core/` + `platform/`): consulta el
mismo `RunState` con un `QTimer` de 120 ms; solo cambia la forma de pintar la
información respecto a una interfaz de texto.

Flujo de una prueba:

1. La interfaz toma la configuración y `Runner::start()` hace **`fork()`**.
2. En el **hijo** se aplican los límites con `setrlimit()` y se **verifican**
   con `getrlimit()`; luego corre la carga (`Load::run_in_child`).
3. En el **padre** un hilo supervisor muestrea `/proc/<pid>` (memoria virtual
   `VmSize`, residente `VmRSS`, `VmSwap`, fallos `minflt/majflt`, tiempo de CPU,
   cambios de contexto) cada N ms, controla pausa/detención y arma el resultado.
4. Padre e hijo se comunican por un bloque en `mmap(MAP_SHARED)`:
   banderas de stop, progreso de la carga y mensajes de los límites.
5. Al terminar, la corrida se agrega a `resultados_lab.csv` y aparece en la
   tabla de historial de la GUI.

### Detalles que el enunciado pide y dónde están

| Requisito | Dónde / cómo |
|---|---|
| Interfaz TUI o GUI para configurar, iniciar, detener y repetir | GUI en `src/gui/MainWindow.cpp` (el enunciado admite TUI o GUI; elegimos GUI) |
| Cambiar nº de hilos e indicar su tipo | campo “Hilos de trabajo” y “Tipo de hilos”: **nativos del SO** (`std::thread` → `pthread`) o **tareas del runtime C++** (`std::async`) |
| Carga que reserva memoria virtual y accede a sus páginas | `Load.cpp`: `mmap()` anónimo + escritura por página → fallos menores y RSS |
| Límites de memoria y CPU en Linux | `Limits.cpp`: `RLIMIT_AS` (ENOMEM en `mmap` si se supera) y `RLIMIT_CPU` (SIGXCPU al agotar los segundos de procesador; **no** cambia el nº de núcleos). Si `setrlimit` falla, la interfaz muestra “NO APLICADO → PRUEBA SIN LÍMITE” |
| Mostrar residente, virtual, SWAP, fallos, duración y resultado | tarjetas + gráfica en vivo + `RunResult` (+ `VmSwap` y SWAP del sistema en `/proc/meminfo`; si no hay SWAP se indica) |
| Repetir y comparar | historial en memoria + `resultados_lab.csv` + comparación con diferencias (2 corridas lado a lado) |
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
- La GUI no usa `Q_OBJECT` en sus clases (solo conexiones con lambdas), por lo
  que **no necesita moc**; y dibuja la gráfica con `QPainter` (sin QtCharts),
  así que la única dependencia nueva es `qt6-base-dev`.

## 6. Limpieza

```bash
make clean            # con Make: borra binario, objetos y resultados_lab.csv
rm -rf build          # con CMake: borra toda la carpeta de compilación
```
