#include "modules/model/kernel.h"
#include "rendering/view3d.h"
#include "ui/theme.h"
#include <QApplication>
#include <QtTest>

using namespace psi;

static int countNonBackground(const QImage& img, const QColor& bg) {
    int n = 0;
    const QRgb b = bg.rgb();
    for (int y = 0; y < img.height(); ++y) { const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y)); for (int x = 0; x < img.width(); ++x) if ((row[x] & 0xFFFFFF) != (b & 0xFFFFFF)) ++n; }
    return n;
}

class TestView3D : public QObject {
    Q_OBJECT
    md::Shape box_;
private Q_SLOTS:
    void initTestCase() {
        Theme::apply(*qApp, ThemeMode::Light, Theme::defaultAccent());
        md::Result r = md::Kernel::box({0, 0, 0}, 800, 400, 100);
        QVERIFY2(r.ok(), qPrintable(r.error));
        box_ = r.shape;
    }
    void offscreenRenderNonEmpty() {
        View3D v;
        v.resize(640, 480);
        v.setShowGrid(false);
        v.setItems({{1, box_.mesh(), QColor(), QStringLiteral("Плита")}});
        v.setStandardView(View3D::StdView::Iso);
        QImage img = v.renderToImage({640, 480});
        QCOMPARE(img.size(), QSize(640, 480));
        int n = countNonBackground(img, Theme::current().canvas);
        QVERIFY2(n > 640 * 480 / 8, qPrintable(QString::number(n)));   // тело занимает заметную часть кадра
        View3D e; e.setShowGrid(false);
        QImage empty = e.renderToImage({640, 480});
        int ne = countNonBackground(empty, Theme::current().canvas);
        QVERIFY2(ne < 2500, qPrintable(QString::number(ne)));   // пустой вид без сетки: только триада
    }
    void pickFaceEdgeVertex() {
        View3D v;
        v.resize(640, 480);
        v.setItems({{7, box_.mesh(), QColor(), QStringLiteral("Плита")}});
        v.setStandardView(View3D::StdView::Top);
        v.setPerspective(false);
        v.show();
        QCoreApplication::processEvents();
        QPointF c = v.project({400, 200, 100});
        View3D::Pick p = v.pickAt(c.toPoint());
        QVERIFY(p.valid());
        QCOMPARE(p.item, quint64(7));
        QVERIFY(p.face >= 1 && p.face <= 6);
        QVERIFY(std::abs(p.normal.z - 1) < 1e-3);          // верхняя грань
        QVERIFY(std::abs(p.point.z - 100) < 1.0);          // точка на верхней грани
        QVERIFY(std::abs(p.point.x - 400) < 2 && std::abs(p.point.y - 200) < 2);
        v.setPickMode(View3D::PickMode::Edge);
        QPointF e = v.project({400, 0, 100});
        View3D::Pick pe = v.pickAt(e.toPoint());
        QVERIFY(pe.valid() && pe.edge >= 1);
        v.setPickMode(View3D::PickMode::Vertex);
        QPointF vx = v.project({800, 400, 100});
        View3D::Pick pv = v.pickAt(vx.toPoint() + QPoint(3, 3));
        QVERIFY(pv.valid());
        QVERIFY(std::abs(pv.point.x - 800) < 1e-3 && std::abs(pv.point.y - 400) < 1e-3);
        QVERIFY(!v.pickAt(QPoint(2, 2)).valid());          // угол кадра пуст
        // выбор подсвечивается акцентом
        v.setPickMode(View3D::PickMode::Face);
        v.setSelection(p);
        QImage img = v.renderToImage({640, 480});
        const QColor acc = Theme::current().accent;
        int accentish = 0;
        for (int y = 0; y < img.height(); y += 2) for (int x = 0; x < img.width(); x += 2) { QColor px = img.pixelColor(x, y); if (std::abs(px.hue() - acc.hue()) < 12 && px.saturation() > 60) ++accentish; }
        QVERIFY2(accentish > 500, qPrintable(QString::number(accentish)));
    }
    void navigation() {
        View3D v;
        v.resize(400, 300);
        v.setItems({{1, box_.mesh(), QColor(), QString()}});
        v.zoomExtents();
        double d0 = v.distance();
        v.zoomBy(2.0);
        QVERIFY(std::abs(v.distance() - d0 / 2) < 1e-9);
        md::Vec3 t0 = v.target();
        v.pan(50, 0);
        QVERIFY((v.target() - t0).length() > 0);
        v.orbit(30, 10);
        v.setStandardView(View3D::StdView::Front, false);
        QPointF a = v.project({0, 0, 0}), b = v.project({100, 0, 0}), c = v.project({0, 0, 100});
        QVERIFY(b.x() > a.x() && std::abs(b.y() - a.y()) < 1e-6);   // X вправо
        QVERIFY(c.y() < a.y() && std::abs(c.x() - a.x()) < 1e-6);   // Z вверх
        QElapsedTimer t; t.start();
        v.renderToImage({1280, 800});
        qInfo("render 1280x800 box: %.1f ms", t.nsecsElapsed() / 1e6);
    }
};

QTEST_MAIN(TestView3D)
#include "test_view3d.moc"
