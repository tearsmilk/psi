#include "core/document/document.h"
#include "io/pdf/pdf_export.h"
#include "rendering/renderer.h"
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

using namespace psi;

namespace {
QByteArray readAll(const QString& path) { QFile f(path); f.open(QIODevice::ReadOnly); return f.readAll(); }
QList<QByteArray> mediaBoxes(const QByteArray& pdf) {
    QList<QByteArray> out; int pos = 0;
    while ((pos = pdf.indexOf("/MediaBox", pos)) >= 0) { int a = pdf.indexOf('[', pos), b = pdf.indexOf(']', pos); out << pdf.mid(a + 1, b - a - 1).simplified(); pos = b; }
    return out;
}
bool boxIs(const QByteArray& box, double w, double h) {
    QList<QByteArray> p = box.split(' ');
    if (p.size() != 4) return false;
    return std::abs(p[2].toDouble() - w) < 0.01 && std::abs(p[3].toDouble() - h) < 0.01;
}
}

class TestPdf : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void exactPageSizes() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Document d;
        d.addEntity(makeRectangle({0, 0}, {100, 50}));
        d.addEntity(makeCircle({50, 25}, 20));
        std::vector<Sheet> sheets = d.sheets();
        Sheet a4 = sheets[0]; a4.name = "A4"; a4.width = 210; a4.height = 297; a4.landscape = false;
        a4.viewports = {SheetViewport{0, Rect{30, 100, 180, 250}, Point{50, 25}, 1.0, false}};
        d.updateSheet(a4);
        Sheet a3; a3.name = "A3 landscape"; a3.width = 297; a3.height = 420; a3.landscape = true; a3.title = TitleBlock::standard();
        a3.viewports = {SheetViewport{0, Rect{30, 70, 400, 285}, Point{50, 25}, 2.0, true}};
        quint64 a3id = d.addSheet(a3);
        Sheet a0; a0.name = "A0"; a0.width = 841; a0.height = 1189; quint64 a0id = d.addSheet(a0);
        QString path = dir.filePath("out.pdf"); QString err; QStringList warnings;
        int calls = 0;
        QVERIFY2(PdfExport::exportSheets(d, {sheets[0].id, a3id, a0id}, path, &err, &warnings, [&](int, int) { ++calls; return true; }), qPrintable(err));
        QCOMPARE(calls, 3);
        QByteArray pdf = readAll(path);
        QVERIFY(pdf.startsWith("%PDF-1."));
        QVERIFY(pdf.contains("%%EOF"));
        QList<QByteArray> boxes = mediaBoxes(pdf);
        QCOMPARE(boxes.size(), 3);
        QVERIFY2(boxIs(boxes[0], 595.276, 841.890), boxes[0].constData());
        QVERIFY2(boxIs(boxes[1], 1190.551, 841.890), boxes[1].constData());
        QVERIFY2(boxIs(boxes[2], 2383.937, 3370.394), boxes[2].constData());
        // vector content: no raster images, a visible stroke
        QVERIFY(!pdf.contains("/Subtype /Image"));
        QVERIFY(pdf.contains("/Type /Page"));
        // xref offsets still valid (we rewrote the boxes without changing the length)
        int sx = pdf.lastIndexOf("startxref");
        QVERIFY(sx > 0);
        qint64 off = pdf.mid(sx + 9, 20).simplified().split(' ')[0].toLongLong();
        QVERIFY(pdf.mid(int(off), 4) == "xref");
    }
    void clippingWarnings() {
        Document d;
        d.addEntity(makeRectangle({0, 0}, {1000, 1000}));
        Sheet s = d.sheets()[0];
        s.viewports = {SheetViewport{0, Rect{20, 20, 120, 120}, Point{500, 500}, 1.0, false}};   // 1:1 → does not fit
        QStringList w = PdfExport::checkClipping(d, s);
        QCOMPARE(w.size(), 1);
        s.viewports[0].scale = 0.05;   // 1:20 → fits
        QVERIFY(PdfExport::checkClipping(d, s).isEmpty());
        s.viewports[0].frame = Rect{150, 150, 300, 300};   // off the page
        QVERIFY(!PdfExport::checkClipping(d, s).isEmpty());
    }
    void sheetRendersTitleBlockAndContent() {
        Document d;
        d.addEntity(makeLine({0, 0}, {100, 0}));
        d.addEntity(makeText({10, 10}, QStringLiteral("Тест"), 5));
        Sheet s = d.sheets()[0]; s.title = TitleBlock::standard(); s.title.fields["name"] = AttrValue::string(QStringLiteral("Деталь"));
        s.viewports = {SheetViewport{0, Rect{30, 100, 180, 250}, Point{50, 5}, 1.0, true}};
        const double k = 4;   // px per mm
        QImage img(int(s.pageWidth() * k), int(s.pageHeight() * k), QImage::Format_ARGB32);
        img.fill(Qt::white);
        QPainter p(&img);
        QTransform t; t.translate(0, s.pageHeight() * k); t.scale(k, -k);
        Renderer::drawSheet(p, d, s, t, k, true);
        p.end();
        auto dark = [&](int x0, int y0, int x1, int y1) { int n = 0; for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) if (qGray(img.pixel(x, y)) < 128) ++n; return n; };
        // title block bottom-right (185 × 55 mm) must contain ink; the margin top-left stays white
        QVERIFY(dark(img.width() - int(180 * k), img.height() - int(50 * k), img.width() - int(10 * k), img.height() - int(10 * k)) > 200);
        QCOMPARE(dark(0, 0, int(3 * k), int(3 * k)), 0);
        // viewport content: frame centre (105,175) shows model (50,5) at 1:1 → the line at model y = 0 lies at sheet y = 170 mm, x 55…155
        int ly = img.height() - int(170 * k);
        QVERIFY(dark(int(60 * k), ly - 3, int(150 * k), ly + 3) > 50);
    }
    void errorsAreReported() {
        Document d;
        QString err;
        QVERIFY(!PdfExport::exportSheets(d, {}, "/nonexistent/dir/x.pdf", &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!PdfExport::exportSheets(d, {d.sheets()[0].id}, "/nonexistent/dir/x.pdf", &err));
        QVERIFY(!err.isEmpty());
    }
};

QTEST_MAIN(TestPdf)
#include "test_pdf.moc"
