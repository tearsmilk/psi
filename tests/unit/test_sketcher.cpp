#include "modules/sketcher/sketch.h"
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QtTest>
#include <cmath>

using namespace psi;
using namespace psi::sk;

static SketchEntity mkLine(double x1, double y1, double x2, double y2) {
    SketchEntity e; e.kind = Kind::Line; e.p = {x1, y1, x2, y2}; return e;
}
static SketchEntity mkCircle(double cx, double cy, double r) {
    SketchEntity e; e.kind = Kind::Circle; e.p = {cx, cy, r}; return e;
}
static Constraint mk(CType t, std::vector<Ref> refs, double v = 0) {
    Constraint c; c.type = t; c.refs = std::move(refs); c.value = v; return c;
}

class TestSketcher : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // прямоугольник: 4 отрезка, совпадения углов, H/V, два размера → после закрепления одной точки DOF = 0
    void rectangleFullyConstrained() {
        Sketch s;
        quint64 a = s.addEntity(mkLine(0, 0, 100, 3));
        quint64 b = s.addEntity(mkLine(101, 3, 102, 50));
        quint64 c = s.addEntity(mkLine(102, 51, -1, 52));
        quint64 d = s.addEntity(mkLine(-1, 52, 1, 1));
        s.addConstraint(mk(CType::Coincident, {{a, 1}, {b, 0}}));
        s.addConstraint(mk(CType::Coincident, {{b, 1}, {c, 0}}));
        s.addConstraint(mk(CType::Coincident, {{c, 1}, {d, 0}}));
        s.addConstraint(mk(CType::Coincident, {{d, 1}, {a, 0}}));
        s.addConstraint(mk(CType::Horizontal, {{a, -1}}));
        s.addConstraint(mk(CType::Horizontal, {{c, -1}}));
        s.addConstraint(mk(CType::Vertical, {{b, -1}}));
        s.addConstraint(mk(CType::Vertical, {{d, -1}}));
        s.addConstraint(mk(CType::Length, {{a, -1}}, 120));
        s.addConstraint(mk(CType::Length, {{b, -1}}, 60));
        SolveResult r = s.solve(true);
        QVERIFY2(r.converged, qPrintable(QString::number(r.maxResidual)));
        QVERIFY(r.conflicts.empty());
        QCOMPARE(r.dof, 2);     // осталась только позиция
        const SketchEntity* la = s.entity(a);
        QVERIFY(std::abs(std::hypot(la->p[2] - la->p[0], la->p[3] - la->p[1]) - 120) < 1e-6);
        QVERIFY(std::abs(la->p[1] - la->p[3]) < 1e-6);
        Constraint fx = mk(CType::Fixed, {{a, 0}}); fx.fixedAt = {0, 0};
        s.addConstraint(fx);
        r = s.solve(true);
        QVERIFY(r.converged);
        QCOMPARE(r.dof, 0);
        QCOMPARE(s.degreesOfFreedom(), 0);
        // редактирование размера через setValue → перерешать
        quint64 lenId = 0; for (const auto& k : s.constraints()) if (k.type == CType::Length && k.refs[0].entity == a) lenId = k.id;
        QVERIFY(s.setValue(lenId, 200));
        r = s.solve();
        QVERIFY(r.converged);
        la = s.entity(a);
        QVERIFY(std::abs(std::hypot(la->p[2] - la->p[0], la->p[3] - la->p[1]) - 200) < 1e-6);
    }
    void tangentLineCircle() {
        Sketch s;
        quint64 c = s.addEntity(mkCircle(50, 50, 20));
        quint64 l = s.addEntity(mkLine(0, 10, 100, 12));
        s.entity(c)->fixed = true;
        s.addConstraint(mk(CType::Tangent, {{l, -1}, {c, -1}}));
        SolveResult r = s.solve(true);
        QVERIFY(r.converged);
        const SketchEntity* L = s.entity(l);
        double dx = L->p[2] - L->p[0], dy = L->p[3] - L->p[1], len = std::hypot(dx, dy);
        double dist = std::abs((50 - L->p[0]) * dy - (50 - L->p[1]) * dx) / len;
        QVERIFY2(std::abs(dist - 20) < 1e-6, qPrintable(QString::number(dist)));
        QCOMPARE(r.dof, 3);     // отрезок: 4 параметра − 1 касание
    }
    void conflictReported() {
        Sketch s;
        quint64 l = s.addEntity(mkLine(0, 0, 100, 0));
        s.addConstraint(mk(CType::Length, {{l, -1}}, 100));
        quint64 bad = s.addConstraint(mk(CType::Length, {{l, -1}}, 150));
        SolveResult r = s.solve(true);
        QVERIFY(!r.converged);
        QVERIFY(!r.conflicts.empty());
        bool marked = false;
        for (const auto& k : s.constraints()) if (k.conflict && (k.id == bad || k.refs[0].entity == l)) marked = true;
        QVERIFY(marked);
        QVERIFY(s.removeConstraint(bad));
        r = s.solve(true);
        QVERIFY(r.converged);
        QVERIFY(r.conflicts.empty());
        for (const auto& k : s.constraints()) QVERIFY(!k.conflict);
    }
    void dragAndAutoConstrain() {
        Sketch s;
        quint64 a = s.addEntity(mkLine(0, 0, 100, 0));
        quint64 b = s.addEntity(mkLine(100.2, 0.1, 100, 80));
        auto added = s.autoConstrain(b, 1.0);
        QVERIFY(added.size() >= 2);   // совпадение концов + вертикаль
        QVERIFY(s.solve().converged);
        QVERIFY(std::abs(s.pointOf({b, 0}).x - s.pointOf({a, 1}).x) < 1e-6);
        s.dragPoint({a, 0}, {-20, 5});
        QVERIFY(std::abs(s.pointOf({a, 0}).x + 20) < 1e-6);
        QVERIFY(std::abs(s.pointOf({b, 0}).x - s.pointOf({a, 1}).x) < 1e-6);
    }
    void jsonAndDraftExport() {
        Sketch s; s.name = QStringLiteral("Тест");
        quint64 l = s.addEntity(mkLine(0, 0, 100, 0));
        SketchEntity cons = mkLine(0, 0, 0, 50); cons.construction = true; s.addEntity(cons);
        s.addEntity(mkCircle(50, 50, 10));
        s.addConstraint(mk(CType::Length, {{l, -1}}, 100));
        QJsonObject o = s.toJson();
        Sketch t = Sketch::fromJson(QJsonDocument::fromJson(QJsonDocument(o).toJson()).object());
        QCOMPARE(t.name, s.name);
        QCOMPARE(t.entities().size(), size_t(3));
        QCOMPARE(t.constraints().size(), size_t(1));
        QVERIFY(t.entities()[0] == s.entities()[0]);
        auto ents = t.toDraftEntities();
        QCOMPARE(ents.size(), size_t(2));    // вспомогательная не выдаётся
    }
    void perf200() {
        Sketch s;
        std::vector<quint64> ids;
        for (int i = 0; i < 200; ++i) {
            double x = (i % 20) * 50.0, y = (i / 20) * 50.0;
            ids.push_back(s.addEntity(mkLine(x, y, x + 40 + (i % 3), y + (i % 2) * 0.5)));
        }
        for (int i = 0; i < 200; ++i) {
            s.addConstraint(mk(CType::Horizontal, {{ids[i], -1}}));
            s.addConstraint(mk(CType::Length, {{ids[i], -1}}, 40));
            if (i % 20) s.addConstraint(mk(CType::Coincident, {{ids[i - 1], 1}, {ids[i], 0}}));
        }
        QVERIFY(s.solve().converged);
        QElapsedTimer t; t.start();
        s.dragPoint({ids[0], 0}, {3, 2});
        qint64 ns = t.nsecsElapsed();
        qInfo("drag re-solve of 200 entities: %.2f ms", ns / 1e6);
        if (ns > 10'000'000) qWarning("perf: drag re-solve took %.1f ms (> 10 ms)", ns / 1e6);
    }
};

QTEST_GUILESS_MAIN(TestSketcher)
#include "test_sketcher.moc"
