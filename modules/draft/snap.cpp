#include "modules/draft/snap.h"
#include <cmath>

namespace psi {

QString SnapEngine::kindName(SnapKind k) {
    switch (k) {
    case SnapKind::Endpoint: return QStringLiteral("Конец");
    case SnapKind::Midpoint: return QStringLiteral("Середина");
    case SnapKind::Center: return QStringLiteral("Центр");
    case SnapKind::Quadrant: return QStringLiteral("Квадрант");
    case SnapKind::Intersection: return QStringLiteral("Пересечение");
    case SnapKind::Perpendicular: return QStringLiteral("Перпендикуляр");
    case SnapKind::Tangent: return QStringLiteral("Касательная");
    case SnapKind::Grid: return QStringLiteral("Сетка");
    case SnapKind::Node: return QStringLiteral("Узел");
    }
    return {};
}

static unsigned flagOf(SnapKind k) {
    switch (k) {
    case SnapKind::Endpoint: case SnapKind::Node: return SnapEndpoint;
    case SnapKind::Midpoint: return SnapMidpoint;
    case SnapKind::Center: return SnapCenter;
    case SnapKind::Quadrant: return SnapQuadrant;
    case SnapKind::Intersection: return SnapIntersection;
    case SnapKind::Perpendicular: return SnapPerpendicular;
    case SnapKind::Tangent: return SnapTangent;
    case SnapKind::Grid: return SnapGrid;
    }
    return 0;
}

Point SnapEngine::roundToStep(Point p, double step) {
    if (step <= 0) return p;
    return {std::round(p.x / step) * step, std::round(p.y / step) * step};
}

SnapResult SnapEngine::snap(const Document& doc, Point cursor, double radius, const std::optional<Point>& from, unsigned flagsOverride, double gridRadius) {
    SnapResult best;
    const DocumentSettings& s = doc.settings();
    unsigned flags = flagsOverride ? flagsOverride : s.snapFlags;
    const bool objects = s.snapEnabled || flagsOverride;
    if (!objects) flags = 0;
    double bestD = radius;
    auto consider = [&](Point p, SnapKind k, quint64 id, double priority = 1.0) {
        if (!(flags & flagOf(k))) return;
        double d = dist(p, cursor) * priority;
        if (d <= bestD) { bestD = d; best = {p, k, id, true}; }
    };
    Rect q(cursor.x - radius, cursor.y - radius, cursor.x + radius, cursor.y + radius);
    std::vector<quint64> ids = doc.query(q);
    std::vector<const Entity*> near;
    for (quint64 id : ids) {
        const Entity* e = doc.entity(id);
        if (!e || !doc.entityVisible(*e)) continue;
        if (hitDistance(*e, cursor) > radius * 3 && !(e->type == EntityType::Circle || e->type == EntityType::Arc)) continue;
        near.push_back(e);
        for (const SnapPoint& sp : snapPoints(*e)) consider(sp.p, sp.kind, e->id, sp.kind == SnapKind::Endpoint ? 0.8 : 1.0);
        if (from) {
            if (auto pp = perpendicularPoint(*e, *from)) consider(*pp, SnapKind::Perpendicular, e->id, 1.1);
            for (Point tp : tangentPoints(*e, *from)) consider(tp, SnapKind::Tangent, e->id, 1.1);
        }
    }
    if (flags & SnapIntersection && near.size() <= 40) {
        for (size_t i = 0; i < near.size(); ++i)
            for (size_t j = i + 1; j < near.size(); ++j)
                for (Point p : intersections(*near[i], *near[j])) consider(p, SnapKind::Intersection, near[i]->id, 0.9);
    }
    if (best.valid) return best;   // object snap wins over the grid magnet
    if ((flags & SnapGrid) && s.gridSpacing > 0) {   // legacy: snap to the visible grid
        consider(roundToStep(cursor, s.gridSpacing), SnapKind::Grid, 0, 1.0);
        if (best.valid) return best;
    }
    if (s.gridSnap && s.snapStep > 0 && !flagsOverride) {   // magnet to round coordinates within gridRadius
        Point g = roundToStep(cursor, s.snapStep);
        double r = gridRadius > 0 ? gridRadius : radius;
        if (dist(g, cursor) <= r) best = {g, SnapKind::Grid, 0, true};
    }
    return best;
}

Point SnapEngine::constrain(const DocumentSettings& s, Point from, Point p, bool orthoHeld) {
    Vec d = p - from;
    if (d.length() < kEps) return p;
    if (s.ortho || orthoHeld) {
        if (std::abs(d.x) >= std::abs(d.y)) return {p.x, from.y};
        return {from.x, p.y};
    }
    if (s.polar && s.polarStep > 0) {
        double step = rad(s.polarStep);
        double a = d.angle();
        double snapped = std::round(a / step) * step;
        if (std::abs(normAngle(a - snapped)) < rad(3) || std::abs(normAngle(snapped - a)) < rad(3))
            return from + Point::polar(d.length(), snapped);
    }
    return p;
}

} // namespace psi
