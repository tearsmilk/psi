#include "core/geometry/entity.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace psi {

// ---------------------------------------------------------------- keys / names
QString entityTypeKey(EntityType t) {
    static const char* k[] = {"line", "polyline", "circle", "arc", "ellipse", "spline", "hatch", "text", "leader", "dimension"};
    return k[static_cast<int>(t)];
}
EntityType entityTypeFromKey(const QString& s) {
    static const QMap<QString, EntityType> m = {{"line", EntityType::Line}, {"polyline", EntityType::Polyline}, {"circle", EntityType::Circle},
        {"arc", EntityType::Arc}, {"ellipse", EntityType::Ellipse}, {"spline", EntityType::Spline}, {"hatch", EntityType::Hatch},
        {"text", EntityType::Text}, {"leader", EntityType::Leader}, {"dimension", EntityType::Dimension}};
    return m.value(s, EntityType::Line);
}
QString entityTypeName(EntityType t) {
    static const char* n[] = {"Линия", "Полилиния", "Окружность", "Дуга", "Эллипс", "Сплайн", "Штриховка", "Текст", "Выноска", "Размер"};
    return QString::fromUtf8(n[static_cast<int>(t)]);
}
QString lineTypeKey(LineType t) {
    static const char* k[] = {"bylayer", "solid", "dashed", "dotted", "dashdot"};
    return k[static_cast<int>(t)];
}
LineType lineTypeFromKey(const QString& s) {
    static const QMap<QString, LineType> m = {{"bylayer", LineType::ByLayer}, {"solid", LineType::Solid}, {"dashed", LineType::Dashed},
                                              {"dotted", LineType::Dotted}, {"dashdot", LineType::DashDot}};
    return m.value(s, LineType::ByLayer);
}
QString dimKindKey(DimKind k) {
    static const char* n[] = {"aligned", "horizontal", "vertical", "radius", "diameter", "angular"};
    return n[static_cast<int>(k)];
}
DimKind dimKindFromKey(const QString& s) {
    static const QMap<QString, DimKind> m = {{"aligned", DimKind::Aligned}, {"horizontal", DimKind::Horizontal}, {"vertical", DimKind::Vertical},
                                             {"radius", DimKind::Radius}, {"diameter", DimKind::Diameter}, {"angular", DimKind::Angular}};
    return m.value(s, DimKind::Aligned);
}

bool Entity::operator==(const Entity& o) const {
    auto eqPts = [](const std::vector<Point>& a, const std::vector<Point>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) return false;
        return true;
    };
    auto eqD = [](double a, double b) { return std::abs(a - b) < 1e-7; };
    return id == o.id && type == o.type && layer == o.layer && color == o.color && eqD(lineWeight, o.lineWeight) && lineType == o.lineType &&
           visible == o.visible && locked == o.locked && eqPts(pts, o.pts) && closed == o.closed && center == o.center &&
           eqD(radius, o.radius) && eqD(startAngle, o.startAngle) && eqD(endAngle, o.endAngle) && eqD(rx, o.rx) && eqD(ry, o.ry) &&
           eqD(rotation, o.rotation) && text == o.text && eqD(textHeight, o.textHeight) && eqD(textAngle, o.textAngle) &&
           dimKind == o.dimKind && refs == o.refs && hatch == o.hatch && eqD(hatchAngle, o.hatchAngle) && eqD(hatchSpacing, o.hatchSpacing) &&
           attrs == o.attrs && bulges.size() == o.bulges.size();
}

// ---------------------------------------------------------------- factories
Entity makeLine(Point a, Point b) { Entity e; e.type = EntityType::Line; e.pts = {a, b}; return e; }
Entity makePolyline(std::vector<Point> pts, bool closed) { Entity e; e.type = EntityType::Polyline; e.pts = std::move(pts); e.closed = closed; return e; }
Entity makeRectangle(Point a, Point b) { return makePolyline({{a.x, a.y}, {b.x, a.y}, {b.x, b.y}, {a.x, b.y}}, true); }
Entity makeCircle(Point c, double r) { Entity e; e.type = EntityType::Circle; e.center = c; e.radius = r; return e; }
Entity makeArc(Point c, double r, double a0, double a1) {
    Entity e; e.type = EntityType::Arc; e.center = c; e.radius = r; e.startAngle = normAngle(a0); e.endAngle = normAngle(a1);
    if (e.endAngle <= e.startAngle + kEps) e.endAngle += 2 * kPi;
    return e;
}
std::optional<std::pair<Point, double>> circleFrom3Points(Point a, Point b, Point c) {
    double d = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    if (std::abs(d) < 1e-12) return std::nullopt;
    double ux = ((a.x * a.x + a.y * a.y) * (b.y - c.y) + (b.x * b.x + b.y * b.y) * (c.y - a.y) + (c.x * c.x + c.y * c.y) * (a.y - b.y)) / d;
    double uy = ((a.x * a.x + a.y * a.y) * (c.x - b.x) + (b.x * b.x + b.y * b.y) * (a.x - c.x) + (c.x * c.x + c.y * c.y) * (b.x - a.x)) / d;
    Point ctr{ux, uy};
    return std::make_pair(ctr, dist(ctr, a));
}
Entity makeArc3(Point p1, Point p2, Point p3) {
    auto cr = circleFrom3Points(p1, p2, p3);
    if (!cr) return makeLine(p1, p3);
    double a1 = (p1 - cr->first).angle(), a2 = (p2 - cr->first).angle(), a3 = (p3 - cr->first).angle();
    // choose direction so that p2 lies on the arc from p1 to p3
    double ccw = normAngle(a3 - a1), mid = normAngle(a2 - a1);
    if (mid <= ccw) return makeArc(cr->first, cr->second, a1, a3);
    return makeArc(cr->first, cr->second, a3, a1);
}
Entity makeEllipse(Point c, double rx, double ry, double rot) {
    Entity e; e.type = EntityType::Ellipse; e.center = c; e.rx = rx; e.ry = ry; e.rotation = rot; e.startAngle = 0; e.endAngle = 2 * kPi; return e;
}
Entity makeSpline(std::vector<Point> fit, bool closed) { Entity e; e.type = EntityType::Spline; e.pts = std::move(fit); e.closed = closed; return e; }
Entity makeText(Point pos, const QString& text, double height, double angle) {
    Entity e; e.type = EntityType::Text; e.pts = {pos}; e.text = text; e.textHeight = height; e.textAngle = angle; return e;
}
Entity makeLeader(std::vector<Point> pts, const QString& text, double height) {
    Entity e; e.type = EntityType::Leader; e.pts = std::move(pts); e.text = text; e.textHeight = height; return e;
}
Entity makeHatch(std::vector<Point> boundary, HatchPattern p, double angle, double spacing) {
    Entity e; e.type = EntityType::Hatch; e.pts = std::move(boundary); e.closed = true; e.hatch = p; e.hatchAngle = angle; e.hatchSpacing = spacing; return e;
}
Entity makeLinearDim(Point a, Point b, Point line, DimKind kind) {
    Entity e; e.type = EntityType::Dimension; e.dimKind = kind; e.pts = {a, b, line}; return e;
}
Entity makeRadialDim(quint64 circleRef, Point center, double r, Point textPt, bool diameter) {
    Entity e; e.type = EntityType::Dimension; e.dimKind = diameter ? DimKind::Diameter : DimKind::Radius;
    if (circleRef) e.refs = {circleRef};
    e.center = center; e.radius = r; e.pts = {textPt}; return e;
}
Entity makeAngularDim(Point vertex, Point ray1, Point ray2, Point arcPt) {
    Entity e; e.type = EntityType::Dimension; e.dimKind = DimKind::Angular; e.pts = {vertex, ray1, ray2, arcPt}; return e;
}

// ---------------------------------------------------------------- flattening
Point arcPoint(const Entity& arc, double angle) { return arc.center + Point::polar(arc.radius, angle); }
double arcSweep(const Entity& arc) { double s = arc.endAngle - arc.startAngle; return s <= kEps ? s + 2 * kPi : s; }

static int arcSteps(double r, double sweep, double tol) {
    if (r <= tol) return 1;
    double step = 2 * std::acos(std::max(-1.0, 1 - tol / r));
    if (step < 1e-4) step = 1e-4;
    return std::max(4, std::min(4096, static_cast<int>(std::ceil(std::abs(sweep) / step))));
}

static Point ellipsePoint(const Entity& e, double t) {
    Point p{e.rx * std::cos(t), e.ry * std::sin(t)};
    double c = std::cos(e.rotation), s = std::sin(e.rotation);
    return {e.center.x + p.x * c - p.y * s, e.center.y + p.x * s + p.y * c};
}

std::vector<Point> catmullRom(const std::vector<Point>& fit, bool closed, int perSpan) {
    std::vector<Point> out;
    const int n = static_cast<int>(fit.size());
    if (n == 0) return out;
    if (n == 1) return {fit[0]};
    if (n == 2 && !closed) return {fit[0], fit[1]};
    auto at = [&](int i) {
        if (closed) return fit[((i % n) + n) % n];
        return fit[std::clamp(i, 0, n - 1)];
    };
    const int spans = closed ? n : n - 1;
    for (int i = 0; i < spans; ++i) {
        Point p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
        for (int k = 0; k < perSpan; ++k) {
            double t = static_cast<double>(k) / perSpan, t2 = t * t, t3 = t2 * t;
            Point p = (p1 * 2 + (p2 - p0) * t + (p0 * 2 - p1 * 5 + p2 * 4 - p3) * t2 + (p1 * 3 - p0 - p2 * 3 + p3) * t3) * 0.5;
            out.push_back(p);
        }
    }
    out.push_back(closed ? fit[0] : fit[n - 1]);
    return out;
}

static std::vector<Point> polylinePoints(const Entity& e, double tol) {
    std::vector<Point> out;
    const size_t n = e.pts.size();
    if (n == 0) return out;
    const size_t segs = e.closed ? n : n - 1;
    out.push_back(e.pts[0]);
    for (size_t i = 0; i < segs; ++i) {
        Point a = e.pts[i], b = e.pts[(i + 1) % n];
        double bulge = i < e.bulges.size() ? e.bulges[i] : 0;
        if (std::abs(bulge) < 1e-9 || dist(a, b) < kEps) { out.push_back(b); continue; }
        double theta = 4 * std::atan(bulge);
        double chord = dist(a, b), r = chord / (2 * std::sin(std::abs(theta) / 2));
        Point m = mid(a, b);
        double h = std::sqrt(std::max(0.0, r * r - chord * chord / 4));
        Vec nrm = (b - a).normalized().perp();
        Point c = bulge > 0 ? m - nrm * h : m + nrm * h;
        if (std::abs(theta) > kPi) c = bulge > 0 ? m + nrm * h : m - nrm * h;
        double a0 = (a - c).angle();
        int steps = arcSteps(r, theta, tol);
        for (int k = 1; k <= steps; ++k) out.push_back(c + Point::polar(r, a0 + theta * k / steps));
        out.back() = b;
    }
    return out;
}

std::vector<Point> flatten(const Entity& e, double tol) {
    switch (e.type) {
    case EntityType::Line: return e.pts;
    case EntityType::Polyline:
    case EntityType::Hatch: return polylinePoints(e, tol);
    case EntityType::Leader: return e.pts;
    case EntityType::Circle: {
        std::vector<Point> out;
        int n = arcSteps(e.radius, 2 * kPi, tol);
        for (int i = 0; i <= n; ++i) out.push_back(arcPoint(e, 2 * kPi * i / n));
        return out;
    }
    case EntityType::Arc: {
        std::vector<Point> out;
        double sw = arcSweep(e);
        int n = arcSteps(e.radius, sw, tol);
        for (int i = 0; i <= n; ++i) out.push_back(arcPoint(e, e.startAngle + sw * i / n));
        return out;
    }
    case EntityType::Ellipse: {
        std::vector<Point> out;
        double sw = e.endAngle - e.startAngle;
        if (sw <= kEps) sw = 2 * kPi;
        int n = arcSteps(std::max(e.rx, e.ry), sw, tol);
        for (int i = 0; i <= n; ++i) out.push_back(ellipsePoint(e, e.startAngle + sw * i / n));
        return out;
    }
    case EntityType::Spline: return catmullRom(e.pts, e.closed, 16);
    case EntityType::Text: {
        // approximate text box: width ≈ 0.6·h per char
        if (e.pts.empty()) return {};
        double w = 0.6 * e.textHeight * std::max<qsizetype>(1, e.text.size()), h = e.textHeight;
        Transform t = Transform::rotateAround(e.pts[0], e.textAngle);
        Point p = e.pts[0];
        return {t.map(p), t.map({p.x + w, p.y}), t.map({p.x + w, p.y + h}), t.map({p.x, p.y + h}), t.map(p)};
    }
    case EntityType::Dimension: {
        std::vector<Point> out;
        DimGeometry g = dimensionGeometry(e, nullptr);
        for (auto& s : g.lines) { out.push_back(s.a); out.push_back(s.b); }
        for (auto& p : g.arc) out.push_back(p);
        return out;
    }
    }
    return {};
}

std::vector<Segment> segments(const Entity& e, double tol) {
    std::vector<Segment> out;
    if (e.type == EntityType::Dimension) {
        DimGeometry g = dimensionGeometry(e, nullptr);
        out = g.lines;
        for (size_t i = 1; i < g.arc.size(); ++i) out.push_back({g.arc[i - 1], g.arc[i]});
        return out;
    }
    std::vector<Point> pts = flatten(e, tol);
    for (size_t i = 1; i < pts.size(); ++i) out.push_back({pts[i - 1], pts[i]});
    if ((e.type == EntityType::Polyline || e.type == EntityType::Spline) && e.closed && pts.size() > 1 && pts.front() != pts.back())
        out.push_back({pts.back(), pts.front()});
    return out;
}

bool isClosedShape(const Entity& e) {
    return e.type == EntityType::Circle || e.type == EntityType::Hatch ||
           (e.type == EntityType::Ellipse && e.endAngle - e.startAngle >= 2 * kPi - 1e-6) ||
           ((e.type == EntityType::Polyline || e.type == EntityType::Spline) && e.closed);
}

// ---------------------------------------------------------------- bbox / distances
Rect entityBBox(const Entity& e) {
    Rect r;
    if (e.type == EntityType::Circle) { r.add({e.center.x - e.radius, e.center.y - e.radius}); r.add({e.center.x + e.radius, e.center.y + e.radius}); return r; }
    if (e.type == EntityType::Ellipse && std::abs(normAngle(e.endAngle - e.startAngle)) < kEps) {   // full ellipse: exact extents
        double c = std::cos(e.rotation), s = std::sin(e.rotation);
        double ex = std::sqrt(e.rx * e.rx * c * c + e.ry * e.ry * s * s), ey = std::sqrt(e.rx * e.rx * s * s + e.ry * e.ry * c * c);
        r.add({e.center.x - ex, e.center.y - ey}); r.add({e.center.x + ex, e.center.y + ey});
        return r;
    }
    for (const Point& p : flatten(e, 0.05)) r.add(p);
    if (e.type == EntityType::Dimension) {
        DimGeometry g = dimensionGeometry(e, nullptr);
        r.add(g.textPos);
        for (auto& s : g.arrows) { r.add(s.a); r.add(s.b); }
    }
    return r;
}

static double segDist(const Point& p, const Point& a, const Point& b, Point* foot = nullptr) {
    Vec d = b - a;
    double l2 = d.dot(d);
    double t = l2 < 1e-18 ? 0 : std::clamp((p - a).dot(d) / l2, 0.0, 1.0);
    Point f = a + d * t;
    if (foot) *foot = f;
    return dist(p, f);
}

Point closestPoint(const Entity& e, Point p) {
    if (e.type == EntityType::Circle) { Vec d = p - e.center; return e.center + d.normalized() * e.radius; }
    double best = 1e300; Point bp = p;
    for (const Segment& s : segments(e, 0.01)) { Point f; double d = segDist(p, s.a, s.b, &f); if (d < best) { best = d; bp = f; } }
    return bp;
}

double hitDistance(const Entity& e, Point p, double tol) {
    if (e.type == EntityType::Circle) return std::abs(dist(p, e.center) - e.radius);
    if (e.type == EntityType::Hatch && e.hatch == HatchPattern::Solid) {
        // inside the boundary counts as a hit
        std::vector<Point> poly = polylinePoints(e, tol);
        bool inside = false;
        for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
            if ((poly[i].y > p.y) != (poly[j].y > p.y) &&
                p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)
                inside = !inside;
        }
        if (inside) return 0;
    }
    if (e.type == EntityType::Text) {
        std::vector<Point> box = flatten(e, tol);
        Transform t = Transform::rotateAround(e.pts[0], -e.textAngle);
        Point q = t.map(p);
        Rect r; for (auto& b : box) r.add(t.map(b));
        if (r.contains(q)) return 0;
    }
    double best = 1e300;
    for (const Segment& s : segments(e, tol)) best = std::min(best, segDist(p, s.a, s.b));
    return best;
}

std::vector<SnapPoint> snapPoints(const Entity& e) {
    std::vector<SnapPoint> out;
    auto add = [&](Point p, SnapKind k) { out.push_back({p, k, e.id}); };
    switch (e.type) {
    case EntityType::Line:
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Leader:
    case EntityType::Hatch: {
        const size_t n = e.pts.size();
        for (size_t i = 0; i < n; ++i) add(e.pts[i], SnapKind::Endpoint);
        const size_t segs = e.closed ? n : (n ? n - 1 : 0);
        for (size_t i = 0; i < segs; ++i) add(mid(e.pts[i], e.pts[(i + 1) % n]), SnapKind::Midpoint);
        break;
    }
    case EntityType::Circle:
        add(e.center, SnapKind::Center);
        for (int k = 0; k < 4; ++k) add(arcPoint(e, k * kPi / 2), SnapKind::Quadrant);
        break;
    case EntityType::Arc:
        add(e.center, SnapKind::Center);
        add(arcPoint(e, e.startAngle), SnapKind::Endpoint);
        add(arcPoint(e, e.endAngle), SnapKind::Endpoint);
        add(arcPoint(e, e.startAngle + arcSweep(e) / 2), SnapKind::Midpoint);
        break;
    case EntityType::Ellipse:
        add(e.center, SnapKind::Center);
        for (int k = 0; k < 4; ++k) add(ellipsePoint(e, k * kPi / 2), SnapKind::Quadrant);
        break;
    case EntityType::Text: add(e.pts[0], SnapKind::Node); break;
    case EntityType::Dimension: for (auto& p : e.pts) add(p, SnapKind::Node); break;
    }
    return out;
}

std::optional<Point> lineIntersection(Point a1, Point a2, Point b1, Point b2, bool segmentsOnly, double tol) {
    Vec r = a2 - a1, s = b2 - b1;
    double den = r.cross(s);
    if (std::abs(den) < 1e-14) return std::nullopt;
    Vec qp = b1 - a1;
    double t = qp.cross(s) / den, u = qp.cross(r) / den;
    if (segmentsOnly) {
        double tl = tol / std::max(r.length(), 1e-12), ul = tol / std::max(s.length(), 1e-12);
        if (t < -tl || t > 1 + tl || u < -ul || u > 1 + ul) return std::nullopt;
    }
    return a1 + r * t;
}

static std::vector<Point> circleLine(Point c, double r, Point a, Point b, bool segment) {
    std::vector<Point> out;
    Vec d = b - a, f = a - c;
    double A = d.dot(d), B = 2 * f.dot(d), C = f.dot(f) - r * r;
    double disc = B * B - 4 * A * C;
    if (A < 1e-18 || disc < 0) return out;
    disc = std::sqrt(disc);
    for (double t : {(-B - disc) / (2 * A), (-B + disc) / (2 * A)}) {
        if (segment && (t < -1e-9 || t > 1 + 1e-9)) continue;
        out.push_back(a + d * t);
        if (disc < 1e-12) break;
    }
    return out;
}

static bool onArc(const Entity& arc, Point p) {
    if (arc.type == EntityType::Circle) return true;
    double a = normAngle((p - arc.center).angle() - arc.startAngle);
    return a <= arcSweep(arc) + 1e-9;
}

std::vector<Point> intersections(const Entity& a, const Entity& b, double tol) {
    std::vector<Point> out;
    auto isRound = [](const Entity& e) { return e.type == EntityType::Circle || e.type == EntityType::Arc; };
    auto push = [&](Point p) { for (auto& q : out) if (dist(p, q) < tol) return; out.push_back(p); };
    if (isRound(a) && isRound(b)) {
        double d = dist(a.center, b.center);
        if (d < 1e-12 || d > a.radius + b.radius + tol || d < std::abs(a.radius - b.radius) - tol) return out;
        double x = (d * d - b.radius * b.radius + a.radius * a.radius) / (2 * d);
        double h2 = a.radius * a.radius - x * x;
        double h = h2 > 0 ? std::sqrt(h2) : 0;
        Vec u = (b.center - a.center).normalized();
        for (double s : {h, -h}) {
            Point p = a.center + u * x + u.perp() * s;
            if (onArc(a, p) && onArc(b, p)) push(p);
            if (h < 1e-12) break;
        }
        return out;
    }
    if (isRound(a) || isRound(b)) {
        const Entity& c = isRound(a) ? a : b;
        const Entity& o = isRound(a) ? b : a;
        bool segment = o.type != EntityType::Line || true;
        for (const Segment& s : segments(o, tol))
            for (Point p : circleLine(c.center, c.radius, s.a, s.b, segment))
                if (onArc(c, p)) push(p);
        return out;
    }
    std::vector<Segment> sa = segments(a, tol), sb = segments(b, tol);
    for (const Segment& x : sa)
        for (const Segment& y : sb)
            if (auto p = lineIntersection(x.a, x.b, y.a, y.b, true, tol)) push(*p);
    return out;
}

std::optional<Point> perpendicularPoint(const Entity& e, Point from) {
    if (e.type == EntityType::Line) {
        Vec d = e.pts[1] - e.pts[0];
        double l2 = d.dot(d);
        if (l2 < 1e-18) return std::nullopt;
        double t = (from - e.pts[0]).dot(d) / l2;
        if (t < -1e-9 || t > 1 + 1e-9) return std::nullopt;
        return e.pts[0] + d * t;
    }
    if (e.type == EntityType::Circle || e.type == EntityType::Arc) {
        Point p = e.center + (from - e.center).normalized() * e.radius;
        if (onArc(e, p)) return p;
        return std::nullopt;
    }
    Point best; double bd = 1e300; bool found = false;
    for (const Segment& s : segments(e, 0.05)) {
        Vec d = s.b - s.a; double l2 = d.dot(d);
        if (l2 < 1e-18) continue;
        double t = (from - s.a).dot(d) / l2;
        if (t < 0 || t > 1) continue;
        Point f = s.a + d * t;
        double dd = dist(f, from);
        if (dd < bd) { bd = dd; best = f; found = true; }
    }
    if (found) return best;
    return std::nullopt;
}

std::vector<Point> tangentPoints(const Entity& e, Point from) {
    std::vector<Point> out;
    if (e.type != EntityType::Circle && e.type != EntityType::Arc) return out;
    double d = dist(from, e.center);
    if (d <= e.radius + 1e-9) return out;
    double ang = std::acos(e.radius / d), base = (from - e.center).angle();
    for (double s : {ang, -ang}) {
        Point p = arcPoint(e, base + s);
        if (onArc(e, p)) out.push_back(p);
    }
    return out;
}

// ---------------------------------------------------------------- transforms
void transformEntity(Entity& e, const Transform& t) {
    for (auto& p : e.pts) p = t.map(p);
    double k = t.uniformScale();
    double rot = std::atan2(t.b, t.a);
    bool mir = t.mirrored();
    if (e.type == EntityType::Circle || e.type == EntityType::Arc || e.type == EntityType::Ellipse ||
        (e.type == EntityType::Dimension && (e.dimKind == DimKind::Radius || e.dimKind == DimKind::Diameter))) {
        e.center = t.map(e.center);
        e.radius *= k; e.rx *= k; e.ry *= k;
        if (e.type == EntityType::Arc) {
            Point s = t.map(e.center + Point::polar(1, e.startAngle)), f = t.map(e.center + Point::polar(1, e.endAngle));
            (void)s; (void)f;
            double a0 = e.startAngle, a1 = e.endAngle;
            if (!mir) { e.startAngle = normAngle(a0 + rot); e.endAngle = normAngle(a1 + rot); }
            else {
                double m = std::atan2(t.b, t.a);   // reflection axis angle = m/2 in mapped space; mirror reverses direction
                double na0 = normAngle(m - a0), na1 = normAngle(m - a1);
                e.startAngle = na1; e.endAngle = na0;
            }
            if (e.endAngle <= e.startAngle + kEps) e.endAngle += 2 * kPi;
        }
        if (e.type == EntityType::Ellipse) e.rotation = mir ? normAngle(rot - e.rotation) : e.rotation + rot;
    }
    if (e.type == EntityType::Text || e.type == EntityType::Leader || e.type == EntityType::Dimension) {
        e.textHeight *= k;
        e.textAngle = mir ? normAngle(rot - e.textAngle) : e.textAngle + rot;
    }
    if (e.type == EntityType::Hatch) { e.hatchAngle = mir ? normAngle(rot - e.hatchAngle) : e.hatchAngle + rot; e.hatchSpacing *= k; }
    if (mir && e.type == EntityType::Polyline) for (auto& b : e.bulges) b = -b;
    if (e.type == EntityType::Dimension && !e.refs.empty() && (k != 1 || mir)) e.refs.clear();   // detach, the circle may not move with us
}

std::vector<Entity> explode(const Entity& e) {
    std::vector<Entity> out;
    auto inherit = [&](Entity x) { x.layer = e.layer; x.color = e.color; x.lineWeight = e.lineWeight; x.lineType = e.lineType; return x; };
    switch (e.type) {
    case EntityType::Polyline: {
        const size_t n = e.pts.size(), segs = e.closed ? n : (n ? n - 1 : 0);
        for (size_t i = 0; i < segs; ++i) {
            double bulge = i < e.bulges.size() ? e.bulges[i] : 0;
            Point a = e.pts[i], b = e.pts[(i + 1) % n];
            if (std::abs(bulge) < 1e-9) { out.push_back(inherit(makeLine(a, b))); continue; }
            Entity seg; seg.type = EntityType::Polyline; seg.pts = {a, b}; seg.bulges = {bulge};
            std::vector<Point> fl = polylinePoints(seg, 0.01);
            auto cr = circleFrom3Points(fl.front(), fl[fl.size() / 2], fl.back());
            if (!cr) { out.push_back(inherit(makeLine(a, b))); continue; }
            double a0 = (a - cr->first).angle(), a1 = (b - cr->first).angle();
            out.push_back(inherit(bulge > 0 ? makeArc(cr->first, cr->second, a0, a1) : makeArc(cr->first, cr->second, a1, a0)));
        }
        break;
    }
    case EntityType::Hatch:
    case EntityType::Spline: {
        std::vector<Point> fl = flatten(e, 0.01);
        for (size_t i = 1; i < fl.size(); ++i) out.push_back(inherit(makeLine(fl[i - 1], fl[i])));
        break;
    }
    case EntityType::Leader: {
        for (size_t i = 1; i < e.pts.size(); ++i) out.push_back(inherit(makeLine(e.pts[i - 1], e.pts[i])));
        if (!e.pts.empty()) out.push_back(inherit(makeText(e.pts.back() + Point{e.textHeight * 0.5, -e.textHeight * 0.5}, e.text, e.textHeight)));
        break;
    }
    case EntityType::Dimension: {
        DimGeometry g = dimensionGeometry(e, nullptr);
        for (auto& s : g.lines) out.push_back(inherit(makeLine(s.a, s.b)));
        for (auto& s : g.arrows) out.push_back(inherit(makeLine(s.a, s.b)));
        for (size_t i = 1; i < g.arc.size(); ++i) out.push_back(inherit(makeLine(g.arc[i - 1], g.arc[i])));
        Entity t = makeText(g.textPos, e.text.isEmpty() ? QString::number(dimensionValue(e, nullptr), 'f', 2) : e.text, e.textHeight, g.textAngle);
        out.push_back(inherit(t));
        break;
    }
    case EntityType::Ellipse: {
        std::vector<Point> fl = flatten(e, 0.01);
        out.push_back(inherit(makePolyline(fl, e.endAngle - e.startAngle >= 2 * kPi - 1e-6)));
        break;
    }
    default: out.push_back(e); out.back().id = 0; break;
    }
    return out;
}

std::optional<Entity> offsetEntity(const Entity& e, double d, Point side) {
    Entity r = e; r.id = 0;
    switch (e.type) {
    case EntityType::Line: {
        Vec n = (e.pts[1] - e.pts[0]).normalized().perp();
        if ((side - e.pts[0]).dot(n) < 0) n = n * -1;
        r.pts = {e.pts[0] + n * d, e.pts[1] + n * d};
        return r;
    }
    case EntityType::Circle:
    case EntityType::Arc: {
        double nr = dist(side, e.center) > e.radius ? e.radius + d : e.radius - d;
        if (nr <= kEps) return std::nullopt;
        r.radius = nr;
        return r;
    }
    case EntityType::Polyline: {
        const size_t n = e.pts.size();
        if (n < 2) return std::nullopt;
        const size_t segs = e.closed ? n : n - 1;
        std::vector<Point> A, B;
        // side: positive if `side` is left of the first segment
        Vec n0 = (e.pts[1] - e.pts[0]).normalized().perp();
        double sgn = (side - e.pts[0]).dot(n0) < 0 ? -1 : 1;
        for (size_t i = 0; i < segs; ++i) {
            Point a = e.pts[i], b = e.pts[(i + 1) % n];
            Vec nn = (b - a).normalized().perp() * (d * sgn);
            A.push_back(a + nn); B.push_back(b + nn);
        }
        std::vector<Point> out;
        for (size_t i = 0; i < segs; ++i) {
            if (i == 0 && !e.closed) { out.push_back(A[0]); continue; }
            size_t prev = (i + segs - 1) % segs;
            auto ip = lineIntersection(A[prev], B[prev], A[i], B[i], false);
            out.push_back(ip ? *ip : A[i]);
        }
        if (!e.closed) out.push_back(B[segs - 1]);
        else std::rotate(out.begin(), out.begin() + 1, out.end());
        r.pts = out; r.bulges.clear();
        return r;
    }
    default: return std::nullopt;
    }
}

static std::vector<Entity> splitLineAt(const Entity& e, std::vector<double> ts) {
    std::vector<Entity> parts;
    std::sort(ts.begin(), ts.end());
    double prev = 0;
    Vec d = e.pts[1] - e.pts[0];
    for (double t : ts) {
        if (t - prev > 1e-9) { Entity p = e; p.id = 0; p.pts = {e.pts[0] + d * prev, e.pts[0] + d * t}; parts.push_back(p); }
        prev = t;
    }
    if (1 - prev > 1e-9) { Entity p = e; p.id = 0; p.pts = {e.pts[0] + d * prev, e.pts[1]}; parts.push_back(p); }
    return parts;
}

std::vector<Entity> trimEntity(const Entity& e, const std::vector<Entity>& cutters, Point pick) {
    std::vector<Point> cuts;
    for (const Entity& c : cutters) for (Point p : intersections(e, c)) cuts.push_back(p);
    if (cuts.empty()) return {};
    std::vector<Entity> parts;
    if (e.type == EntityType::Line) {
        Vec d = e.pts[1] - e.pts[0]; double l2 = d.dot(d);
        std::vector<double> ts;
        for (Point p : cuts) { double t = (p - e.pts[0]).dot(d) / l2; if (t > 1e-9 && t < 1 - 1e-9) ts.push_back(t); }
        if (ts.empty()) return {};
        parts = splitLineAt(e, ts);
    } else if (e.type == EntityType::Circle || e.type == EntityType::Arc) {
        std::vector<double> angs;
        for (Point p : cuts) angs.push_back(normAngle((p - e.center).angle()));
        std::sort(angs.begin(), angs.end());
        if (e.type == EntityType::Circle) {
            if (angs.size() < 2) return {};
            for (size_t i = 0; i < angs.size(); ++i) {
                Entity a = makeArc(e.center, e.radius, angs[i], angs[(i + 1) % angs.size()]);
                a.layer = e.layer; a.color = e.color; a.lineWeight = e.lineWeight; a.lineType = e.lineType;
                parts.push_back(a);
            }
        } else {
            std::vector<double> rel;
            for (double a : angs) { double r = normAngle(a - e.startAngle); if (r > 1e-9 && r < arcSweep(e) - 1e-9) rel.push_back(r); }
            if (rel.empty()) return {};
            std::sort(rel.begin(), rel.end());
            double prev = 0;
            for (double r : rel) { Entity a = e; a.id = 0; a.startAngle = e.startAngle + prev; a.endAngle = e.startAngle + r; parts.push_back(a); prev = r; }
            Entity a = e; a.id = 0; a.startAngle = e.startAngle + prev; a.endAngle = e.startAngle + arcSweep(e); parts.push_back(a);
        }
    } else if (e.type == EntityType::Polyline && e.bulges.empty()) {
        // split the polyline at the cut points lying on it
        std::vector<Point> pts; std::vector<bool> isCut;
        const size_t n = e.pts.size(), segs = e.closed ? n : n - 1;
        for (size_t i = 0; i < segs; ++i) {
            Point a = e.pts[i], b = e.pts[(i + 1) % n];
            pts.push_back(a); isCut.push_back(false);
            std::vector<std::pair<double, Point>> on;
            Vec d = b - a; double l2 = d.dot(d);
            for (Point p : cuts) { if (segDist(p, a, b) < 1e-6) { double t = (p - a).dot(d) / l2; if (t > 1e-9 && t < 1 - 1e-9) on.push_back({t, p}); } }
            std::sort(on.begin(), on.end(), [](auto& x, auto& y) { return x.first < y.first; });
            for (auto& [t, p] : on) { pts.push_back(p); isCut.push_back(true); }
        }
        if (!e.closed) { pts.push_back(e.pts.back()); isCut.push_back(false); }
        if (std::count(isCut.begin(), isCut.end(), true) == 0) return {};
        size_t start = 0;
        if (e.closed) { start = std::find(isCut.begin(), isCut.end(), true) - isCut.begin(); }
        std::vector<Point> cur;
        const size_t total = pts.size();
        for (size_t k = 0; k <= total; ++k) {
            size_t i = (start + k) % total;
            if (k == total && !e.closed) break;
            cur.push_back(pts[i]);
            if (isCut[i] && cur.size() > 1) { Entity p = e; p.id = 0; p.closed = false; p.pts = cur; parts.push_back(p); cur = {pts[i]}; }
        }
        if (cur.size() > 1) { Entity p = e; p.id = 0; p.closed = false; p.pts = cur; parts.push_back(p); }
        if (e.closed && parts.size() > 1 && !parts.back().pts.empty()) {   // join the wrap-around tail with the first part
            Entity& last = parts.back(); Entity& first = parts.front();
            if (last.pts.back() == first.pts.front() && !isCut[start]) { last.pts.insert(last.pts.end(), first.pts.begin() + 1, first.pts.end()); parts.erase(parts.begin()); }
        }
    } else {
        return {};
    }
    if (parts.size() < 2 && e.type != EntityType::Circle) { if (parts.size() == 1 && e.type == EntityType::Arc) {} return {}; }
    size_t remove = 0; double best = 1e300;
    for (size_t i = 0; i < parts.size(); ++i) { double d = hitDistance(parts[i], pick); if (d < best) { best = d; remove = i; } }
    parts.erase(parts.begin() + remove);
    return parts;
}

std::optional<Entity> extendEntity(const Entity& e, const Entity& boundary, Point pick) {
    if (e.type == EntityType::Line) {
        bool endB = dist(pick, e.pts[1]) < dist(pick, e.pts[0]);
        Point fixed = endB ? e.pts[0] : e.pts[1], moving = endB ? e.pts[1] : e.pts[0];
        Vec d = (moving - fixed).normalized();
        Entity ray = makeLine(moving, moving + d * 1e7);
        std::vector<Point> hits = intersections(ray, boundary);
        if (hits.empty()) return std::nullopt;
        Point best = hits[0];
        for (Point p : hits) if (dist(p, moving) < dist(best, moving)) best = p;
        Entity r = e; r.id = 0;
        if (endB) r.pts[1] = best; else r.pts[0] = best;
        return r;
    }
    if (e.type == EntityType::Arc) {
        Entity full = makeCircle(e.center, e.radius);
        std::vector<Point> hits = intersections(full, boundary);
        if (hits.empty()) return std::nullopt;
        bool endB = dist(pick, arcPoint(e, e.endAngle)) < dist(pick, arcPoint(e, e.startAngle));
        double sw = arcSweep(e);
        double bestRel = 1e300; Point bestP; bool found = false;
        for (Point p : hits) {
            double a = normAngle((p - e.center).angle());
            double rel = endB ? normAngle(a - e.endAngle) : normAngle(e.startAngle - a);
            if (rel < 1e-9 || rel + sw >= 2 * kPi) continue;
            if (rel < bestRel) { bestRel = rel; bestP = p; found = true; }
        }
        if (!found) return std::nullopt;
        Entity r = e; r.id = 0;
        double a = normAngle((bestP - e.center).angle());
        if (endB) { r.endAngle = a; if (r.endAngle <= r.startAngle) r.endAngle += 2 * kPi; }
        else { r.startAngle = a; r.endAngle = e.startAngle + sw; if (r.endAngle <= r.startAngle) r.endAngle += 2 * kPi; }
        return r;
    }
    return std::nullopt;
}

static std::optional<Fillet> cornerLines(const Entity& a, const Entity& b, Point pickA, Point pickB, Point* corner, Point* dirA, Point* dirB) {
    if (a.type != EntityType::Line || b.type != EntityType::Line) return std::nullopt;
    auto ip = lineIntersection(a.pts[0], a.pts[1], b.pts[0], b.pts[1], false);
    if (!ip) return std::nullopt;
    *corner = *ip;
    // keep the ends away from the picked sides
    auto keepFar = [&](const Entity& l, Point pick) { return dist(pick, l.pts[0]) < dist(pick, l.pts[1]) ? l.pts[0] : l.pts[1]; };
    Point ka = keepFar(a, pickA), kb = keepFar(b, pickB);
    *dirA = (ka - *corner).normalized(); *dirB = (kb - *corner).normalized();
    Fillet f; f.a = a; f.b = b; f.a.id = 0; f.b.id = 0;
    f.a.pts = {ka, *corner}; f.b.pts = {kb, *corner};
    return f;
}

std::optional<Fillet> filletLines(const Entity& a, const Entity& b, double r, Point pickA, Point pickB) {
    Point c, da, db;
    auto f = cornerLines(a, b, pickA, pickB, &c, &da, &db);
    if (!f) return std::nullopt;
    if (r <= kEps) return f;
    double ang = std::acos(std::clamp(da.dot(db), -1.0, 1.0));
    if (ang < 1e-6 || std::abs(ang - kPi) < 1e-6) return std::nullopt;
    double t = r / std::tan(ang / 2);
    Point pa = c + da * t, pb = c + db * t;
    Vec bis = (da + db).normalized();
    Point center = c + bis * (r / std::sin(ang / 2));
    f->a.pts[1] = pa; f->b.pts[1] = pb;
    double a0 = (pa - center).angle(), a1 = (pb - center).angle();
    Entity arc = normAngle(a1 - a0) <= kPi ? makeArc(center, r, a0, a1) : makeArc(center, r, a1, a0);
    arc.layer = a.layer; arc.color = a.color; arc.lineWeight = a.lineWeight; arc.lineType = a.lineType;
    f->arc = arc;
    return f;
}

std::optional<Fillet> chamferLines(const Entity& a, const Entity& b, double d1, double d2, Point pickA, Point pickB) {
    Point c, da, db;
    auto f = cornerLines(a, b, pickA, pickB, &c, &da, &db);
    if (!f) return std::nullopt;
    if (d1 <= kEps && d2 <= kEps) return f;
    Point pa = c + da * d1, pb = c + db * d2;
    f->a.pts[1] = pa; f->b.pts[1] = pb;
    Entity line = makeLine(pa, pb);
    line.layer = a.layer; line.color = a.color; line.lineWeight = a.lineWeight; line.lineType = a.lineType;
    f->arc = line;
    return f;
}

// ---------------------------------------------------------------- dimensions
static bool radialSource(const Entity& d, const EntityLookup& lookup, Point* c, double* r) {
    if (!d.refs.empty() && lookup) {
        if (const Entity* ref = lookup(d.refs[0]); ref && (ref->type == EntityType::Circle || ref->type == EntityType::Arc)) {
            *c = ref->center; *r = ref->radius; return true;
        }
    }
    *c = d.center; *r = d.radius;
    return d.radius > 0;
}

double dimensionValue(const Entity& d, const EntityLookup& lookup) {
    switch (d.dimKind) {
    case DimKind::Aligned: return d.pts.size() >= 2 ? dist(d.pts[0], d.pts[1]) : 0;
    case DimKind::Horizontal: return d.pts.size() >= 2 ? std::abs(d.pts[1].x - d.pts[0].x) : 0;
    case DimKind::Vertical: return d.pts.size() >= 2 ? std::abs(d.pts[1].y - d.pts[0].y) : 0;
    case DimKind::Radius:
    case DimKind::Diameter: { Point c; double r = 0; radialSource(d, lookup, &c, &r); return d.dimKind == DimKind::Radius ? r : 2 * r; }
    case DimKind::Angular: {
        if (d.pts.size() < 3) return 0;
        double a1 = (d.pts[1] - d.pts[0]).angle(), a2 = (d.pts[2] - d.pts[0]).angle();
        double s = normAngle(a2 - a1);
        return s > kPi ? 2 * kPi - s : s;
    }
    }
    return 0;
}

static void arrow(DimGeometry& g, Point tip, Vec dir, double size) {
    Vec d = dir.normalized() * size, n = d.perp() * 0.35;
    g.arrows.push_back({tip, tip - d + n});
    g.arrows.push_back({tip, tip - d - n});
}

DimGeometry dimensionGeometry(const Entity& d, const EntityLookup& lookup) {
    DimGeometry g;
    const double h = d.textHeight, ext = h * 0.6, arrowSize = h;
    g.textAngle = 0;
    switch (d.dimKind) {
    case DimKind::Aligned:
    case DimKind::Horizontal:
    case DimKind::Vertical: {
        if (d.pts.size() < 3) return g;
        Point a = d.pts[0], b = d.pts[1], l = d.pts[2];
        Vec dir;
        if (d.dimKind == DimKind::Horizontal) dir = {1, 0};
        else if (d.dimKind == DimKind::Vertical) dir = {0, 1};
        else dir = (b - a).normalized();
        if (dir.length() < kEps) dir = {1, 0};
        Vec n = dir.perp();
        double off = (l - a).dot(n);
        Point a2 = a + n * off, b2 = b + n * off;
        if (d.dimKind != DimKind::Aligned) { b2 = a2 + dir * (b - a).dot(dir); }
        double sgn = off >= 0 ? 1 : -1;
        g.lines.push_back({a + n * (sgn * ext * 0.3), a2 + n * (sgn * ext)});
        g.lines.push_back({b + n * (b2 - b).dot(n) * (ext * 0.3 / std::max(std::abs((b2 - b).dot(n)), 1e-9)), b2 + n * (sgn * ext)});
        g.lines.push_back({a2, b2});
        Vec along = (b2 - a2).normalized();
        arrow(g, a2, along * -1, arrowSize);
        arrow(g, b2, along, arrowSize);
        g.textPos = mid(a2, b2) + n * (sgn * h * 0.6);
        g.textAngle = along.angle();
        if (g.textAngle > kPi / 2 + 1e-9 || g.textAngle < -kPi / 2 + 1e-9) { g.textAngle += kPi; }
        g.textAngle = std::atan2(std::sin(g.textAngle), std::cos(g.textAngle));
        return g;
    }
    case DimKind::Radius:
    case DimKind::Diameter: {
        Point c; double r = 0;
        if (!radialSource(d, lookup, &c, &r) || d.pts.empty()) return g;
        Vec dir = (d.pts[0] - c).normalized();
        if (dir.length() < kEps) dir = {1, 0};
        Point onCircle = c + dir * r;
        Point start = d.dimKind == DimKind::Diameter ? c - dir * r : c;
        g.lines.push_back({start, d.pts[0]});
        arrow(g, onCircle, dir, arrowSize);
        if (d.dimKind == DimKind::Diameter) arrow(g, start, dir * -1, arrowSize);
        g.textPos = d.pts[0] + Vec{0, h * 0.4};
        return g;
    }
    case DimKind::Angular: {
        if (d.pts.size() < 4) return g;
        Point v = d.pts[0];
        double a1 = (d.pts[1] - v).angle(), a2 = (d.pts[2] - v).angle();
        double rr = dist(d.pts[3], v);
        if (normAngle(a2 - a1) > kPi) std::swap(a1, a2);
        double sw = normAngle(a2 - a1);
        int n = std::max(8, static_cast<int>(sw / 0.1));
        for (int i = 0; i <= n; ++i) g.arc.push_back(v + Point::polar(rr, a1 + sw * i / n));
        g.lines.push_back({v + Point::polar(std::min(rr, dist(d.pts[1], v)), a1), v + Point::polar(rr + ext, a1)});
        g.lines.push_back({v + Point::polar(std::min(rr, dist(d.pts[2], v)), a2), v + Point::polar(rr + ext, a2)});
        arrow(g, g.arc.front(), Point::polar(1, a1 - kPi / 2), arrowSize);
        arrow(g, g.arc.back(), Point::polar(1, a2 + kPi / 2), arrowSize);
        g.textPos = v + Point::polar(rr + h, a1 + sw / 2);
        return g;
    }
    }
    return g;
}

// ---------------------------------------------------------------- json
static QJsonArray ptsJson(const std::vector<Point>& pts) {
    QJsonArray a;
    for (const Point& p : pts) { a.push_back(p.x); a.push_back(p.y); }
    return a;
}
static std::vector<Point> ptsFromJson(const QJsonArray& a) {
    std::vector<Point> out;
    for (qsizetype i = 0; i + 1 < a.size(); i += 2) out.push_back({a[i].toDouble(), a[i + 1].toDouble()});
    return out;
}

QJsonObject entityToJson(const Entity& e) {
    QJsonObject o;
    o["id"] = QString::number(e.id);
    o["type"] = entityTypeKey(e.type);
    o["layer"] = static_cast<qint64>(e.layer);
    if (e.color != kColorByLayer) o["color"] = e.color;
    if (e.lineWeight >= 0) o["lw"] = e.lineWeight;
    if (e.lineType != LineType::ByLayer) o["lt"] = lineTypeKey(e.lineType);
    if (!e.visible) o["visible"] = false;
    if (e.locked) o["locked"] = true;
    if (!e.pts.empty()) o["pts"] = ptsJson(e.pts);
    if (!e.bulges.empty()) { QJsonArray b; for (double x : e.bulges) b.push_back(x); o["bulges"] = b; }
    if (e.closed) o["closed"] = true;
    switch (e.type) {
    case EntityType::Circle: o["c"] = ptsJson({e.center}); o["r"] = e.radius; break;
    case EntityType::Arc: o["c"] = ptsJson({e.center}); o["r"] = e.radius; o["a0"] = e.startAngle; o["a1"] = e.endAngle; break;
    case EntityType::Ellipse: o["c"] = ptsJson({e.center}); o["rx"] = e.rx; o["ry"] = e.ry; o["rot"] = e.rotation; o["a0"] = e.startAngle; o["a1"] = e.endAngle; break;
    case EntityType::Text: case EntityType::Leader: o["text"] = e.text; o["h"] = e.textHeight; o["ang"] = e.textAngle; break;
    case EntityType::Dimension: {
        o["dim"] = dimKindKey(e.dimKind); o["h"] = e.textHeight; o["ang"] = e.textAngle;
        if (!e.text.isEmpty()) o["text"] = e.text;
        if (!e.refs.empty()) { QJsonArray r; for (auto id : e.refs) r.push_back(QString::number(id)); o["refs"] = r; }
        if (e.radius > 0) { o["c"] = ptsJson({e.center}); o["r"] = e.radius; }
        break;
    }
    case EntityType::Hatch: o["pattern"] = static_cast<int>(e.hatch); o["hang"] = e.hatchAngle; o["hsp"] = e.hatchSpacing; break;
    default: break;
    }
    if (!e.attrs.isEmpty()) o["attrs"] = attributesToJson(e.attrs);
    return o;
}

Entity entityFromJson(const QJsonObject& o) {
    Entity e;
    e.id = o.value("id").toString().toULongLong();
    e.type = entityTypeFromKey(o.value("type").toString());
    e.layer = static_cast<quint32>(o.value("layer").toInteger(0));
    e.color = o.contains("color") ? o.value("color").toInt() : kColorByLayer;
    e.lineWeight = o.contains("lw") ? o.value("lw").toDouble() : -1;
    e.lineType = o.contains("lt") ? lineTypeFromKey(o.value("lt").toString()) : LineType::ByLayer;
    e.visible = o.value("visible").toBool(true);
    e.locked = o.value("locked").toBool(false);
    e.pts = ptsFromJson(o.value("pts").toArray());
    for (const auto& b : o.value("bulges").toArray()) e.bulges.push_back(b.toDouble());
    e.closed = o.value("closed").toBool(false);
    if (o.contains("c")) { auto c = ptsFromJson(o.value("c").toArray()); if (!c.empty()) e.center = c[0]; }
    e.radius = o.value("r").toDouble(0);
    e.startAngle = o.value("a0").toDouble(0);
    e.endAngle = o.value("a1").toDouble(e.type == EntityType::Ellipse ? 2 * kPi : 0);
    e.rx = o.value("rx").toDouble(0); e.ry = o.value("ry").toDouble(0); e.rotation = o.value("rot").toDouble(0);
    e.text = o.value("text").toString();
    e.textHeight = o.value("h").toDouble(3.5);
    e.textAngle = o.value("ang").toDouble(0);
    e.dimKind = dimKindFromKey(o.value("dim").toString());
    for (const auto& r : o.value("refs").toArray()) e.refs.push_back(r.toString().toULongLong());
    e.hatch = static_cast<HatchPattern>(o.value("pattern").toInt(0));
    e.hatchAngle = o.value("hang").toDouble(kPi / 4);
    e.hatchSpacing = o.value("hsp").toDouble(3);
    e.attrs = attributesFromJson(o.value("attrs").toObject());
    return e;
}

} // namespace psi
