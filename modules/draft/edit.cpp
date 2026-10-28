#include "modules/draft/edit.h"
#include <cmath>

namespace psi {

namespace {

EntityLookup lookupOf(const Document& doc) { return [&doc](quint64 id) { return doc.entity(id); }; }

bool linearKind(DimKind k) { return k == DimKind::Aligned || k == DimKind::Horizontal || k == DimKind::Vertical; }

// unit direction of a linear dimension from pts[0] to pts[1]
Vec dimDirection(const Entity& d) {
    switch (d.dimKind) {
    case DimKind::Horizontal: return {d.pts[1].x >= d.pts[0].x ? 1.0 : -1.0, 0};
    case DimKind::Vertical: return {0, d.pts[1].y >= d.pts[0].y ? 1.0 : -1.0};
    default: { Vec v = (d.pts[1] - d.pts[0]).normalized(); return v.length() < kEps ? Vec{1, 0} : v; }
    }
}

// vertices that define the entity's shape (moved by the dimension edit)
std::vector<Point*> vertices(Entity& e) {
    std::vector<Point*> v;
    switch (e.type) {
    case EntityType::Circle: case EntityType::Arc: case EntityType::Ellipse: v.push_back(&e.center); break;
    case EntityType::Dimension:
        if (e.dimKind == DimKind::Radius || e.dimKind == DimKind::Diameter) { v.push_back(&e.center); for (Point& p : e.pts) v.push_back(&p); }
        else for (Point& p : e.pts) v.push_back(&p);
        break;
    default: for (Point& p : e.pts) v.push_back(&p); break;
    }
    return v;
}

EditField num(const char* key, const QString& label, double v, bool angle = false) { EditField f; f.key = QLatin1String(key); f.label = label; f.value = v; f.angle = angle; return f; }

} // namespace

bool isRectangle(const Entity& e) {
    if (e.type != EntityType::Polyline || !e.closed || e.pts.size() != 4) return false;
    for (double b : e.bulges) if (std::abs(b) > 1e-9) return false;
    for (int i = 0; i < 4; ++i) {
        Vec a = e.pts[(i + 1) % 4] - e.pts[i], b = e.pts[(i + 3) % 4] - e.pts[i];
        if (a.length() < kEps || b.length() < kEps) return false;
        if (std::abs(a.normalized().dot(b.normalized())) > 1e-6) return false;
    }
    return true;
}

std::vector<EditField> editFields(const Document& doc, const Entity& e) {
    std::vector<EditField> f;
    switch (e.type) {
    case EntityType::Dimension:
        if (linearKind(e.dimKind) && e.pts.size() >= 2) f.push_back(num("value", QStringLiteral("Размер"), dimensionValue(e, lookupOf(doc))));
        else if (e.dimKind == DimKind::Radius) f.push_back(num("value", QStringLiteral("Радиус"), dimensionValue(e, lookupOf(doc))));
        else if (e.dimKind == DimKind::Diameter) f.push_back(num("value", QStringLiteral("Диаметр"), dimensionValue(e, lookupOf(doc))));
        break;
    case EntityType::Line:
        if (e.pts.size() >= 2) {
            f.push_back(num("length", QStringLiteral("Длина"), dist(e.pts[0], e.pts[1])));
            f.push_back(num("angle", QStringLiteral("Угол"), deg((e.pts[1] - e.pts[0]).angle()), true));
        }
        break;
    case EntityType::Polyline:
        if (isRectangle(e)) {
            f.push_back(num("width", QStringLiteral("Ширина"), dist(e.pts[0], e.pts[1])));
            f.push_back(num("height", QStringLiteral("Высота"), dist(e.pts[0], e.pts[3])));
        }
        break;
    case EntityType::Circle:
        f.push_back(num("radius", QStringLiteral("Радиус"), e.radius));
        f.push_back(num("diameter", QStringLiteral("Диаметр"), 2 * e.radius));
        break;
    case EntityType::Arc:
        f.push_back(num("radius", QStringLiteral("Радиус"), e.radius));
        break;
    case EntityType::Text: case EntityType::Leader: {
        EditField t; t.key = "text"; t.label = QStringLiteral("Текст"); t.isText = true; t.text = e.text; f.push_back(t);
        break;
    }
    default: break;
    }
    return f;
}

std::unique_ptr<EntityCommand> applyEdit(const Document& doc, const Entity& e, const QMap<QString, double>& values, const QString& text) {
    auto cmd = std::make_unique<EntityCommand>(QStringLiteral("Изменить %1").arg(entityTypeName(e.type)));
    auto has = [&](const char* k) { return values.contains(QLatin1String(k)); };
    switch (e.type) {
    case EntityType::Dimension: {
        if (!has("value")) return nullptr;
        double v = values.value("value");
        if (v <= kEps) return nullptr;
        if (linearKind(e.dimKind) && e.pts.size() >= 2) {
            double cur = dimensionValue(e, lookupOf(doc));
            if (std::abs(cur - v) < 1e-9) return nullptr;
            Vec d = dimDirection(e);
            Point oldP1 = e.pts[1];
            Point newP1 = e.dimKind == DimKind::Aligned ? e.pts[0] + d * v
                        : e.dimKind == DimKind::Horizontal ? Point{e.pts[0].x + d.x * v, oldP1.y} : Point{oldP1.x, e.pts[0].y + d.y * v};
            Vec delta = newP1 - oldP1;
            double limit = oldP1.dot(d) - 1e-6;
            // move the measured geometry: entities touching the second point, vertices at / beyond it along d
            for (quint64 id : doc.entityIds()) {
                const Entity* o = doc.entity(id);
                if (!o || o->id == e.id || o->locked) continue;
                Entity m = *o;
                std::vector<Point*> vs = vertices(m);
                bool touches = false;
                for (Point* p : vs) if (dist(*p, oldP1) < 1e-6) { touches = true; break; }
                if (!touches) continue;
                bool changed = false;
                for (Point* p : vs) if (p->dot(d) >= limit) { *p += delta; changed = true; }
                if (changed) cmd->modify(*o, m);
            }
            Entity nd = e; nd.pts[1] = newP1;
            if (nd.pts.size() >= 3 && nd.pts[2].dot(d) >= limit) nd.pts[2] += delta;   // keep the dimension line beside its end
            cmd->modify(e, nd);
            return cmd;
        }
        if (e.dimKind == DimKind::Radius || e.dimKind == DimKind::Diameter) {
            double r = e.dimKind == DimKind::Radius ? v : v / 2;
            const Entity* ref = e.refs.empty() ? nullptr : doc.entity(e.refs[0]);
            if (ref && (ref->type == EntityType::Circle || ref->type == EntityType::Arc)) {
                if (std::abs(ref->radius - r) < 1e-9) return nullptr;
                Entity m = *ref; m.radius = r; cmd->modify(*ref, m);
            } else {
                if (std::abs(e.radius - r) < 1e-9) return nullptr;
                Entity nd = e; nd.radius = r; cmd->modify(e, nd);
            }
            return cmd;
        }
        return nullptr;
    }
    case EntityType::Line: {
        if (e.pts.size() < 2) return nullptr;
        double len = has("length") ? values.value("length") : dist(e.pts[0], e.pts[1]);
        double ang = has("angle") ? rad(values.value("angle")) : (e.pts[1] - e.pts[0]).angle();
        if (len <= kEps) return nullptr;
        Entity m = e; m.pts[1] = e.pts[0] + Point::polar(len, ang);
        if (m.pts[1] == e.pts[1]) return nullptr;
        cmd->modify(e, m);
        return cmd;
    }
    case EntityType::Polyline: {
        if (!isRectangle(e)) return nullptr;
        double w = has("width") ? values.value("width") : dist(e.pts[0], e.pts[1]);
        double h = has("height") ? values.value("height") : dist(e.pts[0], e.pts[3]);
        if (w <= kEps || h <= kEps) return nullptr;
        Vec u = (e.pts[1] - e.pts[0]).normalized(), v = (e.pts[3] - e.pts[0]).normalized();
        Entity m = e;
        m.pts[1] = e.pts[0] + u * w; m.pts[3] = e.pts[0] + v * h; m.pts[2] = e.pts[0] + u * w + v * h;
        if (m == e) return nullptr;
        cmd->modify(e, m);
        return cmd;
    }
    case EntityType::Circle: case EntityType::Arc: {
        double r = has("radius") ? values.value("radius") : has("diameter") ? values.value("diameter") / 2 : e.radius;
        if (r <= kEps || std::abs(r - e.radius) < 1e-9) return nullptr;
        Entity m = e; m.radius = r; cmd->modify(e, m);
        return cmd;
    }
    case EntityType::Text: case EntityType::Leader: {
        if (text == e.text) return nullptr;
        Entity m = e; m.text = text; cmd->modify(e, m);
        return cmd;
    }
    default: return nullptr;
    }
}

} // namespace psi
