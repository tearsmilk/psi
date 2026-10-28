// The real main window offscreen: dock layout survives workspace switching, the tool palette scrolls.
#include "main_window.h"
#include "panels.h"
#include "ui/theme.h"
#include <QApplication>
#include <QDockWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QtTest>

using namespace psi;

class TestMainWindow : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        QCoreApplication::setOrganizationName("psi-test");
        QCoreApplication::setApplicationName("psi-test-main-window");
        QSettings().clear();
        Theme::apply(*qApp, ThemeMode::Light, Theme::defaultAccent());
    }

    void docksSurviveWorkspaceSwitch() {
        MainWindow w;
        w.resize(1440, 900);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.showPage(MainWindow::PageDraft);
        qApp->processEvents();
        auto* props = w.findChild<QDockWidget*>("dockProps");
        auto* dockTools = w.findChild<QDockWidget*>("dockTools");
        QVERIFY(props && dockTools);
        const int before = props->width();
        QVERIFY2(before >= 280, qPrintable(QString::number(before)));

        for (MainWindow::Page p : {MainWindow::PageInterior, MainWindow::PageSketch, MainWindow::PageDraft}) {
            w.showPage(p);
            qApp->processEvents();
        }
        QVERIFY(props->isVisible() && dockTools->isVisible());
        QVERIFY2(props->width() >= 280, qPrintable(QString::number(props->width())));
        QVERIFY2(dockTools->width() >= 230, qPrintable(QString::number(dockTools->width())));
        QVERIFY(w.findChild<QDockWidget*>("dockLayers")->width() >= 280);

        // other workspaces come back intact too
        w.showPage(MainWindow::PageSketch);
        qApp->processEvents();
        QVERIFY(!props->isVisible());
        w.showPage(MainWindow::PageDraft);
        qApp->processEvents();
        QVERIFY(props->isVisible() && props->width() >= 280);
    }

    void toolPaletteScrolls() {
        MainWindow w;
        w.resize(1440, 900);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.showPage(MainWindow::PageDraft);
        auto* tools = w.findChild<ToolsPanel*>();
        QVERIFY(tools);
        tools->setCompact(false);
        for (const QString& g : {QStringLiteral("Избранное"), QStringLiteral("Рисование"), QStringLiteral("Текст и размеры"),
                                 QStringLiteral("Правка"), QStringLiteral("Объединение"), QStringLiteral("Штриховка")})
            tools->setGroupCollapsed(g, false);
        // the «Инструменты» dock shares the left column with the tree → ~half of 900 px
        qApp->processEvents();
        QScrollArea* scroll = tools->scrollArea();
        QVERIFY(scroll);
        QVERIFY(scroll->widget()->sizeHint().height() > scroll->viewport()->height());
        QVERIFY2(scroll->verticalScrollBar()->maximum() > 0, qPrintable(QString::number(scroll->verticalScrollBar()->maximum())));
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        // wheel over a button scrolls the palette
        const int before = scroll->verticalScrollBar()->value();
        QWidget* target = scroll->widget();
        QWheelEvent ev(QPointF(20, 20), target->mapToGlobal(QPoint(20, 20)), QPoint(0, -120), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(scroll->viewport(), &ev);
        qApp->processEvents();
        QVERIFY(scroll->verticalScrollBar()->value() > before);
    }
};

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
