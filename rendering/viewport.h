#pragma once
#include "core/document/document.h"
#include "modules/draft/snap.h"
#include "modules/draft/tool.h"
#include <QPixmap>
#include <QSet>
#include <QTransform>
#include <QWidget>
#include <memory>
#include <optional>

namespace psi {

// Model-space canvas: cached static layer + dynamic overlay (selection, snap, tool preview).
class Viewport : public QWidget {
    Q_OBJECT
public:
    explicit Viewport(Document& doc, QWidget* parent = nullptr);

    Document& document() { return doc_; }
    QTransform worldToScreen() const;
    Point toWorld(QPointF screen) const;
    QPointF toScreen(Point world) const;
    double pxPerMm() const { return scale_; }
    Point center() const { return center_; }
    void setView(Point center, double pxPerMm);
    void zoomExtents();
    void zoomTo(const Rect& r);
    void zoomBy(double factor, std::optional<QPointF> at = std::nullopt);
    void startZoomWindow() { zoomWindow_ = true; setCursor(Qt::CrossCursor); }
    Rect visibleWorld() const;

    void setTool(std::unique_ptr<Tool> tool);
    Tool* tool() const { return tool_.get(); }
    void endTool();                                  // back to Select
    QString toolId() const;

    QSet<quint64>& selection() { return selection_; }
    void setSelection(const QSet<quint64>& s);
    void selectAll();
    void clearSelection();
    quint64 hovered() const { return hover_; }
    std::optional<quint64> hitTest(QPointF screen) const;
    std::vector<quint64> entitiesIn(const Rect& r, bool crossing) const;
    double pickRadiusWorld() const { return 7.0 / scale_; }

    void invalidate();                               // re-render the cached layer on next paint
    QImage renderToImage(const QSize& size, qreal dpr = 1.0);   // headless render (tests)
    void setCrosshair(bool on) { crosshair_ = on; update(); }
    QString snapLabel() const { return snap_.valid ? SnapEngine::kindName(snap_.kind) : QString(); }
    void setPolarHint(bool on) { polarHint_ = on; }

Q_SIGNALS:
    void cursorMoved(Point world);
    void selectionChanged();
    void viewChanged();
    void toolChanged();
    void deleteRequested();
    void promptChanged(const QString& text);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    void renderCache();
    void drawGrid(QPainter& p, const QTransform& t, const QRectF& area);
    void drawEntities(QPainter& p, const RenderOptions& o, const Rect& world);
    void drawOverlay(QPainter& p);
    Point snapped(QPointF screen, bool shift);
    void applyBoxSelection(QPointF a, QPointF b, bool shift);

    Document& doc_;
    Point center_{100, 100};
    double scale_ = 2.0;            // px per mm
    QPixmap cache_;
    bool cacheDirty_ = true;
    quint64 cacheRevision_ = 0;
    std::unique_ptr<Tool> tool_;
    QSet<quint64> selection_;
    quint64 hover_ = 0;
    SnapResult snap_;
    std::optional<Point> trackFrom_;
    QPointF lastMouse_;
    bool panning_ = false;
    bool zoomWindow_ = false;
    bool boxSelecting_ = false;
    QPointF boxStart_;
    QPointF cursorPx_;
    bool haveCursor_ = false;
    bool crosshair_ = true;
    bool polarHint_ = true;
    Point lastWorld_;
};

} // namespace psi
