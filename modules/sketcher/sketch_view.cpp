#include "modules/sketcher/sketch_view.h"
#include "core/units/units.h"
#include "ui/theme.h"
#include <QInputDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace psi::sk {

SketchView::SketchView(QWidget* parent) : CanvasWidget(parent) { scale_ = 2.0; origin_ = QPointF(120, 420); setPrompt(QStringLiteral("Выберите объект или инструмент")); }

QString SketchView::toolName(Tool t) {
    switch (t) {
    case Tool::Select: return QStringLiteral("Выбор");
    case Tool::Point: return QStringLiteral("Точка");
    case Tool::Line: return QStringLiteral("Отрезок");
    case Tool::Polyline: return QStringLiteral("Полилиния");
    case Tool::Rectangle: return QStringLiteral("Прямоугольник");
    case Tool::Circle: return QStringLiteral("Окружность");
    case Tool::Arc: return QStringLiteral("Дуга");
    }
    return {};
}

void SketchView::setSketch(Sketch* s) { sketch_ = s; sel_.clear(); selCon_ = 0; pts_.clear(); drag_.reset(); resolve(); Q_EMIT selectionChanged(); }

void SketchView::setTool(Tool t) {
    tool_ = t; pts_.clear();
    switch (t) {
    case Tool::Select: setPrompt(QStringLiteral("Выберите объект или точку; Shift — добавить к выбору")); break;
    case Tool::Point: setPrompt(QStringLiteral("Точка: укажите положение")); break;
    case Tool::Line: setPrompt(QStringLiteral("Отрезок: первая точка")); break;
    case Tool::Polyline: setPrompt(QStringLiteral("Полилиния: первая точка (Esc — закончить)")); break;
    case Tool::Rectangle: setPrompt(QStringLiteral("Прямоугольник: первый угол")); break;
    case Tool::Circle: setPrompt(QStringLiteral("Окружность: центр")); break;
    case Tool::Arc: setPrompt(QStringLiteral("Дуга: центр")); break;
    }
    Q_EMIT toolChanged(); update();
}

void SketchView::setPrompt(const QString& t) { prompt_ = t; Q_EMIT promptChanged(t); }

std::vector<quint64> SketchView::selectedEntities() const {
    std::vector<quint64> ids;
    for (const Ref& r : sel_) if (std::find(ids.begin(), ids.end(), r.entity) == ids.end()) ids.push_back(r.entity);
    return ids;
}
void SketchView::select(const std::vector<Ref>& refs) { sel_ = refs; Q_EMIT selectionChanged(); update(); }
void SketchView::clearSelection() { if (sel_.empty() && !selCon_) return; sel_.clear(); selCon_ = 0; Q_EMIT selectionChanged(); update(); }
bool SketchView::isSelected(const Ref& r) const { return std::find(sel_.begin(), sel_.end(), r) != sel_.end(); }

void SketchView::escape() {
    if (!pts_.empty()) { pts_.clear(); setTool(tool_); return; }
    if (tool_ != Tool::Select) { setTool(Tool::Select); return; }
    clearSelection();
}

void SketchView::zoomExtents() {
    if (!sketch_ || sketch_->empty()) { zoomTo(Rect(-50, -50, 250, 150)); return; }
    Rect r = sketch_->extents(); r.x0 -= 10; r.y0 -= 10; r.x1 += 10; r.y1 += 10; zoomTo(r);
}

void SketchView::resolve() {
    if (!sketch_) { last_ = {}; update(); Q_EMIT solved(); return; }
    last_ = sketch_->solve(true);
    update();
    Q_EMIT solved();
}

// ---------------------------------------------------------------- picking
Point SketchView::snap(Point raw, bool* snapped) const {
    if (snapped) *snapped = false;
    if (sketch_) {
        double best = pickRadiusWorld(); std::optional<Point> bp;
        for (const SketchEntity& e : sketch_->entities())
            for (int k = 0; k < e.pointCount(); ++k) { Point p = e.point(k); double d = dist(p, raw); if (d < best) { best = d; bp = p; } }
        if (bp) { if (snapped) *snapped = true; return *bp; }
    }
    if (gridSnap_) {
        Point g = snapToGrid(raw);
        if (dist(g, raw) < pickRadiusWorld() * 1.5) { if (snapped) *snapped = true; return g; }
    }
    return raw;
}

std::optional<Ref> SketchView::hitPoint(Point w) const {
    if (!sketch_) return std::nullopt;
    double best = pickRadiusWorld(); std::optional<Ref> br;
    for (const SketchEntity& e : sketch_->entities())
        for (int k = 0; k < e.pointCount(); ++k) { double d = dist(e.point(k), w); if (d < best) { best = d; br = Ref{e.id, e.kind == Kind::Point ? -1 : k}; } }
    return br;
}

static double entityDistance(const SketchEntity& e, Point w) {
    switch (e.kind) {
    case Kind::Point: return dist(e.point(0), w);
    case Kind::Line: {
        Point a = e.point(0), b = e.point(1); Vec d = b - a; double l2 = d.dot(d);
        double t = l2 < 1e-12 ? 0 : std::clamp((w - a).dot(d) / l2, 0.0, 1.0);
        return dist(a + d * t, w);
    }
    case Kind::Circle: return std::abs(dist(e.center(), w) - e.p[2]);
    case Kind::Arc: {
        double ang = std::atan2(w.y - e.p[1], w.x - e.p[0]);
        double a0 = e.p[3], a1 = e.p[4]; double span = a1 - a0; while (span < 0) span += 2 * kPi;
        double t = ang - a0; while (t < 0) t += 2 * kPi;
        if (t <= span) return std::abs(dist(e.center(), w) - e.p[2]);
        return std::min(dist(e.point(1), w), dist(e.point(2), w));
    }
    }
    return 1e300;
}

std::optional<quint64> SketchView::hitEntity(Point w) const {
    if (!sketch_) return std::nullopt;
    double best = pickRadiusWorld(); std::optional<quint64> id;
    for (const SketchEntity& e : sketch_->entities()) { double d = entityDistance(e, w); if (d < best) { best = d; id = e.id; } }
    return id;
}

QPointF SketchView::labelPos(const Constraint& c) const {
    Point base;
    if (c.refs.size() >= 2) base = mid(sketch_->pointOf(c.refs[0]), sketch_->pointOf(c.refs[1]));
    else if (!c.refs.empty()) {
        const SketchEntity* e = sketch_->entity(c.refs[0].entity);
        if (e && (e->kind == Kind::Circle || e->kind == Kind::Arc)) base = e->center() + Vec{e->p[2] * 0.7071, e->p[2] * 0.7071};
        else base = sketch_->pointOf(c.refs[0]);
    }
    QPointF s = toScreen(base);
    return s + QPointF(c.label.x, -c.label.y) + QPointF(0, -14);
}

std::optional<quint64> SketchView::hitLabel(QPointF s) const {
    if (!sketch_) return std::nullopt;
    for (const Constraint& c : sketch_->constraints()) {
        if (!c.hasValue()) continue;
        QPointF p = labelPos(c);
        QRectF r(p.x() - 40, p.y() - 10, 80, 20);
        if (r.contains(s)) return c.id;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------- editing
void SketchView::finishEntity(SketchEntity e) {
    if (!sketch_) return;
    Q_EMIT aboutToChange(QStringLiteral("Эскиз: %1").arg(kindKey(e.kind) == "line" ? QStringLiteral("отрезок") : kindKey(e.kind) == "circle" ? QStringLiteral("окружность") : kindKey(e.kind) == "arc" ? QStringLiteral("дуга") : QStringLiteral("точка")));
    quint64 id = sketch_->addEntity(std::move(e));
    if (auto_) sketch_->autoConstrain(id, pickRadiusWorld());
    resolve();
    Q_EMIT changed();
}

quint64 SketchView::addConstraint(CType t) {
    if (!sketch_) return 0;
    auto ent = [&](const Ref& r) { return sketch_->entity(r.entity); };
    auto isLine = [&](const Ref& r) { auto* e = ent(r); return e && e->kind == Kind::Line && r.point < 0; };
    auto isRound = [&](const Ref& r) { auto* e = ent(r); return e && (e->kind == Kind::Circle || e->kind == Kind::Arc) && r.point < 0; };
    auto isPt = [&](const Ref& r) { auto* e = ent(r); return e && (r.point >= 0 || e->kind == Kind::Point); };
    std::vector<Ref> pts, lines, rounds;
    for (const Ref& r : sel_) { if (isPt(r)) pts.push_back(r); else if (isLine(r)) lines.push_back(r); else if (isRound(r)) rounds.push_back(r); }
    QString err;
    std::vector<Constraint> pending;
    auto push = [&](CType type, std::vector<Ref> refs, double value = 0, Point at = {}) {
        Constraint k; k.type = type; k.refs = std::move(refs); k.value = value; k.fixedAt = at; pending.push_back(k);
    };
    auto lineLen = [&](const Ref& l) { auto* e = ent(l); return dist(e->point(0), e->point(1)); };
    auto ptLine = [&](const Ref& pt, const Ref& l) { auto* e = ent(l); Point a = e->point(0), b = e->point(1); return std::abs((b - a).normalized().cross(sketch_->pointOf(pt) - a)); };
    switch (t) {
    case CType::Coincident: if (pts.size() == 2) push(t, {pts[0], pts[1]}); else err = QStringLiteral("Совпадение: выберите две точки"); break;
    case CType::Horizontal: case CType::Vertical:
        if (!lines.empty()) for (const Ref& l : lines) push(t, {l});
        else if (pts.size() == 2) push(t, {pts[0], pts[1]});
        else err = QStringLiteral("%1: выберите отрезки или две точки").arg(constraintTypeName(t));
        break;
    case CType::Parallel: case CType::Perpendicular: case CType::Angle:
        if (lines.size() == 2) {
            double v = 0;
            if (t == CType::Angle) { auto* a = ent(lines[0]); auto* b = ent(lines[1]); Vec u = a->point(1) - a->point(0), w = b->point(1) - b->point(0); v = std::atan2(u.cross(w), u.dot(w)); }
            push(t, {lines[0], lines[1]}, v);
        } else err = QStringLiteral("%1: выберите два отрезка").arg(constraintTypeName(t));
        break;
    case CType::Tangent:
        if (lines.size() == 1 && rounds.size() == 1) push(t, {lines[0], rounds[0]});
        else if (rounds.size() == 2) push(t, {rounds[0], rounds[1]});
        else err = QStringLiteral("Касание: отрезок и окружность или две окружности");
        break;
    case CType::Equal:
        if (lines.size() >= 2) for (size_t i = 1; i < lines.size(); ++i) push(t, {lines[0], lines[i]});
        else if (rounds.size() >= 2) for (size_t i = 1; i < rounds.size(); ++i) push(t, {rounds[0], rounds[i]});
        else err = QStringLiteral("Равенство: два отрезка или две окружности");
        break;
    case CType::Fixed:
        if (!pts.empty()) for (const Ref& p : pts) push(t, {p}, 0, sketch_->pointOf(p));
        else err = QStringLiteral("Закрепить: выберите точки");
        break;
    case CType::Distance:
        if (pts.size() == 2) push(t, {pts[0], pts[1]}, dist(sketch_->pointOf(pts[0]), sketch_->pointOf(pts[1])));
        else if (pts.size() == 1 && lines.size() == 1) push(t, {pts[0], lines[0]}, ptLine(pts[0], lines[0]));
        else err = QStringLiteral("Расстояние: две точки или точка и отрезок");
        break;
    case CType::Length:   // «размер» по выбору: длина отрезка, радиус окружности, расстояние между точками
        if (!lines.empty()) for (const Ref& l : lines) push(t, {l}, lineLen(l));
        else if (rounds.size() == 1) push(CType::Radius, {rounds[0]}, ent(rounds[0])->p[2]);
        else if (pts.size() == 2) push(CType::Distance, {pts[0], pts[1]}, dist(sketch_->pointOf(pts[0]), sketch_->pointOf(pts[1])));
        else if (pts.size() == 1 && lines.size() == 1) push(CType::Distance, {pts[0], lines[0]}, ptLine(pts[0], lines[0]));
        else err = QStringLiteral("Размер: выберите отрезок, окружность или две точки");
        break;
    case CType::Radius: case CType::Diameter:
        if (!rounds.empty()) for (const Ref& r : rounds) push(t, {r}, ent(r)->p[2] * (t == CType::Diameter ? 2 : 1));
        else err = QStringLiteral("%1: выберите окружность или дугу").arg(constraintTypeName(t));
        break;
    case CType::PointOnLine: if (pts.size() == 1 && lines.size() == 1) push(t, {pts[0], lines[0]}); else err = QStringLiteral("Точка на линии: точка и отрезок"); break;
    case CType::PointOnCircle: if (pts.size() == 1 && rounds.size() == 1) push(t, {pts[0], rounds[0]}); else err = QStringLiteral("Точка на окружности: точка и окружность"); break;
    case CType::Symmetric: if (pts.size() == 2 && lines.size() == 1) push(t, {pts[0], pts[1], lines[0]}); else err = QStringLiteral("Симметрия: две точки и ось"); break;
    }
    if (!err.isEmpty()) { setPrompt(err); return 0; }
    if (pending.empty()) return 0;
    Q_EMIT aboutToChange(QStringLiteral("Ограничение: %1").arg(constraintTypeName(pending[0].type)));
    quint64 lastId = 0;
    for (const Constraint& k : pending) lastId = sketch_->addConstraint(k);
    resolve();
    if (!last_.converged) setPrompt(QStringLiteral("Ограничение конфликтует с существующими (выделено красным)"));
    else setPrompt(QStringLiteral("Добавлено: %1").arg(sketch_->describe(*sketch_->constraint(lastId))));
    selCon_ = lastId;
    Q_EMIT changed();
    return lastId;
}

void SketchView::deleteSelection() {
    if (!sketch_) return;
    if (selCon_ && sel_.empty()) {
        Q_EMIT aboutToChange(QStringLiteral("Удалить ограничение"));
        sketch_->removeConstraint(selCon_); selCon_ = 0;
        resolve(); Q_EMIT changed(); return;
    }
    auto ids = selectedEntities();
    if (ids.empty()) return;
    Q_EMIT aboutToChange(QStringLiteral("Удалить объекты эскиза"));
    for (quint64 id : ids) sketch_->removeEntity(id);
    sel_.clear(); selCon_ = 0;
    resolve(); Q_EMIT selectionChanged(); Q_EMIT changed();
}

bool SketchView::editConstraintValue(quint64 id) {
    if (!sketch_) return false;
    Constraint* c = sketch_->constraint(id);
    if (!c || !c->hasValue()) return false;
    bool ok = false;
    QString cur = c->type == CType::Angle ? QString::number(deg(c->value), 'f', 2) : Units::format(c->value, Unit::Millimeter, 3, false);
    QString text = QInputDialog::getText(this, constraintTypeName(c->type), c->type == CType::Angle ? QStringLiteral("Угол, °:") : QStringLiteral("Значение, мм:"), QLineEdit::Normal, cur, &ok);
    if (!ok) return false;
    double v;
    if (c->type == CType::Angle) { v = rad(text.toDouble(&ok)); if (!ok) return false; }
    else { auto p = Units::parse(text, Unit::Millimeter); if (!p) return false; v = *p; }
    Q_EMIT aboutToChange(QStringLiteral("Размер %1").arg(constraintTypeName(c->type)));
    sketch_->setValue(id, v);
    resolve();
    Q_EMIT changed();
    return true;
}

// ---------------------------------------------------------------- mouse
void SketchView::canvasPress(QMouseEvent* ev) {
    if (!sketch_) return;
    if (ev->button() == Qt::RightButton) { escape(); return; }
    if (ev->button() != Qt::LeftButton) return;
    Point raw = toWorld(ev->position());
    if (tool_ == Tool::Select) {
        if (auto lbl = hitLabel(ev->position())) {
            selCon_ = *lbl; sel_.clear(); dragLabel_ = lbl; dragLabelStart_ = raw; labelOrig_ = sketch_->constraint(*lbl)->label;
            Q_EMIT selectionChanged(); update(); return;
        }
        auto hp = hitPoint(raw);
        std::optional<Ref> hit = hp;
        if (!hit) { if (auto he = hitEntity(raw)) hit = Ref{*he, -1}; }
        bool shift = ev->modifiers() & Qt::ShiftModifier;
        if (!hit) { if (!shift) clearSelection(); return; }
        selCon_ = 0;
        if (shift) { if (isSelected(*hit)) sel_.erase(std::find(sel_.begin(), sel_.end(), *hit)); else sel_.push_back(*hit); }
        else if (!isSelected(*hit)) sel_ = {*hit};
        drag_ = hit; dragStart_ = raw; dragging_ = false;
        Q_EMIT selectionChanged(); update();
        return;
    }
    bool snapped; Point p = snap(raw, &snapped);
    switch (tool_) {
    case Tool::Point: { SketchEntity e; e.kind = Kind::Point; e.p = {p.x, p.y}; finishEntity(e); break; }
    case Tool::Line:
        if (pts_.empty()) { pts_ = {p}; setPrompt(QStringLiteral("Отрезок: вторая точка")); }
        else { SketchEntity e; e.kind = Kind::Line; e.p = {pts_[0].x, pts_[0].y, p.x, p.y}; pts_.clear(); finishEntity(e); setPrompt(QStringLiteral("Отрезок: первая точка")); }
        break;
    case Tool::Polyline:
        if (!pts_.empty()) { SketchEntity e; e.kind = Kind::Line; e.p = {pts_.back().x, pts_.back().y, p.x, p.y}; finishEntity(e); }
        pts_.push_back(p); setPrompt(QStringLiteral("Полилиния: следующая точка (Esc — закончить)"));
        break;
    case Tool::Rectangle:
        if (pts_.empty()) { pts_ = {p}; setPrompt(QStringLiteral("Прямоугольник: противоположный угол")); }
        else {
            Point a = pts_[0]; pts_.clear();
            if (std::abs(a.x - p.x) < 1e-9 || std::abs(a.y - p.y) < 1e-9) { setPrompt(QStringLiteral("Прямоугольник вырожден")); break; }
            Q_EMIT aboutToChange(QStringLiteral("Эскиз: прямоугольник"));
            Point c1{p.x, a.y}, c2{a.x, p.y};
            auto mk = [&](Point s, Point t) { SketchEntity e; e.kind = Kind::Line; e.p = {s.x, s.y, t.x, t.y}; return sketch_->addEntity(e); };
            quint64 l1 = mk(a, c1), l2 = mk(c1, p), l3 = mk(p, c2), l4 = mk(c2, a);
            auto con = [&](CType t, std::vector<Ref> refs) { Constraint k; k.type = t; k.refs = std::move(refs); sketch_->addConstraint(k); };
            con(CType::Coincident, {{l1, 1}, {l2, 0}}); con(CType::Coincident, {{l2, 1}, {l3, 0}}); con(CType::Coincident, {{l3, 1}, {l4, 0}}); con(CType::Coincident, {{l4, 1}, {l1, 0}});
            con(CType::Horizontal, {{l1, -1}}); con(CType::Horizontal, {{l3, -1}}); con(CType::Vertical, {{l2, -1}}); con(CType::Vertical, {{l4, -1}});
            if (auto_) sketch_->autoConstrain(l1, pickRadiusWorld());
            resolve(); Q_EMIT changed();
            setPrompt(QStringLiteral("Прямоугольник: первый угол"));
        }
        break;
    case Tool::Circle:
        if (pts_.empty()) { pts_ = {p}; setPrompt(QStringLiteral("Окружность: точка на окружности")); }
        else { double r = dist(pts_[0], p); if (r > 1e-9) { SketchEntity e; e.kind = Kind::Circle; e.p = {pts_[0].x, pts_[0].y, r}; finishEntity(e); } pts_.clear(); setPrompt(QStringLiteral("Окружность: центр")); }
        break;
    case Tool::Arc:
        if (pts_.empty()) { pts_ = {p}; setPrompt(QStringLiteral("Дуга: начальная точка")); }
        else if (pts_.size() == 1) { pts_.push_back(p); setPrompt(QStringLiteral("Дуга: конечная точка (против часовой)")); }
        else {
            Point c = pts_[0]; double r = dist(c, pts_[1]);
            if (r > 1e-9) { SketchEntity e; e.kind = Kind::Arc; e.p = {c.x, c.y, r, std::atan2(pts_[1].y - c.y, pts_[1].x - c.x), std::atan2(p.y - c.y, p.x - c.x)}; finishEntity(e); }
            pts_.clear(); setPrompt(QStringLiteral("Дуга: центр"));
        }
        break;
    case Tool::Select: break;
    }
    update();
}

void SketchView::canvasMove(QMouseEvent* ev) {
    if (!sketch_) return;
    Point raw = toWorld(ev->position());
    if (dragLabel_) { Constraint* c = sketch_->constraint(*dragLabel_); if (c) { QPointF d = ev->position() - toScreen(dragLabelStart_); c->label = labelOrig_ + Point{d.x(), -d.y()}; update(); } return; }
    if (drag_ && (ev->buttons() & Qt::LeftButton)) {
        if (!dragging_) {
            if (dist(raw, dragStart_) * scale_ < 4) return;
            dragging_ = true;
            Q_EMIT aboutToChange(QStringLiteral("Перетащить"));
        }
        bool snapped; Point p = drag_->point >= 0 || sketch_->entity(drag_->entity)->kind == Kind::Point ? snap(raw, &snapped) : raw;
        if (drag_->point < 0 && sketch_->entity(drag_->entity)->kind != Kind::Point) {
            // перенос целого объекта вместе с остальными выбранными
            Vec d = p - dragStart_;
            for (quint64 id : selectedEntities()) sketch_->moveEntity(id, d);
            dragStart_ = p;
            last_ = sketch_->solve(false);
        } else sketch_->dragPoint(*drag_, p);
        last_.dof = -1;
        update();
        return;
    }
    cursor_ = snap(raw, &cursorSnapped_); hasCursor_ = true;
    if (tool_ != Tool::Select || !pts_.empty()) update();
}

void SketchView::canvasRelease(QMouseEvent* ev) {
    if (ev->button() != Qt::LeftButton) return;
    if (dragLabel_) { dragLabel_.reset(); return; }
    if (drag_) {
        if (dragging_) { dragging_ = false; resolve(); Q_EMIT changed(); }
        drag_.reset();
    }
}

void SketchView::canvasDoubleClick(QMouseEvent* ev) {
    if (!sketch_ || ev->button() != Qt::LeftButton) return;
    if (auto lbl = hitLabel(ev->position())) { drag_.reset(); dragLabel_.reset(); editConstraintValue(*lbl); return; }
    if (tool_ == Tool::Polyline && !pts_.empty()) { pts_.clear(); setPrompt(QStringLiteral("Полилиния: первая точка")); update(); }
}

void SketchView::keyPressEvent(QKeyEvent* ev) {
    switch (ev->key()) {
    case Qt::Key_Escape: escape(); return;
    case Qt::Key_Delete: case Qt::Key_Backspace: deleteSelection(); return;
    case Qt::Key_H: if (ev->modifiers() == Qt::NoModifier) { addConstraint(CType::Horizontal); return; } break;
    case Qt::Key_V: if (ev->modifiers() == Qt::NoModifier) { addConstraint(CType::Vertical); return; } break;
    case Qt::Key_L: if (ev->modifiers() == Qt::NoModifier) { addConstraint(CType::Length); return; } break;
    case Qt::Key_T: if (ev->modifiers() == Qt::NoModifier) { addConstraint(CType::Tangent); return; } break;
    case Qt::Key_Equal: addConstraint(CType::Equal); return;
    case Qt::Key_D: if (ev->modifiers() == Qt::NoModifier) { addConstraint(CType::Distance); return; } break;
    case Qt::Key_C: if (ev->modifiers() == Qt::NoModifier) { addConstraint(CType::Coincident); return; } break;
    case Qt::Key_P: if (ev->modifiers() == Qt::NoModifier) { addConstraint(CType::Parallel); return; } break;
    case Qt::Key_N: if (ev->modifiers() == Qt::NoModifier) { addConstraint(CType::Perpendicular); return; } break;
    case Qt::Key_F: if (ev->modifiers() == Qt::NoModifier) { addConstraint(CType::Fixed); return; } break;
    default: break;
    }
    CanvasWidget::keyPressEvent(ev);
}

// ---------------------------------------------------------------- painting
void SketchView::drawEntity(QPainter& p, const SketchEntity& e, bool selected, bool free) {
    const Palette& pal = Theme::current();
    QColor col = selected ? pal.selected : free ? pal.accent : pal.text;
    QPen pen(col, selected ? 2.5 : 1.6);
    if (e.construction) { pen.setStyle(Qt::DashLine); pen.setWidthF(1.0); pen.setColor(selected ? pal.selected : pal.muted); }
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    switch (e.kind) {
    case Kind::Point: break;
    case Kind::Line: p.drawLine(toScreen(e.point(0)), toScreen(e.point(1))); break;
    case Kind::Circle: { QPointF c = toScreen(e.center()); double r = e.p[2] * scale_; p.drawEllipse(c, r, r); break; }
    case Kind::Arc: {
        QPointF c = toScreen(e.center()); double r = e.p[2] * scale_;
        double a0 = deg(e.p[3]), span = deg(e.p[4] - e.p[3]); while (span < 0) span += 360;
        p.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), int(a0 * 16), int(span * 16));
        break;
    }
    }
    // характерные точки
    for (int k = 0; k < e.pointCount(); ++k) {
        bool ps = isSelected({e.id, e.kind == Kind::Point ? -1 : k});
        QPointF s = toScreen(e.point(k));
        p.setPen(QPen(ps ? pal.selected : col, 1));
        p.setBrush(ps ? pal.selected : (free ? pal.canvas : col));
        double r = e.kind == Kind::Point ? 3.5 : (k == 0 && e.kind != Kind::Line ? 2.5 : 3);
        p.drawEllipse(s, r, r);
    }
}

void SketchView::drawConstraints(QPainter& p) {
    const Palette& pal = Theme::current();
    QFont f = p.font(); f.setPointSizeF(9); p.setFont(f);
    QColor red(0xC0, 0x39, 0x2B);
    for (const Constraint& c : sketch_->constraints()) {
        bool sel = c.id == selCon_;
        QColor col = c.conflict ? red : sel ? pal.selected : pal.muted;
        if (c.hasValue()) {
            QString txt = c.type == CType::Angle ? QStringLiteral("%1°").arg(deg(c.value), 0, 'f', 1) : Units::format(c.value, Unit::Millimeter, 2, false);
            if (c.type == CType::Radius) txt = QStringLiteral("R") + txt; else if (c.type == CType::Diameter) txt = QStringLiteral("⌀") + txt;
            QPointF at = labelPos(c);
            QRectF box(at.x() - 40, at.y() - 10, 80, 20);
            // выноска от объекта к подписи
            QPointF anchor;
            if (c.refs.size() >= 2) { QPointF a = toScreen(sketch_->pointOf(c.refs[0])), b = toScreen(sketch_->pointOf(c.refs[1])); anchor = (a + b) / 2; p.setPen(QPen(col, 0.8, Qt::DotLine)); p.drawLine(a, b); }
            else anchor = toScreen(sketch_->pointOf(c.refs[0]));
            p.setPen(QPen(col, 0.8, Qt::DotLine)); p.drawLine(anchor, at);
            p.setPen(QPen(col, sel ? 1.5 : 0.8)); p.setBrush(sel ? QColor(pal.selected.red(), pal.selected.green(), pal.selected.blue(), 30) : pal.canvas);
            p.drawRoundedRect(box, 4, 4);
            p.setPen(c.conflict ? red : pal.text); p.drawText(box, Qt::AlignCenter, txt);
        } else {
            Point w;
            if (c.refs.size() >= 2) w = mid(sketch_->pointOf(c.refs[0]), sketch_->pointOf(c.refs[1]));
            else if (!c.refs.empty()) w = sketch_->pointOf(c.refs[0]);
            QPointF s = toScreen(w) + QPointF(8, -8);
            QString g = constraintGlyph(c.type);
            QRectF box(s.x() - 7, s.y() - 7, 14, 14);
            p.setPen(QPen(col, sel ? 1.5 : 0.8)); p.setBrush(pal.canvas); p.drawRoundedRect(box, 3, 3);
            p.setPen(col); p.drawText(box, Qt::AlignCenter, g);
        }
    }
}

void SketchView::paintContent(QPainter& p) {
    const Palette& pal = Theme::current();
    if (!sketch_) {
        p.setPen(pal.muted); p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Нет эскиза — создайте его в панели слева"));
        return;
    }
    const std::vector<bool>& free = sketch_->freeEntities();
    const auto& ents = sketch_->entities();
    for (size_t i = 0; i < ents.size(); ++i) drawEntity(p, ents[i], isSelected({ents[i].id, -1}), i < free.size() ? free[i] : true);
    drawConstraints(p);
    // резинка
    if (hasCursor_ && !pts_.empty()) {
        p.setPen(QPen(pal.accent, 1, Qt::DashLine)); p.setBrush(Qt::NoBrush);
        QPointF a = toScreen(pts_.back()), b = toScreen(cursor_);
        switch (tool_) {
        case Tool::Line: case Tool::Polyline: p.drawLine(a, b); break;
        case Tool::Rectangle: p.drawRect(QRectF(a, b).normalized()); break;
        case Tool::Circle: { double r = dist(pts_[0], cursor_) * scale_; p.drawEllipse(toScreen(pts_[0]), r, r); break; }
        case Tool::Arc: {
            QPointF c = toScreen(pts_[0]);
            if (pts_.size() == 1) p.drawLine(c, b);
            else { double r = dist(pts_[0], pts_[1]) * scale_; double a0 = deg(std::atan2(pts_[1].y - pts_[0].y, pts_[1].x - pts_[0].x)); double a1 = deg(std::atan2(cursor_.y - pts_[0].y, cursor_.x - pts_[0].x)); double span = a1 - a0; while (span < 0) span += 360; p.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), int(a0 * 16), int(span * 16)); }
            break;
        }
        default: break;
        }
    }
    if (hasCursor_ && tool_ != Tool::Select) {
        QPointF s = toScreen(cursor_);
        p.setPen(QPen(cursorSnapped_ ? pal.accent : pal.crosshair, 1)); p.setBrush(Qt::NoBrush);
        if (cursorSnapped_) p.drawRect(QRectF(s.x() - 5, s.y() - 5, 10, 10)); else { p.drawLine(s + QPointF(-6, 0), s + QPointF(6, 0)); p.drawLine(s + QPointF(0, -6), s + QPointF(0, 6)); }
    }
    // статус в углу
    QFont f = p.font(); f.setPointSizeF(9); p.setFont(f);
    QString st = last_.dof >= 0 ? QStringLiteral("Свободных степеней: %1").arg(last_.dof) : QStringLiteral("…");
    if (!last_.converged && !last_.conflicts.empty()) st += QStringLiteral("  ·  конфликт ограничений");
    p.setPen(last_.conflicts.empty() ? pal.muted : QColor(0xC0, 0x39, 0x2B));
    p.drawText(QRectF(10, height() - 26, width() - 20, 20), Qt::AlignLeft | Qt::AlignVCenter, st);
}

} // namespace psi::sk
