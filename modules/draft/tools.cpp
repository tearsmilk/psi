#include "modules/draft/input.h"
#include "modules/draft/tool.h"
#include "core/geometry/ops.h"
#include "modules/draft/boolean.h"
#include "rendering/viewport.h"
#include <QStringList>
#include <cmath>

namespace psi {

// ---------------------------------------------------------------- base helpers
Entity ToolContext::styled(Entity e) const {
    Document& d = doc();
    e.layer = d.currentLayer();
    if (e.type == EntityType::Text || e.type == EntityType::Leader || e.type == EntityType::Dimension)
        if (std::abs(e.textHeight - 3.5) < 1e-9) e.textHeight = d.settings().textHeight;
    return e;
}

bool Tool::input(const QString& text) {
    auto p = InputParser::point(text, ctx_.doc().settings().unit, lastPoint(), cursor_);
    if (!p) return false;
    click(*p, false);
    return true;
}

void Tool::ghost(QPainter& p, const RenderOptions& o, const Entity& e) const {
    RenderOptions g = o; g.selected = true;
    Renderer::drawEntity(p, ctx_.doc(), e, g);
}

namespace {

QString fmt(const ToolContext& c, double mm) { return Units::format(mm, c.doc().settings().unit, c.doc().settings().precision, true); }

void commitAdd(const ToolContext& ctx, const QString& title, const std::vector<Entity>& es) {
    auto c = std::make_unique<EntityCommand>(title);
    for (const Entity& e : es) c->add(ctx.styled(e));
    ctx.commit(std::move(c));
}

std::optional<quint64> pick(const ToolContext& ctx, Point p) {
    if (!ctx.view) {   // headless: nearest entity within 2 mm
        std::optional<quint64> best; double bd = 2.0;
        for (quint64 id : ctx.doc().query(Rect(p.x - 2, p.y - 2, p.x + 2, p.y + 2))) {
            const Entity* e = ctx.doc().entity(id);
            if (!e) continue;
            double d = hitDistance(*e, p);
            if (d <= bd) { bd = d; best = id; }
        }
        return best;
    }
    return ctx.view->hitTest(ctx.view->toScreen(p));
}

// ---------------------------------------------------------------- Select
class SelectTool : public Tool {
public:
    using Tool::Tool;
    QString id() const override { return "select"; }
    QString title() const override { return QStringLiteral("Выбор"); }
    void begin() override { setPrompt(QStringLiteral("Выберите объекты (рамка слева направо — внутри, справа налево — пересечение)")); }
    void click(Point, bool) override {}
    bool selecting() const override { return true; }
    void cancel() override { if (ctx_.selection) ctx_.selection->clear(); }
    bool keepAfterFinish() const override { return true; }
};

// ---------------------------------------------------------------- Line
class LineTool : public Tool {
public:
    using Tool::Tool;
    QString id() const override { return "line"; }
    QString title() const override { return QStringLiteral("Линия"); }
    void begin() override { pts_.clear(); setPrompt(QStringLiteral("Линия — укажите первую точку:")); }
    void click(Point p, bool) override {
        if (!pts_.empty()) commitAdd(ctx_, title(), {makeLine(pts_.back(), p)});
        pts_.push_back(p);
        setPrompt(QStringLiteral("Следующая точка (Enter — завершить, «c» — замкнуть):"));
    }
    bool input(const QString& t) override {
        if (t.trimmed().toLower() == "c" || t.trimmed().toLower() == "з") { if (pts_.size() >= 3) click(pts_.front(), false); enter(); return true; }
        return Tool::input(t);
    }
    void enter() override { begin(); }
    void escape() override { if (pts_.empty()) finish(); else begin(); }
    std::optional<Point> lastPoint() const override { return pts_.empty() ? std::nullopt : std::optional<Point>(pts_.back()); }
    void draw(QPainter& p, const RenderOptions& o) override { if (!pts_.empty() && cursor_) ghost(p, o, makeLine(pts_.back(), *cursor_)); }
private:
    std::vector<Point> pts_;
};

// ---------------------------------------------------------------- Polyline / Spline
class PolyTool : public Tool {
public:
    PolyTool(ToolContext c, bool spline) : Tool(std::move(c)), spline_(spline) {}
    QString id() const override { return spline_ ? "spline" : "polyline"; }
    QString title() const override { return spline_ ? QStringLiteral("Сплайн") : QStringLiteral("Полилиния"); }
    void begin() override { pts_.clear(); setPrompt(title() + QStringLiteral(" — укажите первую точку:")); }
    void click(Point p, bool) override { pts_.push_back(p); setPrompt(QStringLiteral("Следующая точка (Enter — завершить, «c» — замкнуть):")); }
    bool input(const QString& t) override {
        QString s = t.trimmed().toLower();
        if (s == "c" || s == "з") { done(true); return true; }
        return Tool::input(t);
    }
    void enter() override { done(false); }
    void escape() override { if (pts_.empty()) finish(); else begin(); }
    std::optional<Point> lastPoint() const override { return pts_.empty() ? std::nullopt : std::optional<Point>(pts_.back()); }
    void draw(QPainter& p, const RenderOptions& o) override {
        if (pts_.empty()) return;
        std::vector<Point> all = pts_;
        if (cursor_) all.push_back(*cursor_);
        ghost(p, o, spline_ ? makeSpline(all) : makePolyline(all, false));
    }
private:
    void done(bool closed) {
        if (pts_.size() >= 2) commitAdd(ctx_, title(), {spline_ ? makeSpline(pts_, closed && pts_.size() >= 3) : makePolyline(pts_, closed && pts_.size() >= 3)});
        begin();
    }
    bool spline_;
    std::vector<Point> pts_;
};

// ---------------------------------------------------------------- Rectangle
class RectTool : public Tool {
public:
    using Tool::Tool;
    QString id() const override { return "rect"; }
    QString title() const override { return QStringLiteral("Прямоугольник"); }
    void begin() override { a_.reset(); setPrompt(QStringLiteral("Прямоугольник — первый угол:")); }
    void click(Point p, bool) override {
        if (!a_) { a_ = p; setPrompt(QStringLiteral("Второй угол (или ширина×высота, например 800x400):")); return; }
        commitAdd(ctx_, title(), {makeRectangle(*a_, p)});
        begin();
    }
    bool input(const QString& t) override {
        QString s = t.trimmed().toLower();
        if (a_ && (s.contains('x') || s.contains(QStringLiteral("х")) || s.contains('*'))) {
            QStringList parts = s.split(QRegularExpression("[x*х]"));
            if (parts.size() == 2) {
                auto w = Units::parse(parts[0], ctx_.doc().settings().unit), h = Units::parse(parts[1], ctx_.doc().settings().unit);
                if (w && h) { click(*a_ + Point{*w, *h}, false); return true; }
            }
            return false;
        }
        return Tool::input(t);
    }
    void escape() override { if (!a_) finish(); else begin(); }
    std::optional<Point> lastPoint() const override { return a_; }
    void draw(QPainter& p, const RenderOptions& o) override { if (a_ && cursor_) ghost(p, o, makeRectangle(*a_, *cursor_)); }
private:
    std::optional<Point> a_;
};

// ---------------------------------------------------------------- Circle
class CircleTool : public Tool {
public:
    using Tool::Tool;
    QString id() const override { return "circle"; }
    QString title() const override { return QStringLiteral("Окружность"); }
    void begin() override { pts_.clear(); mode_ = 0; setPrompt(QStringLiteral("Окружность — центр (или «2p» / «3p»):")); }
    void click(Point p, bool) override {
        pts_.push_back(p);
        if (mode_ == 0) {
            if (pts_.size() == 1) setPrompt(QStringLiteral("Радиус (число или точка на окружности):"));
            else { commitAdd(ctx_, title(), {makeCircle(pts_[0], dist(pts_[0], pts_[1]))}); begin(); }
        } else if (mode_ == 1) {
            if (pts_.size() == 1) setPrompt(QStringLiteral("Вторая точка диаметра:"));
            else { commitAdd(ctx_, title(), {makeCircle(mid(pts_[0], pts_[1]), dist(pts_[0], pts_[1]) / 2)}); begin(); }
        } else {
            if (pts_.size() < 3) setPrompt(QStringLiteral("Следующая точка на окружности:"));
            else { if (auto cr = circleFrom3Points(pts_[0], pts_[1], pts_[2])) commitAdd(ctx_, title(), {makeCircle(cr->first, cr->second)}); begin(); }
        }
    }
    bool input(const QString& t) override {
        QString s = t.trimmed().toLower();
        if (s == "2p" || s == "2т") { mode_ = 1; pts_.clear(); setPrompt(QStringLiteral("Первая точка диаметра:")); return true; }
        if (s == "3p" || s == "3т") { mode_ = 2; pts_.clear(); setPrompt(QStringLiteral("Первая точка на окружности:")); return true; }
        if (mode_ == 0 && pts_.size() == 1) {
            if (auto r = Units::parse(s, ctx_.doc().settings().unit); r && *r > 0) { click(pts_[0] + Point{*r, 0}, false); return true; }
        }
        return Tool::input(t);
    }
    void escape() override { if (pts_.empty()) finish(); else begin(); }
    std::optional<Point> lastPoint() const override { return pts_.empty() ? std::nullopt : std::optional<Point>(pts_.back()); }
    void draw(QPainter& p, const RenderOptions& o) override {
        if (!cursor_ || pts_.empty()) return;
        if (mode_ == 0) ghost(p, o, makeCircle(pts_[0], dist(pts_[0], *cursor_)));
        else if (mode_ == 1) ghost(p, o, makeCircle(mid(pts_[0], *cursor_), dist(pts_[0], *cursor_) / 2));
        else if (pts_.size() == 2) { if (auto cr = circleFrom3Points(pts_[0], pts_[1], *cursor_)) ghost(p, o, makeCircle(cr->first, cr->second)); }
        else ghost(p, o, makeLine(pts_[0], *cursor_));
    }
private:
    std::vector<Point> pts_;
    int mode_ = 0;
};

// ---------------------------------------------------------------- Arc
class ArcTool : public Tool {
public:
    using Tool::Tool;
    QString id() const override { return "arc"; }
    QString title() const override { return QStringLiteral("Дуга"); }
    void begin() override { pts_.clear(); center_ = false; setPrompt(QStringLiteral("Дуга — начальная точка (или «c» — центр, начало, конец):")); }
    void click(Point p, bool) override {
        pts_.push_back(p);
        if (pts_.size() == 1) setPrompt(center_ ? QStringLiteral("Начальная точка:") : QStringLiteral("Вторая точка дуги:"));
        else if (pts_.size() == 2) setPrompt(QStringLiteral("Конечная точка:"));
        else {
            if (center_) { double r = dist(pts_[0], pts_[1]); commitAdd(ctx_, title(), {makeArc(pts_[0], r, (pts_[1] - pts_[0]).angle(), (pts_[2] - pts_[0]).angle())}); }
            else commitAdd(ctx_, title(), {makeArc3(pts_[0], pts_[1], pts_[2])});
            begin();
        }
    }
    bool input(const QString& t) override {
        QString s = t.trimmed().toLower();
        if (s == "c" || s == "ц") { center_ = true; pts_.clear(); setPrompt(QStringLiteral("Центр дуги:")); return true; }
        return Tool::input(t);
    }
    void escape() override { if (pts_.empty()) finish(); else begin(); }
    std::optional<Point> lastPoint() const override { return pts_.empty() ? std::nullopt : std::optional<Point>(pts_.back()); }
    void draw(QPainter& p, const RenderOptions& o) override {
        if (!cursor_ || pts_.empty()) return;
        if (pts_.size() == 1) ghost(p, o, makeLine(pts_[0], *cursor_));
        else if (center_) ghost(p, o, makeArc(pts_[0], dist(pts_[0], pts_[1]), (pts_[1] - pts_[0]).angle(), (*cursor_ - pts_[0]).angle()));
        else ghost(p, o, makeArc3(pts_[0], pts_[1], *cursor_));
    }
private:
    std::vector<Point> pts_;
    bool center_ = false;
};

// ---------------------------------------------------------------- Ellipse
class EllipseTool : public Tool {
public:
    using Tool::Tool;
    QString id() const override { return "ellipse"; }
    QString title() const override { return QStringLiteral("Эллипс"); }
    void begin() override { pts_.clear(); setPrompt(QStringLiteral("Эллипс — центр:")); }
    void click(Point p, bool) override {
        pts_.push_back(p);
        if (pts_.size() == 1) setPrompt(QStringLiteral("Конец первой полуоси:"));
        else if (pts_.size() == 2) setPrompt(QStringLiteral("Длина второй полуоси (число или точка):"));
        else { commitAdd(ctx_, title(), std::vector<Entity>{make(pts_[2])}); begin(); }
    }
    bool input(const QString& t) override {
        if (pts_.size() == 2) if (auto r = Units::parse(t, ctx_.doc().settings().unit); r && *r > 0) { Vec a = pts_[1] - pts_[0]; click(pts_[0] + a.normalized().perp() * *r, false); return true; }
        return Tool::input(t);
    }
    void escape() override { if (pts_.empty()) finish(); else begin(); }
    std::optional<Point> lastPoint() const override { return pts_.empty() ? std::nullopt : std::optional<Point>(pts_.back()); }
    void draw(QPainter& p, const RenderOptions& o) override {
        if (!cursor_ || pts_.empty()) return;
        if (pts_.size() == 1) ghost(p, o, makeLine(pts_[0], *cursor_));
        else ghost(p, o, make(*cursor_));
    }
private:
    Entity make(Point third) const {
        Vec a = pts_[1] - pts_[0];
        double rx = a.length();
        double ry = std::abs((third - pts_[0]).dot(a.normalized().perp()));
        return makeEllipse(pts_[0], std::max(rx, 1e-6), std::max(ry, 1e-6), a.angle());
    }
    std::vector<Point> pts_;
};

// ---------------------------------------------------------------- Text / Leader
class TextTool : public Tool {
public:
    using Tool::Tool;
    QString id() const override { return "text"; }
    QString title() const override { return QStringLiteral("Текст"); }
    void begin() override { setPrompt(QStringLiteral("Текст — точка вставки:")); }
    void click(Point p, bool) override {
        std::optional<QString> t = ctx_.askText ? ctx_.askText(title(), QStringLiteral("Текст:"), QString()) : std::optional<QString>(QStringLiteral("Текст"));
        if (t && !t->isEmpty()) commitAdd(ctx_, title(), {makeText(p, *t, ctx_.doc().settings().textHeight)});
    }
};

class LeaderTool : public Tool {
public:
    using Tool::Tool;
    QString id() const override { return "leader"; }
    QString title() const override { return QStringLiteral("Выноска"); }
    void begin() override { pts_.clear(); setPrompt(QStringLiteral("Выноска — точка стрелки:")); }
    void click(Point p, bool) override { pts_.push_back(p); setPrompt(QStringLiteral("Следующая точка (Enter — ввести текст):")); }
    void enter() override {
        if (pts_.size() >= 2) {
            std::optional<QString> t = ctx_.askText ? ctx_.askText(title(), QStringLiteral("Текст выноски:"), QString()) : std::optional<QString>(QStringLiteral("Выноска"));
            if (t) commitAdd(ctx_, title(), {makeLeader(pts_, *t, ctx_.doc().settings().textHeight)});
        }
        begin();
    }
    void escape() override { if (pts_.empty()) finish(); else begin(); }
    std::optional<Point> lastPoint() const override { return pts_.empty() ? std::nullopt : std::optional<Point>(pts_.back()); }
    void draw(QPainter& p, const RenderOptions& o) override {
        if (pts_.empty() || !cursor_) return;
        std::vector<Point> all = pts_; all.push_back(*cursor_);
        ghost(p, o, makeLeader(all, QString()));
    }
private:
    std::vector<Point> pts_;
};

// ---------------------------------------------------------------- Dimensions
class DimTool : public Tool {
public:
    DimTool(ToolContext c, DimKind k) : Tool(std::move(c)), kind_(k) {}
    QString id() const override {
        switch (kind_) { case DimKind::Radius: return "dim-radius"; case DimKind::Diameter: return "dim-diameter"; case DimKind::Angular: return "dim-angle";
                         case DimKind::Horizontal: return "dim-h"; case DimKind::Vertical: return "dim-v"; default: return "dim"; }
    }
    QString title() const override {
        switch (kind_) { case DimKind::Radius: return QStringLiteral("Размер радиуса"); case DimKind::Diameter: return QStringLiteral("Размер диаметра");
                         case DimKind::Angular: return QStringLiteral("Угловой размер"); case DimKind::Horizontal: return QStringLiteral("Горизонтальный размер");
                         case DimKind::Vertical: return QStringLiteral("Вертикальный размер"); default: return QStringLiteral("Линейный размер"); }
    }
    void begin() override {
        pts_.clear(); refs_.clear();
        if (kind_ == DimKind::Radius || kind_ == DimKind::Diameter) setPrompt(QStringLiteral("Укажите окружность или дугу:"));
        else if (kind_ == DimKind::Angular) setPrompt(QStringLiteral("Укажите первую линию:"));
        else setPrompt(QStringLiteral("Первая точка размера (Enter — выбрать объект):"));
    }
    void click(Point p, bool) override {
        Document& d = ctx_.doc();
        if (kind_ == DimKind::Radius || kind_ == DimKind::Diameter) {
            if (refs_.empty()) {
                auto id = pick(ctx_, p);
                const Entity* e = id ? d.entity(*id) : nullptr;
                if (!e || (e->type != EntityType::Circle && e->type != EntityType::Arc)) { if (ctx_.message) ctx_.message(QStringLiteral("Нужна окружность или дуга")); return; }
                refs_ = {*id}; setPrompt(QStringLiteral("Положение текста:")); return;
            }
            const Entity* e = d.entity(refs_[0]);
            if (e) commitAdd(ctx_, title(), {makeRadialDim(refs_[0], e->center, e->radius, p, kind_ == DimKind::Diameter)});
            begin(); return;
        }
        if (kind_ == DimKind::Angular) {
            if (refs_.size() < 2) {
                auto id = pick(ctx_, p);
                const Entity* e = id ? d.entity(*id) : nullptr;
                if (!e || e->type != EntityType::Line) { if (ctx_.message) ctx_.message(QStringLiteral("Нужна линия")); return; }
                refs_.push_back(*id); pts_.push_back(p);
                setPrompt(refs_.size() == 1 ? QStringLiteral("Вторая линия:") : QStringLiteral("Положение дуги размера:")); return;
            }
            const Entity* a = d.entity(refs_[0]); const Entity* b = d.entity(refs_[1]);
            if (a && b) {
                auto v = lineIntersection(a->pts[0], a->pts[1], b->pts[0], b->pts[1], false);
                if (!v) { if (ctx_.message) ctx_.message(QStringLiteral("Линии параллельны")); begin(); return; }
                auto far = [&](const Entity* l, Point pk) { return dist(l->pts[0], pk) > dist(l->pts[1], pk) ? l->pts[0] : l->pts[1]; };
                Point r1 = far(a, *v), r2 = far(b, *v);
                // choose the quadrant nearest the pick points
                if (dist(pts_[0], *v) > 1e-9) r1 = *v + (pts_[0] - *v).normalized();
                if (dist(pts_[1], *v) > 1e-9) r2 = *v + (pts_[1] - *v).normalized();
                commitAdd(ctx_, title(), {makeAngularDim(*v, r1, r2, p)});
            }
            begin(); return;
        }
        pts_.push_back(p);
        if (pts_.size() == 1) setPrompt(QStringLiteral("Вторая точка:"));
        else if (pts_.size() == 2) setPrompt(QStringLiteral("Положение размерной линии:"));
        else { commitAdd(ctx_, title(), {makeLinearDim(pts_[0], pts_[1], pts_[2], kind_)}); begin(); }
    }
    void enter() override {
        // Enter on the first step: dimension a selected line
        if (pts_.empty() && (kind_ == DimKind::Aligned || kind_ == DimKind::Horizontal || kind_ == DimKind::Vertical) && ctx_.selection && ctx_.selection->size() == 1) {
            const Entity* e = ctx_.doc().entity(*ctx_.selection->begin());
            if (e && e->type == EntityType::Line) { pts_ = {e->pts[0], e->pts[1]}; setPrompt(QStringLiteral("Положение размерной линии:")); }
        }
    }
    void escape() override { if (pts_.empty() && refs_.empty()) finish(); else begin(); }
    std::optional<Point> lastPoint() const override { return pts_.empty() ? std::nullopt : std::optional<Point>(pts_.back()); }
    void draw(QPainter& p, const RenderOptions& o) override {
        if (!cursor_) return;
        Entity ghostDim;
        if ((kind_ == DimKind::Radius || kind_ == DimKind::Diameter) && !refs_.empty()) {
            const Entity* e = ctx_.doc().entity(refs_[0]);
            if (e) ghostDim = makeRadialDim(refs_[0], e->center, e->radius, *cursor_, kind_ == DimKind::Diameter);
        } else if (kind_ != DimKind::Angular && pts_.size() == 2) ghostDim = makeLinearDim(pts_[0], pts_[1], *cursor_, kind_);
        else if (pts_.size() == 1 && kind_ != DimKind::Angular) ghostDim = makeLine(pts_[0], *cursor_);
        else return;
        ghostDim.textHeight = ctx_.doc().settings().textHeight;
        ghost(p, o, ghostDim);
    }
private:
    DimKind kind_;
    std::vector<Point> pts_;
    std::vector<quint64> refs_;
};

// ---------------------------------------------------------------- Hatch
class HatchTool : public Tool {
public:
    using Tool::Tool;
    QString id() const override { return "hatch"; }
    QString title() const override { return QStringLiteral("Штриховка"); }
    void begin() override { setPrompt(QStringLiteral("Штриховка — укажите замкнутый контур («s» — заливка, «l» — линии, «x» — сетка):")); }
    void click(Point p, bool) override {
        auto id = pick(ctx_, p);
        const Entity* e = id ? ctx_.doc().entity(*id) : nullptr;
        if (!e || !isClosedShape(*e)) { if (ctx_.message) ctx_.message(QStringLiteral("Нужен замкнутый контур: окружность, замкнутая полилиния, сплайн или эллипс")); return; }
        std::vector<Point> b = flatten(*e, 0.05);
        if (b.size() > 1 && b.front() == b.back()) b.pop_back();
        commitAdd(ctx_, title(), {makeHatch(b, pattern_, rad(45), 3)});
    }
    bool input(const QString& t) override {
        QString s = t.trimmed().toLower();
        if (s == "s" || s == "з") { pattern_ = HatchPattern::Solid; return true; }
        if (s == "l" || s == "л") { pattern_ = HatchPattern::Lines; return true; }
        if (s == "x" || s == "с") { pattern_ = HatchPattern::Cross; return true; }
        return Tool::input(t);
    }
private:
    HatchPattern pattern_ = HatchPattern::Lines;
};

// ---------------------------------------------------------------- selection-based modify tools
class ModifyTool : public Tool {
public:
    using Tool::Tool;
    void begin() override {
        stage_ = 0; pts_.clear();
        ids_.clear();
        if (ctx_.selection && !ctx_.selection->isEmpty()) { for (quint64 id : *ctx_.selection) ids_.push_back(id); stage_ = 1; afterSelection(); }
        else setPrompt(QStringLiteral("Выберите объекты и нажмите Enter:"));
    }
    bool selecting() const override { return stage_ == 0; }
    void selectionDone() override {
        ids_.clear();
        if (ctx_.selection) for (quint64 id : *ctx_.selection) ids_.push_back(id);
        if (ids_.empty()) { finish(); return; }
        stage_ = 1; afterSelection();
    }
    void escape() override { finish(); }
    bool keepAfterFinish() const override { return false; }
    std::optional<Point> lastPoint() const override { return pts_.empty() ? std::nullopt : std::optional<Point>(pts_.back()); }
protected:
    virtual void afterSelection() = 0;
    std::vector<Entity> originals() const { std::vector<Entity> v; for (quint64 id : ids_) if (const Entity* e = ctx_.doc().entity(id)) v.push_back(*e); return v; }
    void applyTransform(const Transform& t, bool copy, const QString& title) {
        auto c = std::make_unique<EntityCommand>(title);
        for (const Entity& e : originals()) {
            if (e.locked) continue;
            Entity m = e; transformEntity(m, t);
            if (copy) { m.id = 0; c->add(m); } else c->modify(e, m);
        }
        ctx_.commit(std::move(c));
    }
    void drawTransformed(QPainter& p, const RenderOptions& o, const Transform& t) {
        for (const Entity& e : originals()) { Entity m = e; transformEntity(m, t); ghost(p, o, m); }
    }
    int stage_ = 0;
    std::vector<quint64> ids_;
    std::vector<Point> pts_;
};

class MoveCopyTool : public ModifyTool {
public:
    MoveCopyTool(ToolContext c, bool copy) : ModifyTool(std::move(c)), copy_(copy) {}
    QString id() const override { return copy_ ? "copy" : "move"; }
    QString title() const override { return copy_ ? QStringLiteral("Копировать") : QStringLiteral("Переместить"); }
    void afterSelection() override { pts_.clear(); setPrompt(QStringLiteral("Базовая точка:")); }
    void click(Point p, bool) override {
        pts_.push_back(p);
        if (pts_.size() == 1) { setPrompt(QStringLiteral("Вторая точка (куда):")); return; }
        Vec d = pts_[1] - pts_[0];
        applyTransform(Transform::translate(d.x, d.y), copy_, title());
        if (copy_) { pts_.pop_back(); setPrompt(QStringLiteral("Следующая точка (Enter — завершить):")); }
        else finish();
    }
    void enter() override { finish(); }
    void draw(QPainter& p, const RenderOptions& o) override { if (pts_.size() == 1 && cursor_) { Vec d = *cursor_ - pts_[0]; drawTransformed(p, o, Transform::translate(d.x, d.y)); } }
private:
    bool copy_;
};

class RotateTool : public ModifyTool {
public:
    using ModifyTool::ModifyTool;
    QString id() const override { return "rotate"; }
    QString title() const override { return QStringLiteral("Повернуть"); }
    void afterSelection() override { pts_.clear(); setPrompt(QStringLiteral("Центр поворота:")); }
    void click(Point p, bool) override {
        pts_.push_back(p);
        if (pts_.size() == 1) { setPrompt(QStringLiteral("Угол (градусы) или точка направления:")); return; }
        if (pts_.size() == 2) { setPrompt(QStringLiteral("Вторая точка угла:")); return; }
        double a = (pts_[2] - pts_[0]).angle() - (pts_[1] - pts_[0]).angle();
        applyTransform(Transform::rotateAround(pts_[0], a), false, title()); finish();
    }
    bool input(const QString& t) override {
        if (pts_.size() == 1) if (auto a = InputParser::angleDeg(t)) { applyTransform(Transform::rotateAround(pts_[0], rad(*a)), false, title()); finish(); return true; }
        return Tool::input(t);
    }
    void draw(QPainter& p, const RenderOptions& o) override {
        if (!cursor_ || pts_.empty()) return;
        if (pts_.size() == 1) ghost(p, o, makeLine(pts_[0], *cursor_));
        else drawTransformed(p, o, Transform::rotateAround(pts_[0], (*cursor_ - pts_[0]).angle() - (pts_[1] - pts_[0]).angle()));
    }
};

class MirrorTool : public ModifyTool {
public:
    using ModifyTool::ModifyTool;
    QString id() const override { return "mirror"; }
    QString title() const override { return QStringLiteral("Зеркало"); }
    void afterSelection() override { pts_.clear(); setPrompt(QStringLiteral("Первая точка оси отражения:")); }
    void click(Point p, bool) override {
        pts_.push_back(p);
        if (pts_.size() == 1) { setPrompt(QStringLiteral("Вторая точка оси (Enter после — удалить исходные: введите «y»):")); return; }
        applyTransform(Transform::mirror(pts_[0], pts_[1]), true, title()); finish();
    }
    bool input(const QString& t) override {
        if (pts_.size() == 1 && cursor_ && (t.trimmed().toLower() == "y" || t.trimmed().toLower() == "д")) { applyTransform(Transform::mirror(pts_[0], *cursor_), false, title()); finish(); return true; }
        return Tool::input(t);
    }
    void draw(QPainter& p, const RenderOptions& o) override { if (pts_.size() == 1 && cursor_ && dist(pts_[0], *cursor_) > 1e-6) drawTransformed(p, o, Transform::mirror(pts_[0], *cursor_)); }
};

class ScaleTool : public ModifyTool {
public:
    using ModifyTool::ModifyTool;
    QString id() const override { return "scale"; }
    QString title() const override { return QStringLiteral("Масштабировать"); }
    void afterSelection() override { pts_.clear(); setPrompt(QStringLiteral("Базовая точка:")); }
    void click(Point p, bool) override {
        pts_.push_back(p);
        if (pts_.size() == 1) { setPrompt(QStringLiteral("Коэффициент (число) или опорная точка:")); return; }
        if (pts_.size() == 2) { setPrompt(QStringLiteral("Новая точка:")); return; }
        double k = dist(pts_[2], pts_[0]) / std::max(dist(pts_[1], pts_[0]), 1e-9);
        applyTransform(Transform::scaleAround(pts_[0], k), false, title()); finish();
    }
    bool input(const QString& t) override {
        if (pts_.size() == 1) { bool ok = false; double k = QString(t).replace(',', '.').toDouble(&ok); if (ok && k > 0) { applyTransform(Transform::scaleAround(pts_[0], k), false, title()); finish(); return true; } }
        return Tool::input(t);
    }
    void draw(QPainter& p, const RenderOptions& o) override {
        if (!cursor_ || pts_.size() < 2) return;
        drawTransformed(p, o, Transform::scaleAround(pts_[0], dist(*cursor_, pts_[0]) / std::max(dist(pts_[1], pts_[0]), 1e-9)));
    }
};

class ArrayTool : public ModifyTool {
public:
    using ModifyTool::ModifyTool;
    QString id() const override { return "array"; }
    QString title() const override { return QStringLiteral("Массив"); }
    void afterSelection() override {
        setPrompt(QStringLiteral("Массив: введите «ряды,столбцы,шагX,шагY» (например 3,4,50,30) или «p,число,угол» (полярный, например p,6,360):"));
        if (ctx_.askText) {
            auto t = ctx_.askText(title(), QStringLiteral("Прямоугольный: ряды,столбцы,шагX,шагY\nПолярный: p,число,угол (центр — следующая точка)"), QStringLiteral("3,3,50,50"));
            if (!t) { finish(); return; }
            input(*t);
        }
    }
    void click(Point p, bool) override {
        if (polar_) {
            auto c = std::make_unique<EntityCommand>(title());
            for (int i = 1; i < n_; ++i)
                for (const Entity& e : originals()) { Entity m = e; m.id = 0; transformEntity(m, Transform::rotateAround(p, rad(ang_) * i / (std::abs(ang_ - 360) < 1e-9 ? n_ : std::max(1, n_ - 1)))); c->add(m); }
            ctx_.commit(std::move(c)); finish();
        }
    }
    bool input(const QString& t) override {
        QStringList parts = t.trimmed().split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
        if (parts.size() == 3 && (parts[0].toLower() == "p" || parts[0].toLower() == "п")) {
            polar_ = true; n_ = parts[1].toInt(); ang_ = parts[2].toDouble();
            if (n_ < 2) return false;
            setPrompt(QStringLiteral("Центр полярного массива:")); return true;
        }
        if (parts.size() == 4) {
            int rows = parts[0].toInt(), cols = parts[1].toInt();
            auto dx = Units::parse(parts[2], ctx_.doc().settings().unit), dy = Units::parse(parts[3], ctx_.doc().settings().unit);
            if (rows < 1 || cols < 1 || !dx || !dy) return false;
            auto c = std::make_unique<EntityCommand>(title());
            for (int r = 0; r < rows; ++r) for (int k = 0; k < cols; ++k) {
                if (r == 0 && k == 0) continue;
                for (const Entity& e : originals()) { Entity m = e; m.id = 0; transformEntity(m, Transform::translate(k * *dx, r * *dy)); c->add(m); }
            }
            ctx_.commit(std::move(c)); finish(); return true;
        }
        return false;
    }
private:
    bool polar_ = false; int n_ = 0; double ang_ = 360;
};

class ExplodeTool : public ModifyTool {
public:
    using ModifyTool::ModifyTool;
    QString id() const override { return "explode"; }
    QString title() const override { return QStringLiteral("Расчленить"); }
    void click(Point, bool) override {}
    void afterSelection() override {
        auto c = std::make_unique<EntityCommand>(title());
        for (const Entity& e : originals()) {
            if (e.type == EntityType::Line || e.type == EntityType::Circle || e.type == EntityType::Arc || e.type == EntityType::Text) continue;
            for (Entity part : explode(e)) { part.id = 0; c->add(part); }
            c->remove(e);
        }
        if (!c->empty()) ctx_.commit(std::move(c));
        if (ctx_.selection) ctx_.selection->clear();
        finish();
    }
};

class DeleteTool : public ModifyTool {
public:
    using ModifyTool::ModifyTool;
    QString id() const override { return "delete"; }
    QString title() const override { return QStringLiteral("Удалить"); }
    void click(Point, bool) override {}
    void afterSelection() override {
        auto c = std::make_unique<EntityCommand>(title());
        for (const Entity& e : originals()) if (!e.locked) c->remove(e);
        if (!c->empty()) ctx_.commit(std::move(c));
        if (ctx_.selection) ctx_.selection->clear();
        finish();
    }
};

// ---------------------------------------------------------------- Offset / Trim / Extend / Fillet / Chamfer
class OffsetTool : public Tool {
public:
    using Tool::Tool;
    QString id() const override { return "offset"; }
    QString title() const override { return QStringLiteral("Смещение"); }
    void begin() override { target_.reset(); setPrompt(QStringLiteral("Смещение — введите расстояние (сейчас %1):").arg(fmt(ctx_, d_))); }
    bool input(const QString& t) override {
        if (auto v = Units::parse(t, ctx_.doc().settings().unit); v && *v > 0) { d_ = *v; setPrompt(QStringLiteral("Укажите объект:")); return true; }
        return false;
    }
    void click(Point p, bool) override {
        if (!target_) { target_ = pick(ctx_, p); if (!target_) return; setPrompt(QStringLiteral("Укажите сторону смещения:")); return; }
        const Entity* e = ctx_.doc().entity(*target_);
        if (e) if (auto r = offsetEntity(*e, d_, p)) commitAdd(ctx_, title(), {*r});
        target_.reset(); setPrompt(QStringLiteral("Укажите объект (Enter — завершить):"));
    }
    void enter() override { finish(); }
    void escape() override { if (target_) { target_.reset(); setPrompt(QStringLiteral("Укажите объект:")); } else finish(); }
    bool keepAfterFinish() const override { return false; }
    void draw(QPainter& p, const RenderOptions& o) override {
        if (!target_ || !cursor_) return;
        const Entity* e = ctx_.doc().entity(*target_);
        if (e) if (auto r = offsetEntity(*e, d_, *cursor_)) ghost(p, o, *r);
    }
private:
    std::optional<quint64> target_;
    double d_ = 10;
};

class TrimExtendTool : public ModifyTool {
public:
    TrimExtendTool(ToolContext c, bool extend) : ModifyTool(std::move(c)), extend_(extend) {}
    QString id() const override { return extend_ ? "extend" : "trim"; }
    QString title() const override { return extend_ ? QStringLiteral("Удлинить") : QStringLiteral("Обрезать"); }
    void begin() override { stage_ = 0; ids_.clear(); setPrompt(extend_ ? QStringLiteral("Выберите граничные объекты и нажмите Enter:") : QStringLiteral("Выберите режущие объекты и нажмите Enter:")); }
    void afterSelection() override { setPrompt(extend_ ? QStringLiteral("Укажите объект у того конца, который удлинить (Enter — завершить):") : QStringLiteral("Укажите часть, которую убрать (Enter — завершить):")); if (ctx_.selection) ctx_.selection->clear(); }
    void click(Point p, bool) override {
        auto id = pick(ctx_, p);
        if (!id) return;
        const Entity* e = ctx_.doc().entity(*id);
        if (!e) return;
        std::vector<Entity> bounds;
        for (quint64 b : ids_) if (b != *id) if (const Entity* be = ctx_.doc().entity(b)) bounds.push_back(*be);
        auto c = std::make_unique<EntityCommand>(title());
        if (extend_) {
            std::optional<Entity> best;
            for (const Entity& b : bounds) {
                auto r = extendEntity(*e, b, p);
                if (!r) continue;
                if (!best || entityBBox(*r).width() + entityBBox(*r).height() < entityBBox(*best).width() + entityBBox(*best).height()) best = r;
            }
            if (!best) { if (ctx_.message) ctx_.message(QStringLiteral("Нет пересечения с границей")); return; }
            c->modify(*e, *best);
        } else {
            std::vector<Entity> parts = trimEntity(*e, bounds, p);
            if (parts.empty() && e->type != EntityType::Circle) { if (ctx_.message) ctx_.message(QStringLiteral("Нет пересечения с режущими объектами")); return; }
            c->remove(*e);
            for (Entity& part : parts) { part.id = 0; c->add(part); }
        }
        ctx_.commit(std::move(c));
    }
    void enter() override { finish(); }
private:
    bool extend_;
};

class FilletChamferTool : public Tool {
public:
    FilletChamferTool(ToolContext c, bool chamfer) : Tool(std::move(c)), chamfer_(chamfer) {}
    QString id() const override { return chamfer_ ? "chamfer" : "fillet"; }
    QString title() const override { return chamfer_ ? QStringLiteral("Фаска") : QStringLiteral("Скругление"); }
    void begin() override {
        first_.reset();
        setPrompt(chamfer_ ? QStringLiteral("Фаска — введите катеты «d1,d2» (сейчас %1 и %2) или укажите первую линию:").arg(fmt(ctx_, d1_), fmt(ctx_, d2_))
                           : QStringLiteral("Скругление — введите радиус (сейчас %1) или укажите первую линию:").arg(fmt(ctx_, r_)));
    }
    bool input(const QString& t) override {
        Unit u = ctx_.doc().settings().unit;
        if (chamfer_) {
            QStringList parts = t.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
            if (parts.size() >= 1) { auto a = Units::parse(parts[0], u); if (!a) return false; d1_ = *a; d2_ = parts.size() > 1 ? Units::parse(parts[1], u).value_or(*a) : *a; setPrompt(QStringLiteral("Укажите первую линию:")); return true; }
            return false;
        }
        if (auto v = Units::parse(t, u); v && *v >= 0) { r_ = *v; setPrompt(QStringLiteral("Укажите первую линию:")); return true; }
        return false;
    }
    void click(Point p, bool) override {
        auto id = pick(ctx_, p);
        const Entity* e = id ? ctx_.doc().entity(*id) : nullptr;
        if (!e || e->type != EntityType::Line) { if (ctx_.message) ctx_.message(QStringLiteral("Нужна линия")); return; }
        if (!first_) { first_ = {*id, p}; setPrompt(QStringLiteral("Укажите вторую линию:")); return; }
        const Entity* a = ctx_.doc().entity(first_->first);
        if (!a || a->id == e->id) { first_.reset(); return; }
        auto f = chamfer_ ? chamferLines(*a, *e, d1_, d2_, first_->second, p) : filletLines(*a, *e, r_, first_->second, p);
        if (!f) { if (ctx_.message) ctx_.message(QStringLiteral("Линии не пересекаются")); first_.reset(); return; }
        auto c = std::make_unique<EntityCommand>(title());
        c->modify(*a, f->a); c->modify(*e, f->b);
        if (f->arc) { Entity x = *f->arc; x.id = 0; c->add(x); }
        ctx_.commit(std::move(c));
        first_.reset(); setPrompt(QStringLiteral("Укажите первую линию (Enter — завершить):"));
    }
    void enter() override { finish(); }
    void escape() override { if (first_) { first_.reset(); setPrompt(QStringLiteral("Укажите первую линию:")); } else finish(); }
    bool keepAfterFinish() const override { return false; }
private:
    bool chamfer_;
    double r_ = 5, d1_ = 5, d2_ = 5;
    std::optional<std::pair<quint64, Point>> first_;
};

// ---------------------------------------------------------------- Объединение: соединить / булевы / разрезать
class JoinTool : public ModifyTool {
public:
    using ModifyTool::ModifyTool;
    QString id() const override { return "join"; }
    QString title() const override { return QStringLiteral("Соединить"); }
    void begin() override { ModifyTool::begin(); if (stage_ == 0) setPrompt(QStringLiteral("Выберите линии, дуги и полилинии с общими концами и нажмите Enter:")); }
    void click(Point, bool) override {}
    void afterSelection() override {
        std::vector<Entity> src;
        for (const Entity& e : originals()) if (!e.locked) src.push_back(e);
        auto joined = joinEntities(src, 1e-4);
        if (!joined) { if (ctx_.message) ctx_.message(QStringLiteral("Объекты не образуют единую цепочку с общими концами")); finish(); return; }
        auto c = std::make_unique<EntityCommand>(title());
        for (const Entity& e : src) c->remove(e);
        c->add(*joined);
        ctx_.commit(std::move(c));
        if (ctx_.message) ctx_.message(joined->closed ? QStringLiteral("Соединено в замкнутую полилинию") : QStringLiteral("Соединено в полилинию"));
        if (ctx_.selection) ctx_.selection->clear();
        finish();
    }
};

class BooleanTool : public Tool {
public:
    BooleanTool(ToolContext c, BoolOp op) : Tool(std::move(c)), op_(op) {}
    QString id() const override { return op_ == BoolOp::Union ? "union" : op_ == BoolOp::Subtract ? "subtract" : "intersect"; }
    QString title() const override {
        return op_ == BoolOp::Union ? QStringLiteral("Объединить") : op_ == BoolOp::Subtract ? QStringLiteral("Вычесть") : QStringLiteral("Пересечение");
    }
    void begin() override {
        first_.reset();
        if (ctx_.selection && ctx_.selection->size() >= 2) {   // ready selection: apply at once
            std::vector<quint64> ids(ctx_.selection->begin(), ctx_.selection->end());
            std::sort(ids.begin(), ids.end());
            apply(ids, false); return;
        }
        setPrompt(op_ == BoolOp::Subtract ? QStringLiteral("Укажите объект, из которого вычесть:") : QStringLiteral("Укажите первый замкнутый объект:"));
    }
    void click(Point p, bool shift) override {
        auto id = pick(ctx_, p);
        if (!id) return;
        const Entity* e = ctx_.doc().entity(*id);
        if (!e || !isClosedShape(*e)) { if (ctx_.message) ctx_.message(QStringLiteral("Нужен замкнутый объект")); return; }
        if (!first_) {
            first_ = *id;
            setPrompt(op_ == BoolOp::Subtract ? QStringLiteral("Укажите вычитаемый объект (Shift — оставить исходные):")
                                              : QStringLiteral("Укажите второй объект (Shift — оставить исходные):"));
            return;
        }
        if (*id == *first_) return;
        apply({*first_, *id}, shift);
    }
    void enter() override { finish(); }
    bool keepAfterFinish() const override { return false; }
private:
    void apply(const std::vector<quint64>& ids, bool keep) {
        std::vector<Entity> shapes;
        for (quint64 id : ids) if (const Entity* e = ctx_.doc().entity(id)) shapes.push_back(*e);
        std::vector<Entity> res = booleanShapes(shapes, op_);
        if (res.empty()) { if (ctx_.message) ctx_.message(QStringLiteral("Пустой результат: объекты не пересекаются или не замкнуты")); finish(); return; }
        auto c = std::make_unique<EntityCommand>(title());
        if (!keep) for (const Entity& e : shapes) if (!e.locked) c->remove(e);
        for (const Entity& r : res) c->add(r);
        ctx_.commit(std::move(c));
        if (ctx_.selection) ctx_.selection->clear();
        if (ctx_.message) ctx_.message(QStringLiteral("%1: %2 контур(а)").arg(title()).arg(res.size()));
        finish();
    }
    BoolOp op_;
    std::optional<quint64> first_;
};

class SplitTool : public Tool {
public:
    using Tool::Tool;
    QString id() const override { return "split"; }
    QString title() const override { return QStringLiteral("Разрезать"); }
    void begin() override { setPrompt(QStringLiteral("Укажите объект в точке разреза (Enter — завершить):")); }
    void click(Point p, bool) override {
        auto id = pick(ctx_, p);
        if (!id) return;
        const Entity* e = ctx_.doc().entity(*id);
        if (!e || e->locked) return;
        std::vector<Entity> parts = splitEntity(*e, p);
        if (parts.empty()) { if (ctx_.message) ctx_.message(QStringLiteral("Этот объект здесь не разрезать")); return; }
        auto c = std::make_unique<EntityCommand>(title());
        c->remove(*e);
        for (Entity& part : parts) { part.id = 0; c->add(part); }
        ctx_.commit(std::move(c));
        if (ctx_.message) ctx_.message(QStringLiteral("Разрезано: %1 част(и)").arg(parts.size()));
    }
    void enter() override { finish(); }
    bool keepAfterFinish() const override { return false; }
};

class CutByTool : public Tool {
public:
    using Tool::Tool;
    QString id() const override { return "cut"; }
    QString title() const override { return QStringLiteral("Разрезать объектом"); }
    void begin() override { target_.reset(); setPrompt(QStringLiteral("Укажите объект, который разрезать:")); }
    void click(Point p, bool) override {
        auto id = pick(ctx_, p);
        if (!id) return;
        if (!target_) { target_ = *id; setPrompt(QStringLiteral("Укажите режущий объект:")); return; }
        if (*id == *target_) return;
        const Entity* e = ctx_.doc().entity(*target_);
        const Entity* cutter = ctx_.doc().entity(*id);
        if (!e || !cutter || e->locked) { begin(); return; }
        std::vector<Entity> parts = cutEntity(*e, *cutter);
        if (parts.empty()) { if (ctx_.message) ctx_.message(QStringLiteral("Объекты не пересекаются")); begin(); return; }
        auto c = std::make_unique<EntityCommand>(title());
        c->remove(*e);
        for (Entity& part : parts) { part.id = 0; c->add(part); }
        ctx_.commit(std::move(c));
        if (ctx_.message) ctx_.message(QStringLiteral("Разрезано: %1 част(и)").arg(parts.size()));
        finish();
    }
    void enter() override { finish(); }
    bool keepAfterFinish() const override { return false; }
private:
    std::optional<quint64> target_;
};

} // namespace

// ---------------------------------------------------------------- registry
QStringList toolIds() {
    return {"select", "line", "polyline", "rect", "circle", "arc", "ellipse", "spline", "text", "leader", "dim", "dim-h", "dim-v",
            "dim-radius", "dim-diameter", "dim-angle", "hatch", "move", "copy", "rotate", "mirror", "scale", "offset", "trim", "extend",
            "fillet", "chamfer", "array", "explode", "delete", "join", "union", "subtract", "intersect", "split", "cut"};
}

std::unique_ptr<Tool> createTool(const QString& id, const ToolContext& ctx) {
    if (id == "select") return std::make_unique<SelectTool>(ctx);
    if (id == "line") return std::make_unique<LineTool>(ctx);
    if (id == "polyline") return std::make_unique<PolyTool>(ctx, false);
    if (id == "spline") return std::make_unique<PolyTool>(ctx, true);
    if (id == "rect") return std::make_unique<RectTool>(ctx);
    if (id == "circle") return std::make_unique<CircleTool>(ctx);
    if (id == "arc") return std::make_unique<ArcTool>(ctx);
    if (id == "ellipse") return std::make_unique<EllipseTool>(ctx);
    if (id == "text") return std::make_unique<TextTool>(ctx);
    if (id == "leader") return std::make_unique<LeaderTool>(ctx);
    if (id == "dim") return std::make_unique<DimTool>(ctx, DimKind::Aligned);
    if (id == "dim-h") return std::make_unique<DimTool>(ctx, DimKind::Horizontal);
    if (id == "dim-v") return std::make_unique<DimTool>(ctx, DimKind::Vertical);
    if (id == "dim-radius") return std::make_unique<DimTool>(ctx, DimKind::Radius);
    if (id == "dim-diameter") return std::make_unique<DimTool>(ctx, DimKind::Diameter);
    if (id == "dim-angle") return std::make_unique<DimTool>(ctx, DimKind::Angular);
    if (id == "hatch") return std::make_unique<HatchTool>(ctx);
    if (id == "move") return std::make_unique<MoveCopyTool>(ctx, false);
    if (id == "copy") return std::make_unique<MoveCopyTool>(ctx, true);
    if (id == "rotate") return std::make_unique<RotateTool>(ctx);
    if (id == "mirror") return std::make_unique<MirrorTool>(ctx);
    if (id == "scale") return std::make_unique<ScaleTool>(ctx);
    if (id == "offset") return std::make_unique<OffsetTool>(ctx);
    if (id == "trim") return std::make_unique<TrimExtendTool>(ctx, false);
    if (id == "extend") return std::make_unique<TrimExtendTool>(ctx, true);
    if (id == "fillet") return std::make_unique<FilletChamferTool>(ctx, false);
    if (id == "chamfer") return std::make_unique<FilletChamferTool>(ctx, true);
    if (id == "array") return std::make_unique<ArrayTool>(ctx);
    if (id == "explode") return std::make_unique<ExplodeTool>(ctx);
    if (id == "delete") return std::make_unique<DeleteTool>(ctx);
    if (id == "join") return std::make_unique<JoinTool>(ctx);
    if (id == "union") return std::make_unique<BooleanTool>(ctx, BoolOp::Union);
    if (id == "subtract") return std::make_unique<BooleanTool>(ctx, BoolOp::Subtract);
    if (id == "intersect") return std::make_unique<BooleanTool>(ctx, BoolOp::Intersect);
    if (id == "split") return std::make_unique<SplitTool>(ctx);
    if (id == "cut") return std::make_unique<CutByTool>(ctx);
    return nullptr;
}

QString toolTitle(const QString& id) {
    ToolContext c;
    auto t = createTool(id, c);
    return t ? t->title() : id;
}

} // namespace psi
