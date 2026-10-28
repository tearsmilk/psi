#pragma once
// 3D-вид рабочего пространства «Модель»: программный растеризатор (z-буфер + буфер идентификаторов) поверх
// QImage / QPainter — детерминированно работает офскрин (тесты) и на macOS 11 без OpenGL-контекста.
// Орбита / панорама / зум, стандартные виды, перспектива / ортогональ, выбор грани / ребра / вершины / тела,
// подсветка выбора акцентным цветом, сетка плоскости XY, сглаженные рёбра.
#include "modules/model/kernel.h"
#include <QColor>
#include <QImage>
#include <QWidget>
#include <optional>
#include <vector>

namespace psi {

struct View3DItem {
    quint64 id = 0;                 // идентификатор тела (индекс + 1 или id операции)
    md::Mesh mesh;
    QColor color;                   // пусто → цвет темы
    QString name;
};

class View3D : public QWidget {
    Q_OBJECT
public:
    enum class StdView { Front, Back, Top, Bottom, Left, Right, Iso };
    enum class PickMode { Face, Edge, Vertex, Body };
    struct Pick {
        quint64 item = 0;           // 0 = ничего
        int face = 0;               // индекс грани (1..n) в теле
        int edge = 0;               // индекс ребра (1..n)
        md::Vec3 point;             // точка в модели (на грани / вершина)
        md::Vec3 normal;            // нормаль грани в точке (для отверстий)
        bool valid() const { return item != 0; }
        bool operator==(const Pick& o) const { return item == o.item && face == o.face && edge == o.edge; }
    };

    explicit View3D(QWidget* parent = nullptr);

    void setItems(std::vector<View3DItem> items);
    const std::vector<View3DItem>& items() const { return items_; }
    void clear();

    void setStandardView(StdView v, bool fit = true);
    void zoomExtents();
    void setPerspective(bool on);
    bool perspective() const { return perspective_; }
    void setShowGrid(bool on) { grid_ = on; invalidate(); }
    bool showGrid() const { return grid_; }
    void setShowEdges(bool on) { edges_ = on; invalidate(); }
    bool showEdges() const { return edges_; }
    void setPickMode(PickMode m) { pickMode_ = m; hover_ = {}; update(); }
    PickMode pickMode() const { return pickMode_; }
    void orbit(double dYawDeg, double dPitchDeg);
    void pan(double dxPx, double dyPx);
    void zoomBy(double factor);
    md::Vec3 target() const { return target_; }
    double distance() const { return dist_; }

    Pick pickAt(QPoint px) const;              // по текущему кадру
    Pick selection() const { return selection_; }
    void setSelection(const Pick& p);
    void clearSelection() { setSelection({}); }

    QImage renderToImage(const QSize& size, qreal dpr = 1.0);   // полный кадр (тесты, печать)
    QPointF project(const md::Vec3& p) const;                    // модель → экран (логические px)
    void invalidate() { dirty_ = true; update(); }

Q_SIGNALS:
    void picked(const Pick& p);                // клик
    void doubleClicked(const Pick& p);
    void hoverChanged(const Pick& p);
    void viewChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void leaveEvent(QEvent*) override;
    QSize sizeHint() const override { return {800, 600}; }

private:
    struct Frame {
        QImage color;
        std::vector<float> depth;
        std::vector<uint32_t> ids;      // (item+1) << 20 | face
        int w = 0, h = 0;
        qreal dpr = 1;
    };
    struct Camera { md::Vec3 eye, right, up, forward; double focal = 1; };
    Camera camera(int w, int h) const;
    bool toScreen(const Camera& c, const md::Vec3& p, double& sx, double& sy, double& depth) const;
    void renderFrame(Frame& f, const QSize& size, qreal dpr) const;
    void rasterTriangles(Frame& f, const Camera& cam) const;
    void drawSegments(QPainter& p, const Frame& f, const Camera& cam, const std::vector<md::Vec3>& seg, const QColor& color, double width, bool depthTest) const;
    void drawGrid(QPainter& p, Frame& f, const Camera& cam) const;
    void drawOverlay(QPainter& p, const Frame& f, const Camera& cam) const;
    void drawTriad(QPainter& p, const Camera& cam, int w, int h) const;
    void ensureFrame();
    Pick pickIn(const Frame& f, QPointF px) const;
    md::Bbox sceneBbox() const;

    std::vector<View3DItem> items_;
    md::Vec3 target_{0, 0, 0};
    double yaw_ = -45, pitch_ = 35.264, dist_ = 1500, fovDeg_ = 30;
    bool perspective_ = true, grid_ = true, edges_ = true;
    PickMode pickMode_ = PickMode::Face;
    Pick selection_, hover_;
    Frame frame_;
    bool dirty_ = true;
    QPoint lastMouse_;
    bool dragging_ = false, panning_ = false, moved_ = false;
};

} // namespace psi
