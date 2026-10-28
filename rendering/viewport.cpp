#include "rendering/viewport.h"
#include "ui/theme.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QWheelEvent>
#include <cmath>
#include <map>

namespace psi {

Viewport::Viewport(Document& doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::BlankCursor);
    setAttribute(Qt::WA_OpaquePaintEvent);
    auto inval = [this] { invalidate(); };
    connect(&doc_, &Document::entitiesChanged, this, [this](const std::vector<quint64>& ids) {
        bool changed = false;
        for (quint64 id : ids) if (selection_.remove(id) && !doc_.entity(id)) changed = true;
        for (quint64 id : ids) if (doc_.entity(id) && selection_.contains(id)) {}
        invalidate();
        if (changed) Q_EMIT selectionChanged();
    });
    connect(&doc_, &Document::layersChanged, this, inval);
    connect(&doc_, &Document::settingsChanged, this, inval);
    connect(&doc_, &Document::documentReset, this, [this] { selection_.clear(); invalidate(); Q_EMIT selectionChanged(); });
}

QTransform Viewport::worldToScreen() const {
    QTransform t;
    t.translate(width() / 2.0, height() / 2.0);
    t.scale(scale_, -scale_);
    t.translate(-center_.x, -center_.y);
    return t;
}

Point Viewport::toWorld(QPointF s) const { QPointF w = worldToScreen().inverted().map(s); return {w.x(), w.y()}; }
QPointF Viewport::toScreen(Point w) const { return worldToScreen().map(QPointF(w.x, w.y)); }

Rect Viewport::visibleWorld() const {
    Point a = toWorld(QPointF(0, 0)), b = toWorld(QPointF(width(), height()));
    return Rect(a, b);
}

void Viewport::setView(Point c, double s) {
    center_ = c;
    scale_ = std::clamp(s, 1e-4, 1e5);
    invalidate();
    Q_EMIT viewChanged();
}

void Viewport::zoomTo(const Rect& r) {
    if (!r.valid() || width() <= 0) return;
    double w = std::max(r.width(), 1e-3), h = std::max(r.height(), 1e-3);
    double s = std::min(width() / (w * 1.15), height() / (h * 1.15));
    setView(r.center(), s);
}

void Viewport::zoomExtents() {
    Rect r = doc_.extents();
    if (!r.valid()) r = Rect(0, 0, 297, 210);
    if (r.width() < 1 && r.height() < 1) r = r.grown(50);
    zoomTo(r);
}

void Viewport::zoomBy(double factor, std::optional<QPointF> at) {
    QPointF anchor = at.value_or(QPointF(width() / 2.0, height() / 2.0));
    Point before = toWorld(anchor);
    scale_ = std::clamp(scale_ * factor, 1e-4, 1e5);
    Point after = toWorld(anchor);
    center_ = center_ + (before - after);
    invalidate();
    Q_EMIT viewChanged();
}

void Viewport::invalidate() { cacheDirty_ = true; update(); }

void Viewport::setTool(std::unique_ptr<Tool> t) {
    tool_ = std::move(t);
    trackFrom_.reset();
    if (tool_) tool_->begin();
    Q_EMIT toolChanged();
    Q_EMIT promptChanged(tool_ ? tool_->prompt() : QString());
    update();
}

void Viewport::endTool() { Q_EMIT toolChanged(); }
QString Viewport::toolId() const { return tool_ ? tool_->id() : QString(); }

void Viewport::setSelection(const QSet<quint64>& s) { selection_ = s; Q_EMIT selectionChanged(); update(); }
void Viewport::selectAll() { selection_.clear(); for (quint64 id : doc_.entityIds()) { const Entity* e = doc_.entity(id); if (e && doc_.entityVisible(*e) && !e->locked) selection_.insert(id); } Q_EMIT selectionChanged(); update(); }
void Viewport::clearSelection() { if (selection_.isEmpty()) return; selection_.clear(); Q_EMIT selectionChanged(); update(); }

std::optional<quint64> Viewport::hitTest(QPointF screen) const {
    Point w = toWorld(screen);
    double r = pickRadiusWorld();
    Rect q(w.x - r, w.y - r, w.x + r, w.y + r);
    std::optional<quint64> best; double bd = r;
    for (quint64 id : doc_.query(q)) {
        const Entity* e = doc_.entity(id);
        if (!e || !doc_.entityVisible(*e)) continue;
        const Layer* l = doc_.layer(e->layer);
        if (l && l->locked) continue;
        double d = hitDistance(*e, w, 0.5 / scale_);
        if (d <= bd) { bd = d; best = id; }
    }
    return best;
}

std::vector<quint64> Viewport::entitiesIn(const Rect& r, bool crossing) const {
    std::vector<quint64> out;
    for (quint64 id : doc_.query(r)) {
        const Entity* e = doc_.entity(id);
        if (!e || !doc_.entityVisible(*e)) continue;
        const Layer* l = doc_.layer(e->layer);
        if (l && l->locked) continue;
        Rect bb = entityBBox(*e);
        if (r.contains(bb)) { out.push_back(id); continue; }
        if (!crossing) continue;
        // crossing: any segment intersects the box
        Entity box = makeRectangle({r.x0, r.y0}, {r.x1, r.y1});
        if (!intersections(*e, box).empty() || r.contains(bb.center())) out.push_back(id);
    }
    return out;
}

// ---------------------------------------------------------------- painting
void Viewport::drawGrid(QPainter& p, const QTransform& t, const QRectF& area) {
    const DocumentSettings& s = doc_.settings();
    if (!s.gridVisible || s.gridSpacing <= 0) return;
    double step = s.gridSpacing;
    while (step * scale_ < 9) step *= (std::fmod(std::log10(step / s.gridSpacing), 1.0) > 0.3 ? 2 : 5);
    Rect w = visibleWorld();
    const Palette& pal = Theme::current();
    QPen minor(pal.grid, 0), major(pal.gridMajor, 0);
    minor.setCosmetic(true); major.setCosmetic(true);
    long long x0 = static_cast<long long>(std::floor(w.x0 / step)), x1 = static_cast<long long>(std::ceil(w.x1 / step));
    long long y0 = static_cast<long long>(std::floor(w.y0 / step)), y1 = static_cast<long long>(std::ceil(w.y1 / step));
    if ((x1 - x0) * (y1 - y0) > 400000) return;
    p.setTransform(QTransform(), false);
    for (long long i = x0; i <= x1; ++i) {
        double x = t.map(QPointF(i * step, 0)).x();
        p.setPen(i % 5 == 0 ? major : minor);
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }
    for (long long j = y0; j <= y1; ++j) {
        double y = t.map(QPointF(0, j * step)).y();
        p.setPen(j % 5 == 0 ? major : minor);
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }
    // axes
    QPen ax(pal.faint, 0); ax.setCosmetic(true); p.setPen(ax);
    QPointF o = t.map(QPointF(0, 0));
    p.drawLine(QPointF(area.left(), o.y()), QPointF(area.right(), o.y()));
    p.drawLine(QPointF(o.x(), area.top()), QPointF(o.x(), area.bottom()));
}

void Viewport::drawEntities(QPainter& p, const RenderOptions& o, const Rect& world) {
    // batch plain lines / polylines by pen to keep tens of thousands of entities interactive
    struct Batch { QPen pen; QVector<QLineF> lines; QPainterPath path; };
    std::map<std::tuple<int, int, int>, Batch> batches;
    std::vector<quint64> ids = doc_.query(world);
    std::vector<const Entity*> special;
    for (quint64 id : ids) {
        const Entity* e = doc_.entity(id);
        if (!e || !doc_.entityVisible(*e)) continue;
        if (e->type == EntityType::Line || e->type == EntityType::Polyline || e->type == EntityType::Circle || e->type == EntityType::Arc ||
            e->type == EntityType::Spline || e->type == EntityType::Ellipse) {
            QPen pen = Renderer::pen(doc_, *e, o);
            auto key = std::make_tuple(pen.color().rgb(), static_cast<int>(pen.widthF() * 10), static_cast<int>(doc_.effectiveLineType(*e)));
            Batch& b = batches[key];
            b.pen = pen;
            if (e->type == EntityType::Line) b.lines.push_back(QLineF(e->pts[0].x, e->pts[0].y, e->pts[1].x, e->pts[1].y));
            else b.path.addPath(Renderer::worldPath(*e, o.tol));
        } else {
            special.push_back(e);
        }
    }
    p.save();
    p.setTransform(o.world, false);
    p.setBrush(Qt::NoBrush);
    for (auto& [key, b] : batches) {
        p.setPen(b.pen);
        if (!b.lines.isEmpty()) p.drawLines(b.lines);
        if (!b.path.isEmpty()) p.drawPath(b.path);
    }
    p.restore();
    for (const Entity* e : special) Renderer::drawEntity(p, doc_, *e, o);
}

void Viewport::renderCache() {
    const qreal dpr = devicePixelRatioF();
    QSize sz(std::max(1, width()), std::max(1, height()));
    if (cache_.size() != sz * dpr) { cache_ = QPixmap(sz * dpr); cache_.setDevicePixelRatio(dpr); }
    const Palette& pal = Theme::current();
    cache_.fill(pal.canvas);
    QPainter p(&cache_);
    p.setRenderHint(QPainter::Antialiasing, true);
    QTransform t = worldToScreen();
    drawGrid(p, t, QRectF(0, 0, width(), height()));
    RenderOptions o;
    o.world = t;
    o.pxPerMm = 2.4 * dpr / dpr;   // paper mm on screen ≈ 2.4 px
    o.ink = pal.text;
    o.accent = pal.accent;
    o.tol = std::max(0.05, 0.3 / scale_);
    drawEntities(p, o, visibleWorld().grown(10 / scale_));
    cacheDirty_ = false;
    cacheRevision_ = doc_.revision();
}

void Viewport::paintEvent(QPaintEvent*) {
    if (cacheDirty_ || cacheRevision_ != doc_.revision()) renderCache();
    QPainter p(this);
    p.drawPixmap(0, 0, cache_);
    p.setRenderHint(QPainter::Antialiasing, true);
    drawOverlay(p);
}

void Viewport::drawOverlay(QPainter& p) {
    const Palette& pal = Theme::current();
    QTransform t = worldToScreen();
    RenderOptions o;
    o.world = t; o.pxPerMm = 2.4; o.ink = pal.text; o.accent = pal.accent; o.tol = std::max(0.05, 0.3 / scale_);
    // hover
    if (hover_ && !selection_.contains(hover_)) {
        if (const Entity* e = doc_.entity(hover_)) { RenderOptions h = o; h.hovered = true; Renderer::drawEntity(p, doc_, *e, h); }
    }
    // selection
    for (quint64 id : selection_) {
        const Entity* e = doc_.entity(id);
        if (!e) continue;
        RenderOptions s = o; s.selected = true;
        Renderer::drawEntity(p, doc_, *e, s);
        // grips
        p.save(); p.resetTransform();
        p.setPen(QPen(pal.accent, 1)); p.setBrush(pal.card);
        for (const SnapPoint& sp : snapPoints(*e)) {
            if (sp.kind != SnapKind::Endpoint && sp.kind != SnapKind::Center && sp.kind != SnapKind::Node) continue;
            QPointF c = toScreen(sp.p);
            p.drawRect(QRectF(c.x() - 3, c.y() - 3, 6, 6));
        }
        p.restore();
    }
    // tool preview
    if (tool_) { p.save(); tool_->draw(p, o); p.restore(); }
    p.resetTransform();
    // rubber band / zoom window
    if (boxSelecting_ || (zoomWindow_ && panning_)) {
        QRectF r(boxStart_, cursorPx_);
        bool crossing = cursorPx_.x() < boxStart_.x();
        QColor fill = crossing ? QColor(pal.accent) : QColor(pal.accent);
        fill.setAlpha(crossing ? 30 : 50);
        p.setPen(QPen(pal.accent, 1, crossing ? Qt::DashLine : Qt::SolidLine));
        p.setBrush(fill);
        p.drawRect(r.normalized());
    }
    // tracking line from the previous point
    if (haveCursor_ && trackFrom_ && polarHint_) {
        QPointF a = toScreen(*trackFrom_), b = cursorPx_;
        QPen tp(pal.faint, 1, Qt::DashLine); p.setPen(tp); p.drawLine(a, b);
    }
    // snap marker
    if (snap_.valid && haveCursor_) {
        QPointF c = toScreen(snap_.p);
        p.setPen(QPen(pal.accent, 1.5)); p.setBrush(Qt::NoBrush);
        switch (snap_.kind) {
        case SnapKind::Endpoint: case SnapKind::Node: p.drawRect(QRectF(c.x() - 5, c.y() - 5, 10, 10)); break;
        case SnapKind::Midpoint: { QPolygonF tri; tri << QPointF(c.x(), c.y() - 6) << QPointF(c.x() + 6, c.y() + 5) << QPointF(c.x() - 6, c.y() + 5); p.drawPolygon(tri); break; }
        case SnapKind::Center: p.drawEllipse(c, 6, 6); break;
        case SnapKind::Quadrant: { QPolygonF d; d << QPointF(c.x(), c.y() - 6) << QPointF(c.x() + 6, c.y()) << QPointF(c.x(), c.y() + 6) << QPointF(c.x() - 6, c.y()); p.drawPolygon(d); break; }
        case SnapKind::Intersection: p.drawLine(c + QPointF(-5, -5), c + QPointF(5, 5)); p.drawLine(c + QPointF(-5, 5), c + QPointF(5, -5)); break;
        case SnapKind::Perpendicular: p.drawLine(c + QPointF(-5, 5), c + QPointF(-5, -5)); p.drawLine(c + QPointF(-5, 5), c + QPointF(5, 5)); p.drawRect(QRectF(c.x() - 5, c.y() - 1, 4, 4)); break;
        case SnapKind::Tangent: p.drawEllipse(c, 5, 5); p.drawLine(c + QPointF(-6, -6), c + QPointF(6, -6)); break;
        case SnapKind::Grid: p.setBrush(pal.accent); p.drawRect(QRectF(c.x() - 3, c.y() - 3, 6, 6)); p.setBrush(Qt::NoBrush); break;
        }
    }
    // crosshair cursor
    if (haveCursor_ && crosshair_ && !panning_) {
        QPen cp(pal.crosshair, 1); p.setPen(cp);
        QPointF c = snap_.valid ? toScreen(snap_.p) : cursorPx_;
        p.drawLine(QPointF(c.x() - 14, c.y()), QPointF(c.x() + 14, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - 14), QPointF(c.x(), c.y() + 14));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(c.x() - 4, c.y() - 4, 8, 8));
    }
}

QImage Viewport::renderToImage(const QSize& size, qreal dpr) {
    resize(size);
    cacheDirty_ = true;
    renderCache();
    QImage img = cache_.toImage();
    img.setDevicePixelRatio(dpr);
    return img;
}

// ---------------------------------------------------------------- interaction
Point Viewport::snapped(QPointF screen, bool shift) {
    Point raw = toWorld(screen);
    std::optional<Point> from = tool_ ? tool_->lastPoint() : std::nullopt;
    snap_ = SnapEngine::snap(doc_, raw, pickRadiusWorld() * 1.6, from, 0, 8.0 / scale_);
    Point p = snap_.valid ? snap_.p : raw;
    if (from && (!snap_.valid || snap_.kind == SnapKind::Grid)) {
        // ortho / polar first, then the magnet on the free coordinate (the constraint is re-applied to the rounded point)
        Point c = SnapEngine::constrain(doc_.settings(), *from, raw, shift);
        if (snap_.valid) {
            Point g = SnapEngine::roundToStep(c, doc_.settings().snapStep);
            if (dist(g, c) <= 8.0 / scale_) { p = SnapEngine::constrain(doc_.settings(), *from, g, shift); snap_.p = p; }
            else { snap_.valid = false; p = c; }
        } else p = c;
    }
    return p;
}

void Viewport::applyBoxSelection(QPointF a, QPointF b, bool shift) {
    bool crossing = b.x() < a.x();
    Rect r(toWorld(a), toWorld(b));
    std::vector<quint64> ids = entitiesIn(r, crossing);
    if (!shift) selection_.clear();
    for (quint64 id : ids) selection_.insert(id);
    Q_EMIT selectionChanged();
}

void Viewport::mousePressEvent(QMouseEvent* ev) {
    setFocus();
    lastMouse_ = ev->position();
    if (ev->button() == Qt::MiddleButton || (ev->button() == Qt::LeftButton && (ev->modifiers() & Qt::AltModifier))) {
        panning_ = true; setCursor(Qt::ClosedHandCursor); return;
    }
    if (ev->button() == Qt::LeftButton && zoomWindow_) { panning_ = true; boxStart_ = ev->position(); return; }
    if (ev->button() == Qt::RightButton) {
        if (tool_) { if (tool_->selecting()) tool_->selectionDone(); else tool_->enter(); Q_EMIT promptChanged(tool_ ? tool_->prompt() : QString()); }
        update();
        return;
    }
    if (ev->button() != Qt::LeftButton) return;
    const bool shift = ev->modifiers() & Qt::ShiftModifier;
    if (!tool_ || tool_->selecting()) {
        auto hit = hitTest(ev->position());
        if (hit) {
            if (shift) { if (!selection_.remove(*hit)) selection_.insert(*hit); }
            else { selection_.clear(); selection_.insert(*hit); }
            Q_EMIT selectionChanged();
        } else {
            boxSelecting_ = true; boxStart_ = ev->position();
        }
        update();
        return;
    }
    Point p = snapped(ev->position(), shift);
    tool_->click(p, shift);
    Q_EMIT promptChanged(tool_ ? tool_->prompt() : QString());
    trackFrom_ = tool_ ? tool_->lastPoint() : std::nullopt;
    update();
}

void Viewport::mouseMoveEvent(QMouseEvent* ev) {
    cursorPx_ = ev->position();
    haveCursor_ = true;
    if (panning_ && !zoomWindow_) {
        QPointF d = ev->position() - lastMouse_;
        center_ = center_ - Point{d.x() / scale_, -d.y() / scale_};
        lastMouse_ = ev->position();
        invalidate();
        Q_EMIT viewChanged();
        return;
    }
    const bool shift = ev->modifiers() & Qt::ShiftModifier;
    Point p = snapped(ev->position(), shift);
    lastWorld_ = p;
    if (tool_ && !tool_->selecting()) tool_->move(p);
    else {
        auto hit = boxSelecting_ ? std::nullopt : hitTest(ev->position());
        hover_ = hit.value_or(0);
    }
    Q_EMIT cursorMoved(p);
    update();
}

void Viewport::mouseReleaseEvent(QMouseEvent* ev) {
    if (panning_) {
        panning_ = false;
        if (zoomWindow_) {
            zoomWindow_ = false;
            QRectF r(boxStart_, ev->position());
            if (r.normalized().width() > 4 && r.normalized().height() > 4) zoomTo(Rect(toWorld(r.topLeft()), toWorld(r.bottomRight())));
        }
        setCursor(Qt::BlankCursor);
        update();
        return;
    }
    if (boxSelecting_ && ev->button() == Qt::LeftButton) {
        boxSelecting_ = false;
        QRectF r(boxStart_, ev->position());
        if (r.normalized().width() > 3 || r.normalized().height() > 3) applyBoxSelection(boxStart_, ev->position(), ev->modifiers() & Qt::ShiftModifier);
        else if (!(ev->modifiers() & Qt::ShiftModifier)) { selection_.clear(); Q_EMIT selectionChanged(); }
        update();
    }
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::MiddleButton) zoomExtents();
}

void Viewport::wheelEvent(QWheelEvent* ev) {
    double steps = ev->angleDelta().y() / 120.0;
    if (steps == 0) steps = ev->pixelDelta().y() / 60.0;
    zoomBy(std::pow(1.15, steps), ev->position());
}

void Viewport::keyPressEvent(QKeyEvent* ev) {
    switch (ev->key()) {
    case Qt::Key_Escape:
        if (tool_) { tool_->escape(); Q_EMIT promptChanged(tool_ ? tool_->prompt() : QString()); }
        else clearSelection();
        trackFrom_ = tool_ ? tool_->lastPoint() : std::nullopt;
        update();
        return;
    case Qt::Key_Return: case Qt::Key_Enter:
        if (tool_) { if (tool_->selecting()) tool_->selectionDone(); else tool_->enter(); Q_EMIT promptChanged(tool_ ? tool_->prompt() : QString()); }
        trackFrom_ = tool_ ? tool_->lastPoint() : std::nullopt;
        update();
        return;
    case Qt::Key_Delete: case Qt::Key_Backspace:
        Q_EMIT deleteRequested();
        return;
    case Qt::Key_Space:
        if (!ev->isAutoRepeat()) { /* space+drag pan: handled as Alt-drag; keep for parity */ }
        break;
    default: break;
    }
    QWidget::keyPressEvent(ev);
}

void Viewport::leaveEvent(QEvent*) { haveCursor_ = false; hover_ = 0; update(); }

void Viewport::resizeEvent(QResizeEvent*) { invalidate(); }

} // namespace psi
