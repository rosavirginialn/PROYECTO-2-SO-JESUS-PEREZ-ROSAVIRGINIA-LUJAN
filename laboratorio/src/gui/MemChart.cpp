// ============================================================
// Proyecto 2 - Laboratorio de concurrencia y memoria
// MemChart.cpp - Implementación de la gráfica en vivo
// Integrantes: Jesús Pérez - Rosa Virginia Luján
// ============================================================
#include "../../include/gui/MemChart.hpp"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace {

// Paleta acorde al resto de la interfaz (tema oscuro)
const QColor BG_PANEL   = QColor("#21262f");
const QColor GRID       = QColor("#2b323d");
const QColor TXT_MUTED  = QColor("#98a2b3");
const QColor RSS_BLUE   = QColor("#4f8cff");
const QColor VIRT_GRAY  = QColor("#9aa3b0");
const QColor LIMIT_RED  = QColor("#e25656");

constexpr int MARGIN_L = 52;   // espacio para etiquetas del eje Y
constexpr int MARGIN_R = 14;
constexpr int MARGIN_T = 26;   // espacio para la leyenda
constexpr int MARGIN_B = 22;   // espacio para etiquetas del eje X

} // namespace

// ------------------------------------------------------------
MemChart::MemChart(QWidget* parent)
    : QWidget(parent)
{
    // Altura compacta: las tarjetas de métricas son las protagonistas
    setMinimumHeight(190);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

// ------------------------------------------------------------
void MemChart::setSamples(const std::vector<Sample>& samples) {
    samples_ = samples;     // copia local (la serie puede seguir creciendo)
    update();               // agenda un repintado (paintEvent)
}

// ------------------------------------------------------------
void MemChart::setLimitKB(long kb) {
    limitKB_ = kb;
    update();
}

// ------------------------------------------------------------
void MemChart::reset() {
    samples_.clear();
    limitKB_ = 0;
    update();
}

// ------------------------------------------------------------
void MemChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Fondo del panel
    p.fillRect(rect(), BG_PANEL);

    const int w = width(), h = height();
    // Área útil de dibujo (dentro de los márgenes)
    const QRectF plot(MARGIN_L, MARGIN_T,
                      w - MARGIN_L - MARGIN_R, h - MARGIN_T - MARGIN_B);

    // ---- Escala Y: el máximo entre virtual, límite y un mínimo razonable ----
    double yMaxKB = 64.0 * 1024.0;              // mínimo visible: 64 MB
    for (const Sample& s : samples_) {
        yMaxKB = std::max<double>(yMaxKB, s.vsizeKB);
        yMaxKB = std::max<double>(yMaxKB, s.rssKB);
    }
    if (limitKB_ > 0) yMaxKB = std::max<double>(yMaxKB, limitKB_ * 1.05);
    yMaxKB *= 1.12;                              // aire arriba de la curva

    // ---- Escala X: tiempo real transcurrido (mínimo 5 s para que no salte) ----
    double tMax = 5.0;
    if (!samples_.empty()) tMax = std::max(tMax, samples_.back().t);

    // Conversión de un punto de datos a coordenadas de píxel
    auto toPix = [&](double t, double kb) -> QPointF {
        return QPointF(plot.left() + (t / tMax) * plot.width(),
                       plot.bottom() - (kb / yMaxKB) * plot.height());
    };

    // ---- Rejilla horizontal + etiquetas del eje Y (en MB) ----
    QFont fMon = font();
    fMon.setPointSize(8);
    p.setFont(fMon);
    p.setPen(GRID);
    const int nGrid = 4;
    for (int i = 0; i <= nGrid; ++i) {
        const double frac = double(i) / nGrid;
        const double y = plot.bottom() - frac * plot.height();
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(TXT_MUTED);
        const double mb = (yMaxKB * frac) / 1024.0;
        p.drawText(QRectF(0, y - 6, MARGIN_L - 6, 12),
                   Qt::AlignRight | Qt::AlignVCenter,
                   (mb >= 100 ? QString::number(mb, 'f', 0)
                              : QString::number(mb, 'f', 1)) + " MB");
        p.setPen(GRID);
    }

    // ---- Rejilla vertical + etiquetas del eje X (segundos) ----
    double tStep = 1.0;
    while (tMax / tStep > 8.0) tStep += (tStep < 5.0 ? 1.0 : 5.0);
    for (double t = tStep; t < tMax; t += tStep) {
        const double x = plot.left() + (t / tMax) * plot.width();
        p.setPen(GRID);
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        p.setPen(TXT_MUTED);
        p.drawText(QRectF(x - 20, plot.bottom() + 4, 40, 12),
                   Qt::AlignCenter, QString::number(t, 'f', 0) + " s");
    }

    // ---- Línea roja del límite de memoria (RLIMIT_AS) ----
    if (limitKB_ > 0) {
        const double y = plot.bottom() - (limitKB_ / yMaxKB) * plot.height();
        QPen penLim(LIMIT_RED, 1.5, Qt::DashLine);
        p.setPen(penLim);
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(LIMIT_RED);
        p.drawText(QRectF(plot.left() + 4, y - 14, 220, 12),
                   Qt::AlignLeft, "Límite RLIMIT_AS");
    }

    // ---- Curvas: virtual (discontinua) y RSS (sólida) ----
    if (samples_.size() >= 2) {
        // Memoria virtual: gris discontinua
        QPolygonF polyVirt;
        for (const Sample& s : samples_)
            polyVirt << toPix(s.t, double(s.vsizeKB));
        QPen penV(VIRT_GRAY, 1.8, Qt::DashLine);
        p.setPen(penV);
        p.drawPolyline(polyVirt);

        // RAM residente: azul sólida con relleno suave debajo
        QPolygonF polyRss;
        for (const Sample& s : samples_)
            polyRss << toPix(s.t, double(s.rssKB));
        QPainterPath fillPath(polyRss.first());
        for (int i = 1; i < polyRss.size(); ++i) fillPath.lineTo(polyRss[i]);
        fillPath.lineTo(polyRss.last().x(), plot.bottom());
        fillPath.lineTo(polyRss.first().x(), plot.bottom());
        fillPath.closeSubpath();
        p.fillPath(fillPath, QColor(79, 140, 255, 28));   // azul con alpha

        QPen penR(RSS_BLUE, 2.4);
        p.setPen(penR);
        p.drawPolyline(polyRss);

        // Punto actual (extremo derecho de la curva RSS)
        p.setBrush(RSS_BLUE);
        p.setPen(Qt::NoPen);
        p.drawEllipse(polyRss.last(), 3.5, 3.5);
    } else {
        p.setPen(TXT_MUTED);
        p.drawText(rect(), Qt::AlignCenter,
                   "Inicie una prueba para ver la memoria en vivo");
    }

    // ---- Leyenda arriba a la derecha ----
    p.setPen(RSS_BLUE);
    p.drawText(QRectF(w - 250, 4, 120, 14), Qt::AlignLeft, "— RAM residente");
    p.setPen(VIRT_GRAY);
    p.drawText(QRectF(w - 130, 4, 126, 14), Qt::AlignLeft, "- - Virtual");
}
