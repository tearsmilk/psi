#include "modules/draft/boolean.h"
#include <QtTest>

using namespace psi;

class TestBoolean : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void unionOfTwoRectangles() {
        Entity a = makeRectangle({0, 0}, {80, 50}); a.layer = 2;
        Entity b = makeRectangle({40, 20}, {120, 70});
        auto res = booleanShapes({a, b}, BoolOp::Union);
        QCOMPARE(res.size(), size_t(1));
        QCOMPARE(res[0].type, EntityType::Polyline);
        QVERIFY(res[0].closed);
        QCOMPARE(res[0].pts.size(), size_t(8));
        QCOMPARE(res[0].layer, quint32(2));
        Rect bb = entityBBox(res[0]);
        QVERIFY(std::abs(bb.width() - 120) < 1e-6 && std::abs(bb.height() - 70) < 1e-6);
    }
    void subtractCircleFromRectangle() {
        auto res = booleanShapes({makeRectangle({0, 0}, {100, 60}), makeCircle({100, 30}, 20)}, BoolOp::Subtract);
        QCOMPARE(res.size(), size_t(1));
        QVERIFY(res[0].pts.size() > 4);
        QVERIFY(hitDistance(res[0], {100, 30}) > 15);   // the notch is cut out
    }
    void intersectAndEmpty() {
        auto res = booleanShapes({makeRectangle({0, 0}, {80, 50}), makeRectangle({40, 20}, {120, 70})}, BoolOp::Intersect);
        QCOMPARE(res.size(), size_t(1));
        QCOMPARE(res[0].pts.size(), size_t(4));
        Rect bb = entityBBox(res[0]);
        QVERIFY(std::abs(bb.width() - 40) < 1e-6 && std::abs(bb.height() - 30) < 1e-6);
        QVERIFY(booleanShapes({makeRectangle({0, 0}, {10, 10}), makeRectangle({50, 50}, {60, 60})}, BoolOp::Intersect).empty());
        QVERIFY(booleanShapes({makeRectangle({0, 0}, {10, 10}), makeLine({0, 0}, {5, 5})}, BoolOp::Union).empty());   // open input
    }
    void holeKeepsTwoContours() {
        auto res = booleanShapes({makeRectangle({0, 0}, {100, 100}), makeCircle({50, 50}, 20)}, BoolOp::Subtract);
        QCOMPARE(res.size(), size_t(2));
    }
};

QTEST_GUILESS_MAIN(TestBoolean)
#include "test_boolean.moc"
