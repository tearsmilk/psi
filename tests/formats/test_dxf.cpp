#include "core/document/document.h"
#include "io/dxf/dxf.h"
#include <QtTest>

using namespace psi;

namespace {
const char* kMinimalDxf =
    "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n"
    "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n1\n0\nLAYER\n2\nWalls\n70\n0\n62\n1\n6\nCONTINUOUS\n0\nENDTAB\n0\nENDSEC\n"
    "0\nSECTION\n2\nENTITIES\n"
    "0\nLINE\n8\nWalls\n10\n0.0\n20\n0.0\n11\n100.0\n21\n0.0\n"
    "0\nCIRCLE\n8\n0\n10\n50.0\n20\n50.0\n40\n10.0\n"
    "0\nARC\n8\n0\n10\n0.0\n20\n0.0\n40\n5.0\n50\n0.0\n51\n90.0\n"
    "0\nLWPOLYLINE\n8\nWalls\n90\n3\n70\n1\n10\n0\n20\n0\n10\n10\n20\n0\n42\n1.0\n10\n10\n20\n10\n"
    "0\nTEXT\n8\n0\n10\n1\n20\n2\n40\n3.5\n1\nHello\n"
    "0\nMTEXT\n8\n0\n10\n1\n20\n8\n40\n2.5\n1\nMulti\\PLine\n"
    "0\nELLIPSE\n8\n0\n10\n0\n20\n0\n11\n20\n21\n0\n40\n0.5\n41\n0\n42\n6.283185\n"
    "0\n3DFACE\n8\n0\n10\n0\n20\n0\n30\n0\n11\n1\n21\n0\n31\n0\n12\n1\n22\n1\n32\n0\n13\n0\n23\n1\n33\n0\n"
    "0\nENDSEC\n0\nEOF\n";
}

class TestDxf : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parseMinimal() {
        std::vector<Entity> es; std::vector<Layer> ls; DxfReport rep; QString err;
        QVERIFY2(Dxf::parse(QByteArray(kMinimalDxf), &es, &ls, &rep, &err), qPrintable(err));
        QCOMPARE(es.size(), size_t(7));
        QCOMPARE(rep.unit, Unit::Millimeter);
        QVERIFY(!rep.unitGuessed);
        QCOMPARE(rep.counts.value("LINE"), 1);
        QVERIFY(rep.unsupported.contains("3DFACE"));
        bool walls = false; for (const Layer& l : ls) if (l.name == "Walls") walls = true;
        QVERIFY(walls);
        // polyline bulge preserved
        const Entity* pl = nullptr; for (const Entity& e : es) if (e.type == EntityType::Polyline) pl = &e;
        QVERIFY(pl); QVERIFY(pl->closed); QCOMPARE(pl->bulges.size(), size_t(3)); QCOMPARE(pl->bulges[1], 1.0);
        // arc angles in radians
        const Entity* arc = nullptr; for (const Entity& e : es) if (e.type == EntityType::Arc) arc = &e;
        QVERIFY(arc); QVERIFY(std::abs(arc->endAngle - kPi / 2) < 1e-6);
        // mtext line break decoded
        bool multi = false; for (const Entity& e : es) if (e.type == EntityType::Text && e.text.contains('\n')) multi = true;
        QVERIFY(multi);
        QVERIFY(!rep.summary().isEmpty());
    }
    void unitsConversion() {
        QByteArray inches = QByteArray(kMinimalDxf).replace("70\n4\n0\nENDSEC", "70\n1\n0\nENDSEC");   // $INSUNITS = 1 (inches)
        std::vector<Entity> es; std::vector<Layer> ls; DxfReport rep; QString err;
        QVERIFY(Dxf::parse(inches, &es, &ls, &rep, &err));
        QCOMPARE(rep.unit, Unit::Inch);
        const Entity* line = nullptr; for (const Entity& e : es) if (e.type == EntityType::Line) line = &e;
        QVERIFY(line); QVERIFY(std::abs(line->pts[1].x - 2540.0) < 1e-6);
        // no $INSUNITS → guessed, fallback applied
        QByteArray noUnits = QByteArray(kMinimalDxf).replace("9\n$INSUNITS\n70\n4\n", "");
        es.clear(); rep = {};
        DxfOptions opt; opt.fallbackUnit = Unit::Centimeter;
        QVERIFY(Dxf::parse(noUnits, &es, &ls, &rep, &err, opt));
        QVERIFY(rep.unitGuessed);
        line = nullptr; for (const Entity& e : es) if (e.type == EntityType::Line) line = &e;
        QVERIFY(line); QVERIFY(std::abs(line->pts[1].x - 1000.0) < 1e-6);
    }
    void importIntoDocument() {
        std::vector<Entity> es; std::vector<Layer> ls; DxfReport rep; QString err;
        QVERIFY(Dxf::parse(QByteArray(kMinimalDxf), &es, &ls, &rep, &err));
        Document d;
        Layer existing; existing.name = "Walls"; existing.color = 0xABCDEF; d.addLayer(existing);
        int n = Dxf::import(d, es, ls);
        QCOMPARE(n, 7);
        QCOMPARE(d.entityCount(), size_t(7));
        QCOMPARE(d.layers().size(), size_t(2));   // Walls merged, 0 reused
        int onWalls = 0; for (quint64 id : d.entityIds()) if (d.entity(id)->layer == d.layers()[1].id) ++onWalls;
        QCOMPARE(onWalls, 2);
    }
    void writeReadRoundTrip() {
        Document d;
        Layer l; l.name = QStringLiteral("Стены"); l.color = 0xFF0000; l.lineWeight = 0.5; quint32 lid = d.addLayer(l);
        auto on = [lid](Entity e) { e.layer = lid; return e; };
        d.addEntity(on(makeLine({0, 0}, {100, 0})));
        d.addEntity(on(makeCircle({50, 50}, 10)));
        d.addEntity(makeArc({0, 0}, 20, 0.5, 2.5));
        Entity pl = makePolyline({{0, 0}, {10, 0}, {10, 10}}, true); pl.bulges = {0.5, 0, 0};
        d.addEntity(pl);
        d.addEntity(makeEllipse({5, 5}, 10, 4, 0.3));
        d.addEntity(makeSpline({{0, 0}, {5, 5}, {10, 0}, {15, 5}}));
        d.addEntity(makeText({1, 2}, QStringLiteral("Привет"), 3.5, 0.1));
        d.addEntity(makeHatch({{0, 0}, {5, 0}, {5, 5}}, HatchPattern::Lines, kPi / 4, 2));
        d.addEntity(makeLinearDim({0, 0}, {100, 0}, {50, -10}));
        d.addEntity(makeLeader({{0, 0}, {5, 5}, {10, 5}}, QStringLiteral("Выноска")));
        QByteArray dxf = Dxf::write(d);
        QVERIFY(dxf.contains("AC1015"));
        QVERIFY(dxf.contains("$INSUNITS"));
        QVERIFY(dxf.contains("LWPOLYLINE"));
        QVERIFY(dxf.contains("SPLINE"));
        QVERIFY(dxf.contains("HATCH"));
        QVERIFY(dxf.contains(QStringLiteral("Стены").toUtf8()));
        std::vector<Entity> es; std::vector<Layer> ls; DxfReport rep; QString err;
        QVERIFY2(Dxf::parse(dxf, &es, &ls, &rep, &err), qPrintable(err));
        QVERIFY(rep.unsupported.isEmpty());
        QCOMPARE(rep.unit, Unit::Millimeter);
        int lines = 0, circles = 0, arcs = 0, polys = 0, ellipses = 0, splines = 0, texts = 0, hatches = 0;
        for (const Entity& e : es) switch (e.type) {
            case EntityType::Line: ++lines; break; case EntityType::Circle: ++circles; break; case EntityType::Arc: ++arcs; break;
            case EntityType::Polyline: ++polys; break; case EntityType::Ellipse: ++ellipses; break; case EntityType::Spline: ++splines; break;
            case EntityType::Text: ++texts; break; case EntityType::Hatch: ++hatches; break; default: break; }
        QCOMPARE(circles, 1); QCOMPARE(arcs, 1); QCOMPARE(ellipses, 1); QCOMPARE(splines, 1); QCOMPARE(hatches, 1);
        QVERIFY(polys >= 1);
        QVERIFY(texts >= 3);     // text + dimension text + leader text
        QVERIFY(lines >= 1 + 2); // line + dimension/leader geometry
        bool walls = false; for (const Layer& l2 : ls) if (l2.name == QStringLiteral("Стены")) { walls = true; QCOMPARE(l2.color, 0xFF0000); }
        QVERIFY(walls);
        // geometry survives
        const Entity* c = nullptr; for (const Entity& e : es) if (e.type == EntityType::Circle) c = &e;
        QVERIFY(c && std::abs(c->radius - 10) < 1e-6 && std::abs(c->center.x - 50) < 1e-6);
        const Entity* p2 = nullptr; for (const Entity& e : es) if (e.type == EntityType::Polyline && e.closed) p2 = &e;
        QVERIFY(p2 && std::abs(p2->bulges[0] - 0.5) < 1e-6);
    }
    void selectionExportAndCancel() {
        Document d;
        quint64 a = d.addEntity(makeLine({0, 0}, {1, 1}));
        d.addEntity(makeCircle({0, 0}, 1));
        QByteArray part = Dxf::write(d, {a});
        QVERIFY(part.contains("LINE"));
        QVERIFY(!part.contains("CIRCLE"));
        std::vector<Entity> es; std::vector<Layer> ls; DxfReport rep; QString err;
        bool ok = Dxf::parse(QByteArray(kMinimalDxf), &es, &ls, &rep, &err, {}, [] { return true; });
        QVERIFY(!ok || es.empty());
        es.clear();
        QVERIFY(!Dxf::parse(QByteArray("this is not a dxf file at all"), &es, &ls, &rep, &err) || es.empty());
        es.clear();
        Dxf::parse(QByteArray("0\nSECTION\n2\nENTITIES\n0\nLINE\n10\nabc"), &es, &ls, &rep, &err);   // truncated / garbage: must not crash
    }
};

QTEST_GUILESS_MAIN(TestDxf)
#include "test_dxf.moc"
