// Offscreen UI tests: tools through the viewport, snapping, selection, icons, theme.
#include "modules/draft/snap.h"
#include "modules/draft/tool.h"
#include "rendering/viewport.h"
#include "modules/draft/edit.h"
#include "ui/icons.h"
#include "ui/inline_editor.h"
#include "ui/theme.h"
#include <QApplication>
#include <QLineEdit>
#include <QSignalSpy>
#include <QtTest>

using namespace psi;

class TestUi : public QObject {
    Q_OBJECT
    Project project_;
    Viewport* view_ = nullptr;
    QString prompt_;

    ToolContext ctx() {
        ToolContext c; c.project = &project_; c.view = view_; c.selection = &view_->selection();
        c.setPrompt = [this](const QString& p) { prompt_ = p; };
        c.finished = [this] { view_->setTool(createTool("select", ctx())); };
        c.askText = [](const QString&, const QString&, const QString&) -> std::optional<QString> { return QStringLiteral("Текст из теста"); };
        return c;
    }
    void start(const QString& id) { view_->setTool(createTool(id, ctx())); }
    QPointF px(Point w) { return view_->toScreen(w); }
    void click(Point w, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QPoint p = px(w).toPoint();
        QTest::mouseMove(view_, p);
        QTest::mouseClick(view_, Qt::LeftButton, mods, p);
    }

private Q_SLOTS:
    void initTestCase() {
        Theme::apply(*qApp, ThemeMode::Light, Theme::defaultAccent());
        view_ = new Viewport(project_.document());
        view_->resize(1000, 700);
        view_->show();
        QVERIFY(QTest::qWaitForWindowExposed(view_));
    }
    void init() {
        project_.newProject();
        view_->clearSelection();
        view_->setView(Point{100, 100}, 3.0);
        start("select");
    }
    void themeAndIcons() {
        QVERIFY(!Theme::isDark());
        QCOMPARE(Theme::current().accent, Theme::defaultAccent());
        QVERIFY(!Theme::serifFamily().isEmpty());
        QVERIFY(!qApp->styleSheet().isEmpty());
        for (const QString& n : Icons::names()) {
            QPixmap pm = Icons::pixmap(n, Qt::black, 20);
            QVERIFY2(!pm.isNull(), qPrintable(n));
            QImage img = pm.toImage(); bool ink = false;
            for (int y = 0; y < img.height() && !ink; ++y) for (int x = 0; x < img.width(); ++x) if (qAlpha(img.pixel(x, y)) > 0) { ink = true; break; }
            QVERIFY2(ink, qPrintable(n));
        }
        QVERIFY(!Icons::logo(32, Qt::white, Qt::black).isNull());
        Palette dark = Theme::palette(true, Theme::defaultAccent());
        QVERIFY(dark.bg.lightness() < dark.text.lightness());
        for (const QString& id : toolIds()) QVERIFY(Icons::names().contains(id) || id.startsWith("dim") || id == "polyline" || id == "spline");
    }
    void drawLineWithMouseAndSnap() {
        project_.document().addEntity(makeLine({0, 0}, {100, 0}));
        start("line");
        QVERIFY(prompt_.contains(QStringLiteral("первую")));
        click({50.4, 0.6});    // snaps to the midpoint (50,0)
        QVERIFY(view_->tool()->lastPoint());
        QVERIFY(std::abs(view_->tool()->lastPoint()->x - 50) < 1e-6);
        QVERIFY(std::abs(view_->tool()->lastPoint()->y) < 1e-6);
        click({51, 40}, Qt::ShiftModifier);   // Shift = ortho → (50, 40)
        QCOMPARE(project_.document().entityCount(), size_t(2));
        const Entity* l = project_.document().entity(project_.document().entityIds().back());
        QVERIFY(std::abs(l->pts[1].x - 50) < 1e-6 && std::abs(l->pts[1].y - 40) < 1e-6);
        QTest::keyClick(view_, Qt::Key_Escape);
        QTest::keyClick(view_, Qt::Key_Escape);
        QCOMPARE(view_->toolId(), QStringLiteral("select"));
    }
    void snapEngineKinds() {
        Document& d = project_.document();
        d.addEntity(makeCircle({150, 50}, 10));
        d.addEntity(makeLine({0, 0}, {100, 100}));
        d.addEntity(makeLine({0, 100}, {100, 0}));
        SnapResult s = SnapEngine::snap(d, {160.3, 50.2}, 2.0);
        QVERIFY(s.valid); QCOMPARE(int(s.kind), int(SnapKind::Quadrant));
        s = SnapEngine::snap(d, {150.4, 49.7}, 2.0);
        QVERIFY(s.valid); QCOMPARE(int(s.kind), int(SnapKind::Center));
        s = SnapEngine::snap(d, {0.5, 0.2}, 2.0);
        QVERIFY(s.valid); QCOMPARE(int(s.kind), int(SnapKind::Endpoint));
        // the two diagonals cross at (50,50), which is also both midpoints: intersection or midpoint, both exact
        s = SnapEngine::snap(d, {50.3, 49.8}, 2.0);
        QVERIFY(s.valid); QVERIFY(std::abs(s.p.x - 50) < 1e-6 && std::abs(s.p.y - 50) < 1e-6);
        QVERIFY(s.kind == SnapKind::Intersection || s.kind == SnapKind::Midpoint);
        // perpendicular from a base point (40,0) onto y = x → foot (20,20)
        s = SnapEngine::snap(d, {20.2, 19.9}, 2.0, Point{40, 0});
        QVERIFY(s.valid); QCOMPARE(int(s.kind), int(SnapKind::Perpendicular));
        QCOMPARE(int(SnapEngine::snap(d, {500, 500}, 2.0).kind), int(SnapKind::Grid));   // empty space: only the magnet
        QVERIFY(!SnapEngine::snap(d, {500.4, 500.4}, 2.0, std::nullopt, 0, 0.3).valid);   // outside the magnet radius
        QVERIFY(!SnapEngine::kindName(SnapKind::Midpoint).isEmpty());
        DocumentSettings st; st.ortho = true;
        Point c = SnapEngine::constrain(st, {0, 0}, {10, 3}, false);
        QVERIFY(std::abs(c.y) < 1e-9 && std::abs(c.x - 10) < 1e-9);
        st.ortho = false; st.polar = true; st.polarStep = 45;
        c = SnapEngine::constrain(st, {0, 0}, {10, 9.5}, false);
        QVERIFY(std::abs(c.x - c.y) < 1e-6);   // snapped to 45°
    }
    void selectionAndBox() {
        Document& d = project_.document();
        quint64 a = d.addEntity(makeCircle({50, 50}, 5));
        quint64 b = d.addEntity(makeCircle({150, 150}, 5));
        QSignalSpy spy(view_, &Viewport::selectionChanged);
        click({55, 50});
        QVERIFY(view_->selection().contains(a));
        QVERIFY(!view_->selection().contains(b));
        QVERIFY(spy.count() >= 1);
        click({155, 150}, Qt::ShiftModifier);
        QCOMPARE(view_->selection().size(), 2);
        click({100, 10});   // empty space → clears
        QVERIFY(view_->selection().isEmpty());
        // window selection left→right (inside only) and crossing right→left
        QTest::mousePress(view_, Qt::LeftButton, Qt::NoModifier, px({30, 30}).toPoint());
        QTest::mouseMove(view_, px({60, 60}).toPoint());
        QTest::mouseRelease(view_, Qt::LeftButton, Qt::NoModifier, px({60, 60}).toPoint());
        QCOMPARE(view_->selection().size(), 1);
        QVERIFY(view_->selection().contains(a));
        QTest::mousePress(view_, Qt::LeftButton, Qt::NoModifier, px({170, 149}).toPoint());
        QTest::mouseMove(view_, px({152, 151}).toPoint());
        QTest::mouseRelease(view_, Qt::LeftButton, Qt::NoModifier, px({152, 151}).toPoint());
        QVERIFY(view_->selection().contains(b));
        auto hits = view_->entitiesIn(Rect(0, 0, 200, 200), false);
        QCOMPARE(hits.size(), size_t(2));
        view_->selectAll();
        QCOMPARE(view_->selection().size(), 2);
        QSignalSpy del(view_, &Viewport::deleteRequested);
        QTest::keyClick(view_, Qt::Key_Delete);
        QCOMPARE(del.count(), 1);
    }
    void toolsThroughKeyboard() {
        Document& d = project_.document();
        start("rect"); view_->tool()->input("0,0"); view_->tool()->input("800x400");
        QCOMPARE(d.entityCount(), size_t(1));
        QVERIFY(std::abs(entityBBox(*d.entity(d.entityIds()[0])).width() - 800) < 1e-9);
        start("circle"); view_->tool()->input("400,200"); view_->tool()->input("50");
        QCOMPARE(d.entityIds().size(), size_t(2));
        QVERIFY(std::abs(d.entity(d.entityIds()[1])->radius - 50) < 1e-9);
        start("circle"); view_->tool()->input("2p"); view_->tool()->input("0,0"); view_->tool()->input("100,0");
        QVERIFY(std::abs(d.entity(d.entityIds()[2])->radius - 50) < 1e-9 && std::abs(d.entity(d.entityIds()[2])->center.x - 50) < 1e-9);
        start("polyline"); view_->tool()->input("0,0"); view_->tool()->input("10,0"); view_->tool()->input("10,10"); view_->tool()->input("c");
        QVERIFY(d.entity(d.entityIds()[3])->closed);
        start("text"); view_->tool()->click({1, 1}, false);
        QCOMPARE(d.entity(d.entityIds()[4])->text, QStringLiteral("Текст из теста"));
        start("arc"); view_->tool()->input("c"); view_->tool()->input("0,0"); view_->tool()->input("10,0"); view_->tool()->input("0,10");
        QCOMPARE(d.entity(d.entityIds()[5])->type, EntityType::Arc);
        start("ellipse"); view_->tool()->input("0,0"); view_->tool()->input("20,0"); view_->tool()->input("5");
        QCOMPARE(d.entity(d.entityIds()[6])->type, EntityType::Ellipse);
        QVERIFY(std::abs(d.entity(d.entityIds()[6])->ry - 5) < 1e-9);
        start("hatch"); view_->tool()->input("x"); view_->tool()->click({400 + 50, 200}, false);   // on the circle
        QCOMPARE(d.entity(d.entityIds()[7])->type, EntityType::Hatch);
        QCOMPARE(int(d.entity(d.entityIds()[7])->hatch), int(HatchPattern::Cross));
        // modify tools with a selection
        view_->setSelection({d.entityIds()[1]});
        start("copy"); view_->tool()->input("0,0"); view_->tool()->input("@100,0"); view_->tool()->enter();
        QCOMPARE(d.entityIds().size(), size_t(9));
        QVERIFY(std::abs(d.entity(d.entityIds()[8])->center.x - 500) < 1e-9);
        view_->setSelection({d.entityIds()[8]});
        start("rotate"); view_->tool()->input("400,200"); view_->tool()->input("90");
        QVERIFY(std::abs(d.entity(d.entityIds()[8])->center.y - 300) < 1e-6);
        view_->setSelection({d.entityIds()[8]});
        start("scale"); view_->tool()->input("400,300"); view_->tool()->input("2");
        QVERIFY(std::abs(d.entity(d.entityIds()[8])->radius - 100) < 1e-6);
        view_->setSelection({d.entityIds()[8]});
        start("mirror"); view_->tool()->input("0,0"); view_->tool()->input("0,100");
        QCOMPARE(d.entityIds().size(), size_t(10));
        QVERIFY(std::abs(d.entity(d.entityIds()[9])->center.x + 400) < 1e-6);
        view_->setSelection({d.entityIds()[9]});
        start("array"); view_->tool()->input("2,3,500,500");
        QCOMPARE(d.entityIds().size(), size_t(15));
        view_->setSelection({d.entityIds()[0]});
        start("explode");
        QVERIFY(d.entityIds().size() >= 18);   // rectangle → 4 lines
        // undo everything
        while (project_.commands().canUndo()) project_.commands().undo();
        QCOMPARE(d.entityCount(), size_t(0));
    }
    void trimExtendFilletViaTools() {
        Document& d = project_.document();
        quint64 h = d.addEntity(makeLine({0, 0}, {100, 0}));
        quint64 v = d.addEntity(makeLine({50, -50}, {50, 50}));
        view_->setSelection({v});
        start("trim"); view_->tool()->selectionDone();
        view_->tool()->click({80, 0}, false);
        QVERIFY(!d.entity(h));
        const Entity* left = d.entity(d.entityIds().back());
        QVERIFY(std::abs(left->pts[1].x - 50) < 1e-6 || std::abs(left->pts[0].x - 50) < 1e-6);
        view_->tool()->enter();
        // fillet two perpendicular lines
        project_.newProject();
        quint64 a = d.addEntity(makeLine({0, 0}, {100, 0}));
        quint64 b = d.addEntity(makeLine({100, 0}, {100, 100}));
        start("fillet"); view_->tool()->input("10");
        view_->tool()->click({20, 0}, false); view_->tool()->click({100, 80}, false);
        QCOMPARE(d.entityCount(), size_t(3));
        QVERIFY(std::abs(d.entity(a)->pts[1].x - 90) < 1e-6);
        QVERIFY(std::abs(d.entity(b)->pts[1].y - 10) < 1e-6);   // the corner end is trimmed
        // extend a short line to a boundary
        quint64 s = d.addEntity(makeLine({0, 50}, {40, 50}));
        view_->setSelection({b});
        start("extend"); view_->tool()->selectionDone();
        view_->tool()->click({35, 50}, false);
        QVERIFY(std::abs(d.entity(s)->pts[1].x - 100) < 1e-6);
    }
    void gridMagnet() {
        Document& d = project_.document();
        DocumentSettings st = d.settings(); st.snapStep = 10; st.gridSnap = true; d.setSettings(st);
        view_->setView(Point{100, 100}, 1.0);   // 8 px magnet radius = 8 mm
        QSignalSpy spy(view_, &Viewport::cursorMoved);
        start("line");
        click({103.4, 97.2});
        QVERIFY(spy.count() >= 1);
        Point moved = spy.last().at(0).value<Point>();
        QVERIFY(std::abs(moved.x - 100) < 1e-9 && std::abs(moved.y - 100) < 1e-9);
        QCOMPARE(view_->snapLabel(), QStringLiteral("Сетка"));
        click({203.1, 100.4});
        QCOMPARE(d.entityCount(), size_t(1));
        const Entity* l = d.entity(d.entityIds().front());
        QVERIFY(std::abs(l->pts[0].x - 100) < 1e-9 && std::abs(l->pts[0].y - 100) < 1e-9);
        QVERIFY(std::abs(l->pts[1].x - 200) < 1e-9 && std::abs(l->pts[1].y - 100) < 1e-9);
        // object snap wins over the magnet
        view_->tool()->escape(); start("select");
        d.addEntity(makeCircle({50.3, 50.3}, 20));
        SnapResult s = SnapEngine::snap(d, {50.5, 50.0}, 3.0, std::nullopt, 0, 8.0);
        QCOMPARE(int(s.kind), int(SnapKind::Center));
        // outside the magnet radius nothing snaps
        QVERIFY(!SnapEngine::snap(d, {155, 155}, 3.0, std::nullopt, 0, 2.0).valid);
        st.gridSnap = false; d.setSettings(st);
        QVERIFY(!SnapEngine::snap(d, {300.2, 300.2}, 3.0, std::nullopt, 0, 8.0).valid);
    }
    void doubleClickEditsGeometry() {
        Document& d = project_.document();
        quint64 rid = d.addEntity(makeRectangle({0, 0}, {800, 500}));
        quint64 did = d.addEntity(makeLinearDim({0, 0}, {800, 0}, {400, -60}, DimKind::Horizontal));
        view_->zoomExtents();
        // double-click on the dimension line asks for an editor
        QSignalSpy spy(view_, &Viewport::editRequested);
        QPoint at = px({400, -60}).toPoint();
        QTest::mouseMove(view_, at);
        QTest::mouseDClick(view_, Qt::LeftButton, Qt::NoModifier, at);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last().at(0).value<quint64>(), did);
        // fields and the inline editor: 800 → 1000 through Enter
        std::vector<EditField> f = editFields(d, *d.entity(did));
        QCOMPARE(f.size(), size_t(1));
        QCOMPARE(f[0].value, 800.0);
        auto* ed = new InlineEditor(f, d.settings().unit, d.settings().precision, view_);
        QMap<QString, double> got; bool applied = false;
        connect(ed, &InlineEditor::applied, this, [&](const QMap<QString, double>& v, const QString&) { got = v; applied = true; });
        ed->popup(px({400, -60}));
        QVERIFY(ed->isVisible());
        ed->setFieldText("value", "1000");
        QTest::keyClick(static_cast<QWidget*>(ed->field("value")), Qt::Key_Return);
        QVERIFY(applied);
        QCOMPARE(got.value("value"), 1000.0);
        auto cmd = applyEdit(d, *d.entity(did), got);
        QVERIFY(cmd);
        project_.commands().push(std::move(cmd));
        const Entity* rect = d.entity(rid);
        QVERIFY(std::abs(entityBBox(*rect).width() - 1000) < 1e-6);
        QVERIFY(std::abs(entityBBox(*rect).height() - 500) < 1e-6);
        QVERIFY(std::abs(dimensionValue(*d.entity(did), nullptr) - 1000) < 1e-6);
        project_.commands().undo();
        QVERIFY(std::abs(entityBBox(*d.entity(rid)).width() - 800) < 1e-6);
        // line: length / angle; rectangle: width × height; circle: radius / diameter; text
        quint64 lid = d.addEntity(makeLine({0, 0}, {100, 0}));
        auto lf = editFields(d, *d.entity(lid));
        QCOMPARE(lf.size(), size_t(2));
        project_.commands().push(applyEdit(d, *d.entity(lid), {{"length", 50}, {"angle", 90}}));
        QVERIFY(std::abs(d.entity(lid)->pts[1].x) < 1e-6 && std::abs(d.entity(lid)->pts[1].y - 50) < 1e-6);
        auto rf = editFields(d, *d.entity(rid));
        QCOMPARE(rf.size(), size_t(2));
        project_.commands().push(applyEdit(d, *d.entity(rid), {{"height", 300}}));
        QVERIFY(std::abs(entityBBox(*d.entity(rid)).height() - 300) < 1e-6 && std::abs(entityBBox(*d.entity(rid)).width() - 800) < 1e-6);
        quint64 cid = d.addEntity(makeCircle({0, 0}, 10));
        project_.commands().push(applyEdit(d, *d.entity(cid), {{"diameter", 50}}));
        QCOMPARE(d.entity(cid)->radius, 25.0);
        quint64 rdim = d.addEntity(makeRadialDim(cid, {0, 0}, 25, {30, 30}, false));
        project_.commands().push(applyEdit(d, *d.entity(rdim), {{"value", 40}}));
        QCOMPARE(d.entity(cid)->radius, 40.0);
        quint64 tid = d.addEntity(makeText({5, 5}, QStringLiteral("Старый"), 3.5));
        QVERIFY(editFields(d, *d.entity(tid))[0].isText);
        project_.commands().push(applyEdit(d, *d.entity(tid), {}, QStringLiteral("Новый")));
        QCOMPARE(d.entity(tid)->text, QStringLiteral("Новый"));
        QVERIFY(!applyEdit(d, *d.entity(tid), {}, QStringLiteral("Новый")));   // no change → no command
        // Esc cancels
        auto* ed2 = new InlineEditor(lf, d.settings().unit, d.settings().precision, view_);
        QSignalSpy cancel(ed2, &InlineEditor::cancelled);
        ed2->popup(px({50, 0}));
        QTest::keyClick(static_cast<QWidget*>(ed2->field("length")), Qt::Key_Escape);
        QCOMPARE(cancel.count(), 1);
    }
    void joinBooleanSplitViaTools() {
        Document& d = project_.document();
        for (auto [a, b] : std::vector<std::pair<Point, Point>>{{{0, 0}, {80, 0}}, {{80, 0}, {80, 50}}, {{80, 50}, {0, 50}}, {{0, 50}, {0, 0}}}) d.addEntity(makeLine(a, b));
        QSet<quint64> all; for (quint64 id : d.entityIds()) all.insert(id);
        view_->setSelection(all);
        start("join"); view_->tool()->selectionDone();
        QCOMPARE(d.entityCount(), size_t(1));
        const Entity* rect = d.entity(d.entityIds().front());
        QVERIFY(rect->type == EntityType::Polyline && rect->closed && rect->pts.size() == 4);
        project_.commands().undo(); QCOMPARE(d.entityCount(), size_t(4));
        project_.commands().redo(); QCOMPARE(d.entityCount(), size_t(1));
        // union with a second rectangle by two picks
        d.addEntity(makeRectangle({40, 20}, {120, 70}));
        view_->clearSelection();
        start("union");
        view_->tool()->click({0, 25}, false); view_->tool()->click({120, 45}, false);
        QCOMPARE(d.entityCount(), size_t(1));
        QCOMPARE(d.entity(d.entityIds().front())->pts.size(), size_t(8));
        // Shift keeps the originals
        d.addEntity(makeCircle({0, 0}, 20));
        start("subtract");
        view_->tool()->click({120, 45}, false); view_->tool()->click({-20, 0}, true);
        QCOMPARE(d.entityCount(), size_t(3));
        // split a line at its midpoint
        project_.newProject();
        d.addEntity(makeLine({0, 0}, {100, 0}));
        start("split"); view_->tool()->click({50, 0}, false);
        QCOMPARE(d.entityCount(), size_t(2));
        for (quint64 id : d.entityIds()) QVERIFY(std::abs(dist(d.entity(id)->pts[0], d.entity(id)->pts[1]) - 50) < 1e-6);
        // cut by object: a circle cuts the horizontal line into three
        project_.newProject();
        d.addEntity(makeLine({0, 0}, {100, 0})); d.addEntity(makeCircle({50, 0}, 10));
        start("cut"); view_->tool()->click({10, 0}, false); view_->tool()->click({50, 10}, false);
        QCOMPARE(d.entityCount(), size_t(4));
    }
    void zoomAndRender() {
        Document& d = project_.document();
        d.addEntity(makeRectangle({0, 0}, {1000, 500}));
        view_->zoomExtents();
        Rect vis = view_->visibleWorld();
        QVERIFY(vis.x0 <= 0 && vis.x1 >= 1000 && vis.y0 <= 0 && vis.y1 >= 500);
        double before = view_->pxPerMm();
        QTest::mouseMove(view_, QPoint(500, 350));
        QWheelEvent wheel(QPointF(500, 350), QPointF(500, 350), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view_, &wheel);
        QVERIFY(view_->pxPerMm() > before);
        QImage img = view_->renderToImage(QSize(400, 300), 2.0);
        QCOMPARE(img.width(), 400);
        // theme colours: canvas background, accent selection
        view_->setSelection({d.entityIds()[0]});
        QVERIFY(view_->selection().size() == 1);
        view_->repaint();
    }
};

QTEST_MAIN(TestUi)
#include "test_ui.moc"
