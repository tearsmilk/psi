#include "modules/drawing/sheet.h"
#include <QtTest>

using namespace psi;

class TestSheets : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void paperSizesIso() {
        const auto& ps = paperSizes();
        QVERIFY(ps.size() >= 7);
        QCOMPARE(ps[0].name, QStringLiteral("A0"));
        QCOMPARE(ps[0].width, 841.0); QCOMPARE(ps[0].height, 1189.0);
        bool a4 = false;
        for (const auto& p : ps) if (p.name == "A4") { a4 = true; QCOMPARE(p.width, 210.0); QCOMPARE(p.height, 297.0); }
        QVERIFY(a4);
    }
    void scales() {
        QCOMPARE(parseScale("1:5"), 0.2);
        QCOMPARE(parseScale("2:1"), 2.0);
        QCOMPARE(parseScale("1 : 100"), 0.01);
        QCOMPARE(parseScale("0.5"), 0.5);
        QCOMPARE(parseScale("abc"), 0.0);
        QCOMPARE(scaleLabel(0.02), QStringLiteral("1:50"));
        QCOMPARE(scaleLabel(1), QStringLiteral("1:1"));
        QCOMPARE(scaleLabel(2), QStringLiteral("2:1"));
        QVERIFY(!scalePresets().empty());
    }
    void sheetGeometry() {
        Sheet s;
        QCOMPARE(s.pageWidth(), 210.0); QCOMPARE(s.pageHeight(), 297.0);
        QCOMPARE(s.paperName(), QStringLiteral("A4"));
        s.landscape = true;
        QCOMPARE(s.pageWidth(), 297.0); QCOMPARE(s.pageHeight(), 210.0);
        Rect f = s.frameRect();
        QCOMPARE(f.x0, 20.0); QCOMPARE(f.y0, 5.0); QCOMPARE(f.x1, 292.0); QCOMPARE(f.y1, 205.0);
        s.width = 300; s.height = 400;
        QVERIFY(s.paperName().contains("300"));
    }
    void viewportMapping() {
        SheetViewport vp;
        vp.frame = Rect{10, 10, 110, 60};    // 100 × 50 mm on paper
        vp.scale = 0.02;                     // 1:50
        vp.modelCenter = Point{3000, 2000};
        Rect w = vp.modelWindow();
        QVERIFY(std::abs(w.width() - 5000) < 1e-9 && std::abs(w.height() - 2500) < 1e-9);
        QVERIFY(std::abs(w.center().x - 3000) < 1e-9);
        Transform t = vp.modelToSheet();
        Point c = t.map(vp.modelCenter);
        QVERIFY(std::abs(c.x - 60) < 1e-9 && std::abs(c.y - 35) < 1e-9);
        Point corner = t.map({3000 - 2500, 2000 - 1250});
        QVERIFY(std::abs(corner.x - 10) < 1e-9 && std::abs(corner.y - 10) < 1e-9);
    }
    void titleBlockAndJson() {
        Sheet s;
        s.name = QStringLiteral("План");
        s.title = TitleBlock::standard();
        s.title.fields["name"] = AttrValue::string(QStringLiteral("Деталь ✓"));
        s.title.fields["sheets"] = AttrValue::integer(3);
        SheetViewport vp; vp.frame = Rect{1, 2, 3, 4}; vp.scale = 0.5; vp.modelCenter = {7, 8}; vp.border = true; vp.id = 11;
        s.viewports.push_back(vp);
        s.id = 5; s.landscape = true; s.marginLeft = 25;
        Sheet back = Sheet::fromJson(s.toJson());
        QCOMPARE(back.id, s.id); QCOMPARE(back.name, s.name); QCOMPARE(back.landscape, true); QCOMPARE(back.marginLeft, 25.0);
        QCOMPARE(back.title.fields["name"].display(), QStringLiteral("Деталь ✓"));
        QCOMPARE(back.title.fields["sheets"].value.toInt(), 3);
        QCOMPARE(back.viewports.size(), size_t(1));
        QCOMPARE(back.viewports[0].id, quint64(11));
        QCOMPARE(back.viewports[0].scale, 0.5);
        QVERIFY(back.viewports[0].border);
        QCOMPARE(back.viewports[0].modelCenter.x, 7.0);
    }
};

QTEST_GUILESS_MAIN(TestSheets)
#include "test_sheets.moc"
