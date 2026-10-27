#include "core/geometry/entity.h"
#include <QtTest>

using namespace psi;

namespace {
bool near(Point a, Point b, double eps = 1e-6) { return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps; }
}

class TestGeometry : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void transforms() {
        Transform t = Transform::translate(10, 5) * Transform::rotate(kPi / 2);
        Point p = t.map({1, 0});
        QVERIFY(near(p, {10, 6}));
        Transform inv = t.inverted();
        QVERIFY(near(inv.map(p), {1, 0}));
        Transform m = Transform::mirror({0, 0}, {0, 1});
        QVERIFY(near(m.map({3, 2}), {-3, 2}));
        QVERIFY(m.mirrored());
        Transform s = Transform::scaleAround({10, 10}, 2);
        QVERIFY(near(s.map({11, 10}), {12, 10}));
        QVERIFY(std::abs(s.uniformScale() - 2) < 1e-9);
    }
    void bboxes() {
        Rect r = entityBBox(makeCircle({0, 0}, 5));
        QCOMPARE(r.x0, -5.0); QCOMPARE(r.x1, 5.0);
        Rect a = entityBBox(makeArc({0, 0}, 10, 0, kPi / 2));
        QVERIFY(std::abs(a.x0) < 1e-6 && std::abs(a.y0) < 1e-6 && std::abs(a.x1 - 10) < 1e-6 && std::abs(a.y1 - 10) < 1e-6);
        Rect e = entityBBox(makeEllipse({0, 0}, 10, 5, kPi / 2));
        QVERIFY(std::abs(e.x1 - 5) < 1e-6 && std::abs(e.y1 - 10) < 1e-6);
    }
    void rectangleAndPolyline() {
        Entity r = makeRectangle({0, 0}, {40, 20});
        QCOMPARE(r.type, EntityType::Polyline);
        QVERIFY(r.closed);
        QCOMPARE(r.pts.size(), size_t(4));
        QVERIFY(isClosedShape(r));
        auto segs = segments(r);
        QCOMPARE(segs.size(), size_t(4));
        auto parts = explode(r);
        QCOMPARE(parts.size(), size_t(4));
        QCOMPARE(parts[0].type, EntityType::Line);
    }
    void polylineBulge() {
        Entity p = makePolyline({{0, 0}, {10, 0}, {10, 10}}, false);
        p.bulges = {1.0, 0.0};   // first segment is a semicircle
        auto pts = flatten(p, 0.01);
        QVERIFY(pts.size() > 10);
        Rect bb = entityBBox(p);
        QVERIFY(std::abs(bb.y0 + 5) < 0.05);   // bulge dips below the chord
    }
    void intersectionsLineCircle() {
        auto pts = intersections(makeLine({-10, 0}, {10, 0}), makeCircle({0, 0}, 5));
        QCOMPARE(pts.size(), size_t(2));
        auto cross = intersections(makeLine({0, 0}, {10, 10}), makeLine({0, 10}, {10, 0}));
        QCOMPARE(cross.size(), size_t(1));
        QVERIFY(near(cross[0], {5, 5}));
        auto none = intersections(makeLine({0, 0}, {1, 0}), makeLine({0, 1}, {1, 1}));
        QVERIFY(none.empty());
        auto cc = intersections(makeCircle({0, 0}, 5), makeCircle({6, 0}, 5));
        QCOMPARE(cc.size(), size_t(2));
    }
    void hitAndSnap() {
        Entity l = makeLine({0, 0}, {10, 0});
        QVERIFY(std::abs(hitDistance(l, {5, 1}) - 1) < 1e-9);
        QVERIFY(hitDistance(l, {20, 0}) > 9.9);
        auto sp = snapPoints(l);
        bool endpoint = false, mid = false;
        for (const auto& s : sp) { if (s.kind == SnapKind::Midpoint && near(s.p, {5, 0})) mid = true; if (s.kind == SnapKind::Endpoint && near(s.p, {10, 0})) endpoint = true; }
        QVERIFY(endpoint && mid);
        auto c = snapPoints(makeCircle({0, 0}, 2));
        int quadrants = 0, centers = 0;
        for (const auto& s : c) { if (s.kind == SnapKind::Quadrant) ++quadrants; if (s.kind == SnapKind::Center) ++centers; }
        QCOMPARE(quadrants, 4); QCOMPARE(centers, 1);
        auto foot = perpendicularPoint(l, {3, 7});
        QVERIFY(foot && near(*foot, {3, 0}));
        auto tang = tangentPoints(makeCircle({0, 0}, 5), {10, 0});
        QCOMPARE(tang.size(), size_t(2));
        QVERIFY(std::abs(tang[0].length() - 5) < 1e-6);
    }
    void offset() {
        auto o = offsetEntity(makeLine({0, 0}, {10, 0}), 2, {5, 5});
        QVERIFY(o && near(o->pts[0], {0, 2}) && near(o->pts[1], {10, 2}));
        auto oc = offsetEntity(makeCircle({0, 0}, 5), 1, {0, 0});
        QVERIFY(oc && std::abs(oc->radius - 4) < 1e-9);
        auto op = offsetEntity(makeRectangle({0, 0}, {10, 10}), 1, {5, 5});
        QVERIFY(op && op->closed);
        Rect bb = entityBBox(*op);
        QVERIFY(std::abs(bb.width() - 8) < 1e-6);
    }
    void trimExtend() {
        Entity l = makeLine({0, 0}, {10, 0});
        Entity cutter = makeLine({5, -5}, {5, 5});
        auto parts = trimEntity(l, {cutter}, {8, 0});
        QCOMPARE(parts.size(), size_t(1));
        QVERIFY(near(parts[0].pts[0], {0, 0}) && near(parts[0].pts[1], {5, 0}));
        // trimming a circle with two cutters leaves an arc
        Entity c = makeCircle({0, 0}, 5);
        Entity v = makeLine({0, -10}, {0, 10});
        auto arcs = trimEntity(c, {v}, {5, 0});
        QVERIFY(!arcs.empty());
        QCOMPARE(arcs[0].type, EntityType::Arc);
        // extend to a boundary
        Entity b = makeLine({20, -5}, {20, 5});
        auto ext = extendEntity(l, b, {9, 0});
        QVERIFY(ext && near(ext->pts[1], {20, 0}));
        QVERIFY(!extendEntity(l, makeLine({0, 5}, {10, 5}), {9, 0}));   // parallel: nothing
    }
    void filletChamfer() {
        Entity a = makeLine({0, 0}, {10, 0});
        Entity b = makeLine({10, 0}, {10, 10});
        auto f = filletLines(a, b, 2, {2, 0}, {10, 8});
        QVERIFY(f && f->arc);
        QVERIFY(near(f->a.pts[1], {8, 0}) && near(f->b.pts[1], {10, 2}));   // pts[1] is the trimmed (corner) end
        QVERIFY(std::abs(f->arc->radius - 2) < 1e-9);
        QVERIFY(near(f->arc->center, {8, 2}));
        auto c = chamferLines(a, b, 3, 1, {2, 0}, {10, 8});
        QVERIFY(c && c->arc);
        QCOMPARE(c->arc->type, EntityType::Line);
        QVERIFY(near(c->a.pts[1], {7, 0}) && near(c->b.pts[1], {10, 1}));
        QVERIFY(!filletLines(a, makeLine({0, 5}, {10, 5}), 1, {0, 0}, {0, 5}));
    }
    void dimensions() {
        Entity d = makeLinearDim({0, 0}, {30, 40}, {0, 50});
        QVERIFY(std::abs(dimensionValue(d, {}) - 50) < 1e-9);
        Entity h = makeLinearDim({0, 0}, {30, 40}, {0, 50}, DimKind::Horizontal);
        QVERIFY(std::abs(dimensionValue(h, {}) - 30) < 1e-9);
        Entity circle = makeCircle({0, 0}, 7); circle.id = 42;
        Entity r = makeRadialDim(42, {0, 0}, 7, {5, 5}, true);
        auto lookup = [&](quint64 id) -> const Entity* { return id == 42 ? &circle : nullptr; };
        QVERIFY(std::abs(dimensionValue(r, lookup) - 14) < 1e-9);
        circle.radius = 10;
        QVERIFY(std::abs(dimensionValue(r, lookup) - 20) < 1e-9);   // associative
        Entity ang = makeAngularDim({0, 0}, {10, 0}, {0, 10}, {5, 5});
        QVERIFY(std::abs(dimensionValue(ang, {}) - kPi / 2) < 1e-9);
        DimGeometry g = dimensionGeometry(d, {});
        QVERIFY(!g.lines.empty());
        QVERIFY(g.arrows.size() >= 2);
    }
    void jsonRoundTrip() {
        std::vector<Entity> es{makeLine({1, 2}, {3, 4}), makeCircle({5, 5}, 2.5), makeArc({0, 0}, 3, 0.1, 2.0), makeEllipse({1, 1}, 4, 2, 0.3),
                               makeSpline({{0, 0}, {1, 2}, {3, 1}}), makeText({2, 2}, QStringLiteral("Текст ✓"), 3.5, 0.2), makeLeader({{0, 0}, {5, 5}}, QStringLiteral("Выноска")),
                               makeHatch({{0, 0}, {5, 0}, {5, 5}}, HatchPattern::Cross, 0.5, 2), makeLinearDim({0, 0}, {5, 0}, {0, 3}), makeAngularDim({0, 0}, {1, 0}, {0, 1}, {1, 1})};
        int i = 1;
        for (Entity& e : es) {
            e.id = i++; e.layer = 3; e.color = 0x123456; e.lineWeight = 0.35; e.lineType = LineType::Dashed; e.locked = (i % 2) == 0;
            e.attrs["material"] = AttrValue::material(QStringLiteral("Сталь"));
            e.attrs["qty"] = AttrValue::quantity(2.5, QStringLiteral("кг"));
            Entity back = entityFromJson(entityToJson(e));
            QVERIFY2(back == e, qPrintable(entityTypeName(e.type)));
        }
    }
    void explodeDimension() {
        Entity d = makeLinearDim({0, 0}, {50, 0}, {0, 10});
        auto parts = explode(d);
        QVERIFY(parts.size() >= 3);
        bool text = false;
        for (const Entity& p : parts) if (p.type == EntityType::Text) text = true;
        QVERIFY(text);
    }
    void threePointCircle() {
        auto c = circleFrom3Points({0, 5}, {5, 0}, {-5, 0});
        QVERIFY(c && near(c->first, {0, 0}) && std::abs(c->second - 5) < 1e-9);
        QVERIFY(!circleFrom3Points({0, 0}, {1, 1}, {2, 2}));
        Entity arc = makeArc3({-5, 0}, {0, 5}, {5, 0});
        QVERIFY(std::abs(arcSweep(arc) - kPi) < 1e-6);
    }
};

QTEST_GUILESS_MAIN(TestGeometry)
#include "test_geometry.moc"
