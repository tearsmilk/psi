// Offscreen UI tests: tools through the viewport, snapping, selection, icons, theme.
#include "modules/draft/snap.h"
#include "modules/draft/tool.h"
#include "rendering/viewport.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include <QApplication>
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
        QVERIFY(!SnapEngine::snap(d, {500, 500}, 2.0).valid);
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
