#include "modules/draft/boolean.h"
#include "rendering/renderer.h"
#include <QPainterPath>
#include <QPolygonF>
#include <cmath>

namespace psi {

namespace {

std::vector<Point> simplify(const QPolygonF& poly, double tol) {
    std::vector<Point> pts;
    for (const QPointF& q : poly) {
        Point p{q.x(), q.y()};
        if (pts.empty() || dist(pts.back(), p) > tol) pts.push_back(p);
    }
    while (pts.size() > 1 && dist(pts.front(), pts.back()) <= tol) pts.pop_back();
    // drop collinear vertices (closed polygon)
    bool changed = true;
    while (changed && pts.size() > 3) {
        changed = false;
        for (size_t i = 0; i < pts.size(); ++i) {
            const Point& a = pts[(i + pts.size() - 1) % pts.size()], &b = pts[i], &c = pts[(i + 1) % pts.size()];
            Vec u = (b - a), v = (c - b);
            double cross = u.cross(v), lu = u.length(), lv = v.length();
            if (lu < kEps || lv < kEps || std::abs(cross) / (lu * lv) < 1e-6) { pts.erase(pts.begin() + static_cast<long>(i)); changed = true; break; }
        }
    }
    return pts;
}

} // namespace

std::vector<Entity> booleanShapes(const std::vector<Entity>& shapes, BoolOp op, double tol) {
    std::vector<Entity> out;
    if (shapes.size() < 2) return out;
    for (const Entity& s : shapes) if (!isClosedShape(s)) return out;
    QPainterPath acc = Renderer::worldPath(shapes[0], tol);
    acc.setFillRule(Qt::WindingFill);
    for (size_t i = 1; i < shapes.size(); ++i) {
        QPainterPath p = Renderer::worldPath(shapes[i], tol);
        p.setFillRule(Qt::WindingFill);
        switch (op) {
        case BoolOp::Union: acc = acc.united(p); break;
        case BoolOp::Subtract: acc = acc.subtracted(p); break;
        case BoolOp::Intersect: acc = acc.intersected(p); break;
        }
    }
    for (const QPolygonF& poly : acc.toSubpathPolygons()) {
        std::vector<Point> pts = simplify(poly, std::max(tol * 0.5, 1e-6));
        if (pts.size() < 3) continue;
        Entity e = shapes[0];
        e.id = 0; e.type = EntityType::Polyline; e.pts = std::move(pts); e.bulges.clear(); e.closed = true;
        e.hatch = HatchPattern::Solid;
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace psi
