#include "core/geometry/ops.h"
#include <algorithm>
#include <cmath>

namespace psi {

namespace {

struct Chain { std::vector<Point> pts; std::vector<double> bulges; };   // bulges.size() == pts.size() - 1

double bulgeOf(const Entity& arc) { return std::tan(arcSweep(arc) / 4); }

std::optional<Chain> toChain(const Entity& e) {
    Chain c;
    switch (e.type) {
    case EntityType::Line: c.pts = e.pts; c.bulges = {0}; return c;
    case EntityType::Arc: c.pts = {arcPoint(e, e.startAngle), arcPoint(e, e.endAngle)}; c.bulges = {bulgeOf(e)}; return c;
    case EntityType::Polyline:
        if (e.closed || e.pts.size() < 2) return std::nullopt;
        c.pts = e.pts; c.bulges = e.bulges; c.bulges.resize(c.pts.size() - 1, 0.0);
        return c;
    default: return std::nullopt;
    }
}

void reverseChain(Chain& c) {
    std::reverse(c.pts.begin(), c.pts.end());
    std::reverse(c.bulges.begin(), c.bulges.end());
    for (double& b : c.bulges) b = -b;
}

void appendChain(Chain& dst, const Chain& src) {   // src.pts.front() coincides with dst.pts.back()
    dst.pts.insert(dst.pts.end(), src.pts.begin() + 1, src.pts.end());
    dst.bulges.insert(dst.bulges.end(), src.bulges.begin(), src.bulges.end());
}

struct BulgeArc { Point c; double r; double a0; double sweep; };   // sweep signed, CCW positive

std::optional<BulgeArc> bulgeArc(Point a, Point b, double bulge) {
    double chord = dist(a, b);
    if (std::abs(bulge) < 1e-9 || chord < kEps) return std::nullopt;
    double theta = 4 * std::atan(bulge);
    double r = chord / (2 * std::sin(std::abs(theta) / 2));
    Point m = mid(a, b);
    double h = std::sqrt(std::max(0.0, r * r - chord * chord / 4));
    Vec nrm = (b - a).normalized().perp();
    Point c = bulge > 0 ? m - nrm * h : m + nrm * h;
    if (std::abs(theta) > kPi) c = bulge > 0 ? m + nrm * h : m - nrm * h;
    return BulgeArc{c, r, (a - c).angle(), theta};
}

// fraction (0..1) of `p` along the segment a→b (straight or bulged); nullopt if p is not on it
std::optional<double> segmentParam(Point a, Point b, double bulge, Point p, double tol) {
    if (auto arc = bulgeArc(a, b, bulge)) {
        if (std::abs(dist(p, arc->c) - arc->r) > std::max(tol, 0.1)) return std::nullopt;   // flattened hits are approximate
        double phi = (p - arc->c).angle();
        double rel = arc->sweep > 0 ? normAngle(phi - arc->a0) : normAngle(arc->a0 - phi);
        double t = rel / std::abs(arc->sweep);
        if (t > 1 + 1e-6) return std::nullopt;
        return std::clamp(t, 0.0, 1.0);
    }
    Vec d = b - a; double l2 = d.dot(d);
    if (l2 < kEps) return std::nullopt;
    double t = (p - a).dot(d) / l2;
    if (t < -1e-6 || t > 1 + 1e-6) return std::nullopt;
    t = std::clamp(t, 0.0, 1.0);
    if (dist(a + d * t, p) > tol) return std::nullopt;
    return t;
}

Entity fromChain(const Entity& proto, Chain c, bool closed) {
    Entity out = proto; out.id = 0;
    bool anyBulge = std::any_of(c.bulges.begin(), c.bulges.end(), [](double b) { return std::abs(b) > 1e-12; });
    if (c.pts.size() == 2 && !closed) {
        if (!anyBulge) { out.type = EntityType::Line; out.pts = c.pts; out.bulges.clear(); out.closed = false; return out; }
        if (auto arc = bulgeArc(c.pts[0], c.pts[1], c.bulges[0])) {
            Entity a = makeArc(arc->c, arc->r, arc->sweep > 0 ? arc->a0 : arc->a0 + arc->sweep, arc->sweep > 0 ? arc->a0 + arc->sweep : arc->a0);
            a.layer = proto.layer; a.color = proto.color; a.lineWeight = proto.lineWeight; a.lineType = proto.lineType; a.attrs = proto.attrs;
            return a;
        }
    }
    out.type = EntityType::Polyline; out.pts = std::move(c.pts); out.closed = closed;
    out.bulges = anyBulge ? std::move(c.bulges) : std::vector<double>{};
    return out;
}

// split a chain-like entity (line / arc / polyline) at the given points
std::vector<Entity> splitChain(const Entity& e, const std::vector<Point>& at, double tol) {
    Chain c;
    if (e.type == EntityType::Polyline) { c.pts = e.pts; c.bulges = e.bulges; c.bulges.resize(e.pts.size(), 0.0); }
    else if (auto ch = toChain(e)) { c = *ch; c.bulges.resize(c.pts.size(), 0.0); }
    else return {};
    const size_t n = c.pts.size();
    if (n < 2) return {};
    const bool closed = e.type == EntityType::Polyline && e.closed;
    const size_t segs = closed ? n : n - 1;
    std::vector<Point> pts; std::vector<double> bulges; std::vector<bool> isCut;   // bulges[i] = segment after pts[i]
    for (size_t i = 0; i < segs; ++i) {
        Point a = c.pts[i], b = c.pts[(i + 1) % n]; double bulge = c.bulges[i];
        std::vector<double> ts;
        for (Point p : at) { auto t = segmentParam(a, b, bulge, p, tol); if (t && *t > 1e-6 && *t < 1 - 1e-6) ts.push_back(*t); }
        std::sort(ts.begin(), ts.end());
        ts.erase(std::unique(ts.begin(), ts.end(), [](double x, double y) { return std::abs(x - y) < 1e-9; }), ts.end());
        double prev = 0;
        auto arc = bulgeArc(a, b, bulge);
        bool vertexCut = (closed || i > 0) && std::any_of(at.begin(), at.end(), [&](Point p) { return dist(p, a) < tol; });
        pts.push_back(a); isCut.push_back(vertexCut);
        for (double t : ts) {
            bulges.push_back(arc ? std::tan(arc->sweep * (t - prev) / 4) : 0.0);
            Point p = arc ? arc->c + Point::polar(arc->r, arc->a0 + arc->sweep * t) : lerp(a, b, t);
            pts.push_back(p); isCut.push_back(true);
            prev = t;
        }
        bulges.push_back(arc ? std::tan(arc->sweep * (1 - prev) / 4) : 0.0);
    }
    if (!closed) { pts.push_back(c.pts.back()); isCut.push_back(false); }
    if (std::count(isCut.begin(), isCut.end(), true) == 0) return {};
    std::vector<Entity> parts;
    const size_t total = pts.size();
    size_t start = closed ? static_cast<size_t>(std::find(isCut.begin(), isCut.end(), true) - isCut.begin()) : 0;
    Chain cur; cur.pts.push_back(pts[start]);
    const size_t steps = closed ? total : total - 1;
    for (size_t k = 0; k < steps; ++k) {
        size_t i = (start + k) % total, j = (i + 1) % total;
        cur.bulges.push_back(bulges[i]); cur.pts.push_back(pts[j]);
        if (isCut[j] && (k + 1 < steps || !closed)) { parts.push_back(fromChain(e, cur, false)); cur = Chain{}; cur.pts.push_back(pts[j]); }
    }
    if (cur.pts.size() > 1) parts.push_back(fromChain(e, cur, false));
    return parts;
}

std::vector<Entity> splitRound(const Entity& e, const std::vector<Point>& at, double tol) {
    std::vector<double> angs;
    for (Point p : at) {
        if (std::abs(dist(p, e.center) - e.radius) > tol) continue;
        double a = normAngle((p - e.center).angle());
        if (e.type == EntityType::Arc) {
            double rel = normAngle(a - e.startAngle);
            if (rel < 1e-6 || rel > arcSweep(e) - 1e-6) continue;
            a = rel;
        }
        angs.push_back(a);
    }
    std::sort(angs.begin(), angs.end());
    angs.erase(std::unique(angs.begin(), angs.end(), [](double x, double y) { return std::abs(x - y) < 1e-9; }), angs.end());
    if (angs.empty()) return {};
    auto piece = [&](double a0, double a1) {
        Entity a = makeArc(e.center, e.radius, a0, a1);
        a.layer = e.layer; a.color = e.color; a.lineWeight = e.lineWeight; a.lineType = e.lineType; a.attrs = e.attrs;
        return a;
    };
    std::vector<Entity> parts;
    if (e.type == EntityType::Circle) {
        if (angs.size() == 1) { parts.push_back(piece(angs[0], angs[0])); return parts; }   // one point: full open arc
        for (size_t i = 0; i < angs.size(); ++i) parts.push_back(piece(angs[i], angs[(i + 1) % angs.size()]));
        return parts;
    }
    double prev = 0;
    for (double r : angs) { parts.push_back(piece(e.startAngle + prev, e.startAngle + r)); prev = r; }
    parts.push_back(piece(e.startAngle + prev, e.startAngle + arcSweep(e)));
    return parts;
}

std::vector<Entity> splitSpline(const Entity& e, const std::vector<Point>& at) {
    const int per = 12;
    std::vector<Point> fl = catmullRom(e.pts, e.closed, per);
    if (fl.size() < 2) return {};
    const int n = static_cast<int>(e.pts.size());
    struct Cut { int span; double t; Point p; };
    std::vector<Cut> cuts;
    for (Point p : at) {
        size_t best = 0; double bd = 1e300;
        for (size_t i = 0; i + 1 < fl.size(); ++i) {
            Vec d = fl[i + 1] - fl[i]; double l2 = d.dot(d);
            double t = l2 < kEps ? 0 : std::clamp((p - fl[i]).dot(d) / l2, 0.0, 1.0);
            double dd = dist(fl[i] + d * t, p);
            if (dd < bd) { bd = dd; best = i; }
        }
        int span = static_cast<int>(best / per);
        double t = (static_cast<double>(best % per) + 0.5) / per;
        if (!e.closed && ((span == 0 && t < 1e-3) || (span == n - 2 && t > 1 - 1e-3))) continue;
        cuts.push_back({span, t, p});
    }
    if (cuts.empty()) return {};
    std::sort(cuts.begin(), cuts.end(), [](const Cut& a, const Cut& b) { return a.span < b.span || (a.span == b.span && a.t < b.t); });
    // rebuild the fit list with the cut points inserted and flagged
    std::vector<Point> pts; std::vector<bool> isCut;
    const int spans = e.closed ? n : n - 1;
    for (int i = 0; i < spans; ++i) {
        pts.push_back(e.pts[i]); isCut.push_back(false);
        for (const Cut& c : cuts) if (c.span == i) { pts.push_back(c.p); isCut.push_back(true); }
    }
    if (!e.closed) { pts.push_back(e.pts.back()); isCut.push_back(false); }
    const size_t total = pts.size();
    size_t start = e.closed ? static_cast<size_t>(std::find(isCut.begin(), isCut.end(), true) - isCut.begin()) : 0;
    std::vector<Entity> parts;
    std::vector<Point> cur{pts[start]};
    const size_t steps = e.closed ? total : total - 1;
    for (size_t k = 0; k < steps; ++k) {
        size_t j = (start + k + 1) % total;
        cur.push_back(pts[j]);
        if (isCut[j] && (k + 1 < steps || !e.closed)) { Entity s = e; s.id = 0; s.closed = false; s.pts = cur; parts.push_back(s); cur = {pts[j]}; }
    }
    if (cur.size() > 1) { Entity s = e; s.id = 0; s.closed = false; s.pts = cur; parts.push_back(s); }
    return parts;
}

std::vector<Entity> splitEllipse(const Entity& e, const std::vector<Point>& at) {
    double sw = e.endAngle - e.startAngle; if (sw <= kEps) sw = 2 * kPi;
    const bool full = sw >= 2 * kPi - 1e-9;
    std::vector<double> ts;
    for (Point p : at) {
        Point d = p - e.center;
        double cs = std::cos(-e.rotation), sn = std::sin(-e.rotation);
        Point l{d.x * cs - d.y * sn, d.x * sn + d.y * cs};
        if (e.rx < kEps || e.ry < kEps) return {};
        double t = normAngle(std::atan2(l.y / e.ry, l.x / e.rx));
        double rel = normAngle(t - e.startAngle);
        if (!full && (rel < 1e-6 || rel > sw - 1e-6)) continue;
        ts.push_back(rel);
    }
    std::sort(ts.begin(), ts.end());
    if (ts.empty()) return {};
    auto piece = [&](double a0, double a1) { Entity p = e; p.id = 0; p.startAngle = normAngle(a0); p.endAngle = p.startAngle + (a1 - a0); return p; };
    std::vector<Entity> parts;
    if (full) {
        if (ts.size() == 1) { parts.push_back(piece(e.startAngle + ts[0], e.startAngle + ts[0] + 2 * kPi)); return parts; }
        for (size_t i = 0; i < ts.size(); ++i) {
            double a0 = ts[i], a1 = ts[(i + 1) % ts.size()]; if (a1 <= a0) a1 += 2 * kPi;
            parts.push_back(piece(e.startAngle + a0, e.startAngle + a1));
        }
        return parts;
    }
    double prev = 0;
    for (double r : ts) { parts.push_back(piece(e.startAngle + prev, e.startAngle + r)); prev = r; }
    parts.push_back(piece(e.startAngle + prev, e.startAngle + sw));
    return parts;
}

} // namespace

std::optional<Entity> joinEntities(const std::vector<Entity>& parts, double tol) {
    std::vector<Chain> chains;
    for (const Entity& e : parts) { auto c = toChain(e); if (!c) return std::nullopt; chains.push_back(*c); }
    if (chains.empty()) return std::nullopt;
    std::vector<bool> used(chains.size(), false);
    Chain acc = chains[0]; used[0] = true;
    bool grew = true;
    while (grew) {
        grew = false;
        for (size_t i = 0; i < chains.size(); ++i) {
            if (used[i]) continue;
            Chain c = chains[i];
            if (dist(c.pts.front(), acc.pts.back()) < tol) { appendChain(acc, c); }
            else if (dist(c.pts.back(), acc.pts.back()) < tol) { reverseChain(c); appendChain(acc, c); }
            else if (dist(c.pts.back(), acc.pts.front()) < tol) { appendChain(c, acc); acc = std::move(c); }
            else if (dist(c.pts.front(), acc.pts.front()) < tol) { reverseChain(c); appendChain(c, acc); acc = std::move(c); }
            else continue;
            used[i] = true; grew = true;
        }
    }
    if (std::find(used.begin(), used.end(), false) != used.end()) return std::nullopt;
    bool closed = acc.pts.size() >= 4 && dist(acc.pts.front(), acc.pts.back()) < tol;
    if (closed) acc.pts.pop_back();   // bulges.size() == pts.size() for a closed polyline
    if (acc.pts.size() < 2) return std::nullopt;
    Entity out = fromChain(parts[0], std::move(acc), closed);
    if (out.type != EntityType::Polyline) {   // a single piece: keep it as a polyline anyway
        auto c = toChain(out); Entity p = parts[0]; p.id = 0; p.type = EntityType::Polyline; p.pts = c->pts; p.closed = false;
        bool any = std::any_of(c->bulges.begin(), c->bulges.end(), [](double b) { return std::abs(b) > 1e-12; });
        p.bulges = any ? c->bulges : std::vector<double>{};
        return p;
    }
    return out;
}

std::vector<Entity> splitEntityAt(const Entity& e, const std::vector<Point>& at) {
    const double tol = 1e-4;
    switch (e.type) {
    case EntityType::Line:
    case EntityType::Polyline: return splitChain(e, at, tol);
    case EntityType::Circle:
    case EntityType::Arc: return splitRound(e, at, tol);
    case EntityType::Spline: return splitSpline(e, at);
    case EntityType::Ellipse: return splitEllipse(e, at);
    default: return {};
    }
}

std::vector<Entity> splitEntity(const Entity& e, Point at) {
    switch (e.type) {
    case EntityType::Line: case EntityType::Polyline: case EntityType::Circle: case EntityType::Arc:
    case EntityType::Spline: case EntityType::Ellipse: break;
    default: return {};
    }
    return splitEntityAt(e, {closestPoint(e, at)});
}

std::vector<Entity> cutEntity(const Entity& e, const Entity& cutter) {
    std::vector<Point> pts = intersections(e, cutter, 1e-6);
    if (pts.empty()) return {};
    return splitEntityAt(e, pts);
}

} // namespace psi
