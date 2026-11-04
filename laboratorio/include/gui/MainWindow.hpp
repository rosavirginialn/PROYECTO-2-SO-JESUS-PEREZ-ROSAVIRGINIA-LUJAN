// ============================================================
// Proyecto 2 - Laboratorio de concurrencia y memoria
// MainWindow.hpp - Ventana principal de la interfaz gráfica (Qt6)
//
// Es la única interfaz del proyecto. Arquitectura:
//   * El motor (core/ + platform/) no cambia: Runner hace fork(),
//     el supervisor muestrea /proc y Limits aplica setrlimit.
//   * La GUI solo CONSULTA el RunState con un QTimer y pinta.
// Secciones:
//   [Presets 1..4] los 4 experimentos del enunciado
//   Panel izquierdo: configuración editable de la prueba
//   Derecha: tarjetas de métricas, gráfica en vivo, progreso
//            e historial con comparación
// ============================================================
#pragma once
#include <QMainWindow>
#include <QTimer>

#include <memory>
#include <vector>

#include "../common/Types.hpp"
#include "../platform/ProcFs.hpp"   // SystemInfo (miembro sys_)

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class MemChart;
struct RunState;

namespace Gui {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);


protected:
    // Al cerrar la ventana: detener la prueba de forma controlada
    void closeEvent(QCloseEvent* event) override;

private:
    // ---------- construcción de la interfaz ----------
    QWidget*  buildLeftPanel();     // presets + configuración + botones
    QWidget*  buildRightPanel();    // tarjetas + gráfica + progreso + historial

    // ---------- acciones del usuario ----------
    void applyPreset(int p);        // presets 1..4 del enunciado
    void startRun();                // Runner::start con la config de los campos
    void togglePause();             // Runner::toggle_pause (SIGSTOP/SIGCONT)
    void stopRun();                 // Runner::request_stop
    void refreshHistoryTable();     // recarga la tabla desde history_
    void compareSelected();         // compara 2 corridas de la tabla
    void clearHistory();            // borra el CSV con confirmación
    void updateWarnings();          // avisos amarillos de seguridad

    // ---------- latido de la interfaz (cada 120 ms) ----------
    void onTick();

    // ---------- estado ----------
    TestConfig cfgFromWidgets() const;   // lee los campos -> TestConfig
    void setConfigWidgetsEnabled(bool on);

    SystemInfo sys_{};
    long capMB_ = 1024;                  // tope seguro de reserva (ProcFs)

    std::shared_ptr<RunState> run_;      // prueba en curso (nullptr si no)
    bool recorded_ = false;              // ¿ya se guardó en el historial?

    std::vector<RunResult> history_;     // historial cargado del CSV

    QTimer tick_;                        // latido de refresco de la GUI

    // ---------- widgets que se actualizan ----------
    // panel izquierdo
    QComboBox*   typeCmb_      = nullptr;
    QSpinBox*    threadsSpn_   = nullptr;
    QComboBox*   threadKindCmb_= nullptr;
    QSpinBox*    memSpn_       = nullptr;
    QSpinBox*    touchSpn_     = nullptr;
    QSpinBox*    burstSpn_     = nullptr;
    QSpinBox*    sleepSpn_     = nullptr;
    QSpinBox*    durationSpn_  = nullptr;
    QComboBox*   syncCmb_      = nullptr;
    QSpinBox*    itersSpn_     = nullptr;
    QWidget*     touchRow_     = nullptr;   // filas completas (etiqueta+control)
    QWidget*     burstRow_     = nullptr;   // que se ocultan según el tipo
    QWidget*     sleepRow_     = nullptr;
    QWidget*     durationRow_  = nullptr;
    QWidget*     syncRow_      = nullptr;
    QWidget*     itersRow_     = nullptr;
    QSpinBox*    memLimitSpn_  = nullptr;
    QSpinBox*    cpuLimitSpn_  = nullptr;
    QSpinBox*    sampleSpn_    = nullptr;
    QLabel*      warnBig_      = nullptr;   // aviso: reserva grande
    QLabel*      warnTight_    = nullptr;   // aviso: límite muy justo
    QPushButton* startBtn_     = nullptr;
    QPushButton* pauseBtn_     = nullptr;
    QPushButton* stopBtn_      = nullptr;
    QLabel*      msgLbl_       = nullptr;   // mensajes (presets, estado)
    std::vector<QWidget*> cfgWidgets_;      // se desactivan durante la prueba
    // panel derecho
    QLabel* rssVal_ = nullptr;  QLabel* rssSub_  = nullptr;
    QLabel* vsizeVal_ = nullptr; QLabel* vsizeSub_ = nullptr;
    QLabel* swapVal_ = nullptr;  QLabel* swapSub_  = nullptr;
    QLabel* fltVal_ = nullptr;   QLabel* fltSub_   = nullptr;
    QLabel* statusLbl_ = nullptr;            // PID + estado + límites
    QLabel* timeLbl_ = nullptr;              // "12.4 s / 15 s"
    MemChart* chart_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QTableWidget* histTbl_ = nullptr;
    QPushButton* compareBtn_ = nullptr;
};

} // namespace Gui
