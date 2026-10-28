#include "modules/model/part.h"
#include "modules/sketcher/sketch.h"
#include <QJsonDocument>
#include <QtEndian>
#include <algorithm>
#include <cmath>

namespace psi::md {

// ---------------------------------------------------------------- типы операций
namespace {
struct TypeInfo { FeatureType t; const char* key; const char* name; const char* icon; };
const TypeInfo kTypes[] = {
    {FeatureType::Sketch, "sketch", "Эскиз", "sketch"},
    {FeatureType::Extrude, "extrude", "Выдавливание", "extrude"},
    {FeatureType::Revolve, "revolve", "Вращение", "revolve"},
    {FeatureType::Hole, "hole", "Отверстие", "hole"},
    {FeatureType::Fillet, "fillet", "Скругление", "fillet"},
    {FeatureType::Chamfer, "chamfer", "Фаска", "chamfer"},
    {FeatureType::LinearPattern, "linearPattern", "Массив линейный", "array"},
    {FeatureType::CircularPattern, "circularPattern", "Массив круговой", "patternCircular"},
    {FeatureType::Mirror, "mirror", "Зеркало", "mirror"},
    {FeatureType::Boolean, "boolean", "Булева операция", "union"},
    {FeatureType::DatumPlane, "datumPlane", "Плоскость", "plane"},
    {FeatureType::DatumAxis, "datumAxis", "Ось", "axis"},
    {FeatureType::Shell, "shell", "Оболочка", "shell"},
    {FeatureType::MeshBody, "meshBody", "Тело-сетка", "stl"},
    {FeatureType::ImportBody, "importBody", "Импорт", "step"},
};
const TypeInfo& info(FeatureType t) { for (const auto& i : kTypes) if (i.t == t) return i; return kTypes[0]; }

Vec3 vec(const QJsonValue& v, const Vec3& def = {}) {
    QJsonArray a = v.toArray();
    if (a.size() != 3) return def;
    return {a[0].toDouble(), a[1].toDouble(), a[2].toDouble()};
}
std::vector<int> ints(const QJsonValue& v) { std::vector<int> r; for (const QJsonValue& x : v.toArray()) r.push_back(x.toInt()); return r; }
} // namespace

QString featureTypeName(FeatureType t) { return QString::fromUtf8(info(t).name); }
QString featureTypeKey(FeatureType t) { return QString::fromLatin1(info(t).key); }
QString featureIcon(FeatureType t) { return QString::fromLatin1(info(t).icon); }
FeatureType featureTypeFromKey(const QString& key, bool* ok) {
    for (const auto& i : kTypes) if (key == QLatin1String(i.key)) { if (ok) *ok = true; return i.t; }
    if (ok) *ok = false;
    return FeatureType::Sketch;
}

QJsonObject Feature::toJson() const {
    QJsonObject o{{"id", double(id)}, {"type", featureTypeKey(type)}, {"name", name}, {"params", params}};
    if (suppressed) o["suppressed"] = true;
    return o;
}
Feature Feature::fromJson(const QJsonObject& o) {
    Feature f;
    f.id = quint64(o["id"].toDouble());
    f.type = featureTypeFromKey(o["type"].toString());
    f.name = o["name"].toString();
    f.suppressed = o["suppressed"].toBool(false);
    f.params = o["params"].toObject();
    return f;
}

QJsonObject Material::toJson() const { return {{"name", name}, {"density", density}, {"color", color}}; }
Material Material::fromJson(const QJsonObject& o) {
    Material m;
    if (o.contains("name")) m.name = o["name"].toString();
    if (o.contains("density")) m.density = o["density"].toDouble(m.density);
    if (o.contains("color")) m.color = o["color"].toString();
    return m;
}

// ---------------------------------------------------------------- эскиз → профиль
Profile profileFromSketch(const sk::Sketch& sketch, QString* warning) {
    Profile prof;
    struct Seg { double x1, y1, x2, y2, bulge; bool used = false; };
    std::vector<Seg> segs;
    for (const sk::SketchEntity& e : sketch.entities()) {
        if (e.construction) continue;
        if (e.kind == sk::Kind::Circle && e.p.size() >= 3 && e.p[2] > 1e-9) prof.circles.push_back({e.p[0], e.p[1], e.p[2]});
        else if (e.kind == sk::Kind::Line && e.p.size() >= 4) segs.push_back({e.p[0], e.p[1], e.p[2], e.p[3], 0});
        else if (e.kind == sk::Kind::Arc && e.p.size() >= 5) {
            double cx = e.p[0], cy = e.p[1], r = e.p[2], a0 = e.p[3], a1 = e.p[4];
            double sweep = a1 - a0;
            while (sweep <= 0) sweep += 2 * M_PI;
            while (sweep > 2 * M_PI) sweep -= 2 * M_PI;
            segs.push_back({cx + r * std::cos(a0), cy + r * std::sin(a0), cx + r * std::cos(a1), cy + r * std::sin(a1), std::tan(sweep / 4)});
        }
    }
    const double tol = 1e-6;
    auto near = [&](double ax, double ay, double bx, double by) { return std::hypot(ax - bx, ay - by) < tol; };
    int open = 0;
    for (size_t start = 0; start < segs.size(); ++start) {
        if (segs[start].used) continue;
        ProfileLoop loop;
        segs[start].used = true;
        loop.segments.push_back({segs[start].x1, segs[start].y1, segs[start].x2, segs[start].y2, segs[start].bulge});
        double cx = segs[start].x2, cy = segs[start].y2;
        const double sx = segs[start].x1, sy = segs[start].y1;
        bool closed = near(cx, cy, sx, sy);
        while (!closed) {
            bool found = false;
            for (Seg& s : segs) {
                if (s.used) continue;
                if (near(s.x1, s.y1, cx, cy)) { s.used = true; loop.segments.push_back({s.x1, s.y1, s.x2, s.y2, s.bulge}); cx = s.x2; cy = s.y2; found = true; }
                else if (near(s.x2, s.y2, cx, cy)) { s.used = true; loop.segments.push_back({s.x2, s.y2, s.x1, s.y1, -s.bulge}); cx = s.x1; cy = s.y1; found = true; }
                if (found) break;
            }
            if (!found) break;
            closed = near(cx, cy, sx, sy);
        }
        if (closed && loop.segments.size() >= 2) prof.loops.push_back(loop);
        else ++open;
    }
    if (open && warning) *warning = QStringLiteral("%1 незамкнутых цепочек пропущено").arg(open);
    return prof;
}

// ---------------------------------------------------------------- Part
Part::Part() = default;

Feature* Part::feature(quint64 id) { for (Feature& f : features_) if (f.id == id) return &f; return nullptr; }
const Feature* Part::feature(quint64 id) const { for (const Feature& f : features_) if (f.id == id) return &f; return nullptr; }
int Part::indexOf(quint64 id) const { for (size_t i = 0; i < features_.size(); ++i) if (features_[i].id == id) return int(i); return -1; }

quint64 Part::addFeature(FeatureType type, const QJsonObject& params, const QString& name) {
    Feature f;
    f.id = nextId_++;
    f.type = type;
    f.params = params;
    int n = 0; for (const Feature& g : features_) if (g.type == type) ++n;
    f.name = name.isEmpty() ? QStringLiteral("%1 %2").arg(featureTypeName(type)).arg(n + 1) : name;
    features_.push_back(f);
    dirtyFrom_ = dirtyFrom_ < 0 ? int(features_.size()) - 1 : std::min(dirtyFrom_, int(features_.size()) - 1);
    return f.id;
}

bool Part::removeFeature(quint64 id) {
    int i = indexOf(id);
    if (i < 0) return false;
    features_.erase(features_.begin() + i);
    dirtyFrom_ = dirtyFrom_ < 0 ? i : std::min(dirtyFrom_, i);
    return true;
}

bool Part::moveFeature(quint64 id, int newIndex) {
    int i = indexOf(id);
    if (i < 0 || newIndex < 0 || newIndex >= int(features_.size()) || i == newIndex) return false;
    Feature f = features_[size_t(i)];
    features_.erase(features_.begin() + i);
    features_.insert(features_.begin() + newIndex, f);
    int from = std::min(i, newIndex);
    dirtyFrom_ = dirtyFrom_ < 0 ? from : std::min(dirtyFrom_, from);
    return true;
}

bool Part::setParams(quint64 id, const QJsonObject& params) {
    int i = indexOf(id);
    if (i < 0) return false;
    features_[size_t(i)].params = params;
    dirtyFrom_ = dirtyFrom_ < 0 ? i : std::min(dirtyFrom_, i);
    return true;
}

bool Part::setSuppressed(quint64 id, bool on) {
    int i = indexOf(id);
    if (i < 0 || features_[size_t(i)].suppressed == on) return false;
    features_[size_t(i)].suppressed = on;
    dirtyFrom_ = dirtyFrom_ < 0 ? i : std::min(dirtyFrom_, i);
    return true;
}

bool Part::rename(quint64 id, const QString& name) { Feature* f = feature(id); if (!f) return false; f->name = name; return true; }

Plane Part::namedPlane(const QString& name, double offset) {
    if (name == QLatin1String("XZ")) return Plane::XZ(offset);
    if (name == QLatin1String("YZ")) return Plane::YZ(offset);
    return Plane::XY(offset);
}

Plane Part::planeOf(quint64 datumId, bool* ok) const {
    if (ok) *ok = false;
    const Feature* f = feature(datumId);
    if (!f) return Plane::XY();
    if (f->type == FeatureType::DatumPlane || f->type == FeatureType::Sketch) {
        QString err;
        Plane p = sketchPlane(*f, &err);
        if (ok) *ok = err.isEmpty();
        return p;
    }
    return Plane::XY();
}

bool Part::axisOf(quint64 datumId, Vec3& point, Vec3& dir) const {
    const Feature* f = feature(datumId);
    if (!f || f->type != FeatureType::DatumAxis) return false;
    const QString base = f->params["base"].toString();
    point = vec(f->params["point"]);
    if (base == QLatin1String("X")) dir = {1, 0, 0};
    else if (base == QLatin1String("Y")) dir = {0, 1, 0};
    else if (base == QLatin1String("Z")) dir = {0, 0, 1};
    else dir = vec(f->params["dir"], {0, 0, 1});
    return dir.length() > 1e-12;
}

// Плоскость эскиза / опорной плоскости: стандартная со смещением, плоскость грани или другая опорная плоскость.
Plane Part::sketchPlane(const Feature& f, QString* error) const {
    const QString kind = f.params.contains("plane") ? f.params["plane"].toString() : f.params["base"].toString();
    const double offset = f.params["offset"].toDouble();
    if (kind == QLatin1String("face")) {
        const quint64 ref = quint64(f.params["faceFeature"].toDouble());
        const int faceIndex = f.params["faceIndex"].toInt();
        int idx = indexOf(ref);
        int self = indexOf(f.id);
        if (idx < 0 || idx >= self || idx >= int(states_.size())) { if (error) *error = QStringLiteral("Грань: опорная операция не найдена или стоит ниже"); return Plane::XY(); }
        // грань ищем во всех телах состояния после опорной операции
        for (const Body& b : states_[size_t(idx)].bodies) {
            bool ok = false;
            Plane p = b.shape.planeOfFace(faceIndex, &ok);
            if (ok) { p.origin = p.origin + p.normal * offset; return p; }
        }
        if (error) *error = QStringLiteral("Грань %1 не плоская или не найдена").arg(faceIndex);
        return Plane::XY();
    }
    if (kind == QLatin1String("datum")) {
        const quint64 ref = quint64(f.params["datum"].toDouble());
        const Feature* d = feature(ref);
        if (!d || indexOf(ref) >= indexOf(f.id)) { if (error) *error = QStringLiteral("Опорная плоскость не найдена"); return Plane::XY(); }
        Plane p = sketchPlane(*d, error);
        p.origin = p.origin + p.normal * offset;
        return p;
    }
    return namedPlane(kind.isEmpty() ? QStringLiteral("XY") : kind, offset);
}

Body* Part::bodyAt(State& st, int index) const {
    if (st.bodies.empty()) return nullptr;
    if (index < 0 || index >= int(st.bodies.size())) return &st.bodies.back();
    return &st.bodies[size_t(index)];
}

// Применить тело-инструмент к состоянию: new → новое тело, add → объединить, cut → вычесть.
bool Part::applyTool(const Feature& f, const Shape& tool, const QString& mode, int bodyIndex, State& st, bool meshOk) {
    if (mode == QLatin1String("new") || st.bodies.empty()) {
        Body b; b.name = f.name; b.shape = tool; b.origin = f.id; b.mesh = meshOk;
        st.bodies.push_back(b);
        return true;
    }
    Body* target = bodyAt(st, bodyIndex);
    if (!target) return false;
    if (target->mesh) { const_cast<Feature&>(f).error = QStringLiteral("%1: тело-сетка не поддерживает булевы операции").arg(f.name); return false; }
    Result r = Kernel::boolean(target->shape, tool, mode == QLatin1String("cut") ? BoolOp::Subtract : BoolOp::Union);
    if (!r.ok()) { const_cast<Feature&>(f).error = QStringLiteral("%1: %2").arg(f.name, r.error); return false; }
    target->shape = r.shape;
    return true;
}

// Тело-инструмент операции (без применения): призма, тело вращения, цилиндр отверстия.
Shape Part::buildTool(const Feature& f, const State& st, QString* error, QString* mode, int* bodyIndex) const {
    auto fail = [&](const QString& e) { if (error) *error = e; return Shape(); };
    if (mode) *mode = f.params.contains("mode") ? f.params["mode"].toString() : QStringLiteral("add");
    if (bodyIndex) *bodyIndex = f.params.contains("body") ? f.params["body"].toInt() : -1;
    switch (f.type) {
    case FeatureType::Extrude:
    case FeatureType::Revolve: {
        const quint64 sid = quint64(f.params["sketch"].toDouble());
        const Feature* s = feature(sid);
        if (!s || s->type != FeatureType::Sketch) return fail(QStringLiteral("эскиз не выбран"));
        if (indexOf(sid) >= indexOf(f.id)) return fail(QStringLiteral("эскиз стоит ниже операции"));
        if (!s->ok() && s->built) return fail(QStringLiteral("эскиз «%1» с ошибкой").arg(s->name));
        sk::Sketch sketch = sk::Sketch::fromJson(s->params["sketch"].toObject());
        QString warn;
        Profile prof = profileFromSketch(sketch, &warn);
        if (prof.empty()) return fail(QStringLiteral("эскиз «%1» не содержит замкнутого контура").arg(s->name));
        QString perr;
        Plane plane = sketchPlane(*s, &perr);
        if (!perr.isEmpty()) return fail(perr);
        if (f.type == FeatureType::Extrude) {
            Result r = Kernel::extrude(prof, plane, f.params["distance"].toDouble(), f.params["distance2"].toDouble(), f.params["symmetric"].toBool());
            if (!r.ok()) return fail(r.error);
            return r.shape;
        }
        Vec3 ap, ad;
        const QString axis = f.params["axis"].toString(QStringLiteral("Y"));
        if (axis == QLatin1String("X")) { ap = plane.origin; ad = plane.xdir; }
        else if (axis == QLatin1String("Y")) { ap = plane.origin; ad = plane.ydir(); }
        else if (axis == QLatin1String("datum")) { if (!axisOf(quint64(f.params["datum"].toDouble()), ap, ad)) return fail(QStringLiteral("ось не найдена")); }
        else { ap = vec(f.params["axisPoint"]); ad = vec(f.params["axisDir"], {0, 0, 1}); }
        Result r = Kernel::revolve(prof, plane, ap, ad, f.params["angle"].toDouble(360));
        if (!r.ok()) return fail(r.error);
        return r.shape;
    }
    case FeatureType::Hole: {
        if (mode) *mode = QStringLiteral("cut");
        const double d = f.params["diameter"].toDouble();
        if (d <= 0) return fail(QStringLiteral("диаметр должен быть больше нуля"));
        Vec3 pt = vec(f.params["point"]);
        Vec3 dir = vec(f.params["dir"], {0, 0, 1});
        const int faceIndex = f.params["faceIndex"].toInt();
        const Body* body = st.bodies.empty() ? nullptr : (bodyIndex && *bodyIndex >= 0 && *bodyIndex < int(st.bodies.size()) ? &st.bodies[size_t(*bodyIndex)] : &st.bodies.back());
        if (!body) return fail(QStringLiteral("нет тела для отверстия"));
        if (faceIndex > 0) dir = body->shape.faceNormalAt(faceIndex, pt);   // наружу из грани
        dir = dir.normalized();
        Bbox bb = body->shape.bbox();
        const double diag = bb.valid ? bb.size().length() : 1000;
        double depth = f.params["through"].toBool(true) ? diag * 2 : f.params["depth"].toDouble();
        if (depth <= 0) return fail(QStringLiteral("глубина должна быть больше нуля"));
        // цилиндр идёт внутрь тела (против нормали), чуть выступая наружу, чтобы отверстие вскрылось
        const double lift = 0.5;
        Vec3 base = pt + dir * lift;
        Result cyl = Kernel::cylinder(base, dir * -1, d / 2, depth + lift);
        if (!cyl.ok()) return fail(cyl.error);
        Shape tool = cyl.shape;
        if (f.params["countersink"].toBool()) {
            const double csD = f.params["csDiameter"].toDouble(d * 2);
            const double csA = f.params["csAngle"].toDouble(90);
            if (csD <= d) return fail(QStringLiteral("диаметр зенковки должен быть больше диаметра отверстия"));
            const double h = (csD - d) / 2 / std::tan(csA / 2 * M_PI / 180);
            Result cone = Kernel::cone(base, dir * -1, csD / 2 + lift * (csD - d) / 2 / h, d / 2, h + lift);
            if (!cone.ok()) return fail(cone.error);
            Result u = Kernel::boolean(tool, cone.shape, BoolOp::Union);
            if (!u.ok()) return fail(u.error);
            tool = u.shape;
        }
        return tool;
    }
    default: return fail(QStringLiteral("операция не создаёт тело-инструмент"));
    }
}

Shape Part::toolShape(quint64 featureId) const {
    int i = indexOf(featureId);
    if (i < 0 || i >= int(tools_.size())) return Shape();
    return tools_[size_t(i)];
}

bool Part::applyFeature(Feature& f, State& st) {
    f.error.clear();
    f.built = true;
    const int self = indexOf(f.id);
    auto fail = [&](QString e) {
        const QString prefix = featureTypeName(f.type) + QStringLiteral(": ");
        if (e.startsWith(prefix)) e.remove(0, prefix.size());   // ядро уже назвало операцию
        f.error = QStringLiteral("%1: %2").arg(f.name, e);
        return false;
    };
    tools_[size_t(self)] = Shape();
    switch (f.type) {
    case FeatureType::Sketch: {
        QString err;
        sketchPlane(f, &err);
        if (!err.isEmpty()) return fail(err);
        sk::Sketch sketch = sk::Sketch::fromJson(f.params["sketch"].toObject());
        if (sketch.entities().empty()) return fail(QStringLiteral("эскиз пуст"));
        return true;
    }
    case FeatureType::DatumPlane: { QString err; sketchPlane(f, &err); return err.isEmpty() ? true : fail(err); }
    case FeatureType::DatumAxis: { Vec3 p, d; return axisOf(f.id, p, d) ? true : fail(QStringLiteral("направление оси не задано")); }
    case FeatureType::Extrude:
    case FeatureType::Revolve:
    case FeatureType::Hole: {
        QString err, mode; int body = -1;
        Shape tool = buildTool(f, st, &err, &mode, &body);
        if (tool.isNull()) return fail(err);
        tools_[size_t(self)] = tool;
        return applyTool(f, tool, mode, body, st);
    }
    case FeatureType::Fillet:
    case FeatureType::Chamfer: {
        Body* b = bodyAt(st, f.params.contains("body") ? f.params["body"].toInt() : -1);
        if (!b) return fail(QStringLiteral("нет тела"));
        if (b->mesh) return fail(QStringLiteral("тело-сетка не поддерживает операцию"));
        Result r = f.type == FeatureType::Fillet ? Kernel::fillet(b->shape, ints(f.params["edges"]), f.params["radius"].toDouble())
                                                 : Kernel::chamfer(b->shape, ints(f.params["edges"]), f.params["distance"].toDouble());
        if (!r.ok()) return fail(r.error);
        b->shape = r.shape;
        return true;
    }
    case FeatureType::Shell: {
        Body* b = bodyAt(st, f.params.contains("body") ? f.params["body"].toInt() : -1);
        if (!b) return fail(QStringLiteral("нет тела"));
        if (b->mesh) return fail(QStringLiteral("тело-сетка не поддерживает операцию"));
        Result r = Kernel::shell(b->shape, ints(f.params["faces"]), f.params["thickness"].toDouble());
        if (!r.ok()) return fail(r.error);
        b->shape = r.shape;
        return true;
    }
    case FeatureType::LinearPattern:
    case FeatureType::CircularPattern:
    case FeatureType::Mirror: {
        const quint64 ref = quint64(f.params["feature"].toDouble());
        const Feature* src = feature(ref);
        QString mode = QStringLiteral("add"); int body = -1;
        Shape tool;
        bool wholeBody = false;
        if (src && indexOf(ref) < self) {
            tool = tools_[size_t(indexOf(ref))];
            if (tool.isNull()) return fail(QStringLiteral("операция «%1» не создаёт тело-инструмент").arg(src->name));
            mode = src->type == FeatureType::Hole ? QStringLiteral("cut") : src->params["mode"].toString(QStringLiteral("add"));
            if (mode == QLatin1String("new")) mode = QStringLiteral("add");
            body = src->params.contains("body") ? src->params["body"].toInt() : -1;
        } else if (f.type == FeatureType::Mirror || f.params.contains("body")) {
            Body* b = bodyAt(st, f.params.contains("body") ? f.params["body"].toInt() : -1);
            if (!b) return fail(QStringLiteral("нет тела"));
            if (b->mesh) return fail(QStringLiteral("тело-сетка не поддерживает операцию"));
            tool = b->shape;
            body = int(b - &st.bodies[0]);
            wholeBody = true;
        } else return fail(QStringLiteral("исходная операция не выбрана"));
        std::vector<Shape> copies;
        if (f.type == FeatureType::LinearPattern) {
            const int n1 = std::max(1, f.params["count"].toInt(2)), n2 = std::max(1, f.params["count2"].toInt(1));
            const Vec3 s1 = vec(f.params["step"], {50, 0, 0}), s2 = vec(f.params["step2"], {0, 50, 0});
            if (n1 * n2 > 2000) return fail(QStringLiteral("слишком много копий (%1)").arg(n1 * n2));
            if (s1.length() < 1e-9) return fail(QStringLiteral("шаг равен нулю"));
            for (int i = 0; i < n1; ++i) for (int j = 0; j < n2; ++j) {
                if (i == 0 && j == 0) continue;
                Result r = Kernel::translate(tool, s1 * i + s2 * j);
                if (!r.ok()) return fail(r.error);
                copies.push_back(r.shape);
            }
        } else if (f.type == FeatureType::CircularPattern) {
            const int n = std::max(2, f.params["count"].toInt(4));
            const double total = f.params["angle"].toDouble(360);
            Vec3 ap = vec(f.params["axisPoint"]), ad = vec(f.params["axisDir"], {0, 0, 1});
            if (f.params.contains("datum") && !axisOf(quint64(f.params["datum"].toDouble()), ap, ad)) return fail(QStringLiteral("ось не найдена"));
            if (n > 720) return fail(QStringLiteral("слишком много копий (%1)").arg(n));
            const bool full = std::abs(std::abs(total) - 360) < 1e-9;
            const double step = full ? total / n : total / (n - 1);
            for (int i = 1; i < n; ++i) {
                Result r = Kernel::rotate(tool, ap, ad, step * i);
                if (!r.ok()) return fail(r.error);
                copies.push_back(r.shape);
            }
        } else {
            Plane pl;
            const QString pk = f.params["plane"].toString(QStringLiteral("XY"));
            if (pk == QLatin1String("datum")) { bool ok = false; pl = planeOf(quint64(f.params["datum"].toDouble()), &ok); if (!ok) return fail(QStringLiteral("плоскость не найдена")); }
            else pl = namedPlane(pk, f.params["offset"].toDouble());
            Result r = Kernel::mirror(tool, pl);
            if (!r.ok()) return fail(r.error);
            copies.push_back(r.shape);
        }
        if (copies.empty()) return fail(QStringLiteral("нет копий"));
        Result all = Kernel::compound(copies);
        if (!all.ok()) return fail(all.error);
        tools_[size_t(self)] = all.shape;
        if (wholeBody) {
            Body* b = bodyAt(st, body);
            Result u = Kernel::boolean(b->shape, all.shape, BoolOp::Union);
            if (!u.ok()) return fail(u.error);
            b->shape = u.shape;
            return true;
        }
        // копии применяются по одной — так массив отверстий устойчив к несмежным копиям
        for (const Shape& c : copies) if (!applyTool(f, c, mode, body, st)) return false;
        return true;
    }
    case FeatureType::Boolean: {
        const int ia = f.params["a"].toInt(0), ib = f.params["b"].toInt(1);
        if (ia < 0 || ib < 0 || ia >= int(st.bodies.size()) || ib >= int(st.bodies.size()) || ia == ib) return fail(QStringLiteral("нужны два разных тела"));
        if (st.bodies[size_t(ia)].mesh || st.bodies[size_t(ib)].mesh) return fail(QStringLiteral("тело-сетка не поддерживает булевы операции"));
        const QString op = f.params["op"].toString(QStringLiteral("union"));
        BoolOp bo = op == QLatin1String("subtract") ? BoolOp::Subtract : op == QLatin1String("intersect") ? BoolOp::Intersect : BoolOp::Union;
        Result r = Kernel::boolean(st.bodies[size_t(ia)].shape, st.bodies[size_t(ib)].shape, bo);
        if (!r.ok()) return fail(r.error);
        st.bodies[size_t(ia)].shape = r.shape;
        st.bodies.erase(st.bodies.begin() + ib);
        return true;
    }
    case FeatureType::MeshBody: {
        QByteArray raw = QByteArray::fromBase64(f.params["mesh"].toString().toLatin1());
        if (raw.size() < 8) return fail(QStringLiteral("сетка пуста"));
        const uchar* p = reinterpret_cast<const uchar*>(raw.constData());
        quint32 n = qFromLittleEndian<quint32>(p), t = qFromLittleEndian<quint32>(p + 4);
        if (raw.size() < int(8 + n * 12 + t * 12)) return fail(QStringLiteral("сетка повреждена"));
        std::vector<float> pos(n * 3); std::vector<uint32_t> idx(t * 3);
        for (quint32 i = 0; i < n * 3; ++i) { quint32 u = qFromLittleEndian<quint32>(p + 8 + i * 4); float fl; memcpy(&fl, &u, 4); pos[i] = fl; }
        for (quint32 i = 0; i < t * 3; ++i) idx[i] = qFromLittleEndian<quint32>(p + 8 + n * 12 + i * 4);
        Result r = Kernel::fromMesh(pos, idx);
        if (!r.ok()) return fail(r.error);
        Body b; b.name = f.params["name"].toString(f.name); b.shape = r.shape; b.mesh = true; b.origin = f.id;
        st.bodies.push_back(b);
        return true;
    }
    case FeatureType::ImportBody: {
        QString err;
        Shape s = Shape::fromBrep(QByteArray::fromBase64(f.params["brep"].toString().toLatin1()), &err);
        if (s.isNull()) return fail(err.isEmpty() ? QStringLiteral("тело не прочитано") : err);
        Body b; b.name = f.params["name"].toString(f.name); b.shape = s; b.origin = f.id;
        st.bodies.push_back(b);
        return true;
    }
    }
    return fail(QStringLiteral("неизвестная операция"));
}

RebuildReport Part::rebuild(int fromIndex) {
    RebuildReport rep;
    const int n = int(features_.size());
    if (fromIndex < 0) fromIndex = dirtyFrom_ < 0 ? n : dirtyFrom_;
    else if (dirtyFrom_ >= 0) fromIndex = std::min(fromIndex, dirtyFrom_);
    fromIndex = std::clamp(fromIndex, 0, n);
    if (int(states_.size()) < fromIndex) fromIndex = int(states_.size());
    states_.resize(size_t(n));
    tools_.resize(size_t(n));
    State st = fromIndex > 0 ? states_[size_t(fromIndex - 1)] : State{};
    for (int i = fromIndex; i < n; ++i) {
        Feature& f = features_[size_t(i)];
        if (f.suppressed) { f.error.clear(); f.built = false; tools_[size_t(i)] = Shape(); states_[size_t(i)] = st; continue; }
        State before = st;
        bool ok = applyFeature(f, st);
        ++rep.rebuilt;
        if (!ok) { st = before; rep.errors << f.error; rep.failed.push_back(f.id); }   // последнее хорошее состояние сохраняется
        states_[size_t(i)] = st;
    }
    for (int i = 0; i < fromIndex; ++i) if (!features_[size_t(i)].ok()) { rep.errors << features_[size_t(i)].error; rep.failed.push_back(features_[size_t(i)].id); }
    bodies_ = st.bodies;
    dirtyFrom_ = -1;
    return rep;
}

Shape Part::shape() const {
    std::vector<Shape> all;
    for (const Body& b : bodies_) all.push_back(b.shape);
    Result r = Kernel::compound(all);
    return r.ok() ? r.shape : Shape();
}

MassProps Part::massProps() const {
    MassProps m;
    double w = 0;
    for (const Body& b : bodies_) {
        MassProps p = b.shape.massProps();
        m.volume += p.volume; m.area += p.area;
        m.centroid = m.centroid + p.centroid * p.volume; w += p.volume;
    }
    if (w > 0) m.centroid = m.centroid * (1.0 / w);
    return m;
}

double Part::mass() const { return massProps().volume / 1000.0 * material.density; }   // мм³ → см³ · г/см³

QJsonObject Part::toJson() const {
    QJsonArray fs;
    for (const Feature& f : features_) fs.append(f.toJson());
    return {{"id", double(id)}, {"name", name}, {"material", material.toJson()}, {"nextId", double(nextId_)}, {"features", fs}};
}

Part Part::fromJson(const QJsonObject& o) {
    Part p;
    p.id = quint64(o["id"].toDouble());
    p.name = o["name"].toString(p.name);
    p.material = Material::fromJson(o["material"].toObject());
    for (const QJsonValue& v : o["features"].toArray()) p.features_.push_back(Feature::fromJson(v.toObject()));
    p.nextId_ = quint64(o["nextId"].toDouble(1));
    for (const Feature& f : p.features_) p.nextId_ = std::max(p.nextId_, f.id + 1);
    p.dirtyFrom_ = 0;
    return p;
}

bool Part::restoreFromBrep(const QByteArray& data) {
    QString err;
    Shape s = Shape::fromBrep(data, &err);
    if (s.isNull()) return false;
    bodies_.clear();
    Body b; b.name = name; b.shape = s;
    bodies_.push_back(b);
    return true;
}

} // namespace psi::md
