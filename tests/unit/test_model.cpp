#include "io/step/step.h"
#include "io/stl/stl.h"
#include "modules/model/part.h"
#include "modules/sketcher/sketch.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using namespace psi;
using namespace psi::md;

static sk::Sketch rectSketch(double w, double h) {
    sk::Sketch s;
    auto L = [&](double a, double b, double c, double d) { sk::SketchEntity e; e.kind = sk::Kind::Line; e.p = {a, b, c, d}; s.addEntity(e); };
    L(0, 0, w, 0); L(w, 0, w, h); L(w, h, 0, h); L(0, h, 0, 0);
    return s;
}

class TestModel : public QObject {
    Q_OBJECT
    Part part_;
    quint64 sketch_ = 0, extrude_ = 0, hole_ = 0;
    static constexpr double kVol = 800.0 * 400.0 * 100.0;

private Q_SLOTS:
    void initTestCase() {
        QVERIFY(Kernel::version().startsWith(QStringLiteral("Open CASCADE")));
        sketch_ = part_.addFeature(FeatureType::Sketch, {{"plane", "XY"}, {"sketch", rectSketch(800, 400).toJson()}});
        extrude_ = part_.addFeature(FeatureType::Extrude, {{"sketch", double(sketch_)}, {"distance", 100}, {"mode", "new"}});
    }
    void boxVolume() {
        RebuildReport r = part_.rebuild();
        QVERIFY2(r.ok(), qPrintable(r.errors.join("; ")));
        QCOMPARE(part_.bodies().size(), size_t(1));
        QVERIFY(std::abs(part_.massProps().volume - kVol) < 1e-3);
        QCOMPARE(part_.shape().faceCount(), 6);
        QCOMPARE(part_.shape().edgeCount(), 12);
        QVERIFY(part_.shape().isValid());
        QVERIFY(std::abs(part_.mass() - kVol / 1000.0 * 7.85) < 1e-6);   // г при плотности стали
        Bbox bb = part_.shape().bbox();
        QVERIFY(bb.valid && std::abs(bb.size().x - 800) < 1e-3 && std::abs(bb.size().z - 100) < 1e-3);
    }
    void holeCutsVolume() {
        hole_ = part_.addFeature(FeatureType::Hole, {{"point", QJsonArray{400, 200, 100}}, {"dir", QJsonArray{0, 0, 1}}, {"diameter", 20}, {"through", true}});
        RebuildReport r = part_.rebuild();
        QVERIFY2(r.ok(), qPrintable(r.errors.join("; ")));
        QVERIFY(std::abs(part_.massProps().volume - (kVol - M_PI * 100 * 100)) < 1e-3);
        QCOMPARE(part_.shape().faceCount(), 7);
    }
    void holeByFaceWithCountersink() {
        Part p;
        quint64 s = p.addFeature(FeatureType::Sketch, {{"plane", "XY"}, {"sketch", rectSketch(100, 100).toJson()}});
        p.addFeature(FeatureType::Extrude, {{"sketch", double(s)}, {"distance", 20}, {"mode", "new"}});
        QVERIFY(p.rebuild().ok());
        int top = 0;
        for (int i = 1; i <= p.shape().faceCount(); ++i) { bool ok; Plane pl = p.shape().planeOfFace(i, &ok); if (ok && pl.normal.z > 0.9) top = i; }
        QVERIFY(top > 0);
        p.addFeature(FeatureType::Hole, {{"faceIndex", top}, {"point", QJsonArray{50, 50, 20}}, {"diameter", 10}, {"depth", 10}, {"through", false}, {"countersink", true}, {"csDiameter", 16}, {"csAngle", 90}});
        RebuildReport r = p.rebuild();
        QVERIFY2(r.ok(), qPrintable(r.errors.join("; ")));
        double v = p.massProps().volume;
        QVERIFY2(v < 100 * 100 * 20 - M_PI * 25 * 10 && v > 100 * 100 * 20 - M_PI * 64 * 10, qPrintable(QString::number(v)));
    }
    void filletChangesFaceCount() {
        int before = part_.shape().faceCount();
        quint64 f = part_.addFeature(FeatureType::Fillet, {{"radius", 5}});
        RebuildReport r = part_.rebuild();
        QVERIFY2(r.ok(), qPrintable(r.errors.join("; ")));
        QVERIFY(part_.shape().faceCount() > before);
        QVERIFY(part_.massProps().volume < kVol - M_PI * 100 * 100);
        part_.removeFeature(f);
        QVERIFY(part_.rebuild().ok());
        QCOMPARE(part_.shape().faceCount(), before);
    }
    void chamferAndSuppress() {
        quint64 c = part_.addFeature(FeatureType::Chamfer, {{"distance", 3}});
        QVERIFY(part_.rebuild().ok());
        QVERIFY(part_.shape().faceCount() > 7);
        QVERIFY(part_.setSuppressed(c, true));
        QVERIFY(part_.rebuild().ok());
        QCOMPARE(part_.shape().faceCount(), 7);
        part_.removeFeature(c);
    }
    void rebuildAfterDistanceChange() {
        QVERIFY(part_.setParams(extrude_, {{"sketch", double(sketch_)}, {"distance", 50}, {"mode", "new"}}));
        QVERIFY(part_.dirty());
        RebuildReport r = part_.rebuild();
        QVERIFY2(r.ok(), qPrintable(r.errors.join("; ")));
        QCOMPARE(r.rebuilt, 2);   // выдавливание + отверстие, эскиз не трогали
        QVERIFY(std::abs(part_.massProps().volume - (800.0 * 400 * 50 - M_PI * 100 * 50)) < 1e-3);
        QVERIFY(part_.setParams(extrude_, {{"sketch", double(sketch_)}, {"distance", 100}, {"mode", "new"}}));
        QVERIFY(part_.rebuild().ok());
    }
    void failingFilletKeepsLastGood() {
        double vol = part_.massProps().volume;
        quint64 f = part_.addFeature(FeatureType::Fillet, {{"radius", 500}});
        RebuildReport r = part_.rebuild();
        QVERIFY(!r.ok());
        QCOMPARE(r.failed.size(), size_t(1));
        QCOMPARE(r.failed[0], f);
        QVERIFY2(r.errors[0].contains(QStringLiteral("500")), qPrintable(r.errors[0]));
        QVERIFY(r.errors[0].startsWith(part_.feature(f)->name));
        QVERIFY(!part_.feature(f)->ok());
        QVERIFY(std::abs(part_.massProps().volume - vol) < 1e-6);   // последнее хорошее тело
        QCOMPARE(part_.shape().faceCount(), 7);
        part_.removeFeature(f);
        QVERIFY(part_.rebuild().ok());
    }
    void linearAndCircularPattern() {
        quint64 lp = part_.addFeature(FeatureType::LinearPattern, {{"feature", double(hole_)}, {"count", 3}, {"step", QJsonArray{100, 0, 0}}});
        RebuildReport r = part_.rebuild();
        QVERIFY2(r.ok(), qPrintable(r.errors.join("; ")));
        QVERIFY(std::abs(part_.massProps().volume - (kVol - 3 * M_PI * 100 * 100)) < 1e-3);
        QCOMPARE(part_.shape().faceCount(), 9);   // 6 + 3 отверстия
        part_.removeFeature(lp);
        quint64 cp = part_.addFeature(FeatureType::CircularPattern, {{"feature", double(hole_)}, {"count", 4}, {"angle", 360}, {"axisPoint", QJsonArray{400, 200, 0}}, {"axisDir", QJsonArray{0, 0, 1}}});
        // отверстие в центре → копии совпадают с оригиналом; сдвинем отверстие
        QVERIFY(part_.setParams(hole_, {{"point", QJsonArray{500, 200, 100}}, {"dir", QJsonArray{0, 0, 1}}, {"diameter", 20}, {"through", true}}));
        r = part_.rebuild();
        QVERIFY2(r.ok(), qPrintable(r.errors.join("; ")));
        QVERIFY(std::abs(part_.massProps().volume - (kVol - 4 * M_PI * 100 * 100)) < 1e-3);
        part_.removeFeature(cp);
        QVERIFY(part_.setParams(hole_, {{"point", QJsonArray{400, 200, 100}}, {"dir", QJsonArray{0, 0, 1}}, {"diameter", 20}, {"through", true}}));
        QVERIFY(part_.rebuild().ok());
    }
    void mirrorBooleanAndBodies() {
        Part p;
        quint64 s = p.addFeature(FeatureType::Sketch, {{"plane", "XY"}, {"sketch", rectSketch(100, 50).toJson()}});
        quint64 e = p.addFeature(FeatureType::Extrude, {{"sketch", double(s)}, {"distance", 30}, {"mode", "new"}});
        p.addFeature(FeatureType::Mirror, {{"feature", double(e)}, {"plane", "YZ"}});   // копия при x<0, смежная по грани x=0
        QVERIFY(p.rebuild().ok());
        QVERIFY(std::abs(p.massProps().volume - 2 * 100 * 50 * 30) < 1e-3);
        // второе тело и булево вычитание
        quint64 s2 = p.addFeature(FeatureType::Sketch, {{"plane", "XY"}, {"offset", 10}, {"sketch", rectSketch(20, 20).toJson()}});
        p.addFeature(FeatureType::Extrude, {{"sketch", double(s2)}, {"distance", 10}, {"mode", "new"}});
        QVERIFY(p.rebuild().ok());
        QCOMPARE(p.bodies().size(), size_t(2));
        p.addFeature(FeatureType::Boolean, {{"a", 0}, {"b", 1}, {"op", "subtract"}});
        RebuildReport r = p.rebuild();
        QVERIFY2(r.ok(), qPrintable(r.errors.join("; ")));
        QCOMPARE(p.bodies().size(), size_t(1));
        QVERIFY(std::abs(p.massProps().volume - (2 * 100 * 50 * 30 - 20 * 20 * 10)) < 1e-3);
    }
    void revolveAndDatum() {
        Part p;
        quint64 ax = p.addFeature(FeatureType::DatumAxis, {{"base", "Z"}});
        quint64 pl = p.addFeature(FeatureType::DatumPlane, {{"base", "XZ"}, {"offset", 0}});
        // прямоугольник 10..30 × 0..50 в плоскости XZ → вращение вокруг Z даёт трубу r=10..30, h=50
        quint64 s = p.addFeature(FeatureType::Sketch, {{"plane", "datum"}, {"datum", double(pl)}, {"sketch", [] { sk::Sketch k; auto L = [&](double a, double b, double c, double d) { sk::SketchEntity e; e.kind = sk::Kind::Line; e.p = {a, b, c, d}; k.addEntity(e); }; L(10, 0, 30, 0); L(30, 0, 30, 50); L(30, 50, 10, 50); L(10, 50, 10, 0); return k.toJson(); }()}});
        p.addFeature(FeatureType::Revolve, {{"sketch", double(s)}, {"axis", "datum"}, {"datum", double(ax)}, {"angle", 360}, {"mode", "new"}});
        RebuildReport r = p.rebuild();
        QVERIFY2(r.ok(), qPrintable(r.errors.join("; ")));
        QVERIFY2(std::abs(p.massProps().volume - M_PI * (900 - 100) * 50) < 1e-2, qPrintable(QString::number(p.massProps().volume)));
    }
    void sketchWithArcAndHole() {
        sk::Sketch k;
        auto L = [&](double a, double b, double c, double d) { sk::SketchEntity e; e.kind = sk::Kind::Line; e.p = {a, b, c, d}; k.addEntity(e); };
        L(0, 0, 100, 0); L(100, 0, 100, 50);
        { sk::SketchEntity e; e.kind = sk::Kind::Arc; e.p = {50, 50, 50, 0, M_PI}; k.addEntity(e); }   // полукруг 100,50 → 0,50
        L(0, 50, 0, 0);
        { sk::SketchEntity e; e.kind = sk::Kind::Circle; e.p = {50, 40, 10}; k.addEntity(e); }
        { sk::SketchEntity e; e.kind = sk::Kind::Line; e.p = {0, 0, 100, 100}; e.construction = true; k.addEntity(e); }
        QString warn;
        Profile prof = profileFromSketch(k, &warn);
        QCOMPARE(prof.loops.size(), size_t(1));
        QCOMPARE(prof.circles.size(), size_t(1));
        QVERIFY(warn.isEmpty());
        Result r = Kernel::extrude(prof, Plane::XY(), 10);
        QVERIFY2(r.ok(), qPrintable(r.error));
        double expect = (100 * 50 + M_PI * 2500 / 2 - M_PI * 100) * 10;
        QVERIFY2(std::abs(r.shape.massProps().volume - expect) < 1e-2, qPrintable(QString::number(r.shape.massProps().volume)));
    }
    void jsonRoundTrip() {
        QJsonObject o = part_.toJson();
        Part q = Part::fromJson(QJsonDocument::fromJson(QJsonDocument(o).toJson()).object());
        QCOMPARE(q.features().size(), part_.features().size());
        QVERIFY(q.dirty());
        QVERIFY(q.rebuild().ok());
        QVERIFY(std::abs(q.massProps().volume - part_.massProps().volume) < 1e-6);
        QByteArray brep = part_.brepCache();
        QVERIFY(brep.size() > 100);
        Part c; QVERIFY(c.restoreFromBrep(brep));
        QVERIFY(std::abs(c.massProps().volume - part_.massProps().volume) < 1e-6);
    }
    void stepRoundTrip() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("part.step");
        StepReport wr; QString err;
        QVERIFY2(Step::write(path, {{QStringLiteral("Плита"), part_.shape()}}, &err, &wr), qPrintable(err));
        QCOMPARE(wr.bodies, 1);
        QVERIFY(QFileInfo(path).size() > 1000);
        StepReport rr;
        auto bodies = Step::read(path, &rr, &err);
        QVERIFY2(!bodies.empty(), qPrintable(err));
        QCOMPARE(rr.bodies, 1);
        double v0 = part_.massProps().volume, v1 = bodies[0].shape.massProps().volume;
        QVERIFY2(std::abs(v0 - v1) / v0 < 1e-6, qPrintable(QStringLiteral("%1 vs %2").arg(v0).arg(v1)));
        QVERIFY(rr.summary().contains(QStringLiteral("мм")));
        bool cancel = true;
        StepReport cr;
        Step::read(path, &cr, nullptr, [&] { return cancel; });
        QVERIFY(cr.cancelled);
    }
    void stlExportImport() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        for (bool binary : {true, false}) {
            const QString path = dir.filePath(binary ? "part.stl" : "part_ascii.stl");
            StlOptions opt; opt.binary = binary; opt.deflection = 0.5;
            StlReport wr; QString err;
            QVERIFY2(Stl::write(path, {part_.shape()}, opt, &err, &wr), qPrintable(err));
            QVERIFY(wr.triangles > 0);
            StlReport rr;
            StlMesh m = Stl::read(path, &rr, &err);
            QVERIFY2(!m.indices.empty(), qPrintable(err));
            QCOMPARE(rr.triangles, wr.triangles);
            QCOMPARE(rr.binary, binary);
            Shape body = Stl::toMeshBody(m, &err);
            QVERIFY2(!body.isNull(), qPrintable(err));
            Bbox bb = body.bbox();
            QVERIFY(bb.valid);
            QVERIFY2(std::abs(bb.size().x - 800) < 0.6 && std::abs(bb.size().y - 400) < 0.6 && std::abs(bb.size().z - 100) < 0.6, qPrintable(QStringLiteral("%1 %2 %3").arg(bb.size().x).arg(bb.size().y).arg(bb.size().z)));
            QVERIFY(body.mesh().triangleCount() == size_t(rr.triangles));
            // тело-сетка в дереве: MeshBody-операция и отказ в STEP
            Part p;
            p.addFeature(FeatureType::MeshBody, {{"name", "Сетка"}, {"mesh", QString::fromLatin1(Stl::encodeMesh(m).toBase64())}});
            QVERIFY(p.rebuild().ok());
            QVERIFY(p.bodies()[0].mesh);
            p.addFeature(FeatureType::Fillet, {{"radius", 1}});
            QVERIFY(!p.rebuild().ok());
            StepReport sr;
            QVERIFY(!Step::write(dir.filePath("mesh.step"), {{QStringLiteral("Сетка"), p.bodies()[0].shape}}, &err, &sr));
            QVERIFY(!sr.unsupported.isEmpty());
        }
    }
    void meshHasNormalsAndEdges() {
        Mesh m = part_.shape().mesh(0.5);
        QVERIFY(m.triangleCount() > 0);
        QCOMPARE(m.normals.size(), m.positions.size());
        QCOMPARE(m.faceOfTriangle.size(), m.triangleCount());
        QVERIFY(m.segmentCount() >= 12);
        uint32_t maxFace = 0; for (uint32_t f : m.faceOfTriangle) maxFace = std::max(maxFace, f);
        QCOMPARE(int(maxFace), part_.shape().faceCount());
    }
    void failureMessagesAreRussian() {
        Result r = Kernel::extrude(Profile{}, Plane::XY(), 10);
        QVERIFY(!r.ok()); QVERIFY(r.error.contains(QStringLiteral("Профиль")));
        r = Kernel::fillet(Shape(), {}, 1);
        QVERIFY(r.error.contains(QStringLiteral("пусто")));
        Part p;
        p.addFeature(FeatureType::Extrude, {{"sketch", 99}, {"distance", 10}});
        RebuildReport rep = p.rebuild();
        QVERIFY(!rep.ok()); QVERIFY(rep.errors[0].contains(QStringLiteral("эскиз")));
    }
};

QTEST_GUILESS_MAIN(TestModel)
#include "test_model.moc"
