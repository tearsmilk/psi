#include "core/geometry/ops.h"
#include <QtTest>

using namespace psi;

namespace {
bool near(Point a, Point b, double eps = 1e-6) { return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps; }
double lineLen(const Entity& e) { return dist(e.pts[0], e.pts[1]); }
}

class TestOps : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void joinFourLinesToRectangle() {
        std::vector<Entity> lines = {makeLine({0, 0}, {80, 0}), makeLine({80, 50}, {0, 50}), makeLine({80, 0}, {80, 50}), makeLine({0, 50}, {0, 0})};
        lines[0].layer = 3;
        auto j = joinEntities(lines);
        QVERIFY(j.has_value());
        QCOMPARE(j->type, EntityType::Polyline);
        QVERIFY(j->closed);
        QCOMPARE(j->pts.size(), size_t(4));
        QVERIFY(j->bulges.empty());
        QCOMPARE(j->layer, quint32(3));
        QVERIFY(isClosedShape(*j));
    }
    void joinLineAndArcKeepsBulge() {
        Entity l = makeLine({0, 0}, {10, 0});
        Entity a = makeArc({10, 5}, 5, -kPi / 2, kPi / 2);   // from (10,0) up to (10,10)
        Entity l2 = makeLine({10, 10}, {0, 10});
        auto j = joinEntities({l2, a, l});                  // any order, reversed pieces
        QVERIFY(j.has_value());
        QVERIFY(!j->closed);
        QCOMPARE(j->pts.size(), size_t(4));
        QCOMPARE(j->bulges.size(), size_t(3));
        // the arc segment is a half circle: |bulge| == tan(π/4) == 1
        bool found = false;
        for (double b : j->bulges) if (std::abs(std::abs(b) - 1.0) < 1e-6) found = true;
        QVERIFY(found);
        // flattened result passes through the arc's midpoint (15,5)
        QVERIFY(hitDistance(*j, {15, 5}, 0.001) < 0.01);
    }
    void joinRejectsDisconnected() {
        QVERIFY(!joinEntities({makeLine({0, 0}, {10, 0}), makeLine({20, 0}, {30, 0})}).has_value());
        QVERIFY(!joinEntities({}).has_value());
    }
    void splitLineAtMidpoint() {
        Entity l = makeLine({0, 0}, {100, 0});
        auto parts = splitEntity(l, {50, 3});   // the pick is projected onto the line
        QCOMPARE(parts.size(), size_t(2));
        QCOMPARE(parts[0].type, EntityType::Line);
        QVERIFY(std::abs(lineLen(parts[0]) - 50) < 1e-9);
        QVERIFY(std::abs(lineLen(parts[1]) - 50) < 1e-9);
        QVERIFY(near(parts[0].pts[1], {50, 0}));
        QVERIFY(splitEntity(l, {0, 0}).empty());   // at an end: nothing to split
    }
    void splitCircleArcPolyline() {
        auto c = splitEntity(makeCircle({0, 0}, 10), {10, 0});
        QCOMPARE(c.size(), size_t(1));
        QCOMPARE(c[0].type, EntityType::Arc);
        QVERIFY(std::abs(arcSweep(c[0]) - 2 * kPi) < 1e-9);
        auto a = splitEntity(makeArc({0, 0}, 10, 0, kPi), {0, 10});
        QCOMPARE(a.size(), size_t(2));
        QVERIFY(std::abs(arcSweep(a[0]) - kPi / 2) < 1e-9 && std::abs(arcSweep(a[1]) - kPi / 2) < 1e-9);
        Entity rect = makeRectangle({0, 0}, {80, 50});
        auto r = splitEntity(rect, {40, 0});
        QCOMPARE(r.size(), size_t(1));
        QVERIFY(!r[0].closed);
        QCOMPARE(r[0].pts.size(), size_t(6));
        QVERIFY(near(r[0].pts.front(), {40, 0}) && near(r[0].pts.back(), {40, 0}));
        auto open = splitEntity(r[0], {80, 25});
        QCOMPARE(open.size(), size_t(2));
    }
    void cutByObject() {
        Entity l = makeLine({0, 0}, {100, 0});
        auto parts = cutEntity(l, makeCircle({50, 0}, 10));
        QCOMPARE(parts.size(), size_t(3));
        QVERIFY(std::abs(lineLen(parts[0]) - 40) < 1e-6 && std::abs(lineLen(parts[1]) - 20) < 1e-6 && std::abs(lineLen(parts[2]) - 40) < 1e-6);
        auto circ = cutEntity(makeCircle({0, 0}, 10), makeLine({-20, 0}, {20, 0}));
        QCOMPARE(circ.size(), size_t(2));
        QVERIFY(cutEntity(l, makeLine({0, 10}, {100, 10})).empty());
    }
    void splitPolylineWithBulge() {
        Entity p = makePolyline({{0, 0}, {10, 0}, {10, 10}}, false);
        p.bulges = {1.0, 0.0};   // first segment: half circle below/above
        auto parts = splitEntity(p, {10, 0});
        QCOMPARE(parts.size(), size_t(2));
        QCOMPARE(parts[0].type, EntityType::Arc);
        QCOMPARE(parts[1].type, EntityType::Line);
        // joining back restores the polyline shape
        auto j = joinEntities(parts);
        QVERIFY(j.has_value());
        QCOMPARE(j->pts.size(), size_t(3));
        QVERIFY(std::abs(std::abs(j->bulges[0]) - 1.0) < 1e-6);
    }
};

QTEST_GUILESS_MAIN(TestOps)
#include "test_ops.moc"
