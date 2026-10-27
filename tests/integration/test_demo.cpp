// Demo scenario (ТЗ §22): build the room plan, dimension it, place it on an A3 sheet, save / load, export DXF and PDF.
#include "io/dxf/dxf.h"
#include "io/pdf/pdf_export.h"
#include "modules/draft/demo.h"
#include "modules/draft/tool.h"
#include "rendering/viewport.h"
#include <QTemporaryDir>
#include <QtTest>

using namespace psi;

class TestDemo : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void scenario() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Project p;
        Demo::build(p);
        Document& d = p.document();

        // 1. the plan exists: walls, door arc, window, hatch, texts, dimensions on their layers
        QVERIFY(d.entityCount() >= 25);
        int dims = 0, arcs = 0, hatches = 0, texts = 0, leaders = 0;
        for (quint64 id : d.entityIds()) { const Entity* e = d.entity(id); switch (e->type) { case EntityType::Dimension: ++dims; break; case EntityType::Arc: ++arcs; break; case EntityType::Hatch: ++hatches; break; case EntityType::Text: ++texts; break; case EntityType::Leader: ++leaders; break; default: break; } }
        QVERIFY(dims >= 5); QCOMPARE(arcs, 1); QCOMPARE(hatches, 2); QVERIFY(texts >= 2); QCOMPARE(leaders, 1);
        QCOMPARE(d.layers().size(), size_t(6));
        // the horizontal dimension measures the room width
        bool width = false;
        for (quint64 id : d.entityIds()) { const Entity* e = d.entity(id); if (e->type == EntityType::Dimension && e->dimKind == DimKind::Horizontal && std::abs(dimensionValue(*e, d.lookup()) - Demo::kRoomWidth) < 1e-6) width = true; }
        QVERIFY(width);
        // the radius dimension follows the arc when the arc changes (associativity)
        quint64 arcId = 0, rdim = 0;
        for (quint64 id : d.entityIds()) { const Entity* e = d.entity(id); if (e->type == EntityType::Arc) arcId = id; if (e->type == EntityType::Dimension && e->dimKind == DimKind::Radius) rdim = id; }
        QVERIFY(arcId && rdim);
        QVERIFY(std::abs(dimensionValue(*d.entity(rdim), d.lookup()) - 900) < 1e-6);
        Entity arc = *d.entity(arcId); arc.radius = 1000;
        auto c = std::make_unique<EntityCommand>("r"); c->modify(*d.entity(arcId), arc); p.commands().push(std::move(c));
        QVERIFY(std::abs(dimensionValue(*d.entity(rdim), d.lookup()) - 1000) < 1e-6);
        p.commands().undo();
        QVERIFY(std::abs(dimensionValue(*d.entity(rdim), d.lookup()) - 900) < 1e-6);

        // 2. user draws a partition with the Line tool via keyboard input, then moves it
        ToolContext ctx; ctx.project = &p; QSet<quint64> sel; ctx.selection = &sel;
        size_t before = d.entityCount();
        auto line = createTool("line", ctx); line->begin();
        QVERIFY(line->input("3000,200"));
        QVERIFY(line->input("@0,3600"));
        line->enter();
        QCOMPARE(d.entityCount(), before + 1);
        quint64 partition = d.entityIds().back();
        sel = {partition};
        auto move = createTool("move", ctx); move->begin();
        move->click({0, 0}, false); move->click({500, 0}, false);
        QVERIFY(std::abs(d.entity(partition)->pts[0].x - 3500) < 1e-9);
        // offset it to make a 100 mm wall
        sel.clear();
        auto offset = createTool("offset", ctx); offset->begin();
        QVERIFY(offset->input("100"));
        offset->click({3500, 2000}, false);
        offset->click({3600, 2000}, false);
        QCOMPARE(d.entityCount(), before + 2);
        QVERIFY(std::abs(d.entityIds().size() ? d.entity(d.entityIds().back())->pts[0].x - 3600 : 1) < 1e-9);
        // aligned dimension of the partition
        auto dim = createTool("dim", ctx); dim->begin();
        dim->click({3500, 200}, false); dim->click({3500, 3800}, false); dim->click({3300, 2000}, false);
        QVERIFY(std::abs(dimensionValue(*d.entity(d.entityIds().back()), d.lookup()) - 3600) < 1e-6);

        // 3. sheet: A3 landscape 1:50, content fits, title block filled
        QCOMPARE(d.sheets().size(), size_t(1));
        const Sheet& s = d.sheets()[0];
        QVERIFY(s.landscape); QCOMPARE(s.pageWidth(), 420.0); QCOMPARE(s.pageHeight(), 297.0);
        QCOMPARE(s.viewports.size(), size_t(1));
        QCOMPARE(scaleLabel(s.viewports[0].scale), QStringLiteral("1:50"));
        QVERIFY2(PdfExport::checkClipping(d, s).isEmpty(), qPrintable(PdfExport::checkClipping(d, s).join("; ")));
        QCOMPARE(s.title.fields["scale"].display(), QStringLiteral("1:50"));
        QCOMPARE(s.title.fields["name"].display(), QStringLiteral("План комнаты"));

        // 4. save → reopen → identical
        QString path = dir.filePath("demo.psi"); QString err; QStringList warnings;
        QVERIFY2(p.saveAs(path, &err), qPrintable(err));
        Project q;
        QVERIFY2(q.open(path, &err, &warnings), qPrintable(err));
        QCOMPARE(q.document().entityCount(), d.entityCount());
        for (quint64 id : d.entityIds()) QVERIFY(*d.entity(id) == *q.document().entity(id));
        QCOMPARE(q.document().sheets()[0].viewports[0].scale, s.viewports[0].scale);
        QCOMPARE(q.meta().name, p.meta().name);

        // 5. DXF export and re-import into a fresh document keeps the walls
        QByteArray dxf = Dxf::write(d);
        std::vector<Entity> es; std::vector<Layer> ls; DxfReport rep;
        QVERIFY2(Dxf::parse(dxf, &es, &ls, &rep, &err), qPrintable(err));
        Project r; r.newProject();
        QVERIFY(Dxf::import(r.document(), es, ls) > 0);
        Rect ext = r.document().extents();
        QVERIFY(ext.width() >= Demo::kRoomWidth - 1e-6);
        bool wallsLayer = false; for (const Layer& l : r.document().layers()) if (l.name == QStringLiteral("Стены")) wallsLayer = true;
        QVERIFY(wallsLayer);

        // 6. PDF with the exact A3 landscape page
        QString pdf = dir.filePath("demo.pdf");
        QVERIFY2(PdfExport::exportSheets(d, {s.id}, pdf, &err, &warnings), qPrintable(err));
        QFile f(pdf); QVERIFY(f.open(QIODevice::ReadOnly)); QByteArray bytes = f.readAll();
        QVERIFY(bytes.contains("/MediaBox [0 0 1190.55"));
        QVERIFY(bytes.size() > 2000);

        // 7. the viewport renders the whole plan
        Viewport v(d); v.zoomExtents();
        QImage img = v.renderToImage(QSize(1200, 800));
        QVERIFY(!img.isNull());
        int inked = 0; QRgb bg = img.pixel(1, 1);
        for (int y = 0; y < img.height(); y += 3) for (int x = 0; x < img.width(); x += 3) if (img.pixel(x, y) != bg) ++inked;
        QVERIFY(inked > 300);
    }
};

QTEST_MAIN(TestDemo)
#include "test_demo.moc"
