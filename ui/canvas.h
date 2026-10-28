#pragma once
#include "core/geometry/point.h"
#include <QWidget>

namespace psi {

// Общий 2D-холст рабочих пространств «Эскиз» и «Интерьер»: панорамирование (средняя кнопка / пробел),
// масштаб колесом, сетка в стиле Theme. Подклассы рисуют содержимое в paintContent и обрабатывают инструменты.
class CanvasWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanvasWidget(QWidget* parent = nullptr);
    Point toWorld(QPointF screen) const;
    QPointF toScreen(Point world) const;
    double pxPerMm() const { return scale_; }
    double pickRadiusWorld() const { return 8.0 / scale_; }
    void zoomTo(const Rect& r, double marginPx = 40);
    void zoomBy(double factor, QPointF anchor);
    double gridStep() const;                      // шаг сетки в мм при текущем масштабе
    Point snapToGrid(Point p) const;              // ближайший узел сетки
    bool gridVisible() const { return grid_; }
    void setGridVisible(bool on) { grid_ = on; update(); }

Q_SIGNALS:
    void cursorMoved(Point world);
    void viewChanged();

protected:
    virtual void paintContent(QPainter& p) = 0;
    virtual void canvasPress(QMouseEvent*) {}
    virtual void canvasMove(QMouseEvent*) {}
    virtual void canvasRelease(QMouseEvent*) {}
    virtual void canvasDoubleClick(QMouseEvent*) {}
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void drawGrid(QPainter& p);

    double scale_ = 0.15;           // px per mm
    QPointF origin_{80, 500};       // экранные координаты мировой точки (0,0); ось Y вверх
    bool grid_ = true;
    bool panning_ = false, spaceHeld_ = false;
    QPointF panLast_;
};

} // namespace psi
