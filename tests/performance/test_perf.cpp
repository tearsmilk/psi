#include "core/command-system/command.h"
#include "core/document/document.h"
#include "io/native/psi_file.h"
#include "rendering/viewport.h"
#include <QElapsedTimer>
#include <QtTest>

using namespace psi;

namespace {
void fill(Document& d, int n) {
    for (int i = 0; i < n; ++i) {
        double x = double(i % 250) * 10, y = double(i / 250) * 10;
        d.addEntity(makeLine({x, y}, {x + 8, y + 6}));
    }
}
// CI machines vary; budgets are generous multiples of what a 2015 MacBook does, the numbers are logged anyway.
constexpr qint64 kRenderBudgetMs = 1500, kQueryBudgetMs = 200, kUndoBudgetMs = 1000, kSaveBudgetMs = 3000;
}

class TestPerf : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void render50kLines() {
        Document d;
        QElapsedTimer t; t.start();
        fill(d, 50000);
        qInfo("insert 50 000: %lld ms", t.elapsed());
        Viewport v(d);
        v.zoomExtents();
        t.restart();
        QImage img = v.renderToImage(QSize(1920, 1080));
        qint64 ms = t.elapsed();
        qInfo("render 50 000 lines full extents: %lld ms", ms);
        QVERIFY(!img.isNull());
        QVERIFY2(ms < kRenderBudgetMs, qPrintable(QString::number(ms)));
        // zoomed in: only a window of the drawing is rendered → must be much cheaper than full
        v.zoomTo(Rect(1000, 1000, 1100, 1060));
        t.restart();
        v.renderToImage(QSize(1920, 1080));
        qint64 zoomed = t.elapsed();
        qInfo("render zoomed window: %lld ms", zoomed);
        QVERIFY(zoomed < kRenderBudgetMs);
    }
    void spatialQueries() {
        Document d; fill(d, 50000);
        QElapsedTimer t; t.start();
        size_t total = 0;
        for (int i = 0; i < 2000; ++i) { double x = double(i % 100) * 25, y = double(i / 100) * 25; total += d.query(Rect(x, y, x + 30, y + 30)).size(); }
        qint64 ms = t.elapsed();
        qInfo("2000 window queries: %lld ms (%zu hits)", ms, total);
        QVERIFY(total > 2000);
        QVERIFY2(ms < kQueryBudgetMs, qPrintable(QString::number(ms)));
    }
    void undoRedoBulk() {
        Document d;
        CommandStack stack(d);
        QElapsedTimer t; t.start();
        auto c = std::make_unique<EntityCommand>("bulk");
        for (int i = 0; i < 20000; ++i) c->add(makeCircle({double(i), 0}, 1));
        stack.push(std::move(c));
        stack.undo();
        QCOMPARE(d.entityCount(), size_t(0));
        stack.redo();
        QCOMPARE(d.entityCount(), size_t(20000));
        qint64 ms = t.elapsed();
        qInfo("20 000 add + undo + redo: %lld ms", ms);
        QVERIFY2(ms < kUndoBudgetMs, qPrintable(QString::number(ms)));
    }
    void saveLoad50k() {
        Document d; fill(d, 50000);
        QElapsedTimer t; t.start();
        QByteArray bytes = PsiFile::write(d, ProjectMeta{}, {});
        qint64 w = t.elapsed();
        Document d2; ProjectMeta m; QMap<QString, QByteArray> f; QString err;
        t.restart();
        QVERIFY2(PsiFile::read(bytes, d2, m, f, &err), qPrintable(err));
        qint64 r = t.elapsed();
        qInfo("psi write 50 000: %lld ms (%lld KiB), read: %lld ms", w, qlonglong(bytes.size() / 1024), r);
        QCOMPARE(d2.entityCount(), size_t(50000));
        QVERIFY(w < kSaveBudgetMs && r < kSaveBudgetMs);
    }
};

QTEST_MAIN(TestPerf)
#include "test_perf.moc"
