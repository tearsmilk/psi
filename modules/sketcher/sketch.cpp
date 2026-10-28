#include "modules/sketcher/sketch.h"
#include "core/document/document.h"
#include "core/units/units.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <unordered_map>

namespace psi::sk {

// ---------------------------------------------------------------- entity helpers
int SketchEntity::paramCount() const {
    switch (kind) { case Kind::Point: return 2; case Kind::Line: return 4; case Kind::Circle: return 3; case Kind::Arc: return 5; }
    return 0;
}
int SketchEntity::pointCount() const {
    switch (kind) { case Kind::Point: return 1; case Kind::Line: return 2; case Kind::Circle: return 1; case Kind::Arc: return 3; }
    return 0;
}
Point SketchEntity::point(int i) const {
    switch (kind) {
    case Kind::Point: return {p[0], p[1]};
    case Kind::Line: return i == 0 ? Point{p[0], p[1]} : Point{p[2], p[3]};
    case Kind::Circle: return {p[0], p[1]};
    case Kind::Arc:
        if (i == 0) return {p[0], p[1]};
        if (i == 1) return {p[0] + p[2] * std::cos(p[3]), p[1] + p[2] * std::sin(p[3])};
        return {p[0] + p[2] * std::cos(p[4]), p[1] + p[2] * std::sin(p[4])};
    }
    return {};
}
Rect SketchEntity::bbox() const {
    Rect r;
    switch (kind) {
    case Kind::Point: r.add(point(0)); break;
    case Kind::Line: r.add(point(0)); r.add(point(1)); break;
    case Kind::Circle: r.add({p[0] - p[2], p[1] - p[2]}); r.add({p[0] + p[2], p[1] + p[2]}); break;
    case Kind::Arc: {
        r.add(point(1)); r.add(point(2));
        double a0 = normAngle(p[3]), sweep = normAngle(p[4] - p[3]);
        for (int q = 0; q < 4; ++q) { double a = q * kPi / 2; if (normAngle(a - a0) <= sweep) r.add({p[0] + p[2] * std::cos(a), p[1] + p[2] * std::sin(a)}); }
        break;
    }
    }
    return r;
}
QJsonObject SketchEntity::toJson() const {
    QJsonObject o; o["id"] = double(id); o["k"] = kindKey(kind);
    QJsonArray a; for (double v : p) a.push_back(v);
    o["p"] = a; if (construction) o["c"] = true; if (fixed) o["f"] = true;
    return o;
}
SketchEntity SketchEntity::fromJson(const QJsonObject& o) {
    SketchEntity e; e.id = quint64(o.value("id").toDouble()); e.kind = kindFromKey(o.value("k").toString());
    for (const auto& v : o.value("p").toArray()) e.p.push_back(v.toDouble());
    e.p.resize(e.paramCount(), 0.0);
    e.construction = o.value("c").toBool(); e.fixed = o.value("f").toBool();
    return e;
}

QString kindKey(Kind k) { switch (k) { case Kind::Point: return "point"; case Kind::Line: return "line"; case Kind::Circle: return "circle"; case Kind::Arc: return "arc"; } return "line"; }
Kind kindFromKey(const QString& k) { if (k == "point") return Kind::Point; if (k == "circle") return Kind::Circle; if (k == "arc") return Kind::Arc; return Kind::Line; }

static const std::vector<std::pair<CType, const char*>>& ctypeKeys() {
    static const std::vector<std::pair<CType, const char*>> k{
        {CType::Coincident, "coincident"}, {CType::Horizontal, "horizontal"}, {CType::Vertical, "vertical"}, {CType::Parallel, "parallel"},
        {CType::Perpendicular, "perpendicular"}, {CType::Tangent, "tangent"}, {CType::Equal, "equal"}, {CType::Fixed, "fixed"},
        {CType::Distance, "distance"}, {CType::Length, "length"}, {CType::Radius, "radius"}, {CType::Diameter, "diameter"},
        {CType::Angle, "angle"}, {CType::PointOnLine, "pointOnLine"}, {CType::PointOnCircle, "pointOnCircle"}, {CType::Symmetric, "symmetric"}};
    return k;
}
QString constraintTypeKey(CType t) { for (const auto& p : ctypeKeys()) if (p.first == t) return p.second; return "coincident"; }
CType constraintTypeFromKey(const QString& k) { for (const auto& p : ctypeKeys()) if (k == p.second) return p.first; return CType::Coincident; }
QString constraintTypeName(CType t) {
    switch (t) {
    case CType::Coincident: return QStringLiteral("Совпадение"); case CType::Horizontal: return QStringLiteral("Горизонталь");
    case CType::Vertical: return QStringLiteral("Вертикаль"); case CType::Parallel: return QStringLiteral("Параллельность");
    case CType::Perpendicular: return QStringLiteral("Перпендикулярность"); case CType::Tangent: return QStringLiteral("Касательность");
    case CType::Equal: return QStringLiteral("Равенство"); case CType::Fixed: return QStringLiteral("Закрепление");
    case CType::Distance: return QStringLiteral("Расстояние"); case CType::Length: return QStringLiteral("Длина");
    case CType::Radius: return QStringLiteral("Радиус"); case CType::Diameter: return QStringLiteral("Диаметр");
    case CType::Angle: return QStringLiteral("Угол"); case CType::PointOnLine: return QStringLiteral("Точка на линии");
    case CType::PointOnCircle: return QStringLiteral("Точка на окружности"); case CType::Symmetric: return QStringLiteral("Симметрия");
    }
    return {};
}
QString constraintGlyph(CType t) {
    switch (t) {
    case CType::Coincident: return QStringLiteral("●"); case CType::Horizontal: return QStringLiteral("—"); case CType::Vertical: return QStringLiteral("|");
    case CType::Parallel: return QStringLiteral("∥"); case CType::Perpendicular: return QStringLiteral("⟂"); case CType::Tangent: return QStringLiteral("◠");
    case CType::Equal: return QStringLiteral("="); case CType::Fixed: return QStringLiteral("⌖"); case CType::Distance: return QStringLiteral("↔");
    case CType::Length: return QStringLiteral("L"); case CType::Radius: return QStringLiteral("R"); case CType::Diameter: return QStringLiteral("⌀");
    case CType::Angle: return QStringLiteral("∠"); case CType::PointOnLine: return QStringLiteral("⊙"); case CType::PointOnCircle: return QStringLiteral("⊚");
    case CType::Symmetric: return QStringLiteral("⇔");
    }
    return {};
}

QJsonObject Constraint::toJson() const {
    QJsonObject o; o["id"] = double(id); o["t"] = constraintTypeKey(type);
    QJsonArray rs; for (const Ref& r : refs) { QJsonArray a; a.push_back(double(r.entity)); a.push_back(r.point); rs.push_back(a); }
    o["refs"] = rs;
    if (hasValue()) o["v"] = value;
    if (type == CType::Fixed) { o["fx"] = fixedAt.x; o["fy"] = fixedAt.y; }
    if (label.x != 0 || label.y != 0) { o["lx"] = label.x; o["ly"] = label.y; }
    return o;
}
Constraint Constraint::fromJson(const QJsonObject& o) {
    Constraint c; c.id = quint64(o.value("id").toDouble()); c.type = constraintTypeFromKey(o.value("t").toString());
    for (const auto& rv : o.value("refs").toArray()) { QJsonArray a = rv.toArray(); if (a.size() == 2) c.refs.push_back({quint64(a[0].toDouble()), a[1].toInt()}); }
    c.value = o.value("v").toDouble(); c.fixedAt = {o.value("fx").toDouble(), o.value("fy").toDouble()};
    c.label = {o.value("lx").toDouble(), o.value("ly").toDouble()};
    return c;
}

// ---------------------------------------------------------------- Sketch: geometry / constraints
quint64 Sketch::addEntity(SketchEntity e) {
    if (e.id == 0) e.id = nextId_++; else nextId_ = std::max(nextId_, e.id + 1);
    e.p.resize(e.paramCount(), 0.0);
    entities_.push_back(std::move(e));
    ++revision_;
    return entities_.back().id;
}
bool Sketch::removeEntity(quint64 id) {
    auto it = std::find_if(entities_.begin(), entities_.end(), [id](const SketchEntity& e) { return e.id == id; });
    if (it == entities_.end()) return false;
    entities_.erase(it);
    constraints_.erase(std::remove_if(constraints_.begin(), constraints_.end(), [id](const Constraint& c) {
        return std::any_of(c.refs.begin(), c.refs.end(), [id](const Ref& r) { return r.entity == id; }); }), constraints_.end());
    ++revision_;
    return true;
}
SketchEntity* Sketch::entity(quint64 id) { for (auto& e : entities_) if (e.id == id) return &e; return nullptr; }
const SketchEntity* Sketch::entity(quint64 id) const { for (const auto& e : entities_) if (e.id == id) return &e; return nullptr; }

quint64 Sketch::addConstraint(Constraint c) {
    if (c.id == 0) c.id = nextId_++; else nextId_ = std::max(nextId_, c.id + 1);
    if (c.type == CType::Fixed && c.refs.size() == 1) c.fixedAt = pointOf(c.refs[0]);
    constraints_.push_back(std::move(c));
    ++revision_;
    return constraints_.back().id;
}
bool Sketch::removeConstraint(quint64 id) {
    auto it = std::find_if(constraints_.begin(), constraints_.end(), [id](const Constraint& c) { return c.id == id; });
    if (it == constraints_.end()) return false;
    constraints_.erase(it); ++revision_;
    return true;
}
Constraint* Sketch::constraint(quint64 id) { for (auto& c : constraints_) if (c.id == id) return &c; return nullptr; }
bool Sketch::setValue(quint64 id, double v) { Constraint* c = constraint(id); if (!c || !c->hasValue()) return false; c->value = v; ++revision_; return true; }
std::vector<quint64> Sketch::constraintsOf(quint64 entityId) const {
    std::vector<quint64> out;
    for (const Constraint& c : constraints_) for (const Ref& r : c.refs) if (r.entity == entityId) { out.push_back(c.id); break; }
    return out;
}
Point Sketch::pointOf(const Ref& r) const {
    const SketchEntity* e = entity(r.entity);
    if (!e) return {};
    if (r.point >= 0 && r.point < e->pointCount()) return e->point(r.point);
    if (e->kind == Kind::Line) return mid(e->point(0), e->point(1));
    return e->center();
}

static QString refName(const Sketch& s, const Ref& r) {
    const SketchEntity* e = s.entity(r.entity);
    if (!e) return QStringLiteral("?");
    QString base;
    switch (e->kind) { case Kind::Point: base = "P"; break; case Kind::Line: base = "L"; break; case Kind::Circle: base = "C"; break; case Kind::Arc: base = "A"; break; }
    base += QString::number(e->id);
    if (r.point >= 0 && e->kind != Kind::Point) base += QStringLiteral(".%1").arg(r.point + 1);
    return base;
}

QString Sketch::describe(const Constraint& c) const {
    QStringList rs; for (const Ref& r : c.refs) rs << refName(*this, r);
    QString s = constraintTypeName(c.type) + QStringLiteral(" ") + rs.join(QStringLiteral(", "));
    if (c.type == CType::Angle) s += QStringLiteral(" = %1°").arg(deg(c.value), 0, 'f', 1);
    else if (c.hasValue()) s += QStringLiteral(" = %1").arg(Units::format(c.value, Unit::Millimeter, 2, true));
    return s;
}

// ---------------------------------------------------------------- solver
namespace {
struct Res {                                  // one scalar residual with the entity indices it depends on
    std::function<double()> f;
    std::vector<int> ents;
    int constraint;                           // index into constraints_
};

double lineDist(Point a, Point b, Point p) { Vec d = b - a; double l = d.length(); return l < 1e-12 ? dist(a, p) : d.cross(p - a) / l; }
double wrapAngle(double a) { while (a > kPi) a -= 2 * kPi; while (a <= -kPi) a += 2 * kPi; return a; }
constexpr double kAngScale = 100.0;           // angular residuals in «mm» so that one tolerance fits all

// conjugate gradient on (JᵀJ + λI) x = b with sparse J given as rows of (col, val)
using SparseRow = std::vector<std::pair<int, double>>;
void applyA(const std::vector<SparseRow>& J, double lambda, const std::vector<double>& x, std::vector<double>& out) {
    std::fill(out.begin(), out.end(), 0.0);
    for (const SparseRow& row : J) {
        double jx = 0; for (const auto& [c, v] : row) jx += v * x[c];
        for (const auto& [c, v] : row) out[c] += v * jx;
    }
    for (size_t i = 0; i < x.size(); ++i) out[i] += lambda * x[i];
}
std::vector<double> cg(const std::vector<SparseRow>& J, double lambda, const std::vector<double>& b) {
    size_t n = b.size();
    std::vector<double> x(n, 0.0), r = b, p = b, Ap(n);
    double rr = 0; for (double v : r) rr += v * v;
    if (rr < 1e-30) return x;
    for (size_t it = 0; it < n * 2 + 10; ++it) {
        applyA(J, lambda, p, Ap);
        double pAp = 0; for (size_t i = 0; i < n; ++i) pAp += p[i] * Ap[i];
        if (std::abs(pAp) < 1e-300) break;
        double alpha = rr / pAp;
        for (size_t i = 0; i < n; ++i) { x[i] += alpha * p[i]; r[i] -= alpha * Ap[i]; }
        double rr2 = 0; for (double v : r) rr2 += v * v;
        if (rr2 < 1e-24) break;
        double beta = rr2 / rr; rr = rr2;
        for (size_t i = 0; i < n; ++i) p[i] = r[i] + beta * p[i];
    }
    return x;
}
} // namespace

struct Sketch::Residuals {
    std::vector<Res> list;
    std::vector<double*> params;              // free parameters
    std::vector<std::vector<int>> paramsOfEntity;   // entity index → param indices
    std::unordered_map<quint64, int> index;   // entity id → index
};

void Sketch::collectParams(std::vector<double*>& params, std::vector<std::vector<int>>& byEntity) {
    byEntity.assign(entities_.size(), {});
    for (size_t i = 0; i < entities_.size(); ++i) {
        SketchEntity& e = entities_[i];
        if (e.fixed) continue;
        for (int k = 0; k < e.paramCount(); ++k) { byEntity[i].push_back(int(params.size())); params.push_back(&e.p[k]); }
    }
}

static void buildResiduals(Sketch& s, Sketch::Residuals& R, std::vector<SketchEntity>& ents, const std::vector<Constraint>& cons) {
    (void)s;
    for (size_t i = 0; i < ents.size(); ++i) R.index[ents[i].id] = int(i);
    auto idx = [&](const Ref& r) -> int { auto it = R.index.find(r.entity); return it == R.index.end() ? -1 : it->second; };
    auto pt = [&](const Ref& r) -> std::function<Point()> { int i = idx(r); int k = r.point; return [&ents, i, k] { return ents[i].point(std::max(0, k)); }; };
    auto dir = [&](const Ref& r) -> std::function<Vec()> { int i = idx(r); return [&ents, i] { const auto& p = ents[i].p; return Vec{p[2] - p[0], p[3] - p[1]}; }; };
    auto rad = [&](const Ref& r) -> std::function<double()> { int i = idx(r); return [&ents, i] { return ents[i].p[2]; }; };
    auto isLine = [&](const Ref& r) { int i = idx(r); return i >= 0 && ents[i].kind == Kind::Line && r.point < 0; };
    auto isRound = [&](const Ref& r) { int i = idx(r); return i >= 0 && (ents[i].kind == Kind::Circle || ents[i].kind == Kind::Arc) && r.point < 0; };
    auto isPt = [&](const Ref& r) { int i = idx(r); return i >= 0 && (r.point >= 0 || ents[i].kind == Kind::Point); };

    for (size_t ci = 0; ci < cons.size(); ++ci) {
        const Constraint& c = cons[ci];
        bool ok = std::all_of(c.refs.begin(), c.refs.end(), [&](const Ref& r) { return idx(r) >= 0; });
        if (!ok || c.refs.empty()) continue;
        std::vector<int> ents_; for (const Ref& r : c.refs) ents_.push_back(idx(r));
        auto add = [&](std::function<double()> f) { R.list.push_back({std::move(f), ents_, int(ci)}); };
        const Ref& a = c.refs[0];
        const Ref b = c.refs.size() > 1 ? c.refs[1] : Ref{};
        switch (c.type) {
        case CType::Coincident:
            if (c.refs.size() == 2 && isPt(a) && isPt(b)) { auto pa = pt(a), pb = pt(b); add([pa, pb] { return pa().x - pb().x; }); add([pa, pb] { return pa().y - pb().y; }); }
            break;
        case CType::Horizontal:
            if (c.refs.size() == 1 && isLine(a)) { auto d = dir(a); add([d] { return d().y; }); }
            else if (c.refs.size() == 2 && isPt(a) && isPt(b)) { auto pa = pt(a), pb = pt(b); add([pa, pb] { return pa().y - pb().y; }); }
            break;
        case CType::Vertical:
            if (c.refs.size() == 1 && isLine(a)) { auto d = dir(a); add([d] { return d().x; }); }
            else if (c.refs.size() == 2 && isPt(a) && isPt(b)) { auto pa = pt(a), pb = pt(b); add([pa, pb] { return pa().x - pb().x; }); }
            break;
        case CType::Parallel:
            if (c.refs.size() == 2 && isLine(a) && isLine(b)) { auto d1 = dir(a), d2 = dir(b); add([d1, d2] { Vec u = d1(), v = d2(); double l = u.length() * v.length(); return l < 1e-12 ? 0.0 : kAngScale * u.cross(v) / l; }); }
            break;
        case CType::Perpendicular:
            if (c.refs.size() == 2 && isLine(a) && isLine(b)) { auto d1 = dir(a), d2 = dir(b); add([d1, d2] { Vec u = d1(), v = d2(); double l = u.length() * v.length(); return l < 1e-12 ? 0.0 : kAngScale * u.dot(v) / l; }); }
            break;
        case CType::Tangent:
            if (c.refs.size() == 2) {
                const Ref* ln = isLine(a) ? &a : isLine(b) ? &b : nullptr;
                const Ref* cr = isRound(a) ? &a : isRound(b) ? &b : nullptr;
                if (ln && cr) { auto p0 = pt({ln->entity, 0}), p1 = pt({ln->entity, 1}), cc = pt({cr->entity, 0}); auto r = rad(*cr); add([p0, p1, cc, r] { return std::abs(lineDist(p0(), p1(), cc())) - r(); }); }
                else if (isRound(a) && isRound(b)) {
                    auto c1 = pt({a.entity, 0}), c2 = pt({b.entity, 0}); auto r1 = rad(a), r2 = rad(b);
                    bool external = std::abs(dist(c1(), c2()) - (r1() + r2())) <= std::abs(dist(c1(), c2()) - std::abs(r1() - r2()));
                    add([c1, c2, r1, r2, external] { return dist(c1(), c2()) - (external ? r1() + r2() : std::abs(r1() - r2())); });
                }
            }
            break;
        case CType::Equal:
            if (c.refs.size() == 2 && isLine(a) && isLine(b)) { auto d1 = dir(a), d2 = dir(b); add([d1, d2] { return d1().length() - d2().length(); }); }
            else if (c.refs.size() == 2 && isRound(a) && isRound(b)) { auto r1 = rad(a), r2 = rad(b); add([r1, r2] { return r1() - r2(); }); }
            break;
        case CType::Fixed:
            if (c.refs.size() == 1 && isPt(a)) { auto pa = pt(a); Point at = c.fixedAt; add([pa, at] { return pa().x - at.x; }); add([pa, at] { return pa().y - at.y; }); }
            break;
        case CType::Distance:
            if (c.refs.size() == 2 && isPt(a) && isPt(b)) { auto pa = pt(a), pb = pt(b); double v = c.value; add([pa, pb, v] { return dist(pa(), pb()) - v; }); }
            else if (c.refs.size() == 2 && (isPt(a) && isLine(b))) { auto pa = pt(a), p0 = pt({b.entity, 0}), p1 = pt({b.entity, 1}); double v = c.value; add([pa, p0, p1, v] { return std::abs(lineDist(p0(), p1(), pa())) - v; }); }
            else if (c.refs.size() == 2 && (isLine(a) && isPt(b))) { auto pb = pt(b), p0 = pt({a.entity, 0}), p1 = pt({a.entity, 1}); double v = c.value; add([pb, p0, p1, v] { return std::abs(lineDist(p0(), p1(), pb())) - v; }); }
            break;
        case CType::Length:
            if (c.refs.size() == 1 && isLine(a)) { auto d = dir(a); double v = c.value; add([d, v] { return d().length() - v; }); }
            break;
        case CType::Radius:
            if (c.refs.size() == 1 && isRound(a)) { auto r = rad(a); double v = c.value; add([r, v] { return r() - v; }); }
            break;
        case CType::Diameter:
            if (c.refs.size() == 1 && isRound(a)) { auto r = rad(a); double v = c.value; add([r, v] { return 2 * r() - v; }); }
            break;
        case CType::Angle:
            if (c.refs.size() == 2 && isLine(a) && isLine(b)) { auto d1 = dir(a), d2 = dir(b); double v = c.value; add([d1, d2, v] { Vec u = d1(), w = d2(); return kAngScale * wrapAngle(std::atan2(u.cross(w), u.dot(w)) - v); }); }
            break;
        case CType::PointOnLine:
            if (c.refs.size() == 2 && isPt(a) && isLine(b)) { auto pa = pt(a), p0 = pt({b.entity, 0}), p1 = pt({b.entity, 1}); add([pa, p0, p1] { return lineDist(p0(), p1(), pa()); }); }
            break;
        case CType::PointOnCircle:
            if (c.refs.size() == 2 && isPt(a) && isRound(b)) { auto pa = pt(a), cc = pt({b.entity, 0}); auto r = rad(b); add([pa, cc, r] { return dist(pa(), cc()) - r(); }); }
            break;
        case CType::Symmetric:
            if (c.refs.size() == 3 && isPt(a) && isPt(b) && isLine(c.refs[2])) {
                auto pa = pt(a), pb = pt(b), p0 = pt({c.refs[2].entity, 0}), p1 = pt({c.refs[2].entity, 1});
                add([pa, pb, p0, p1] { return lineDist(p0(), p1(), mid(pa(), pb())); });
                add([pa, pb, p0, p1] { Vec d = (p1() - p0()).normalized(); return (pb() - pa()).dot(d); });
            }
            break;
        }
    }
}

double Sketch::residual(const Constraint& c) const {
    Sketch& self = const_cast<Sketch&>(*this);
    Residuals R;
    std::vector<Constraint> one{c};
    buildResiduals(self, R, self.entities_, one);
    double m = 0; for (const Res& r : R.list) m = std::max(m, std::abs(r.f()));
    return m;
}

SolveResult Sketch::solve(bool computeDof, int maxIterations) {
    SolveResult out;
    for (Constraint& c : constraints_) c.conflict = false;
    Residuals R;
    collectParams(R.params, R.paramsOfEntity);
    buildResiduals(*this, R, entities_, constraints_);
    const size_t n = R.params.size(), m = R.list.size();
    const double tol = 1e-6;
    auto maxRes = [&](std::vector<double>* vec) { double mx = 0; for (size_t i = 0; i < m; ++i) { double v = R.list[i].f(); if (vec) (*vec)[i] = v; mx = std::max(mx, std::abs(v)); } return mx; };
    std::vector<double> r(m);
    double cur = maxRes(&r);
    double lambda = 1e-3;
    std::vector<double> x0(n);
    for (size_t i = 0; i < n; ++i) x0[i] = *R.params[i];
    std::vector<SparseRow> J(m);
    int it = 0;
    for (; it < maxIterations && cur > tol && n > 0 && m > 0; ++it) {
        // numeric Jacobian over the entities each residual touches
        for (size_t i = 0; i < m; ++i) {
            J[i].clear();
            for (int ei : R.list[i].ents) for (int pj : R.paramsOfEntity[ei]) {
                if (std::any_of(J[i].begin(), J[i].end(), [pj](const auto& cv) { return cv.first == pj; })) continue;
                double* p = R.params[pj]; double save = *p; double h = 1e-6 * std::max(1.0, std::abs(save));
                *p = save + h; double f1 = R.list[i].f(); *p = save - h; double f0 = R.list[i].f(); *p = save;
                double d = (f1 - f0) / (2 * h);
                if (std::abs(d) > 1e-12) J[i].push_back({pj, d});
            }
        }
        // b = -Jᵀ r  (plus a weak pull toward the pre-solve position: minimal change)
        std::vector<double> b(n, 0.0);
        for (size_t i = 0; i < m; ++i) for (const auto& [c, v] : J[i]) b[c] -= v * r[i];
        bool accepted = false;
        for (int tries = 0; tries < 6 && !accepted; ++tries) {
            std::vector<double> dx = cg(J, lambda, b);
            std::vector<double> save(n); for (size_t i = 0; i < n; ++i) { save[i] = *R.params[i]; *R.params[i] += dx[i]; }
            std::vector<double> r2(m);
            double nxt = maxRes(&r2);
            if (nxt < cur) { cur = nxt; r = r2; lambda = std::max(lambda / 3, 1e-9); accepted = true; }
            else { for (size_t i = 0; i < n; ++i) *R.params[i] = save[i]; lambda *= 8; }
        }
        if (!accepted) break;
    }
    out.iterations = it;
    out.maxResidual = cur;
    out.converged = cur <= tol * 10 || m == 0;
    if (!out.converged) {
        for (size_t i = 0; i < m; ++i) if (std::abs(r[i]) > 1e-4) { Constraint& c = constraints_[R.list[i].constraint]; if (!c.conflict) { c.conflict = true; out.conflicts.push_back(c.id); } }
    }
    for (SketchEntity& e : entities_) if (e.kind == Kind::Circle || e.kind == Kind::Arc) e.p[2] = std::abs(e.p[2]);
    ++revision_;
    if (computeDof) out.dof = degreesOfFreedom();
    return out;
}

void Sketch::computeDof() {
    Residuals R;
    collectParams(R.params, R.paramsOfEntity);
    buildResiduals(*this, R, entities_, constraints_);
    const size_t n = R.params.size(), m = R.list.size();
    // dense Jacobian rows → modified Gram-Schmidt rank
    std::vector<std::vector<double>> rows;
    rows.reserve(m);
    for (size_t i = 0; i < m; ++i) {
        std::vector<double> row(n, 0.0);
        for (int ei : R.list[i].ents) for (int pj : R.paramsOfEntity[ei]) {
            double* p = R.params[pj]; double save = *p; double h = 1e-6 * std::max(1.0, std::abs(save));
            *p = save + h; double f1 = R.list[i].f(); *p = save - h; double f0 = R.list[i].f(); *p = save;
            row[pj] = (f1 - f0) / (2 * h);
        }
        rows.push_back(std::move(row));
    }
    int rank = 0;
    std::vector<std::vector<double>> basis;
    for (auto& row : rows) {
        double norm0 = 0; for (double v : row) norm0 += v * v; norm0 = std::sqrt(norm0);
        if (norm0 < 1e-12) continue;
        for (const auto& q : basis) { double d = 0; for (size_t k = 0; k < n; ++k) d += q[k] * row[k]; for (size_t k = 0; k < n; ++k) row[k] -= d * q[k]; }
        double norm = 0; for (double v : row) norm += v * v; norm = std::sqrt(norm);
        if (norm > 1e-6 * norm0) { for (double& v : row) v /= norm; basis.push_back(row); ++rank; }
    }
    dofCache_ = int(n) - rank;
    // параметр k закреплён ⇔ e_k лежит в пространстве строк якобиана
    freeCache_.assign(entities_.size(), false);
    for (size_t ei = 0; ei < entities_.size(); ++ei) {
        for (int pj : R.paramsOfEntity[ei]) {
            double inSpace = 0;
            for (const auto& q : basis) inSpace += q[size_t(pj)] * q[size_t(pj)];
            if (1.0 - inSpace > 1e-6) { freeCache_[ei] = true; break; }
        }
    }
    dofRevision_ = revision_;
}

int Sketch::degreesOfFreedom() {
    if (dofRevision_ != revision_ || dofCache_ < 0) computeDof();
    return dofCache_;
}

const std::vector<bool>& Sketch::freeEntities() {
    if (dofRevision_ != revision_ || dofCache_ < 0) computeDof();
    return freeCache_;
}

void Sketch::dragPoint(const Ref& r, Point to) {
    SketchEntity* e = entity(r.entity);
    if (!e || e->fixed) return;
    switch (e->kind) {
    case Kind::Point: e->p[0] = to.x; e->p[1] = to.y; break;
    case Kind::Line: if (r.point == 1) { e->p[2] = to.x; e->p[3] = to.y; } else { e->p[0] = to.x; e->p[1] = to.y; } break;
    case Kind::Circle: e->p[0] = to.x; e->p[1] = to.y; break;
    case Kind::Arc:
        if (r.point == 0) { e->p[0] = to.x; e->p[1] = to.y; }
        else { double ang = (to - e->center()).angle(); if (r.point == 1) e->p[3] = ang; else e->p[4] = ang; }
        break;
    }
    ++revision_;
    solve(false, 30);
}

void Sketch::moveEntity(quint64 id, Vec d) {
    SketchEntity* e = entity(id);
    if (!e || e->fixed) return;
    switch (e->kind) {
    case Kind::Point: case Kind::Circle: case Kind::Arc: e->p[0] += d.x; e->p[1] += d.y; break;
    case Kind::Line: e->p[0] += d.x; e->p[1] += d.y; e->p[2] += d.x; e->p[3] += d.y; break;
    }
    ++revision_;
    solve(false, 30);
}

std::vector<quint64> Sketch::autoConstrain(quint64 entityId, double tol, double angleTolDeg) {
    std::vector<quint64> added;
    const SketchEntity* e = entity(entityId);
    if (!e) return added;
    auto exists = [&](CType t, const Ref& a, const Ref& b) {
        for (const Constraint& c : constraints_) if (c.type == t && c.refs.size() == 2 && ((c.refs[0] == a && c.refs[1] == b) || (c.refs[0] == b && c.refs[1] == a))) return true;
        return false;
    };
    for (int k = 0; k < e->pointCount(); ++k) {
        if ((e->kind == Kind::Circle || e->kind == Kind::Arc) && k == 0) continue;   // centres are not auto-joined
        Point p = e->point(k);
        for (const SketchEntity& o : entities_) {
            if (o.id == entityId) continue;
            for (int j = 0; j < o.pointCount(); ++j) {
                if ((o.kind == Kind::Circle || o.kind == Kind::Arc) && j == 0) continue;
                if (dist(o.point(j), p) <= tol && !exists(CType::Coincident, {entityId, k}, {o.id, j})) {
                    Constraint c; c.type = CType::Coincident; c.refs = {{entityId, k}, {o.id, j}};
                    added.push_back(addConstraint(c));
                    goto nextPoint;
                }
            }
        }
    nextPoint:;
    }
    if (e->kind == Kind::Line) {
        Vec d = e->point(1) - e->point(0);
        double ang = std::abs(deg(std::atan2(std::abs(d.y), std::abs(d.x))));
        if (d.length() > tol) {
            if (ang <= angleTolDeg) { Constraint c; c.type = CType::Horizontal; c.refs = {{entityId, -1}}; added.push_back(addConstraint(c)); }
            else if (ang >= 90 - angleTolDeg) { Constraint c; c.type = CType::Vertical; c.refs = {{entityId, -1}}; added.push_back(addConstraint(c)); }
        }
    }
    if (!added.empty()) solve(false, 30);
    return added;
}

std::vector<Entity> Sketch::toDraftEntities() const {
    std::vector<Entity> out;
    for (const SketchEntity& e : entities_) {
        if (e.construction) continue;
        switch (e.kind) {
        case Kind::Point: break;
        case Kind::Line: if (dist(e.point(0), e.point(1)) > 1e-9) out.push_back(makeLine(e.point(0), e.point(1))); break;
        case Kind::Circle: if (e.p[2] > 1e-9) out.push_back(makeCircle(e.center(), e.p[2])); break;
        case Kind::Arc: if (e.p[2] > 1e-9) out.push_back(makeArc(e.center(), e.p[2], e.p[3], e.p[4])); break;
        }
    }
    return out;
}

Rect Sketch::extents() const { Rect r; for (const SketchEntity& e : entities_) r.add(e.bbox()); return r; }

QJsonObject Sketch::toJson() const {
    QJsonObject o; o["id"] = double(id); o["name"] = name; o["plane"] = plane; o["next"] = double(nextId_);
    QJsonArray es; for (const SketchEntity& e : entities_) es.push_back(e.toJson());
    QJsonArray cs; for (const Constraint& c : constraints_) cs.push_back(c.toJson());
    o["entities"] = es; o["constraints"] = cs;
    return o;
}
Sketch Sketch::fromJson(const QJsonObject& o) {
    Sketch s; s.id = quint64(o.value("id").toDouble()); s.name = o.value("name").toString(s.name); s.plane = o.value("plane").toString("XY");
    for (const auto& v : o.value("entities").toArray()) s.entities_.push_back(SketchEntity::fromJson(v.toObject()));
    for (const auto& v : o.value("constraints").toArray()) s.constraints_.push_back(Constraint::fromJson(v.toObject()));
    s.nextId_ = quint64(o.value("next").toDouble(1));
    for (const auto& e : s.entities_) s.nextId_ = std::max(s.nextId_, e.id + 1);
    for (const auto& c : s.constraints_) s.nextId_ = std::max(s.nextId_, c.id + 1);
    return s;
}

} // namespace psi::sk
