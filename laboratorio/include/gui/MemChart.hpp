// ============================================================
// Proyecto 2 - Laboratorio de concurrencia y memoria
// MemChart.hpp - Widget de gráfica en vivo (memoria vs tiempo)
//
// Dibuja con QPainter (sin dependencias externas, sin QtCharts):
//   * curva sólida azul   -> RAM residente (VmRSS)
//   * curva discontinua   -> memoria virtual (VmSize)
//   * línea roja punteada -> límite RLIMIT_AS (si se configuró)
// Recibe una copia de las muestras ya sincronizada con el mutex
// del RunState, así que no toca memoria compartida.
// ============================================================
#pragma once
#include <QWidget>
#include <vector>
#include "../../include/common/Types.hpp"

class MemChart : public QWidget {
    Q_OBJECT               // ← AÑADIR ESTA LÍNEA
public:
    explicit MemChart(QWidget* parent = nullptr);

    // Reemplaza la serie completa (copiar ANTES, fuera de paintEvent)
    void setSamples(const std::vector<Sample>& samples);

    // Línea de referencia del límite de memoria (0 = no dibujar)
    void setLimitKB(long kb);

    // Limpia la gráfica (entre pruebas)
    void reset();

protected:
    // Todo el dibujo ocurre aquí; Qt lo invoca tras update()
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<Sample> samples_;   // copia local de las muestras
    long limitKB_ = 0;              // línea roja del límite (kB)
};
