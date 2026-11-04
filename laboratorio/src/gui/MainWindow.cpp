// ============================================================
// Proyecto 2 - Laboratorio de concurrencia y memoria
// MainWindow.cpp - Interfaz gráfica Qt6 (única interfaz del proyecto)
// Integrantes: Jesús Pérez - Rosa Virginia Luján
//
// PATRÓN GENERAL (el motor está separado del "pintado"):
//   1. El usuario edita la configuración en el panel izquierdo
//      (todos los campos son modificables antes de iniciar).
//   2. "Iniciar prueba" llama Runner::start(cfg) -> fork() del hijo
//      que aplica los límites y corre la carga.
//   3. Un QTimer de 120 ms consulta el RunState (muestras de /proc
//      protegidas por su mutex) y actualiza tarjetas, gráfica,
//      progreso y tabla.
//   4. Al terminar, la corrida se agrega a resultados_lab.csv y a la
//      tabla de historial para poder repetir y comparar.
// Nota: NO usamos la macro Q_OBJECT en nuestras clases (solo
// conectamos señales de Qt con lambdas), por eso no hace falta moc.
// ============================================================
#include "../../include/gui/MainWindow.hpp"

#include "../../include/core/Runner.hpp"
#include "../../include/core/History.hpp"
#include "../../include/platform/ProcFs.hpp"
#include "../../include/gui/MemChart.hpp"

#include <QApplication>
#include <QBrush>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <signal.h>
#include <thread>

namespace Gui {

namespace {

// ------------------- Utilidades de formato -------------------
QString mbTxt(long kb) {                    // kB -> "123.4 MB"
    return QString::asprintf("%.1f MB", double(kb) / 1024.0);
}
QString secTxt(double s) {                  // s -> "12.40 s"
    return QString::asprintf("%.2f s", s);
}
QString resultadoCorto(RunOutcome o) {      // versión corta para la tabla
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
QColor resultadoColor(RunOutcome o) {
    switch (o) {
        case RunOutcome::Completed:       return QColor("#3fbf7f");
        case RunOutcome::CpuLimitSigXcpu: return QColor("#e6a23c");
        case RunOutcome::ReserveError:
        case RunOutcome::ThreadError:
        case RunOutcome::Crashed:         return QColor("#e25656");
        default:                          return QColor("#98a2b3");
    }
}
bool esCarga(const TestConfig& c) { return c.type == TestType::MemoryCpuLoad; }

// Fila "etiqueta encima del control" del panel izquierdo
QWidget* campo(const QString& label, QWidget* ctl) {
    auto* box = new QWidget;
    auto* v   = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    auto* l = new QLabel(label);
    l->setObjectName("fieldLabel");
    v->addWidget(l);
    v->addWidget(ctl);
    return box;
}

// Tarjeta de métrica del panel derecho (clave, valor grande, detalle)
QWidget* tarjeta(const QString& titulo, QLabel** valor, QLabel** detalle) {
    auto* card = new QFrame;
    card->setObjectName("card");
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(14, 10, 14, 10);
    v->setSpacing(2);
    auto* k = new QLabel(titulo);
    k->setObjectName("cardKey");
    auto* val = new QLabel("—");
    val->setObjectName("cardValue");
    auto* sub = new QLabel("");
    sub->setObjectName("cardSub");
    v->addWidget(k);
    v->addWidget(val);
    v->addWidget(sub);
    *valor  = val;
    *detalle= sub;
    return card;
}

// Tema oscuro de toda la app (QSS: hojas de estilo de Qt)
const char* kStylesheet =
    "QWidget { background-color:#161a21; color:#e8ecf2; font-size:12px; }"
    "QLabel#fieldLabel { color:#98a2b3; font-size:11px; }"
    "QLabel#panelTitle { color:#98a2b3; font-size:11px; font-weight:bold;"
    "  letter-spacing:1px; }"
    "QFrame#card, QFrame#panel { background-color:#21262f;"
    "  border:1px solid #333b48; border-radius:10px; }"
    "QLabel#cardKey { color:#98a2b3; font-size:11px; }"
    "QLabel#cardValue { color:#e8ecf2; font-size:23px; font-weight:bold; }"
    "QLabel#cardSub { color:#98a2b3; font-size:11px; }"
    "QLabel#editNote { color:#8fe0b6; background-color:rgba(63,191,127,30);"
    "  border:1px solid rgba(63,191,127,110); border-radius:8px; padding:7px; }"
    "QLabel#warn { color:#e6a23c; font-size:11px; }"
    "QPushButton { background-color:#272d38; border:1px solid #333b48;"
    "  border-radius:7px; padding:6px 12px; color:#e8ecf2; }"
    "QPushButton:hover { background-color:#313848; }"
    "QPushButton:disabled { color:#5b6472; background-color:#1d222b; }"
    "QPushButton#primary { background-color:#4f8cff; color:white;"
    "  font-weight:bold; font-size:14px; padding:10px; }"
    "QPushButton#primary:hover { background-color:#5f97ff; }"
    "QPushButton#danger { color:#ff8a8a; }"
    "QComboBox, QSpinBox { background-color:#272d38; border:1px solid #333b48;"
    "  border-radius:7px; padding:4px 8px; }"
    "QComboBox QAbstractItemView { background-color:#272d38;"
    "  selection-background-color:#4f8cff; }"
    "QProgressBar { background-color:#272d38; border:1px solid #333b48;"
    "  border-radius:6px; height:14px; text-align:center; color:#e8ecf2; }"
    "QProgressBar::chunk { background-color:#4f8cff; border-radius:5px; }"
    "QTableWidget { background-color:#21262f; gridline-color:#2a303b;"
    "  border:1px solid #333b48; border-radius:8px;"
    "  selection-background-color:#33415c; }"
    "QHeaderView::section { background-color:#272d38; color:#98a2b3;"
    "  border:none; padding:5px; font-weight:bold; }"
    "QStatusBar { background-color:#12151b; color:#98a2b3; }"
    "QScrollArea { border:none; }";

} // namespace

// ============================================================
// Construcción
// ============================================================
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Laboratorio de concurrencia y memoria — Proyecto 2 · Sistemas Operativos");
    resize(1280, 800);
    qApp->setStyleSheet(kStylesheet);

    // Información del sistema (barra inferior y topes de los campos)
    sys_   = ProcFs::read_system();
    capMB_ = ProcFs::safe_memory_cap_mb();
    history_ = History::load();

    // Cuerpo: panel izquierdo (config) + columna derecha (métricas)
    auto* central = new QWidget;
    auto* h       = new QHBoxLayout(central);
    h->setContentsMargins(14, 12, 14, 10);
    h->setSpacing(14);

    // El panel izquierdo va en un QScrollArea por si la ventana es baja
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFixedWidth(350);
    scroll->setWidget(buildLeftPanel());
    h->addWidget(scroll);
    h->addWidget(buildRightPanel(), /*stretch*/ 1);

    setCentralWidget(central);

    // Barra de estado: hardware del equipo (lo detecta /proc, no es editable)
    statusBar()->showMessage(QString(
        "RAM disponible: %1 de %2  |  Núcleos: %3  |  SWAP: %4  |  "
        "Tope seguro de reserva: %5 MB  |  Hilos nativos del SO (pthreads)")
        .arg(mbTxt(sys_.memAvailableKB), mbTxt(sys_.memTotalKB))
        .arg(sys_.cores)
        .arg(sys_.swapEnabled() ? "habilitado" : "NO disponible")
        .arg(capMB_));

    // Latido de la interfaz: consulta el RunState y refresca los widgets
    connect(&tick_, &QTimer::timeout, this, [this]{ onTick(); });
    tick_.start(120);

    refreshHistoryTable();

    // ---- Verificación automática sin pantalla (uso en pruebas) ----
    // Con la variable de entorno LAB_SMOKE=1 la GUI arranca una prueba
    // corta sola y se cierra; sirve para validar el flujo completo en
    // modo QT_QPA_PLATFORM=offscreen (sin monitor).
    if (qEnvironmentVariableIsSet("LAB_SMOKE")) {
        // Captura de la ventana para verificación visual (LAB_SHOT=ruta):
        // una a mitad de la prueba y otra al final con el resultado.
        QTimer::singleShot(3200, this, [this] {
            const QString shot = qEnvironmentVariable("LAB_SHOT");
            if (!shot.isEmpty()) grab().save(shot);
        });
        QTimer::singleShot(600, this, [this] {
            threadsSpn_->setValue(2);
            memSpn_->setValue(16);
            burstSpn_->setValue(10);
            sleepSpn_->setValue(20);
            durationSpn_->setValue(2);
            sampleSpn_->setValue(200);
            startRun();
        });
    }
}

// ------------------------------------------------------------
QWidget* MainWindow::buildLeftPanel() {
    auto* panel = new QFrame;
    panel->setObjectName("panel");
    auto* v = new QVBoxLayout(panel);
    v->setContentsMargins(14, 12, 14, 12);
    v->setSpacing(7);

    // ---------- presets: los 4 experimentos del enunciado ----------
    auto* t1 = new QLabel("PRESETS · EXPERIMENTOS DEL ENUNCIADO");
    t1->setObjectName("panelTitle");
    v->addWidget(t1);

    auto* grid = new QGridLayout;
    grid->setSpacing(6);
    const char* presetNames[4] = {
        "1 · Concurrencia (hilos)", "2 · Límite de CPU",
        "3 · Presión de memoria",   "4 · Sincronización"
    };
    const char* presetTips[4] = {
        "Ejecute con 1 hilo y repita con 2, 4 y 8 (solo cambie HILOS).",
        "Ejecute SIN límite y repita con límite CPU = 3 s y 1 s.",
        "Con MEM 800 MB de límite, repita con 200/400/600 MB.",
        "Ejecute con NINGUNA sincronización y repita con atomic y mutex."
    };
    for (int i = 0; i < 4; ++i) {
        auto* b = new QPushButton(presetNames[i]);
        b->setToolTip(presetTips[i]);
        connect(b, &QPushButton::clicked, this, [this, i]{ applyPreset(i + 1); });
        grid->addWidget(b, i / 2, i % 2);
        cfgWidgets_.push_back(b);
    }
    v->addLayout(grid);

    // ---------- título + nota de campos modificables ----------
    auto* t2 = new QLabel("CONFIGURACIÓN DE LA PRUEBA");
    t2->setObjectName("panelTitle");
    v->addSpacing(6);
    v->addWidget(t2);

    auto* note = new QLabel("✏ Todos los campos son modificables: "
                            "ajústelos antes de iniciar; la prueba usará "
                            "exactamente estos valores.");
    note->setObjectName("editNote");
    note->setWordWrap(true);
    v->addWidget(note);

    // ---------- campos de configuración (todos editables con clic) ----------
    typeCmb_ = new QComboBox;
    typeCmb_->addItem("Carga de memoria y CPU");     // TestType::MemoryCpuLoad
    typeCmb_->addItem("Contador compartido");        // TestType::SharedCounter
    // Las filas dependientes del tipo se ocultan COMPLETAS (etiqueta + control)
    connect(typeCmb_, &QComboBox::currentIndexChanged, this,
            [this](int idx){
                const bool carga = (idx == 0);
                touchRow_->setVisible(carga);   burstRow_->setVisible(carga);
                sleepRow_->setVisible(carga);   durationRow_->setVisible(carga);
                syncRow_->setVisible(!carga);   itersRow_->setVisible(!carga);
                updateWarnings();
            });
    v->addWidget(campo("Tipo de carga", typeCmb_));
    cfgWidgets_.push_back(typeCmb_);

    threadsSpn_ = new QSpinBox;
    threadsSpn_->setRange(1, 64);
    threadsSpn_->setValue(4);
    connect(threadsSpn_, &QSpinBox::valueChanged, this, [this](int){ updateWarnings(); });
    v->addWidget(campo("Hilos de trabajo", threadsSpn_));
    cfgWidgets_.push_back(threadsSpn_);

    threadKindCmb_ = new QComboBox;
    threadKindCmb_->addItem("Nativos del SO (std::thread)");   // NativeOs
    threadKindCmb_->addItem("Runtime C++ (std::async)");       // RuntimeManaged
    v->addWidget(campo("Tipo de hilos", threadKindCmb_));
    cfgWidgets_.push_back(threadKindCmb_);

    memSpn_ = new QSpinBox;
    memSpn_->setRange(8, int(capMB_));
    memSpn_->setSingleStep(16);
    memSpn_->setValue(64);
    memSpn_->setSuffix(" MB");
    connect(memSpn_, &QSpinBox::valueChanged, this, [this](int){ updateWarnings(); });
    v->addWidget(campo("Memoria virtual a reservar", memSpn_));
    cfgWidgets_.push_back(memSpn_);

    touchSpn_ = new QSpinBox;
    touchSpn_->setRange(10, 100);
    touchSpn_->setSingleStep(10);
    touchSpn_->setValue(100);
    touchSpn_->setSuffix(" %");
    touchRow_ = campo("Páginas a tocar por pasada", touchSpn_);
    v->addWidget(touchRow_);
    cfgWidgets_.push_back(touchSpn_);

    burstSpn_ = new QSpinBox;
    burstSpn_->setRange(0, 500);
    burstSpn_->setSingleStep(10);
    burstSpn_->setValue(20);
    burstSpn_->setSuffix(" ms");
    burstRow_ = campo("Ráfaga de CPU por pasada", burstSpn_);
    v->addWidget(burstRow_);
    cfgWidgets_.push_back(burstSpn_);

    sleepSpn_ = new QSpinBox;
    sleepSpn_->setRange(0, 1000);
    sleepSpn_->setSingleStep(10);
    sleepSpn_->setValue(80);
    sleepSpn_->setSuffix(" ms");
    sleepRow_ = campo("Pausa entre pasadas", sleepSpn_);
    v->addWidget(sleepRow_);
    cfgWidgets_.push_back(sleepSpn_);

    durationSpn_ = new QSpinBox;
    durationSpn_->setRange(5, 120);
    durationSpn_->setSingleStep(5);
    durationSpn_->setValue(15);
    durationSpn_->setSuffix(" s");
    durationRow_ = campo("Duración de la prueba", durationSpn_);
    v->addWidget(durationRow_);
    cfgWidgets_.push_back(durationSpn_);

    syncCmb_ = new QComboBox;
    syncCmb_->addItem("NINGUNA (sin sincronizar)");   // SyncMode::None
    syncCmb_->addItem("std::atomic");                 // SyncMode::Atomic
    syncCmb_->addItem("std::mutex");                  // SyncMode::Mutex
    syncCmb_->setCurrentIndex(1);
    syncRow_ = campo("Sincronización del contador", syncCmb_);
    syncRow_->setVisible(false);
    v->addWidget(syncRow_);
    cfgWidgets_.push_back(syncCmb_);

    itersSpn_ = new QSpinBox;
    itersSpn_->setRange(1000, 100000000);
    itersSpn_->setSingleStep(500000);
    itersSpn_->setValue(5000000);
    itersSpn_->setGroupSeparatorShown(true);
    itersRow_ = campo("Incrementos por hilo (contador)", itersSpn_);
    itersRow_->setVisible(false);
    v->addWidget(itersRow_);
    cfgWidgets_.push_back(itersSpn_);

    memLimitSpn_ = new QSpinBox;
    memLimitSpn_->setRange(0, int(capMB_));
    memLimitSpn_->setSingleStep(32);
    memLimitSpn_->setSpecialValueText("sin límite");   // 0 = RLIMIT_AS off
    connect(memLimitSpn_, &QSpinBox::valueChanged, this, [this](int){ updateWarnings(); });
    v->addWidget(campo("Límite de memoria (RLIMIT_AS)", memLimitSpn_));
    cfgWidgets_.push_back(memLimitSpn_);

    cpuLimitSpn_ = new QSpinBox;
    cpuLimitSpn_->setRange(0, 120);
    cpuLimitSpn_->setSpecialValueText("sin límite");   // 0 = RLIMIT_CPU off
    v->addWidget(campo("Límite de CPU (RLIMIT_CPU)", cpuLimitSpn_));
    cfgWidgets_.push_back(cpuLimitSpn_);

    sampleSpn_ = new QSpinBox;
    sampleSpn_->setRange(100, 2000);
    sampleSpn_->setSingleStep(50);
    sampleSpn_->setValue(500);
    sampleSpn_->setSuffix(" ms");
    v->addWidget(campo("Intervalo de muestreo de /proc", sampleSpn_));
    cfgWidgets_.push_back(sampleSpn_);

    // ---------- avisos de seguridad (amarillos) ----------
    warnBig_   = new QLabel; warnBig_->setObjectName("warn");
    warnBig_->setWordWrap(true); warnBig_->hide();
    warnTight_ = new QLabel; warnTight_->setObjectName("warn");
    warnTight_->setWordWrap(true); warnTight_->hide();
    v->addWidget(warnBig_);
    v->addWidget(warnTight_);

    // ---------- botones de control ----------
    startBtn_ = new QPushButton("▶  Iniciar prueba");
    startBtn_->setObjectName("primary");
    connect(startBtn_, &QPushButton::clicked, this, [this]{ startRun(); });
    v->addWidget(startBtn_);

    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    pauseBtn_ = new QPushButton("⏸  Pausar");
    pauseBtn_->setEnabled(false);
    connect(pauseBtn_, &QPushButton::clicked, this, [this]{ togglePause(); });
    stopBtn_ = new QPushButton("⏹  Detener");
    stopBtn_->setObjectName("danger");
    stopBtn_->setEnabled(false);
    connect(stopBtn_, &QPushButton::clicked, this, [this]{ stopRun(); });
    row->addWidget(pauseBtn_);
    row->addWidget(stopBtn_);
    v->addLayout(row);

    // ---------- mensajes ----------
    msgLbl_ = new QLabel("Bienvenido. Configure la prueba y presione "
                         "«Iniciar» (o un preset 1–4).");
    msgLbl_->setWordWrap(true);
    msgLbl_->setStyleSheet("color:#3fbf7f;");
    v->addWidget(msgLbl_);
    v->addStretch(1);

    return panel;
}

// ------------------------------------------------------------
QWidget* MainWindow::buildRightPanel() {
    auto* col = new QWidget;
    auto* v   = new QVBoxLayout(col);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);

    // ---------- fila de 4 tarjetas (las protagonistas) ----------
    auto* cards = new QHBoxLayout;
    cards->setSpacing(10);
    cards->addWidget(tarjeta("RAM RESIDENTE",    &rssVal_,   &rssSub_));
    cards->addWidget(tarjeta("MEMORIA VIRTUAL",  &vsizeVal_, &vsizeSub_));
    cards->addWidget(tarjeta("SWAP DEL PROCESO", &swapVal_,  &swapSub_));
    cards->addWidget(tarjeta("FALLOS DE PÁGINA", &fltVal_,   &fltSub_));
    v->addLayout(cards);

    // ---------- estado de la prueba y de los límites ----------
    statusLbl_ = new QLabel("Sin prueba en ejecución.");
    statusLbl_->setWordWrap(true);
    v->addWidget(statusLbl_);

    // ---------- gráfica en vivo (compacta) ----------
    chart_ = new MemChart;
    chart_->setFixedHeight(200);
    v->addWidget(chart_);

    // ---------- progreso ----------
    auto* prow = new QHBoxLayout;
    progress_ = new QProgressBar;
    progress_->setRange(0, 1000);              // milésimas para movimiento suave
    prow->addWidget(progress_, 1);
    timeLbl_ = new QLabel("0.0 s");
    timeLbl_->setStyleSheet("color:#98a2b3;");
    prow->addWidget(timeLbl_);
    v->addLayout(prow);

    // ---------- historial ----------
    auto* histPanel = new QFrame;
    histPanel->setObjectName("panel");
    auto* hv = new QVBoxLayout(histPanel);
    hv->setContentsMargins(12, 10, 12, 10);
    hv->setSpacing(6);

    auto* hrow = new QHBoxLayout;
    auto* ht = new QLabel("HISTORIAL DE CORRIDAS");
    ht->setObjectName("panelTitle");
    hrow->addWidget(ht);
    hrow->addStretch(1);
    compareBtn_ = new QPushButton("⇄ Comparar seleccionadas");
    compareBtn_->setToolTip("Seleccione exactamente 2 filas (Ctrl+clic) y compare. "
                            "Si no hay selección se comparan las 2 últimas "
                            "del mismo tipo.");
    connect(compareBtn_, &QPushButton::clicked, this, [this]{ compareSelected(); });
    auto* clearBtn = new QPushButton("Limpiar");
    connect(clearBtn, &QPushButton::clicked, this, [this]{ clearHistory(); });
    hrow->addWidget(compareBtn_);
    hrow->addWidget(clearBtn);
    hv->addLayout(hrow);

    histTbl_ = new QTableWidget;
    histTbl_->setColumnCount(13);
    histTbl_->setHorizontalHeaderLabels({
        "#", "Tipo", "Hilos", "T. hilos", "Mem", "Lím mem", "Lím CPU",
        "Dur (s)", "CPU (s)", "VM pico", "RSS pico", "SWAP", "Resultado"});
    histTbl_->verticalHeader()->setVisible(false);
    histTbl_->setSelectionBehavior(QAbstractItemView::SelectRows);
    histTbl_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    histTbl_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    histTbl_->setAlternatingRowColors(true);
    histTbl_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    histTbl_->horizontalHeader()->setStretchLastSection(true);
    hv->addWidget(histTbl_);
    v->addWidget(histPanel, /*stretch*/ 1);

    return col;
}

// ============================================================
// Configuración y presets
// ============================================================
void MainWindow::applyPreset(int p) {
    // Valores recomendados para los 4 experimentos del enunciado
    memLimitSpn_->setValue(0);
    cpuLimitSpn_->setValue(0);
    typeCmb_->setCurrentIndex(0);                  // carga Mem+CPU por defecto
    threadKindCmb_->setCurrentIndex(0);            // nativos
    QString msg;
    switch (p) {
        case 1:  // Concurrencia: variar hilos
            threadsSpn_->setValue(1);
            memSpn_->setValue(64);   touchSpn_->setValue(100);
            burstSpn_->setValue(50); sleepSpn_->setValue(50);
            durationSpn_->setValue(10);
            msg = "PRESET 1 Concurrencia: ejecute con 1 hilo y repita con "
                  "2, 4 y 8 (solo cambie HILOS).";
            break;
        case 2:  // Límite de CPU
            threadsSpn_->setValue(2);
            memSpn_->setValue(64);   touchSpn_->setValue(100);
            burstSpn_->setValue(200); sleepSpn_->setValue(0);
            durationSpn_->setValue(30);
            msg = "PRESET 2 Límite CPU: ejecute SIN límite y repita con "
                  "límite CPU = 3 s y 1 s.";
            break;
        case 3:  // Presión de memoria bajo límite
            threadsSpn_->setValue(4);
            memSpn_->setValue(int(std::min<long>(200, capMB_)));
            touchSpn_->setValue(100);
            burstSpn_->setValue(10); sleepSpn_->setValue(200);
            durationSpn_->setValue(15);
            memLimitSpn_->setValue(int(std::min<long>(800, capMB_)));
            msg = "PRESET 3 Presión de memoria: repita variando MEMORIA "
                  "(200/400/600 MB) con el mismo límite.";
            break;
        case 4:  // Contador con/sin sincronización
            typeCmb_->setCurrentIndex(1);          // contador compartido
            threadsSpn_->setValue(4);
            syncCmb_->setCurrentIndex(0);          // NINGUNA
            itersSpn_->setValue(5000000);
            msg = "PRESET 4 Sincronización: ejecute con NINGUNA y repita "
                  "con atomic y mutex.";
            break;
    }
    msgLbl_->setStyleSheet("color:#3fbf7f;");
    msgLbl_->setText(msg);
}

// ------------------------------------------------------------
TestConfig MainWindow::cfgFromWidgets() const {
    TestConfig c;
    c.type       = typeCmb_->currentIndex() == 0 ? TestType::MemoryCpuLoad
                                                 : TestType::SharedCounter;
    c.threads    = threadsSpn_->value();
    c.threadKind = threadKindCmb_->currentIndex() == 0 ? ThreadKind::NativeOs
                                                       : ThreadKind::RuntimeManaged;
    c.memoryMB       = memSpn_->value();
    c.touchPercent   = touchSpn_->value();
    c.cpuBurstMs     = burstSpn_->value();
    c.sleepMs        = sleepSpn_->value();
    c.durationSec    = durationSpn_->value();
    c.sync           = SyncMode(syncCmb_->currentIndex());   // None/Atomic/Mutex
    c.counterIters   = itersSpn_->value();
    c.memLimitMB     = memLimitSpn_->value();
    c.cpuLimitSec    = cpuLimitSpn_->value();
    c.sampleMs       = sampleSpn_->value();
    return c;
}

// ------------------------------------------------------------
void MainWindow::updateWarnings() {
    // Avisos de seguridad: reserva grande y límite muy justo
    const int mem = memSpn_->value();
    if (mem > int(capMB_ * 7 / 10)) {
        warnBig_->setText(QString("! AVISO: reserva grande (%1 MB). "
                                  "El tope seguro es %2 MB.").arg(mem).arg(capMB_));
        warnBig_->show();
    } else warnBig_->hide();

    const int lim = memLimitSpn_->value();
    if (lim > 0 && lim < mem + threadsSpn_->value() * 8 + 60) {
        warnTight_->setText(QString("! AVISO: el límite de memoria (%1 MB) puede "
                                    "no dejar espacio para hilos+runtime.").arg(lim));
        warnTight_->show();
    } else warnTight_->hide();
}

// ------------------------------------------------------------
void MainWindow::setConfigWidgetsEnabled(bool on) {
    for (QWidget* w : cfgWidgets_) w->setEnabled(on);
}

// ============================================================
// Ciclo de una prueba
// ============================================================
void MainWindow::startRun() {
    if (run_ && !run_->finished.load()) return;    // ya hay una en curso
    const TestConfig cfg = cfgFromWidgets();
    run_.reset();
    chart_->reset();
    recorded_ = false;
    setConfigWidgetsEnabled(false);                // se reactivan al terminar
    startBtn_->setEnabled(false);
    pauseBtn_->setEnabled(true);  pauseBtn_->setText("⏸  Pausar");
    stopBtn_->setEnabled(true);
    msgLbl_->setStyleSheet("color:#3fbf7f;");
    msgLbl_->setText("Prueba iniciada. Muestreando /proc cada "
                     + QString::number(cfg.sampleMs) + " ms…");
    chart_->setLimitKB(cfg.memLimitMB > 0 ? long(cfg.memLimitMB) * 1024 : 0);
    progress_->setValue(0);
    run_ = Runner::start(cfg);
}

// ------------------------------------------------------------
void MainWindow::togglePause() {
    if (!run_ || run_->finished.load()) return;
    Runner::toggle_pause(run_);                    // SIGSTOP <-> SIGCONT
    // El texto exacto lo fija onTick() según run_->paused
}

// ------------------------------------------------------------
void MainWindow::stopRun() {
    if (!run_ || run_->finished.load()) return;
    Runner::request_stop(run_);                    // stop -> TERM -> KILL
    msgLbl_->setStyleSheet("color:#e6a23c;");
    msgLbl_->setText("Deteniendo la prueba…");
}

// ------------------------------------------------------------
void MainWindow::onTick() {
    if (!run_) return;
    RunState& rs = *run_;

    // ---- copia protegida de las muestras (el supervisor las escribe) ----
    Sample last;
    std::vector<Sample> snap;
    long vsizePeak = 0, rssPeak = 0;
    double fltRate = 0, cpuPct = 0;
    {
        std::lock_guard<std::mutex> lk(rs.mx);
        snap = rs.samples;
        if (!snap.empty()) last = snap.back();
        for (const Sample& s : snap) {
            vsizePeak = std::max(vsizePeak, s.vsizeKB);
            rssPeak   = std::max(rssPeak,   s.rssKB);
        }
        const int n = int(snap.size());
        if (n >= 2) {
            const Sample& a = snap[n - 2];
            const Sample& b = snap[n - 1];
            const double dt = b.t - a.t;
            if (dt > 0.05) {
                fltRate = double(b.minFlt - a.minFlt) / dt;
                cpuPct  = 100.0 * (b.cpuSec - a.cpuSec) / dt
                              / double(std::max(1, sys_.cores));
                cpuPct  = std::min(cpuPct, 100.0 * sys_.cores);
            }
        }
    }
    const bool fin = rs.finished.load();

    // ---- tarjetas (RAM y virtual con pico; swap; fallos con tasa) ----
    rssVal_->setText(mbTxt(last.rssKB));
    rssSub_->setText("▲ pico " + mbTxt(rssPeak));
    vsizeVal_->setText(mbTxt(last.vsizeKB));
    vsizeSub_->setText("pico " + mbTxt(vsizePeak));
    swapVal_->setText(mbTxt(last.swapKB));
    swapSub_->setText(sys_.swapEnabled()
        ? "sistema: " + mbTxt(sys_.swapFreeKB) + " libres"
        : "NO disponible en este equipo");
    fltVal_->setText(QString::number(last.minFlt + last.majFlt));
    fltSub_->setText(QString("menores %1 (%2/s) · mayores %3")
                     .arg(last.minFlt).arg(fltRate, 0, 'f', 0).arg(last.majFlt));

    // ---- línea de estado: PID, pausa y límites ----
    QString st = QString("PID <b>%1</b> · %2 · estado del proceso: <b>%3</b>")
                 .arg(rs.pid)
                 .arg(fin ? "FINALIZADA" : "EN CURSO")
                 .arg(QChar(last.state));
    if (rs.paused.load() && !fin)
        st += " &nbsp;<span style='color:#c39bff;'>■ EN PAUSA (SIGSTOP): "
              "el kernel congeló todos los hilos</span>";
    auto limTxt = [](bool attempted, bool applied,
                     const QString& val, const QString& err) -> QString {
        if (!attempted) return "<span style='color:#98a2b3;'>no configurado</span>";
        if (applied)    return "<span style='color:#3fbf7f;'>" + val +
                               " APLICADO Y VERIFICADO</span>";
        return "<span style='color:#e25656;'>NO APLICADO (" + err +
               ") → PRUEBA SIN LÍMITE</span>";
    };
    st += "<br>Límite MEM: " +
          limTxt(rs.limits.memAttempted, rs.limits.memApplied,
                 mbTxt(rs.limits.memBytes),
                 QString::fromStdString(rs.limits.memError));
    st += " &nbsp;|&nbsp; Límite CPU: " +
          limTxt(rs.limits.cpuAttempted, rs.limits.cpuApplied,
                 QString::number(rs.limits.cpuSeconds) + " s de procesador",
                 QString::fromStdString(rs.limits.cpuError));
    if (!rs.childError.empty())
        st += "<br><span style='color:#e6a23c;'>Mensaje del hijo: " +
              QString::fromStdString(rs.childError) + "</span>";
    statusLbl_->setText(st);

    // ---- gráfica y progreso ----
    chart_->setSamples(snap);
    if (esCarga(rs.cfg)) {
        const double frac = rs.cfg.durationSec > 0
            ? std::min(1.0, last.t / double(rs.cfg.durationSec)) : 0.0;
        progress_->setValue(int(frac * 1000));
        timeLbl_->setText(QString::asprintf("%.1f s / %d s",
                        last.t, rs.cfg.durationSec));
    } else {
        const long total = rs.cfg.counterIters * rs.cfg.threads;
        long done = fin ? rs.result.iterations
                        : (rs.cb ? long(rs.cb->iterations.load()) : 0);
        progress_->setValue(total > 0 ? int(std::min(1.0,
                            double(done) / double(total)) * 1000) : 0);
        timeLbl_->setText(QString("%1 / %2 incrementos")
                          .arg(done).arg(total));
    }
    pauseBtn_->setText(rs.paused.load() ? "▶  Reanudar" : "⏸  Pausar");

    // ---- fin de la prueba: registrar en el historial y resumir ----
    if (fin && !recorded_) {
        recorded_ = true;
        RunResult rr = rs.result;
        rr.id = History::append(rr);               // -> resultados_lab.csv
        history_.push_back(rr);
        refreshHistoryTable();

        setConfigWidgetsEnabled(true);             // vuelve a poder editarse
        startBtn_->setEnabled(true);
        pauseBtn_->setEnabled(false);
        stopBtn_->setEnabled(false);

        // Resumen en texto (complementa el cuadro emergente)
        QString s = QString("RESULTADO: %1\nDuración: %2 | CPU consumida: %3 | "
                            "Hilos: %4 (%5)\nVirtual pico: %6 | Residente pico: %7"
                            " | SWAP: %8 | f.min: %9 | f.may: %10")
            .arg(outcome_str(rr.outcome))
            .arg(secTxt(rr.wallSeconds), secTxt(rr.cpuSeconds))
            .arg(rr.cfg.threads)
            .arg(rr.cfg.threadKind == ThreadKind::NativeOs ? "nativos" : "runtime")
            .arg(mbTxt(rr.vsizePeakKB), mbTxt(rr.rssPeakKB), mbTxt(rr.swapKB))
            .arg(rr.minFlt).arg(rr.majFlt);
        if (!esCarga(rr.cfg))
            s += QString("\nContador esperado: %1 | obtenido: %2 | %3% perdidas")
                 .arg(rr.counterExpected).arg(rr.counterFinal)
                 .arg(QString::asprintf("%.2f", pct_lost(rr.counterExpected,
                                                         rr.counterFinal)));
        else
            s += QString("\nPasadas: %1 | páginas accedidas: %2")
                 .arg(rr.iterations).arg(rr.pagesTouched);
        s += QString("\nLímites: %1\nAgregada al historial como corrida #%2")
             .arg(QString::fromStdString(rr.notas)).arg(rr.id);

        msgLbl_->setStyleSheet(rr.outcome == RunOutcome::Completed
                               ? "color:#3fbf7f;" : "color:#e6a23c;");
        msgLbl_->setText(QString("Resultado: %1 — corrida #%2 en el historial. "
                                 "Modifique la configuración y repita para comparar.")
                         .arg(outcome_str(rr.outcome)).arg(rr.id));
        // Resumen emergente (se omite en el modo de verificación
        // automática LAB_SMOKE para que la prueba no se quede
        // esperando un clic que nunca llega).
        if (!qEnvironmentVariableIsSet("LAB_SMOKE"))
            QMessageBox::information(this, "Resultado de la prueba", s);

        // Modo de verificación sin pantalla: cerrar solito
        if (qEnvironmentVariableIsSet("LAB_SMOKE")) {
            std::fputs("SMOKE_OK resultado=" , stdout);
            std::puts(outcome_str(rr.outcome));
            std::fflush(stdout);
            QTimer::singleShot(700, this, []{ QApplication::quit(); });
        }
        run_.reset();   // la prueba terminó; liberar el estado
    }
}

// ============================================================
// Historial y comparación
// ============================================================
void MainWindow::refreshHistoryTable() {
    histTbl_->setRowCount(int(history_.size()));
    for (int i = 0; i < int(history_.size()); ++i) {
        const RunResult& r = history_[i];
        auto put = [this, i](int col, const QString& txt, QColor color = {}) {
            auto* it = new QTableWidgetItem(txt);
            if (color.isValid()) it->setForeground(QBrush(color));
            it->setTextAlignment(Qt::AlignCenter);
            histTbl_->setItem(i, col, it);
        };
        put(0,  QString::number(r.id));
        put(1,  esCarga(r.cfg) ? "Carga" : "Contador");
        put(2,  QString::number(r.cfg.threads));
        put(3,  r.cfg.threadKind == ThreadKind::NativeOs ? "Nat" : "Rtm");
        put(4,  QString::number(r.cfg.memoryMB) + "M");
        put(5,  r.cfg.memLimitMB  ? QString::number(r.cfg.memLimitMB) + "M" : "—");
        put(6,  r.cfg.cpuLimitSec ? QString::number(r.cfg.cpuLimitSec) + "s" : "—");
        put(7,  QString::number(r.wallSeconds, 'f', 2));
        put(8,  QString::number(r.cpuSeconds,  'f', 2));
        put(9,  QString::number(r.vsizePeakKB / 1024.0, 'f', 1) + "M");
        put(10, QString::number(r.rssPeakKB  / 1024.0, 'f', 1) + "M");
        put(11, QString::number(r.swapKB));
        put(12, resultadoCorto(r.outcome), resultadoColor(r.outcome));
    }
}

// ------------------------------------------------------------
void MainWindow::compareSelected() {
    if (history_.size() < 2) {
        QMessageBox::information(this, "Comparar",
            "Necesita al menos 2 corridas en el historial.");
        return;
    }
    int a = -1, b = -1;
    const QList<QTableWidgetItem*> sel = histTbl_->selectedItems();
    // filas únicas seleccionadas
    QList<int> rows;
    for (QTableWidgetItem* it : sel) {
        const int r = it->row();
        if (!rows.contains(r)) rows.push_back(r);
    }
    if (rows.size() == 2) {
        a = rows[0]; b = rows[1];
    } else {
        // Sin selección exacta: las 2 últimas del mismo tipo
        const int last = int(history_.size()) - 1;
        const TestType ref = history_[last].cfg.type;
        for (int i = last; i >= 0 && a < 0; --i)
            if (history_[i].cfg.type == ref) a = i;
        for (int i = a - 1; i >= 0 && b < 0; --i)
            if (history_[i].cfg.type == ref) b = i;
        if (b < 0) {
            QMessageBox::information(this, "Comparar",
                "Seleccione exactamente 2 filas con Ctrl+clic para compararlas.");
            return;
        }
    }
    if (a > b) std::swap(a, b);
    const RunResult& A = history_[a];
    const RunResult& B = history_[b];

    // Tabla HTML de comparación, campo por campo con diferencia
    QString h = QString("<table cellpadding='4' cellspacing='0' width='100%'>"
        "<tr><th align='left'>Campo</th><th>Prueba #%1</th><th>Prueba #%2</th>"
        "<th>Diferencia</th></tr>").arg(A.id).arg(B.id);
    auto row = [&](const QString& c, const QString& x, const QString& y,
                   const QString& d = "", bool hi = false) {
        h += QString("<tr><td>%1</td><td align='center'>%2</td>"
                     "<td align='center'>%3</td><td align='center'>%4</td></tr>")
             .arg(c, x, y,
                  hi ? QString("<b style='color:#e6a23c;'>%1</b>").arg(d) : d);
    };
    row("Tipo de prueba", testtype_str(A.cfg.type), testtype_str(B.cfg.type));
    row("Hilos", QString::number(A.cfg.threads), QString::number(B.cfg.threads),
        QString::number(B.cfg.threads - A.cfg.threads), true);
    row("Tipo de hilos", threadkind_str(A.cfg.threadKind),
        threadkind_str(B.cfg.threadKind));
    row("Memoria (MB)", QString::number(A.cfg.memoryMB),
        QString::number(B.cfg.memoryMB),
        QString::number(B.cfg.memoryMB - A.cfg.memoryMB), true);
    row("Límite memoria (MB)", QString::number(A.cfg.memLimitMB),
        QString::number(B.cfg.memLimitMB));
    row("Límite CPU (s)", QString::number(A.cfg.cpuLimitSec),
        QString::number(B.cfg.cpuLimitSec));
    if (!esCarga(A.cfg))
        row("Sincronización", syncmode_str(A.cfg.sync), syncmode_str(B.cfg.sync));
    row("Resultado", outcome_str(A.outcome), outcome_str(B.outcome), "",
        A.outcome != B.outcome);
    row("Duración (s)", secTxt(A.wallSeconds), secTxt(B.wallSeconds),
        secTxt(B.wallSeconds - A.wallSeconds), true);
    row("CPU consumida (s)", secTxt(A.cpuSeconds), secTxt(B.cpuSeconds),
        secTxt(B.cpuSeconds - A.cpuSeconds), true);
    row("Memoria virtual pico", mbTxt(A.vsizePeakKB), mbTxt(B.vsizePeakKB),
        mbTxt(B.vsizePeakKB - A.vsizePeakKB), true);
    row("RAM residente pico", mbTxt(A.rssPeakKB), mbTxt(B.rssPeakKB),
        mbTxt(B.rssPeakKB - A.rssPeakKB), true);
    row("SWAP (KB)", QString::number(A.swapKB), QString::number(B.swapKB),
        QString::number(B.swapKB - A.swapKB), true);
    row("Fallos menores", QString::number(A.minFlt), QString::number(B.minFlt),
        QString::number(B.minFlt - A.minFlt), true);
    row("Fallos mayores", QString::number(A.majFlt), QString::number(B.majFlt),
        QString::number(B.majFlt - A.majFlt), true);
    row("ctxt vol / no vol",
        QString("%1/%2").arg(A.volCtxt).arg(A.nvolCtxt),
        QString("%1/%2").arg(B.volCtxt).arg(B.nvolCtxt));
    if (!esCarga(A.cfg)) {
        row("Contador esperado", QString::number(A.counterExpected),
            QString::number(B.counterExpected));
        row("Contador obtenido", QString::number(A.counterFinal),
            QString::number(B.counterFinal));
        row("Fiabilidad",
            QString::asprintf("%.2f%% perdidas",
                pct_lost(A.counterExpected, A.counterFinal)),
            QString::asprintf("%.2f%% perdidas",
                pct_lost(B.counterExpected, B.counterFinal)), "",
            A.counterFinal != B.counterFinal);
    } else {
        row("Pasadas", QString::number(A.iterations),
            QString::number(B.iterations));
        row("Páginas accedidas", QString::number(A.pagesTouched),
            QString::number(B.pagesTouched));
    }
    h += "</table><p style='color:#98a2b3;'>"
         "Notas A: " + QString::fromStdString(A.notas) +
         "<br>Notas B: " + QString::fromStdString(B.notas) + "</p>";

    // Diálogo de comparación con desplazamiento
    QDialog dlg(this);
    dlg.setWindowTitle(QString("Comparación: prueba #%1 vs prueba #%2")
                       .arg(A.id).arg(B.id));
    dlg.resize(720, 620);
    auto* dv = new QVBoxLayout(&dlg);
    auto* lbl = new QLabel(h);
    lbl->setTextFormat(Qt::RichText);
    lbl->setAlignment(Qt::AlignTop);
    lbl->setWordWrap(true);
    auto* sc = new QScrollArea;
    sc->setWidget(lbl);
    sc->setWidgetResizable(true);
    dv->addWidget(sc);
    dlg.exec();
}

// ------------------------------------------------------------
void MainWindow::clearHistory() {
    const auto r = QMessageBox::question(
        this, "Limpiar historial",
        "Se borrará resultados_lab.csv (todas las corridas). ¿Continuar?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (r != QMessageBox::Yes) return;
    History::clear();
    history_.clear();
    refreshHistoryTable();
    msgLbl_->setStyleSheet("color:#3fbf7f;");
    msgLbl_->setText("Historial borrado.");
}

// ============================================================
// Cierre ordenado
// ============================================================
void MainWindow::closeEvent(QCloseEvent* event) {
    // Cierre ordenado: si hay una prueba viva, reanudarla (si estaba
    // en pausa) y detenerla de forma controlada antes de salir.
    if (run_ && !run_->finished.load()) {
        if (run_->paused.load()) ::kill(run_->pid, SIGCONT);
        Runner::request_stop(run_);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    event->accept();
}

} // namespace Gui
