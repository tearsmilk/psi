#include "ui/canvas.h"
#include "ui/theme.h"
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <cmath>

namespace psi {

CanvasWidget::CanvasWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 150);
}

Point CanvasWidget::toWorld(QPointF s) const { return {(s.x() - origin_.x()) / scale_, (origin_.y() - s.y()) / scale_}; }
QPointF CanvasWidget::toScreen(Point w) const { return {origin_.x() + w.x * scale_, origin_.y() - w.y * scale_}; }

void CanvasWidget::zoomTo(const Rect& r, double marginPx) {
    if (!r.valid() || width() <= 0) return;
    double w = std::max(r.x1 - r.x0, 1.0), h = std::max(r.y1 - r.y0, 1.0);
    scale_ = std::min((width() - 2 * marginPx) / w, (height() - 2 * marginPx) / h);
    scale_ = std::clamp(scale_, 1e-4, 1e4);
    Point c{(r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2};
    origin_ = QPointF(width() / 2.0 - c.x * scale_, height() / 2.0 + c.y * scale_);
    update(); Q_EMIT viewChanged();
}

void CanvasWidget::zoomBy(double f, QPointF anchor) {
    double ns = std::clamp(scale_ * f, 1e-4, 1e4); f = ns / scale_;
    origin_ = anchor + (origin_ - anchor) * f; scale_ = ns;
    update(); Q_EMIT viewChanged();
}

double CanvasWidget::gridStep() const {
    // целевой шаг ≈ 40 px: 1, 2, 5 × 10^n мм
    double target = 40.0 / scale_, p = std::pow(10.0, std::floor(std::log10(target)));
    for (double m : {1.0, 2.0, 5.0, 10.0}) if (m * p >= target) return m * p;
    return 10 * p;
}

Point CanvasWidget::snapToGrid(Point p) const { double g = gridStep(); return {std::round(p.x / g) * g, std::round(p.y / g) * g}; }

void CanvasWidget::drawGrid(QPainter& p) {
    const Palette& pal = Theme::current();
    p.fillRect(rect(), pal.canvas);
    if (!grid_) return;
    double g = gridStep();
    Point a = toWorld(QPointF(0, height())), b = toWorld(QPointF(width(), 0));
    int i0 = int(std::floor(a.x / g)), i1 = int(std::ceil(b.x / g)), j0 = int(std::floor(a.y / g)), j1 = int(std::ceil(b.y / g));
    if ((i1 - i0) * (j1 - j0) > 40000) return;
    for (int i = i0; i <= i1; ++i) { p.setPen(QPen(i % 5 == 0 ? pal.gridMajor : pal.grid, 1)); double x = toScreen({i * g, 0}).x(); p.drawLine(QPointF(x, 0), QPointF(x, height())); }
    for (int j = j0; j <= j1; ++j) { p.setPen(QPen(j % 5 == 0 ? pal.gridMajor : pal.grid, 1)); double y = toScreen({0, j * g}).y(); p.drawLine(QPointF(0, y), QPointF(width(), y)); }
    p.setPen(QPen(pal.crosshair, 1)); QPointF o = toScreen({0, 0});
    p.drawLine(QPointF(o.x() - 12, o.y()), QPointF(o.x() + 12, o.y())); p.drawLine(QPointF(o.x(), o.y() - 12), QPointF(o.x(), o.y() + 12));
}

void CanvasWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    drawGrid(p);
    paintContent(p);
}

void CanvasWidget::mousePressEvent(QMouseEvent* ev) {
    setFocus();
    if (ev->button() == Qt::MiddleButton || (ev->button() == Qt::LeftButton && spaceHeld_)) { panning_ = true; panLast_ = ev->position(); return; }
    canvasPress(ev);
}
void CanvasWidget::mouseMoveEvent(QMouseEvent* ev) {
    Q_EMIT cursorMoved(toWorld(ev->position()));
    if (panning_) { origin_ += ev->position() - panLast_; panLast_ = ev->position(); update(); Q_EMIT viewChanged(); return; }
    canvasMove(ev);
}
void CanvasWidget::mouseReleaseEvent(QMouseEvent* ev) {
    if (panning_ && (ev->button() == Qt::MiddleButton || ev->button() == Qt::LeftButton)) { panning_ = false; return; }
    canvasRelease(ev);
}
void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* ev) { canvasDoubleClick(ev); }
void CanvasWidget::wheelEvent(QWheelEvent* ev) {
    double steps = ev->angleDelta().y() / 120.0;
    if (steps == 0) steps = ev->pixelDelta().y() / 60.0;
    zoomBy(std::pow(1.15, steps), ev->position());
}
void CanvasWidget::keyPressEvent(QKeyEvent* ev) { if (ev->key() == Qt::Key_Space) spaceHeld_ = true; else QWidget::keyPressEvent(ev); }
void CanvasWidget::keyReleaseEvent(QKeyEvent* ev) { if (ev->key() == Qt::Key_Space) spaceHeld_ = false; else QWidget::keyReleaseEvent(ev); }

} // namespace psi
