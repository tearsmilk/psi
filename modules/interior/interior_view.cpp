#include "modules/interior/interior_view.h"
#include "core/units/units.h"
#include "ui/theme.h"
#include <QInputDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace psi::in {

InteriorView::InteriorView(QWidget* parent) : CanvasWidget(parent) {
    scale_ = 0.08; origin_ = QPointF(120, 520);
    placing_ = Building::makeFurniture(FurnitureKind::Wardrobe);
    setPrompt(QStringLiteral("Выберите объект или инструмент"));
}

QString InteriorView::toolName(Tool t) {
    switch (t) {
    case Tool::Select: return QStringLiteral("Выбор");
    case Tool::Wall: return QStringLiteral("Стена");
    case Tool::Door: return QStringLiteral("Дверь");
    case Tool::Window: return QStringLiteral("Окно");
    case Tool::Room: return QStringLiteral("Помещение");
    case Tool::Furniture: return QStringLiteral("Мебель");
    case Tool::Measure: return QStringLiteral("Измерить");
    }
    return {};
}

Floor* InteriorView::floor() const { return building_ && floor_ < building_->floors.size() ? &building_->floors[floor_] : nullptr; }
void InteriorView::setBuilding(Building* b) { building_ = b; floor_ = 0; sel_ = {}; pts_.clear(); refresh(); Q_EMIT selectionChanged(); Q_EMIT floorChanged(); }
void InteriorView::setFloor(size_t i) { if (!building_ || i >= building_->floors.size()) return; floor_ = i; sel_ = {}; pts_.clear(); refresh(); Q_EMIT selectionChanged(); Q_EMIT floorChanged(); }
void InteriorView::setPrompt(const QString& t) { prompt_ = t; Q_EMIT promptChanged(t); }
QPolygonF InteriorView::poly(const std::vector<Point>& pts) const { QPolygonF q; for (const Point& p : pts) q << toScreen(p); return q; }

void InteriorView::setTool(Tool t) {
    tool_ = t; pts_.clear();
    switch (t) {
    case Tool::Select: setPrompt(QStringLiteral("Выберите стену, проём, мебель или помещение; перетаскивайте мышью, R — повернуть")); break;
    case Tool::Wall: setPrompt(QStringLiteral("Стена: укажите точки оси (двойной клик или Esc — закончить)")); break;
    case Tool::Door: setPrompt(QStringLiteral("Дверь: щёлкните по стене")); break;
    case Tool::Window: setPrompt(QStringLiteral("Окно: щёлкните по стене")); break;
    case Tool::Room: setPrompt(QStringLiteral("Помещение: щёлкните внутри, чтобы задать имя и материал пола")); break;
    case Tool::Furniture: setPrompt(QStringLiteral("Мебель «%1»: щёлкните для размещения, R — повернуть").arg(placing_.name)); break;
    case Tool::Measure: setPrompt(QStringLiteral("Измерение: укажите точки (двойной клик — площадь контура)")); break;
    }
    Q_EMIT toolChanged(); update();
}

void InteriorView::select(const Selection& s) { if (sel_ == s) return; sel_ = s; Q_EMIT selectionChanged(); update(); }

void InteriorView::refresh() {
    rooms_.clear(); collisions_.clear();
    if (Floor* f = floor()) { rooms_ = Building::rooms(*f); collisions_ = Building::collisions(*f); }
    update();
}

void InteriorView::escape() {
    if (!pts_.empty()) { if (tool_ == Tool::Wall) finishWall(); else { pts_.clear(); setTool(tool_); } return; }
    if (tool_ != Tool::Select) { setTool(Tool::Select); return; }
    clearSelection();
}

void InteriorView::zoomExtents() {
    if (!building_) { zoomTo(Rect(-1000, -1000, 8000, 6000)); return; }
    Rect r = building_->extents(floor_);
    if (!r.valid()) r = Rect(-1000, -1000, 8000, 6000);
    r.x0 -= 500; r.y0 -= 500; r.x1 += 500; r.y1 += 500; zoomTo(r);
}

// ---------------------------------------------------------------- picking
Point InteriorView::snap(Point raw, bool* snapped) const {
    if (snapped) *snapped = false;
    if (Floor* f = floor()) {
        double best = pickRadiusWorld(); std::optional<Point> bp;
        for (const Wall& w : f->walls) for (const Point& p : w.pts) { double d = dist(p, raw); if (d < best) { best = d; bp = p; } }
        if (bp) { if (snapped) *snapped = true; return *bp; }
        // ортогональность к предыдущей точке стены
        if (!pts_.empty() && tool_ == Tool::Wall) {
            Point a = pts_.back();
            if (std::abs(raw.x - a.x) < pickRadiusWorld() * 2) raw.x = a.x;
            else if (std::abs(raw.y - a.y) < pickRadiusWorld() * 2) raw.y = a.y;
        }
    }
    if (gridVisible()) {
        Point g = snapToGrid(raw);
        if (dist(g, raw) < pickRadiusWorld() * 1.5) { if (snapped) *snapped = true; return g; }
    }
    return raw;
}

static double segDist(Point a, Point b, Point w) {
    Vec d = b - a; double l2 = d.dot(d);
    double t = l2 < 1e-12 ? 0 : std::clamp((w - a).dot(d) / l2, 0.0, 1.0);
    return dist(a + d * t, w);
}
static bool pointInPoly(const std::vector<Point>& poly, Point p) {
    bool in = false;
    for (size_t i = 0, n = poly.size(), j = n - 1; i < n; j = i++) {
        if (((poly[i].y > p.y) != (poly[j].y > p.y)) && (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)) in = !in;
    }
    return in;
}

const Wall* InteriorView::nearestWall(Point w, double* offset, double maxDist) const {
    Floor* f = floor(); if (!f) return nullptr;
    const Wall* best = nullptr; double bd = maxDist;
    for (const Wall& wall : f->walls) {
        for (int i = 0; i + 1 < int(wall.pts.size()); ++i) { double d = segDist(wall.pts[i], wall.pts[i + 1], w); if (d < bd) { bd = d; best = &wall; } }
    }
    if (best && offset) *offset = Building::projectOnWall(*best, w);
    return best;
}

const Room* InteriorView::roomAt(Point w) const {
    for (const Room& r : rooms_) if (pointInPoly(r.inner, w)) return &r;
    return nullptr;
}

InteriorView::Selection InteriorView::hit(Point w) const {
    Floor* f = floor(); if (!f) return {};
    for (auto it = f->furniture.rbegin(); it != f->furniture.rend(); ++it) if (pointInPoly(it->footprint(), w)) return {Selection::Furniture, it->id, {}};
    for (const Opening& o : f->openings) {
        const Wall* wall = f->wall(o.wall); if (!wall) continue;
        Vec along, normal; Point c = Building::openingCenter(*f, o, &along, &normal);
        Vec d = w - c;
        if (std::abs(d.dot(along)) <= o.width / 2 && std::abs(d.dot(normal)) <= wall->thickness / 2 + pickRadiusWorld()) return {Selection::Opening, o.id, {}};
    }
    for (const Wall& wall : f->walls) for (int i = 0; i + 1 < int(wall.pts.size()); ++i)
        if (segDist(wall.pts[i], wall.pts[i + 1], w) <= wall.thickness / 2 + pickRadiusWorld() * 0.5) return {Selection::Wall, wall.id, {}};
    if (const Room* r = roomAt(w)) return {Selection::Room, r->id, Building::roomKey(r->centroid)};
    return {};
}

// ---------------------------------------------------------------- editing
void InteriorView::finishWall() {
    if (pts_.size() >= 2 && building_) {
        Q_EMIT aboutToChange(QStringLiteral("Стена"));
        Wall w; w.pts = pts_; w.thickness = wallThickness; w.height = wallHeight; w.material = wallMaterial;
        building_->addWall(floor_, w);
        refresh(); Q_EMIT changed();
    }
    pts_.clear();
    setPrompt(QStringLiteral("Стена: укажите точки оси (двойной клик или Esc — закончить)"));
    update();
}

void InteriorView::deleteSelection() {
    if (!building_ || sel_.kind == Selection::None) return;
    if (sel_.kind == Selection::Room) {
        Floor* f = floor(); if (!f) return;
        Q_EMIT aboutToChange(QStringLiteral("Сбросить имя помещения"));
        f->roomInfo.remove(sel_.roomKey);
    } else {
        Q_EMIT aboutToChange(QStringLiteral("Удалить объект интерьера"));
        building_->remove(floor_, sel_.id);
    }
    sel_ = {}; refresh(); Q_EMIT selectionChanged(); Q_EMIT changed();
}

void InteriorView::rotateSelection(double degrees) {
    if (tool_ == Tool::Furniture) { placing_.rotation += rad(degrees); update(); return; }
    if (sel_.kind != Selection::Furniture) return;
    Floor* f = floor(); if (!f) return;
    Furniture* m = f->furnitureMut(sel_.id); if (!m) return;
    Q_EMIT aboutToChange(QStringLiteral("Повернуть мебель"));
    m->rotation += rad(degrees);
    refresh(); Q_EMIT changed();
}

// ---------------------------------------------------------------- mouse
void InteriorView::canvasPress(QMouseEvent* ev) {
    if (!building_) return;
    if (ev->button() == Qt::RightButton) { escape(); return; }
    if (ev->button() != Qt::LeftButton) return;
    Point raw = toWorld(ev->position());
    Floor* f = floor(); if (!f) return;
    switch (tool_) {
    case Tool::Select: {
        Selection h = hit(raw);
        select(h);
        dragArmed_ = h.kind == Selection::Furniture || h.kind == Selection::Opening || h.kind == Selection::Wall;
        dragging_ = false; dragStart_ = dragLast_ = raw;
        break;
    }
    case Tool::Wall: {
        Point p = snap(raw);
        if (!pts_.empty() && dist(p, pts_.back()) < 1e-6) { finishWall(); break; }
        pts_.push_back(p);
        setPrompt(QStringLiteral("Стена: следующая точка (длина %1)").arg(Units::format(0, Unit::Millimeter, 0, true)));
        break;
    }
    case Tool::Door: case Tool::Window: {
        double off = 0;
        const Wall* w = nearestWall(raw, &off, pickRadiusWorld() * 3 + wallThickness);
        if (!w) { setPrompt(QStringLiteral("Щёлкните ближе к стене")); break; }
        Q_EMIT aboutToChange(tool_ == Tool::Door ? QStringLiteral("Дверь") : QStringLiteral("Окно"));
        Opening o; o.kind = tool_ == Tool::Door ? OpeningKind::Door : OpeningKind::Window; o.wall = w->id;
        o.offset = std::clamp(off, openingWidth / 2, std::max(openingWidth / 2, w->length() - openingWidth / 2));
        o.width = openingWidth; o.height = tool_ == Tool::Door ? openingHeight : 1400; o.sill = windowSill;
        quint64 id = building_->addOpening(floor_, o);
        refresh(); Q_EMIT changed();
        select({Selection::Opening, id, {}});
        break;
    }
    case Tool::Room: {
        const Room* r = roomAt(raw);
        if (!r) { setPrompt(QStringLiteral("Щёлкните внутри замкнутого контура стен")); break; }
        QString key = Building::roomKey(r->centroid);
        bool ok = false;
        QString name = QInputDialog::getText(this, QStringLiteral("Помещение"), QStringLiteral("Название (%1 м²):").arg(r->areaM2, 0, 'f', 2), QLineEdit::Normal, r->name, &ok);
        if (!ok) break;
        QString mat = QInputDialog::getText(this, QStringLiteral("Помещение"), QStringLiteral("Материал пола:"), QLineEdit::Normal, r->floorMaterial.isEmpty() ? QStringLiteral("Ламинат") : r->floorMaterial, &ok);
        if (!ok) break;
        Q_EMIT aboutToChange(QStringLiteral("Помещение «%1»").arg(name));
        f->roomInfo[key] = {name, mat};
        refresh(); Q_EMIT changed();
        select({Selection::Room, r->id, key});
        break;
    }
    case Tool::Furniture: {
        Q_EMIT aboutToChange(QStringLiteral("Мебель: %1").arg(placing_.name));
        Furniture m = placing_; m.id = 0; m.pos = snap(raw);
        quint64 id = building_->addFurniture(floor_, m);
        refresh(); Q_EMIT changed();
        select({Selection::Furniture, id, {}});
        setPrompt(QStringLiteral("Размещено: %1. Ещё одно — щёлкните снова, Esc — выбор").arg(m.name));
        break;
    }
    case Tool::Measure: {
        Point p = snap(raw);
        pts_.push_back(p);
        if (pts_.size() >= 2) {
            double len = 0; for (size_t i = 1; i < pts_.size(); ++i) len += dist(pts_[i - 1], pts_[i]);
            setPrompt(QStringLiteral("Длина %1 · последний отрезок %2").arg(Units::format(len, Unit::Millimeter, 0, true), Units::format(dist(pts_[pts_.size() - 2], pts_.back()), Unit::Millimeter, 0, true)));
        } else if (const Room* r = roomAt(raw)) setPrompt(QStringLiteral("Помещение «%1»: %2 м², периметр %3").arg(r->name).arg(r->areaM2, 0, 'f', 2).arg(Units::format(r->perimeterMm, Unit::Millimeter, 0, true)));
        break;
    }
    }
    update();
}

void InteriorView::canvasMove(QMouseEvent* ev) {
    Point raw = toWorld(ev->position());
    Floor* f = floor();
    if (f && dragArmed_ && (ev->buttons() & Qt::LeftButton)) {
        if (!dragging_) {
            if (dist(raw, dragStart_) * scale_ < 4) return;
            dragging_ = true;
            Q_EMIT aboutToChange(QStringLiteral("Переместить"));
        }
        Vec d = raw - dragLast_;
        if (sel_.kind == Selection::Furniture) { if (Furniture* m = f->furnitureMut(sel_.id)) { m->pos = snap(m->pos + d); dragLast_ = raw; } }
        else if (sel_.kind == Selection::Wall) { if (Wall* w = f->wallMut(sel_.id)) { for (Point& p : w->pts) p += d; dragLast_ = raw; } }
        else if (sel_.kind == Selection::Opening) {
            for (Opening& o : f->openings) if (o.id == sel_.id) {
                const Wall* w = f->wall(o.wall);
                if (w) o.offset = std::clamp(Building::projectOnWall(*w, raw), o.width / 2, std::max(o.width / 2, w->length() - o.width / 2));
            }
            dragLast_ = raw;
        }
        rooms_ = Building::rooms(*f); collisions_ = Building::collisions(*f);
        update();
        return;
    }
    cursor_ = snap(raw, &cursorSnapped_); hasCursor_ = true;
    if (tool_ == Tool::Wall && !pts_.empty()) setPrompt(QStringLiteral("Стена: следующая точка · длина сегмента %1").arg(Units::format(dist(pts_.back(), cursor_), Unit::Millimeter, 0, true)));
    if (tool_ != Tool::Select || !pts_.empty()) update();
}

void InteriorView::canvasRelease(QMouseEvent* ev) {
    if (ev->button() != Qt::LeftButton) return;
    if (dragArmed_) { dragArmed_ = false; if (dragging_) { dragging_ = false; refresh(); Q_EMIT changed(); } }
}

void InteriorView::canvasDoubleClick(QMouseEvent* ev) {
    if (ev->button() != Qt::LeftButton) return;
    if (tool_ == Tool::Wall) { finishWall(); return; }
    if (tool_ == Tool::Measure && pts_.size() >= 3) {
        double a = std::abs(polygonArea(pts_)) / 1e6, per = 0; for (size_t i = 0; i < pts_.size(); ++i) per += dist(pts_[i], pts_[(i + 1) % pts_.size()]);
        setPrompt(QStringLiteral("Контур: площадь %1 м², периметр %2").arg(a, 0, 'f', 2).arg(Units::format(per, Unit::Millimeter, 0, true)));
        pts_.clear(); update();
    }
}

void InteriorView::keyPressEvent(QKeyEvent* ev) {
    switch (ev->key()) {
    case Qt::Key_Escape: escape(); return;
    case Qt::Key_Delete: case Qt::Key_Backspace: deleteSelection(); return;
    case Qt::Key_R: rotateSelection(ev->modifiers() & Qt::ShiftModifier ? -90 : 90); return;
    case Qt::Key_Return: case Qt::Key_Enter: if (tool_ == Tool::Wall) { finishWall(); return; } break;
    default: break;
    }
    CanvasWidget::keyPressEvent(ev);
}

// ---------------------------------------------------------------- painting
void InteriorView::drawWalls(QPainter& p) {
    const Palette& pal = Theme::current();
    Floor* f = floor(); if (!f) return;
    auto outlines = Building::wallOutlines(*f);
    QBrush hatch(pal.muted, Qt::BDiagPattern);
    for (const WallOutline& o : outlines) {
        bool sel = sel_.kind == Selection::Wall && sel_.id == o.wall;
        QPolygonF q = poly(o.polygon);
        p.setPen(Qt::NoPen); p.setBrush(pal.card); p.drawPolygon(q);
        p.setBrush(hatch); p.drawPolygon(q);
        p.setPen(QPen(sel ? pal.selected : pal.text, sel ? 2.2 : 1.4)); p.setBrush(Qt::NoBrush); p.drawPolygon(q);
    }
    // проёмы: вырез в стене + знак
    for (const Opening& o : f->openings) drawOpening(p, o, sel_.kind == Selection::Opening && sel_.id == o.id, false);
    // ось выбранной стены
    if (sel_.kind == Selection::Wall) if (const Wall* w = f->wall(sel_.id)) {
        p.setPen(QPen(pal.selected, 1, Qt::DashLine)); p.drawPolyline(poly(w->pts));
        p.setBrush(pal.selected); p.setPen(Qt::NoPen); for (const Point& pt : w->pts) p.drawEllipse(toScreen(pt), 3.5, 3.5);
    }
}

void InteriorView::drawOpening(QPainter& p, const Opening& o, bool selected, bool preview) {
    const Palette& pal = Theme::current();
    Floor* f = floor(); if (!f) return;
    const Wall* w = f->wall(o.wall); if (!w) return;
    Vec along, normal; Point c = Building::openingCenter(*f, o, &along, &normal);
    double hw = o.width / 2, ht = w->thickness / 2;
    Point a0 = c - along * hw, a1 = c + along * hw;
    QColor col = selected ? pal.selected : preview ? pal.accent : pal.text;
    // вырез
    p.setPen(Qt::NoPen); p.setBrush(pal.canvas);
    p.drawPolygon(poly({a0 - normal * (ht + 0.5), a1 - normal * (ht + 0.5), a1 + normal * (ht + 0.5), a0 + normal * (ht + 0.5)}));
    p.setPen(QPen(col, selected ? 2 : 1.2)); p.setBrush(Qt::NoBrush);
    p.drawLine(toScreen(a0 - normal * ht), toScreen(a0 + normal * ht));
    p.drawLine(toScreen(a1 - normal * ht), toScreen(a1 + normal * ht));
    if (o.kind == OpeningKind::Window) {
        p.drawLine(toScreen(a0 - normal * ht), toScreen(a1 - normal * ht));
        p.drawLine(toScreen(a0 + normal * ht), toScreen(a1 + normal * ht));
        p.setPen(QPen(col, 0.8)); p.drawLine(toScreen(a0), toScreen(a1));
    } else {
        Point hinge = o.swingLeft ? a0 : a1;
        Vec dirOpen = normal * (o.swingInside ? 1 : -1);
        Point tip = hinge + dirOpen * o.width;
        p.setPen(QPen(col, 1.4)); p.drawLine(toScreen(hinge), toScreen(tip));
        double aStart = deg(dirOpen.angle()), aEnd = deg(((o.swingLeft ? a1 : a0) - hinge).angle());
        double span = aEnd - aStart; while (span > 180) span -= 360; while (span < -180) span += 360;
        QPointF hs = toScreen(hinge); double r = o.width * scale_;
        p.setPen(QPen(col, 0.8, Qt::DashLine));
        p.drawArc(QRectF(hs.x() - r, hs.y() - r, 2 * r, 2 * r), int(aStart * 16), int(span * 16));
    }
}

void InteriorView::drawFurniture(QPainter& p, const Furniture& m, bool selected, bool preview, bool collides) {
    const Palette& pal = Theme::current();
    QColor line = collides ? QColor(0xC0, 0x39, 0x2B) : selected ? pal.selected : preview ? pal.accent : pal.text;
    QPolygonF q = poly(m.footprint());
    p.setPen(QPen(line, selected ? 2 : 1.2)); p.setBrush(preview ? QBrush(Qt::NoBrush) : QBrush(pal.card));
    p.drawPolygon(q);
    // условный знак: в локальных координатах
    p.save();
    QPointF c = toScreen(m.pos);
    p.translate(c); p.rotate(-deg(m.rotation)); p.scale(scale_, -scale_);
    p.setPen(QPen(line, 0)); p.setBrush(Qt::NoBrush);
    double hw = m.width / 2, hd = m.depth / 2;
    switch (m.kind) {
    case FurnitureKind::Wardrobe: p.drawLine(QPointF(-hw, -hd), QPointF(hw, hd)); p.drawLine(QPointF(-hw, hd), QPointF(hw, -hd)); break;
    case FurnitureKind::Table: p.drawRect(QRectF(-hw * 0.8, -hd * 0.7, hw * 1.6, hd * 1.4)); break;
    case FurnitureKind::Shelf: for (int i = 1; i < 4; ++i) p.drawLine(QPointF(-hw + i * m.width / 4, -hd), QPointF(-hw + i * m.width / 4, hd)); break;
    case FurnitureKind::Bed: p.drawRect(QRectF(-hw * 0.9, hd - m.depth * 0.25, hw * 1.8, m.depth * 0.18)); p.drawLine(QPointF(-hw, hd - m.depth * 0.3), QPointF(hw, hd - m.depth * 0.3)); break;
    case FurnitureKind::Nightstand: p.drawEllipse(QPointF(0, 0), hw * 0.3, hd * 0.3); break;
    }
    p.restore();
    if (m.width * scale_ > 50) {
        QFont f = p.font(); f.setPointSizeF(8); p.setFont(f); p.setPen(pal.muted);
        p.drawText(QRectF(c.x() - 60, c.y() + m.depth * scale_ / 2 + 2, 120, 14), Qt::AlignHCenter | Qt::AlignTop, m.name);
    }
}

void InteriorView::drawRooms(QPainter& p) {
    const Palette& pal = Theme::current();
    for (const Room& r : rooms_) {
        bool sel = sel_.kind == Selection::Room && sel_.roomKey == Building::roomKey(r.centroid);
        QPolygonF q = poly(r.inner);
        p.setPen(Qt::NoPen);
        p.setBrush(sel ? QColor(pal.selected.red(), pal.selected.green(), pal.selected.blue(), 28) : QColor(pal.accent.red(), pal.accent.green(), pal.accent.blue(), 10));
        p.drawPolygon(q);
        QPointF c = toScreen(r.centroid);
        QFont f(Theme::serifFamily()); f.setPointSizeF(11); p.setFont(f); p.setPen(pal.text);
        QString name = r.name.isEmpty() ? QStringLiteral("Помещение %1").arg(r.id) : r.name;
        p.drawText(QRectF(c.x() - 120, c.y() - 20, 240, 18), Qt::AlignCenter, name);
        QFont g = p.font(); g.setFamily(Theme::sansFamily()); g.setPointSizeF(9); p.setFont(g); p.setPen(pal.muted);
        p.drawText(QRectF(c.x() - 120, c.y(), 240, 16), Qt::AlignCenter, QStringLiteral("%1 м²").arg(r.areaM2, 0, 'f', 2) + (r.floorMaterial.isEmpty() ? QString() : QStringLiteral(" · ") + r.floorMaterial));
    }
}

void InteriorView::paintContent(QPainter& p) {
    const Palette& pal = Theme::current();
    if (!building_ || !floor()) { p.setPen(pal.muted); p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Нет здания — создайте его в панели слева")); return; }
    Floor* f = floor();
    drawRooms(p);
    drawWalls(p);
    std::vector<quint64> bad; for (const Collision& c : collisions_) { bad.push_back(c.a); bad.push_back(c.b); }
    for (const Furniture& m : f->furniture) drawFurniture(p, m, sel_.kind == Selection::Furniture && sel_.id == m.id, false, std::find(bad.begin(), bad.end(), m.id) != bad.end());
    // предпросмотр
    if (hasCursor_) {
        if (tool_ == Tool::Wall && !pts_.empty()) {
            std::vector<Point> prev = pts_; prev.push_back(cursor_);
            Floor tmp; Wall w; w.pts = prev; w.thickness = wallThickness; tmp.walls.push_back(w);
            p.setPen(QPen(pal.accent, 1, Qt::DashLine)); p.setBrush(Qt::NoBrush);
            for (const WallOutline& o : Building::wallOutlines(tmp)) p.drawPolygon(poly(o.polygon));
            p.drawPolyline(poly(prev));
        } else if (tool_ == Tool::Door || tool_ == Tool::Window) {
            double off = 0;
            if (const Wall* w = nearestWall(cursor_, &off, pickRadiusWorld() * 3 + wallThickness)) {
                Opening o; o.kind = tool_ == Tool::Door ? OpeningKind::Door : OpeningKind::Window; o.wall = w->id; o.width = openingWidth;
                o.offset = std::clamp(off, openingWidth / 2, std::max(openingWidth / 2, w->length() - openingWidth / 2));
                drawOpening(p, o, false, true);
            }
        } else if (tool_ == Tool::Furniture) {
            Furniture m = placing_; m.pos = cursor_;
            drawFurniture(p, m, false, true, false);
        } else if (tool_ == Tool::Measure && !pts_.empty()) {
            std::vector<Point> prev = pts_; prev.push_back(cursor_);
            p.setPen(QPen(pal.accent, 1, Qt::DashLine)); p.setBrush(Qt::NoBrush); p.drawPolyline(poly(prev));
            p.setBrush(pal.accent); p.setPen(Qt::NoPen); for (const Point& pt : pts_) p.drawEllipse(toScreen(pt), 3, 3);
        }
        if (tool_ != Tool::Select) {
            QPointF s = toScreen(cursor_);
            p.setPen(QPen(cursorSnapped_ ? pal.accent : pal.crosshair, 1)); p.setBrush(Qt::NoBrush);
            if (cursorSnapped_) p.drawRect(QRectF(s.x() - 5, s.y() - 5, 10, 10)); else { p.drawLine(s + QPointF(-6, 0), s + QPointF(6, 0)); p.drawLine(s + QPointF(0, -6), s + QPointF(0, 6)); }
        }
    }
    // статус
    QFont fnt = p.font(); fnt.setPointSizeF(9); p.setFont(fnt);
    QString st = QStringLiteral("%1 · стен %2 · проёмов %3 · мебели %4 · помещений %5").arg(f->name).arg(f->walls.size()).arg(f->openings.size()).arg(f->furniture.size()).arg(rooms_.size());
    if (!collisions_.empty()) st += QStringLiteral("  ·  коллизий: %1").arg(collisions_.size());
    p.setPen(collisions_.empty() ? pal.muted : QColor(0xC0, 0x39, 0x2B));
    p.drawText(QRectF(10, height() - 26, width() - 20, 20), Qt::AlignLeft | Qt::AlignVCenter, st);
}

} // namespace psi::in
