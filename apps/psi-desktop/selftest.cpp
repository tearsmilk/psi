#include "selftest.h"
#include "io/dxf/dxf.h"
#include "io/pdf/pdf_export.h"
#include "modules/draft/demo.h"
#include "modules/draft/tool.h"
#include "rendering/viewport.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>

namespace psi {

namespace {
QTextStream& out() { static QTextStream s(stdout); return s; }
int fails = 0;
void check(bool ok, const QString& what) {
    out() << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    out().flush();
    if (!ok) ++fails;
}
}

int runSelfTest() {
    out() << "Пси " PSI_VERSION " — самопроверка\n";
    QTemporaryDir tmp;
    check(tmp.isValid(), QStringLiteral("временный каталог"));

    Theme::apply(*qApp, ThemeMode::Light, Theme::defaultAccent());
    check(!Icons::icon("line", Qt::black).isNull(), QStringLiteral("иконки"));

    Project project;
    Demo::build(project);
    Document& doc = project.document();
    check(doc.entityCount() >= 25, QStringLiteral("демо-сценарий: %1 объектов").arg(doc.entityCount()));
    check(doc.layers().size() == 6, QStringLiteral("демо-сценарий: слои"));
    Rect ext = doc.extents();
    check(ext.width() > Demo::kRoomWidth && ext.height() > Demo::kRoomHeight, QStringLiteral("габариты чертежа"));

    // tools headless: draw a line through the tool API
    {
        ToolContext ctx; ctx.project = &project;
        QSet<quint64> sel; ctx.selection = &sel;
        auto tool = createTool("line", ctx);
        tool->begin();
        size_t before = doc.entityCount();
        tool->click({0, -2000}, false);
        check(tool->input("@1000,0"), QStringLiteral("инструмент «Линия»: ввод @1000,0"));
        tool->input("@0<90");
        check(doc.entityCount() == before + 2, QStringLiteral("инструмент «Линия»: две линии"));
        project.commands().undo(); project.commands().undo();
        check(doc.entityCount() == before, QStringLiteral("отмена"));
        check(toolIds().size() == 30, QStringLiteral("реестр инструментов"));
        for (const QString& id : toolIds()) { auto t = createTool(id, ctx); if (!t) { check(false, "createTool " + id); break; } }
    }

    // .psi round trip
    QString psiPath = tmp.filePath("demo.psi");
    QString err; QStringList warn;
    check(project.saveAs(psiPath, &err), QStringLiteral("сохранение .psi: %1").arg(err));
    {
        Project p2;
        check(p2.open(psiPath, &err, &warn), QStringLiteral("открытие .psi: %1").arg(err));
        check(p2.document().entityCount() == doc.entityCount(), QStringLiteral("число объектов после загрузки"));
        bool same = true;
        for (quint64 id : doc.entityIds()) { const Entity* a = doc.entity(id); const Entity* b = p2.document().entity(id); if (!a || !b || !(*a == *b)) { same = false; break; } }
        check(same, QStringLiteral("объекты идентичны"));
        check(p2.document().sheets().size() == 1 && p2.document().sheets()[0].landscape, QStringLiteral("лист сохранён"));
    }

    // DXF round trip
    {
        QByteArray dxf = Dxf::write(doc);
        check(dxf.contains("AC1015") && dxf.contains("$INSUNITS"), QStringLiteral("DXF: заголовок"));
        std::vector<Entity> es; std::vector<Layer> ls; DxfReport rep;
        check(Dxf::parse(dxf, &es, &ls, &rep, &err), QStringLiteral("DXF: разбор %1").arg(err));
        check(es.size() >= doc.entityCount(), QStringLiteral("DXF: %1 объектов прочитано").arg(es.size()));
        Project p3; p3.newProject();
        int n = Dxf::import(p3.document(), es, ls);
        check(n == int(es.size()), QStringLiteral("DXF: импорт"));
        check(rep.unit == Unit::Millimeter, QStringLiteral("DXF: единицы"));
    }

    // PDF export with exact page size
    {
        QString pdf = tmp.filePath("demo.pdf");
        std::vector<quint64> ids; for (const Sheet& s : doc.sheets()) ids.push_back(s.id);
        check(PdfExport::exportSheets(doc, ids, pdf, &err, &warn), QStringLiteral("PDF: экспорт %1").arg(err));
        QFile f(pdf); f.open(QIODevice::ReadOnly); QByteArray bytes = f.readAll();
        check(bytes.startsWith("%PDF"), QStringLiteral("PDF: заголовок"));
        // A3 landscape: 420 × 297 mm = 1190.55 × 841.89 pt
        bool box = bytes.contains("/MediaBox [0 0 1190.55") && bytes.contains(" 841.88");
        if (!box) { int i = bytes.indexOf("/MediaBox"); out() << "    MediaBox: " << bytes.mid(i, 40) << "\n"; }
        check(box, QStringLiteral("PDF: точный размер страницы A3"));
    }

    // Viewport render + performance
    {
        Viewport v(doc);
        QElapsedTimer t; t.start();
        v.zoomExtents();
        QImage img = v.renderToImage(QSize(1600, 1000));
        check(!img.isNull() && img.width() == 1600, QStringLiteral("рендер вида (%1 мс)").arg(t.elapsed()));
        // count non-background pixels
        int inked = 0; QRgb bg = img.pixel(2, 2);
        for (int y = 0; y < img.height(); y += 4) for (int x = 0; x < img.width(); x += 4) if (img.pixel(x, y) != bg) ++inked;
        check(inked > 500, QStringLiteral("на рендере есть геометрия"));

        Project big; big.newProject();
        Document& bd = big.document();
        for (int i = 0; i < 50000; ++i) bd.addEntity(makeLine({double(i % 250) * 10, double(i / 250) * 10}, {double(i % 250) * 10 + 8, double(i / 250) * 10 + 6}));
        Viewport bv(bd); bv.zoomExtents();
        t.restart();
        bv.renderToImage(QSize(1600, 1000));
        qint64 ms = t.elapsed();
        check(ms < 1500, QStringLiteral("50 000 линий: %1 мс").arg(ms));
    }

    out() << (fails ? QStringLiteral("Самопроверка: %1 ошибок\n").arg(fails) : QStringLiteral("Самопроверка пройдена\n"));
    out().flush();
    return fails ? 1 : 0;
}

} // namespace psi
