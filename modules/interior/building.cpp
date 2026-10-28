#include "modules/interior/building.h"
#include "core/document/document.h"
#include <QJsonArray>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace psi::in {

namespace {
constexpr double kJoinTol = 1.0;   // мм: концы стен считаются общими

QJsonArray ptsJson(const std::vector<Point>& pts) { QJsonArray a; for (const Point& p : pts) { QJsonArray q; q.push_back(p.x); q.push_back(p.y); a.push_back(q); } return a; }
std::vector<Point> ptsFrom(const QJsonArray& a) { std::vector<Point> v; for (const auto& q : a) { QJsonArray p = q.toArray(); if (p.size() == 2) v.push_back({p[0].toDouble(), p[1].toDouble()}); } return v; }
QString nodeKey(Point p) { return QStringLiteral("%1,%2").arg(std::llround(p.x / kJoinTol)).arg(std::llround(p.y / kJoinTol)); }

struct Seg { quint64 wall; int index; Point a, b; double t; };
std::vector<Seg> segmentsOf(const Floor& f) {
    std::vector<Seg> out;
    for (const Wall& w : f.walls) for (int i = 0; i + 1 < int(w.pts.size()); ++i) if (dist(w.pts[i], w.pts[i + 1]) > 1e-6) out.push_back({w.id, i, w.pts[i], w.pts[i + 1], w.thickness});
    return out;
}
} // namespace

// ---------------------------------------------------------------- JSON / basics
double Wall::length() const { double l = 0; for (size_t i = 0; i + 1 < pts.size(); ++i) l += dist(pts[i], pts[i + 1]); return l; }
QJsonObject Wall::toJson() const { QJsonObject o; o["id"] = double(id); o["pts"] = ptsJson(pts); o["t"] = thickness; o["h"] = height; o["material"] = material; return o; }
Wall Wall::fromJson(const QJsonObject& o) { Wall w; w.id = quint64(o.value("id").toDouble()); w.pts = ptsFrom(o.value("pts").toArray()); w.thickness = o.value("t").toDouble(200); w.height = o.value("h").toDouble(2700); w.material = o.value("material").toString(w.material); return w; }

QJsonObject Opening::toJson() const {
    QJsonObject o; o["id"] = double(id); o["kind"] = kind == OpeningKind::Door ? "door" : "window"; o["wall"] = double(wall); o["offset"] = offset;
    o["w"] = width; o["h"] = height; o["sill"] = sill; o["swingLeft"] = swingLeft; o["swingInside"] = swingInside;
    return o;
}
Opening Opening::fromJson(const QJsonObject& o) {
    Opening p; p.id = quint64(o.value("id").toDouble()); p.kind = o.value("kind").toString() == "window" ? OpeningKind::Window : OpeningKind::Door;
    p.wall = quint64(o.value("wall").toDouble()); p.offset = o.value("offset").toDouble(); p.width = o.value("w").toDouble(900); p.height = o.value("h").toDouble(2100);
    p.sill = o.value("sill").toDouble(900); p.swingLeft = o.value("swingLeft").toBool(); p.swingInside = o.value("swingInside").toBool(true);
    return p;
}

QString furnitureKindKey(FurnitureKind k) {
    switch (k) { case FurnitureKind::Wardrobe: return "wardrobe"; case FurnitureKind::Table: return "table"; case FurnitureKind::Shelf: return "shelf"; case FurnitureKind::Bed: return "bed"; case FurnitureKind::Nightstand: return "nightstand"; }
    return "wardrobe";
}
FurnitureKind furnitureKindFromKey(const QString& s) {
    if (s == "table") return FurnitureKind::Table;
    if (s == "shelf") return FurnitureKind::Shelf;
    if (s == "bed") return FurnitureKind::Bed;
    if (s == "nightstand") return FurnitureKind::Nightstand;
    return FurnitureKind::Wardrobe;
}
QString Building::kindName(FurnitureKind k) {
    switch (k) { case FurnitureKind::Wardrobe: return QStringLiteral("Шкаф"); case FurnitureKind::Table: return QStringLiteral("Стол"); case FurnitureKind::Shelf: return QStringLiteral("Полка");
                 case FurnitureKind::Bed: return QStringLiteral("Кровать"); case FurnitureKind::Nightstand: return QStringLiteral("Тумба"); }
    return {};
}
Furniture Building::makeFurniture(FurnitureKind k) {
    Furniture f; f.kind = k; f.name = kindName(k);
    switch (k) {
    case FurnitureKind::Wardrobe: f.width = 1200; f.height = 2000; f.depth = 600; f.shelves = 4; f.doors = 2; f.drawers = 0; f.price = 18000; break;
    case FurnitureKind::Table: f.width = 1400; f.height = 750; f.depth = 700; f.shelves = 0; f.doors = 0; f.drawers = 1; f.material = QStringLiteral("Массив"); f.price = 12000; break;
    case FurnitureKind::Shelf: f.width = 800; f.height = 300; f.depth = 250; f.shelves = 1; f.doors = 0; f.drawers = 0; f.price = 1500; break;
    case FurnitureKind::Bed: f.width = 1600; f.height = 500; f.depth = 2000; f.shelves = 0; f.doors = 0; f.drawers = 2; f.material = QStringLiteral("Массив"); f.price = 25000; break;
    case FurnitureKind::Nightstand: f.width = 450; f.height = 500; f.depth = 400; f.shelves = 0; f.doors = 0; f.drawers = 2; f.price = 3500; break;
    }
    return f;
}
std::vector<Point> Furniture::footprint() const {
    double hw = width / 2, hd = depth / 2;
    std::vector<Point> out{{-hw, -hd}, {hw, -hd}, {hw, hd}, {-hw, hd}};
    for (Point& p : out) p = rotateAround(p, {0, 0}, rotation) + pos;
    return out;
}
Rect Furniture::bbox() const { Rect r; for (const Point& p : footprint()) r.add(p); return r; }
QJsonObject Furniture::toJson() const {
    QJsonObject o; o["id"] = double(id); o["kind"] = furnitureKindKey(kind); o["name"] = name; o["x"] = pos.x; o["y"] = pos.y; o["rot"] = rotation;
    o["w"] = width; o["h"] = height; o["d"] = depth; o["t"] = thickness; o["shelves"] = shelves; o["doors"] = doors; o["drawers"] = drawers;
    o["material"] = material; o["color"] = color; o["article"] = article; o["vendor"] = vendor; o["price"] = price;
    return o;
}
Furniture Furniture::fromJson(const QJsonObject& o) {
    Furniture f = Building::makeFurniture(furnitureKindFromKey(o.value("kind").toString()));
    f.id = quint64(o.value("id").toDouble()); f.name = o.value("name").toString(f.name); f.pos = {o.value("x").toDouble(), o.value("y").toDouble()}; f.rotation = o.value("rot").toDouble();
    f.width = o.value("w").toDouble(f.width); f.height = o.value("h").toDouble(f.height); f.depth = o.value("d").toDouble(f.depth); f.thickness = o.value("t").toDouble(16);
    f.shelves = o.value("shelves").toInt(f.shelves); f.doors = o.value("doors").toInt(f.doors); f.drawers = o.value("drawers").toInt(f.drawers);
    f.material = o.value("material").toString(f.material); f.color = o.value("color").toString(f.color); f.article = o.value("article").toString(); f.vendor = o.value("vendor").toString();
    f.price = o.value("price").toDouble(f.price);
    return f;
}

const Wall* Floor::wall(quint64 id) const { for (const Wall& w : walls) if (w.id == id) return &w; return nullptr; }
Wall* Floor::wallMut(quint64 id) { for (Wall& w : walls) if (w.id == id) return &w; return nullptr; }
const Opening* Floor::opening(quint64 id) const { for (const Opening& o : openings) if (o.id == id) return &o; return nullptr; }
const Furniture* Floor::furnitureAt(quint64 id) const { for (const Furniture& f : furniture) if (f.id == id) return &f; return nullptr; }
Furniture* Floor::furnitureMut(quint64 id) { for (Furniture& f : furniture) if (f.id == id) return &f; return nullptr; }
QJsonObject Floor::toJson() const {
    QJsonObject o; o["id"] = double(id); o["name"] = name; o["level"] = level; o["height"] = height;
    QJsonArray ws, os, fs; for (const Wall& w : walls) ws.push_back(w.toJson()); for (const Opening& p : openings) os.push_back(p.toJson()); for (const Furniture& f : furniture) fs.push_back(f.toJson());
    o["walls"] = ws; o["openings"] = os; o["furniture"] = fs;
    QJsonObject rooms; for (auto it = roomInfo.cbegin(); it != roomInfo.cend(); ++it) { QJsonObject r; r["name"] = it->name; r["floor"] = it->floorMaterial; rooms[it.key()] = r; }
    o["rooms"] = rooms;
    return o;
}
Floor Floor::fromJson(const QJsonObject& o) {
    Floor f; f.id = quint64(o.value("id").toDouble()); f.name = o.value("name").toString(f.name); f.level = o.value("level").toDouble(); f.height = o.value("height").toDouble(2700);
    for (const auto& v : o.value("walls").toArray()) f.walls.push_back(Wall::fromJson(v.toObject()));
    for (const auto& v : o.value("openings").toArray()) f.openings.push_back(Opening::fromJson(v.toObject()));
    for (const auto& v : o.value("furniture").toArray()) f.furniture.push_back(Furniture::fromJson(v.toObject()));
    QJsonObject rooms = o.value("rooms").toObject();
    for (auto it = rooms.begin(); it != rooms.end(); ++it) { QJsonObject r = it.value().toObject(); f.roomInfo[it.key()] = {r.value("name").toString(), r.value("floor").toString()}; }
    return f;
}

Building::Building() { Floor f; f.id = nextId_++; floors.push_back(f); }
Floor* Building::floorById(quint64 id) { for (Floor& f : floors) if (f.id == id) return &f; return nullptr; }
quint64 Building::addFloor(const QString& name) { Floor f; f.id = nextId_++; f.name = name; f.level = floors.empty() ? 0 : floors.back().level + floors.back().height + 300; floors.push_back(f); return f.id; }
quint64 Building::addWall(size_t fi, Wall w) { if (w.id == 0) w.id = nextId_++; else nextId_ = std::max(nextId_, w.id + 1); floors[fi].walls.push_back(w); return w.id; }
quint64 Building::addOpening(size_t fi, Opening o) { if (o.id == 0) o.id = nextId_++; else nextId_ = std::max(nextId_, o.id + 1); floors[fi].openings.push_back(o); return o.id; }
quint64 Building::addFurniture(size_t fi, Furniture f) { if (f.id == 0) f.id = nextId_++; else nextId_ = std::max(nextId_, f.id + 1); floors[fi].furniture.push_back(f); return f.id; }
bool Building::remove(size_t fi, quint64 id) {
    Floor& f = floors[fi];
    auto w = std::find_if(f.walls.begin(), f.walls.end(), [id](const Wall& x) { return x.id == id; });
    if (w != f.walls.end()) { f.walls.erase(w); f.openings.erase(std::remove_if(f.openings.begin(), f.openings.end(), [id](const Opening& o) { return o.wall == id; }), f.openings.end()); return true; }
    auto o = std::find_if(f.openings.begin(), f.openings.end(), [id](const Opening& x) { return x.id == id; });
    if (o != f.openings.end()) { f.openings.erase(o); return true; }
    auto m = std::find_if(f.furniture.begin(), f.furniture.end(), [id](const Furniture& x) { return x.id == id; });
    if (m != f.furniture.end()) { f.furniture.erase(m); return true; }
    return false;
}

// ---------------------------------------------------------------- wall geometry
std::vector<WallOutline> Building::wallOutlines(const Floor& f) {
    std::vector<Seg> segs = segmentsOf(f);
    std::vector<WallOutline> out;
    out.reserve(segs.size());
    for (const Seg& s : segs) {
        Vec n = (s.b - s.a).normalized().perp() * (s.t / 2);
        out.push_back({s.wall, s.index, {s.a + n, s.b + n, s.b - n, s.a - n}});
    }
    // mitre where exactly two segments meet
    std::map<QString, std::vector<std::pair<int, bool>>> nodes;   // key → (segment index, atStart)
    for (size_t i = 0; i < segs.size(); ++i) { nodes[nodeKey(segs[i].a)].push_back({int(i), true}); nodes[nodeKey(segs[i].b)].push_back({int(i), false}); }
    for (auto& [key, list] : nodes) {
        if (list.size() != 2) continue;
        auto [i1, s1] = list[0]; auto [i2, s2] = list[1];
        const Seg &A = segs[i1], &B = segs[i2];
        Point node = s1 ? A.a : A.b;
        Vec d1 = ((s1 ? A.b : A.a) - node).normalized(), d2 = ((s2 ? B.b : B.a) - node).normalized();
        if (std::abs(d1.cross(d2)) < 1e-6) continue;                 // collinear: butt joint is exact
        auto corner = [&](const Seg& S, bool atStart, bool plus, int idxPlus, int idxMinus, Vec d, double t) -> std::pair<int, Point> {
            Vec off = d.perp() * (t / 2) * (plus ? 1 : -1);
            (void)atStart; (void)idxPlus; (void)idxMinus;
            (void)S;
            return {0, node + off};
        };
        (void)corner;
        // offset lines of each segment, oriented away from the node: + side is left of d
        auto line = [&](Vec d, double t, bool plus) { Vec off = d.perp() * (t / 2) * (plus ? 1 : -1); return std::make_pair(node + off, node + off + d); };
        auto inter = [&](const std::pair<Point, Point>& l1, const std::pair<Point, Point>& l2) { return lineIntersection(l1.first, l1.second, l2.first, l2.second, false); };
        auto p1 = inter(line(d1, A.t, true), line(d2, B.t, false));   // left of A ↔ right of B
        auto p2 = inter(line(d1, A.t, false), line(d2, B.t, true));
        if (!p1 || !p2) continue;
        // limit the mitre spike at very acute angles
        double lim = std::max(A.t, B.t) * 3;
        if (dist(*p1, node) > lim || dist(*p2, node) > lim) continue;
        auto apply = [&](int si, bool atStart, Point leftPt, Point rightPt) {
            // polygon [a+n, b+n, b-n, a-n]; n = perp(dir a→b). Away-from-node direction d = dir if atStart else -dir,
            // so «left of d» = +n at the start, −n at the end.
            WallOutline& w = out[si];
            if (atStart) { w.polygon[0] = leftPt; w.polygon[3] = rightPt; }
            else { w.polygon[2] = leftPt; w.polygon[1] = rightPt; }
        };
        apply(i1, s1, *p1, *p2);
        apply(i2, s2, *p2, *p1);
    }
    return out;
}

double Building::projectOnWall(const Wall& w, Point p, int* segment) {
    double best = 1e300, along = 0, acc = 0; int bestSeg = 0;
    for (size_t i = 0; i + 1 < w.pts.size(); ++i) {
        Point a = w.pts[i], b = w.pts[i + 1];
        Vec d = b - a; double l = d.length();
        if (l < 1e-9) continue;
        double t = std::clamp((p - a).dot(d) / (l * l), 0.0, 1.0);
        double dd = dist(lerp(a, b, t), p);
        if (dd < best) { best = dd; along = acc + t * l; bestSeg = int(i); }
        acc += l;
    }
    if (segment) *segment = bestSeg;
    return along;
}

Point Building::openingCenter(const Floor& f, const Opening& o, Vec* along, Vec* normal) {
    const Wall* w = f.wall(o.wall);
    if (!w || w->pts.size() < 2) return {};
    double acc = 0;
    for (size_t i = 0; i + 1 < w->pts.size(); ++i) {
        Point a = w->pts[i], b = w->pts[i + 1];
        double l = dist(a, b);
        if (o.offset <= acc + l || i + 2 == w->pts.size()) {
            double t = l < 1e-9 ? 0 : std::clamp((o.offset - acc) / l, 0.0, 1.0);
            Vec d = (b - a).normalized();
            if (along) *along = d;
            if (normal) *normal = d.perp();
            return lerp(a, b, t);
        }
        acc += l;
    }
    return w->pts.back();
}

// ---------------------------------------------------------------- rooms (planar faces of the axis graph)
double polygonArea(const std::vector<Point>& poly) {
    double a = 0;
    for (size_t i = 0, n = poly.size(); i < n; ++i) a += poly[i].cross(poly[(i + 1) % n]);
    return a / 2;
}

std::vector<Point> offsetPolygon(const std::vector<Point>& poly, double d) {
    size_t n = poly.size();
    std::vector<Point> out(n);
    if (n < 3) return poly;
    double sign = polygonArea(poly) > 0 ? 1 : -1;   // CCW: inward is to the left of each edge → +perp
    for (size_t i = 0; i < n; ++i) {
        Point p0 = poly[(i + n - 1) % n], p1 = poly[i], p2 = poly[(i + 1) % n];
        Vec d1 = (p1 - p0).normalized(), d2 = (p2 - p1).normalized();
        Vec n1 = d1.perp() * (sign * d), n2 = d2.perp() * (sign * d);
        auto ip = lineIntersection(p0 + n1, p1 + n1, p1 + n2, p2 + n2, false);
        out[i] = ip ? *ip : p1 + n1;
    }
    return out;
}

QString Building::roomKey(Point c) { return QStringLiteral("%1,%2").arg(std::llround(c.x / 100)).arg(std::llround(c.y / 100)); }

std::vector<Room> Building::rooms(const Floor& f) {
    std::vector<Room> rooms;
    std::vector<Seg> raw = segmentsOf(f);
    if (raw.empty()) return rooms;
    // split segments at mutual intersections and at endpoints lying on other segments
    struct E { Point a, b; double t; };
    std::vector<E> edges;
    for (size_t i = 0; i < raw.size(); ++i) {
        std::vector<double> cuts{0, 1};
        Vec d = raw[i].b - raw[i].a; double l2 = d.dot(d);
        for (size_t j = 0; j < raw.size(); ++j) {
            if (i == j) continue;
            auto ip = lineIntersection(raw[i].a, raw[i].b, raw[j].a, raw[j].b, true);
            if (ip) cuts.push_back(std::clamp((*ip - raw[i].a).dot(d) / l2, 0.0, 1.0));
            for (Point e : {raw[j].a, raw[j].b}) {
                double t = (e - raw[i].a).dot(d) / l2;
                if (t > 1e-6 && t < 1 - 1e-6 && std::abs(d.cross(e - raw[i].a)) / std::sqrt(l2) <= kJoinTol) cuts.push_back(t);
            }
        }
        std::sort(cuts.begin(), cuts.end());
        for (size_t k = 0; k + 1 < cuts.size(); ++k) if (cuts[k + 1] - cuts[k] > 1e-6) edges.push_back({lerp(raw[i].a, raw[i].b, cuts[k]), lerp(raw[i].a, raw[i].b, cuts[k + 1]), raw[i].t});
    }
    // nodes
    std::map<QString, int> nodeIndex; std::vector<Point> nodes;
    auto node = [&](Point p) { QString k = nodeKey(p); auto it = nodeIndex.find(k); if (it != nodeIndex.end()) return it->second; nodeIndex[k] = int(nodes.size()); nodes.push_back(p); return int(nodes.size()) - 1; };
    struct DE { int u, v; double t; };
    std::vector<DE> de;
    std::set<std::pair<int, int>> seen;
    for (const E& e : edges) { int u = node(e.a), v = node(e.b); if (u == v || seen.count({std::min(u, v), std::max(u, v)})) continue; seen.insert({std::min(u, v), std::max(u, v)}); de.push_back({u, v, e.t}); de.push_back({v, u, e.t}); }
    // neighbours sorted CCW
    std::vector<std::vector<int>> adj(nodes.size());   // directed edge indices leaving node
    for (size_t i = 0; i < de.size(); ++i) adj[de[i].u].push_back(int(i));
    for (auto& list : adj) std::sort(list.begin(), list.end(), [&](int x, int y) { return (nodes[de[x].v] - nodes[de[x].u]).angle() < (nodes[de[y].v] - nodes[de[y].u]).angle(); });
    auto next = [&](int ei) {   // edge u→v: at v take the neighbour just clockwise of (v→u)
        int u = de[ei].u, v = de[ei].v;
        const auto& list = adj[v];
        int back = -1;
        for (size_t k = 0; k < list.size(); ++k) if (de[list[k]].v == u) back = int(k);
        if (back < 0) return -1;
        return list[(back + list.size() - 1) % list.size()];
    };
    std::vector<bool> used(de.size(), false);
    for (size_t s = 0; s < de.size(); ++s) {
        if (used[s]) continue;
        std::vector<int> cycle;
        int e = int(s);
        for (size_t guard = 0; guard < de.size() + 1 && e >= 0 && !used[e]; ++guard) { used[e] = true; cycle.push_back(e); e = next(e); if (e == int(s)) break; }
        if (cycle.size() < 3) continue;
        // drop spurs (u→v followed by v→u)
        bool changed = true;
        while (changed && cycle.size() >= 2) {
            changed = false;
            for (size_t i = 0; i < cycle.size(); ++i) {
                size_t j = (i + 1) % cycle.size();
                if (de[cycle[i]].u == de[cycle[j]].v && de[cycle[i]].v == de[cycle[j]].u) {
                    if (j > i) { cycle.erase(cycle.begin() + j); cycle.erase(cycle.begin() + i); } else { cycle.erase(cycle.begin() + i); cycle.erase(cycle.begin() + j); }
                    changed = true; break;
                }
            }
        }
        if (cycle.size() < 3) continue;
        std::vector<Point> poly; std::vector<double> th;
        for (int ei : cycle) { poly.push_back(nodes[de[ei].u]); th.push_back(de[ei].t); }
        double area = polygonArea(poly);
        if (area <= 1.0) continue;   // outer face or degenerate
        Room r;
        r.axis = poly;
        // inner contour: offset each edge inward by its wall half-thickness
        size_t n = poly.size();
        r.inner.resize(n);
        for (size_t i = 0; i < n; ++i) {
            Point p0 = poly[(i + n - 1) % n], p1 = poly[i], p2 = poly[(i + 1) % n];
            Vec d1 = (p1 - p0).normalized(), d2 = (p2 - p1).normalized();
            Vec n1 = d1.perp() * (th[(i + n - 1) % n] / 2), n2 = d2.perp() * (th[i] / 2);
            auto ip = lineIntersection(p0 + n1, p1 + n1, p1 + n2, p2 + n2, false);
            r.inner[i] = ip ? *ip : p1 + n1;
        }
        r.areaM2 = std::abs(polygonArea(r.inner)) / 1e6;
        for (size_t i = 0; i < n; ++i) r.perimeterMm += dist(r.inner[i], r.inner[(i + 1) % n]);
        Point c; for (const Point& p : r.inner) c += p; r.centroid = c / double(n);
        r.id = quint64(rooms.size() + 1);
        auto it = f.roomInfo.find(roomKey(r.centroid));
        r.name = it != f.roomInfo.end() ? it->name : QStringLiteral("Помещение %1").arg(rooms.size() + 1);
        r.floorMaterial = it != f.roomInfo.end() && !it->floorMaterial.isEmpty() ? it->floorMaterial : QStringLiteral("Ламинат");
        rooms.push_back(r);
    }
    return rooms;
}

// ---------------------------------------------------------------- collisions / schedules
namespace {
bool convexOverlap(const std::vector<Point>& a, const std::vector<Point>& b) {   // SAT for convex polygons
    for (const auto* poly : {&a, &b}) {
        for (size_t i = 0, n = poly->size(); i < n; ++i) {
            Vec axis = ((*poly)[(i + 1) % n] - (*poly)[i]).perp().normalized();
            double amin = 1e300, amax = -1e300, bmin = 1e300, bmax = -1e300;
            for (const Point& p : a) { double v = p.dot(axis); amin = std::min(amin, v); amax = std::max(amax, v); }
            for (const Point& p : b) { double v = p.dot(axis); bmin = std::min(bmin, v); bmax = std::max(bmax, v); }
            if (amax <= bmin + 1e-6 || bmax <= amin + 1e-6) return false;
        }
    }
    return true;
}
}

std::vector<Collision> Building::collisions(const Floor& f) {
    std::vector<Collision> out;
    for (size_t i = 0; i < f.furniture.size(); ++i)
        for (size_t j = i + 1; j < f.furniture.size(); ++j)
            if (convexOverlap(f.furniture[i].footprint(), f.furniture[j].footprint()))
                out.push_back({f.furniture[i].id, f.furniture[j].id, QStringLiteral("%1 пересекается с %2").arg(f.furniture[i].name, f.furniture[j].name)});
    std::vector<WallOutline> walls = wallOutlines(f);
    for (const Furniture& m : f.furniture)
        for (const WallOutline& w : walls)
            if (convexOverlap(m.footprint(), w.polygon)) { out.push_back({m.id, w.wall, QStringLiteral("%1 заходит в стену").arg(m.name)}); break; }
    return out;
}

std::vector<ScheduleRow> Building::furnitureSchedule(const Floor& f) {
    std::map<QString, ScheduleRow> rows;
    for (const Furniture& m : f.furniture) {
        QString size = QStringLiteral("%1 × %2 × %3").arg(qRound(m.width)).arg(qRound(m.height)).arg(qRound(m.depth));
        QString key = m.name + "|" + m.article + "|" + size + "|" + m.material + "|" + QString::number(m.price);
        ScheduleRow& r = rows[key];
        if (r.count == 0) { r.name = m.name; r.article = m.article; r.size = size; r.material = m.material; r.price = m.price; }
        ++r.count; r.sum += m.price;
    }
    std::vector<ScheduleRow> out; for (auto& [k, r] : rows) out.push_back(r);
    return out;
}

std::vector<MaterialRow> Building::materialSchedule(const Floor& f) {
    std::map<QString, double> walls, floors;
    for (const Wall& w : f.walls) {
        double area = w.length() * w.height;
        for (const Opening& o : f.openings) if (o.wall == w.id) area -= o.width * o.height;
        walls[w.material] += std::max(0.0, area) / 1e6;
    }
    for (const Room& r : rooms(f)) floors[r.floorMaterial] += r.areaM2;
    std::vector<MaterialRow> out;
    for (auto& [m, a] : walls) out.push_back({m, QStringLiteral("Стены"), a});
    for (auto& [m, a] : floors) out.push_back({m, QStringLiteral("Пол"), a});
    return out;
}

QString Building::csv(const std::vector<ScheduleRow>& rows) {
    QString s; QTextStream ts(&s);
    ts << "Наименование;Артикул;Размер (Ш×В×Г, мм);Материал;Кол-во;Цена;Сумма\n";
    for (const ScheduleRow& r : rows) ts << r.name << ';' << r.article << ';' << r.size << ';' << r.material << ';' << r.count << ';' << QString::number(r.price, 'f', 2) << ';' << QString::number(r.sum, 'f', 2) << '\n';
    return s;
}

// ---------------------------------------------------------------- export to draft
std::vector<Entity> Building::toDraftEntities(size_t fi, Document& doc) const {
    std::vector<Entity> out;
    if (fi >= floors.size()) return out;
    const Floor& f = floors[fi];
    auto layerId = [&](const QString& name, int color) {
        for (const Layer& l : doc.layers()) if (l.name == name) return l.id;
        Layer l; l.name = name; l.color = color; l.lineWeight = 0.35;
        return doc.addLayer(l);
    };
    quint32 lWalls = layerId(QStringLiteral("Стены"), 0x141413), lOpen = layerId(QStringLiteral("Проёмы"), 0x8A6D3B), lFurn = layerId(QStringLiteral("Мебель"), 0x4A6FA5), lRooms = layerId(QStringLiteral("Помещения"), 0x6B6B6B);
    auto push = [&](Entity e, quint32 layer) { e.layer = layer; out.push_back(std::move(e)); };
    for (const WallOutline& w : wallOutlines(f)) push(makePolyline(w.polygon, true), lWalls);
    for (const Opening& o : f.openings) {
        const Wall* w = f.wall(o.wall);
        if (!w) continue;
        Vec along, normal;
        Point c = openingCenter(f, o, &along, &normal);
        double hw = o.width / 2, ht = w->thickness / 2;
        Point a0 = c - along * hw, a1 = c + along * hw;
        push(makeLine(a0 - normal * ht, a0 + normal * ht), lOpen);
        push(makeLine(a1 - normal * ht, a1 + normal * ht), lOpen);
        if (o.kind == OpeningKind::Window) {
            push(makeLine(a0 - normal * ht, a1 - normal * ht), lOpen);
            push(makeLine(a0, a1), lOpen);
            push(makeLine(a0 + normal * ht, a1 + normal * ht), lOpen);
        } else {
            Point hinge = o.swingLeft ? a0 : a1;
            Vec dirOpen = normal * (o.swingInside ? 1 : -1);
            Point tip = hinge + dirOpen * o.width;
            push(makeLine(hinge, tip), lOpen);
            double aStart = (dirOpen).angle(), aEnd = ((o.swingLeft ? a1 : a0) - hinge).angle();
            Entity arc = o.swingLeft ? makeArc(hinge, o.width, aEnd, aStart) : makeArc(hinge, o.width, aStart, aEnd);
            if (normAngle(arc.endAngle - arc.startAngle) > kPi) std::swap(arc.startAngle, arc.endAngle);
            push(arc, lOpen);
        }
    }
    for (const Furniture& m : f.furniture) {
        push(makePolyline(m.footprint(), true), lFurn);
        push(makeText(m.pos, m.name, 100, m.rotation), lFurn);
    }
    for (const Room& r : rooms(f)) push(makeText(r.centroid, QStringLiteral("%1\n%2 м²").arg(r.name).arg(r.areaM2, 0, 'f', 2), 150), lRooms);
    return out;
}

Rect Building::extents(size_t fi) const {
    Rect r;
    if (fi >= floors.size()) return r;
    const Floor& f = floors[fi];
    for (const Wall& w : f.walls) for (const Point& p : w.pts) r.add(p);
    for (const Furniture& m : f.furniture) r.add(m.bbox());
    return r.grown(300);
}

QJsonObject Building::toJson() const {
    QJsonObject o; o["id"] = double(id); o["name"] = name; o["next"] = double(nextId_);
    QJsonArray fs; for (const Floor& f : floors) fs.push_back(f.toJson());
    o["floors"] = fs;
    return o;
}
Building Building::fromJson(const QJsonObject& o) {
    Building b; b.floors.clear();
    b.id = quint64(o.value("id").toDouble()); b.name = o.value("name").toString(b.name); b.nextId_ = quint64(o.value("next").toDouble(1));
    for (const auto& v : o.value("floors").toArray()) b.floors.push_back(Floor::fromJson(v.toObject()));
    if (b.floors.empty()) { Floor f; f.id = b.nextId_++; b.floors.push_back(f); }
    for (const Floor& f : b.floors) {
        b.nextId_ = std::max(b.nextId_, f.id + 1);
        for (const Wall& w : f.walls) b.nextId_ = std::max(b.nextId_, w.id + 1);
        for (const Opening& p : f.openings) b.nextId_ = std::max(b.nextId_, p.id + 1);
        for (const Furniture& m : f.furniture) b.nextId_ = std::max(b.nextId_, m.id + 1);
    }
    return b;
}

} // namespace psi::in
