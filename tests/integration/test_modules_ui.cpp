// Offscreen UI tests of the sketch and interior canvases: tools by mouse, constraints by keys, drag re-solve.
#include "modules/interior/interior_view.h"
#include "modules/sketcher/sketch_view.h"
#include "ui/theme.h"
#include <QApplication>
#include <QSignalSpy>
#include <QtTest>

using namespace psi;

class TestModulesUi : public QObject {
    Q_OBJECT
    static void click(QWidget* w, QPointF s, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QTest::mouseMove(w, s.toPoint());
        QTest::mouseClick(w, Qt::LeftButton, mods, s.toPoint());
    }
private Q_SLOTS:
    void initTestCase() { Theme::apply(*qApp, ThemeMode::Light, Theme::defaultAccent()); }

    void sketchDrawAndConstrain() {
        sk::Sketch s;
        sk::SketchView v; v.resize(800, 600); v.show(); QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.setSketch(&s); v.setGridSnap(false);
        QSignalSpy changed(&v, &sk::SketchView::changed);
        // отрезок двумя щелчками
        v.setTool(sk::SketchView::Tool::Line);
        click(&v, v.toScreen({0, 0})); click(&v, v.toScreen({100, 0.3}));
        QCOMPARE(s.entities().size(), size_t(1));
        QCOMPARE(changed.count(), 1);
        // автопривязка: почти горизонтальный отрезок получил ограничение Horizontal
        bool horizontal = false; for (const auto& c : s.constraints()) if (c.type == sk::CType::Horizontal) horizontal = true;
        QVERIFY(horizontal);
        QVERIFY(std::abs(s.entities()[0].p[3] - s.entities()[0].p[1]) < 1e-6);
        // прямоугольник: 4 отрезка + 8 ограничений + совпадение с концом первого отрезка (автопривязка)
        v.setTool(sk::SketchView::Tool::Rectangle);
        click(&v, v.toScreen({100, 0})); click(&v, v.toScreen({180, 50}));
        QCOMPARE(s.entities().size(), size_t(5));
        QVERIFY(s.constraints().size() >= 9);
        QVERIFY(v.lastResult().converged);
        // окружность и касание по клавише T
        v.setTool(sk::SketchView::Tool::Circle);
        click(&v, v.toScreen({50, 40})); click(&v, v.toScreen({50, 60}));
        QCOMPARE(s.entities().size(), size_t(6));
        v.setTool(sk::SketchView::Tool::Select);
        click(&v, v.toScreen({50, 0}));                                   // отрезок
        click(&v, v.toScreen({50, 60}), Qt::ShiftModifier);               // окружность (на дуге)
        QCOMPARE(v.selectedEntities().size(), size_t(2));
        QTest::keyClick(&v, Qt::Key_T);
        bool tangent = false; for (const auto& c : s.constraints()) if (c.type == sk::CType::Tangent) tangent = true;
        QVERIFY(tangent);
        QVERIFY(v.lastResult().converged);
        const sk::SketchEntity* circle = nullptr; for (const auto& e : s.entities()) if (e.kind == sk::Kind::Circle) circle = &e;
        QVERIFY(circle);
        const sk::SketchEntity& l0 = s.entities()[0];
        double dl = std::abs((l0.point(1) - l0.point(0)).normalized().cross(circle->center() - l0.point(0)));
        QVERIFY2(std::abs(dl - circle->p[2]) < 1e-5, qPrintable(QStringLiteral("distance %1 vs r %2").arg(dl).arg(circle->p[2])));
        // размер по L на выбранном отрезке → подпись; двойной клик по подписи открывает диалог — проверяем hit через editConstraintValue недоступно без UI, поэтому через setValue
        v.clearSelection(); click(&v, v.toScreen({50, 0}));
        QTest::keyClick(&v, Qt::Key_L);
        quint64 lenId = 0; for (const auto& c : s.constraints()) if (c.type == sk::CType::Length) lenId = c.id;
        QVERIFY(lenId);
        QVERIFY(s.setValue(lenId, 150)); v.resolve();
        QVERIFY(v.lastResult().converged);
        QVERIFY(std::abs(dist(s.entities()[0].point(0), s.entities()[0].point(1)) - 150) < 1e-5);
        // перетаскивание свободной точки с перерешением
        v.clearSelection();
        QPointF from = v.toScreen(s.entities()[0].point(0));
        QTest::mousePress(&v, Qt::LeftButton, Qt::NoModifier, from.toPoint());
        QTest::mouseMove(&v, (from + QPointF(0, -40)).toPoint());
        QTest::mouseMove(&v, (from + QPointF(0, -80)).toPoint());
        QTest::mouseRelease(&v, Qt::LeftButton, Qt::NoModifier, (from + QPointF(0, -80)).toPoint());
        QVERIFY(v.lastResult().converged);
        QVERIFY(std::abs(dist(s.entities()[0].point(0), s.entities()[0].point(1)) - 150) < 1e-5);   // размер удержан
        QVERIFY(s.entities()[0].point(0).y > 10);                                                      // точка ушла вверх
        // удаление по Delete
        click(&v, v.toScreen(s.entities()[0].point(1)) + QPointF(0, 0));
        QTest::keyClick(&v, Qt::Key_Delete);
        QCOMPARE(s.entities().size(), size_t(5));
        QVERIFY(v.lastResult().dof >= 0);
    }

    void interiorTools() {
        in::Building b;
        in::InteriorView v; v.resize(900, 700); v.show(); QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.setBuilding(&b); v.zoomTo(Rect(-500, -500, 7000, 5000));
        QSignalSpy changed(&v, &in::InteriorView::changed);
        v.setTool(in::InteriorView::Tool::Wall);
        for (Point p : {Point{0, 0}, Point{6200, 0}, Point{6200, 4200}, Point{0, 4200}}) click(&v, v.toScreen(p));
        click(&v, v.toScreen({0, 0}));                       // замыкание на первую точку
        QTest::mouseDClick(&v, Qt::LeftButton, Qt::NoModifier, v.toScreen({0, 0}).toPoint());
        QCOMPARE(b.floor(0).walls.size(), size_t(1));
        QCOMPARE(b.floor(0).walls[0].pts.size(), size_t(5));
        QCOMPARE(v.rooms().size(), size_t(1));
        QVERIFY(std::abs(v.rooms()[0].areaM2 - 24.0) < 0.01);
        // дверь на нижней стене
        v.setTool(in::InteriorView::Tool::Door);
        click(&v, v.toScreen({2000, 60}));
        QCOMPARE(b.floor(0).openings.size(), size_t(1));
        QVERIFY(std::abs(b.floor(0).openings[0].offset - 2000) < 60);
        // окно на правой стене, с автоматическим зажимом к длине стены
        v.setTool(in::InteriorView::Tool::Window);
        click(&v, v.toScreen({6150, 2000}));
        QCOMPARE(b.floor(0).openings.size(), size_t(2));
        QCOMPARE(b.floor(0).openings[1].kind, in::OpeningKind::Window);
        // мебель: поворот R перед размещением
        v.setPlacing(in::Building::makeFurniture(in::FurnitureKind::Bed));
        QTest::keyClick(&v, Qt::Key_R);
        click(&v, v.toScreen({3000, 2000}));
        QCOMPARE(b.floor(0).furniture.size(), size_t(1));
        QVERIFY(std::abs(b.floor(0).furniture[0].rotation - kPi / 2) < 1e-9);
        QVERIFY(v.collisions().empty());
        // выбор и перенос мебели в стену → коллизия
        v.setTool(in::InteriorView::Tool::Select);
        QPointF from = v.toScreen({3000, 2000});
        QTest::mousePress(&v, Qt::LeftButton, Qt::NoModifier, from.toPoint());
        QCOMPARE(v.selection().kind, in::InteriorView::Selection::Furniture);
        QTest::mouseMove(&v, (from + QPointF(0, 60)).toPoint());
        QPointF to = v.toScreen({3000, 300});
        QTest::mouseMove(&v, to.toPoint());
        QTest::mouseRelease(&v, Qt::LeftButton, Qt::NoModifier, to.toPoint());
        QVERIFY(b.floor(0).furniture[0].pos.y < 800);
        QVERIFY(!v.collisions().empty());
        // удаление
        QTest::keyClick(&v, Qt::Key_Delete);
        QCOMPARE(b.floor(0).furniture.size(), size_t(0));
        QVERIFY(changed.count() >= 5);
        // ведомости по этажу
        QCOMPARE(in::Building::materialSchedule(b.floor(0)).size() >= 1, true);
    }
};

QTEST_MAIN(TestModulesUi)
#include "test_modules_ui.moc"
