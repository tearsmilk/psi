#include "main_window.h"
#include "ui/inline_editor.h"
#include "modules/draft/edit.h"
#include <algorithm>
#include <QMenu>
#include "dialogs.h"
#include "io/dxf/dxf.h"
#include "io/pdf/pdf_export.h"
#include "modules/draft/demo.h"
#include "panels.h"
#include "rendering/viewport.h"
#include "sheets_page.h"
#include "sketch_page.h"
#include "interior_page.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "welcome.h"
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <atomic>
#include <memory>

namespace psi {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Пси"));
    resize(1440, 900);
    setAcceptDrops(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);
    setUnifiedTitleAndToolBarOnMac(true);

    stack_ = new QStackedWidget;
    welcome_ = new WelcomePage;
    draftPage_ = new QWidget;
    {
        auto* v = new QVBoxLayout(draftPage_); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
        view_ = new Viewport(project_.document());
        v->addWidget(view_, 1);
        auto* bar = new QWidget; bar->setObjectName("Card");
        auto* h = new QHBoxLayout(bar); h->setContentsMargins(10, 4, 10, 4); h->setSpacing(8);
        prompt_ = new QLabel(QStringLiteral("Выберите инструмент")); prompt_->setObjectName("Hint");
        prompt_->setMinimumWidth(260);
        h->addWidget(prompt_);
        command_ = new QLineEdit;
        command_->setPlaceholderText(QStringLiteral("Команда или координаты: 100,50 · @40<30 · 250 · l / c / r …"));
        command_->setClearButtonEnabled(true);
        h->addWidget(command_, 1);
        v->addWidget(bar);
    }
    sheets_ = new SheetsPage(project_);
    sketch_ = new SketchPage(project_);
    interior_ = new InteriorPage(project_);
    stack_->addWidget(welcome_);
    stack_->addWidget(draftPage_);
    stack_->addWidget(sheets_);
    stack_->addWidget(sketch_);
    stack_->addWidget(interior_);
    setCentralWidget(stack_);

    buildActions();
    buildMenus();
    buildWorkspaceBar();
    buildDocks();
    buildStatusBar();
    applyHotkeys();

    view_->installEventFilter(this);
    QSettings st;
    view_->setCrosshair(st.value("ui/crosshair", true).toBool());
    project_.setAutosaveMinutes(st.value("autosave/minutes", 5).toInt());

    connect(welcome_, &WelcomePage::newRequested, this, &MainWindow::newProject);
    connect(welcome_, &WelcomePage::openRequested, this, [this](const QString& p) { if (p.isEmpty()) openProject(); else openPath(p); });
    connect(welcome_, &WelcomePage::importDxfRequested, this, [this] { importDxf(QString()); });
    connect(welcome_, &WelcomePage::demoRequested, this, &MainWindow::loadDemo);
    connect(sheets_, &SheetsPage::exportPdfRequested, this, &MainWindow::exportPdf);
    // рабочие пространства «Эскиз» и «Интерьер»: своя отмена, выдача в чертёж, статус
    connect(sketch_, &SketchPage::undoChanged, this, &MainWindow::updateUndoRedo);
    connect(interior_, &InteriorPage::undoChanged, this, &MainWindow::updateUndoRedo);
    connect(sketch_, &SketchPage::statusMessage, this, [this](const QString& t) { statusBar()->showMessage(t, 5000); });
    connect(interior_, &InteriorPage::statusMessage, this, [this](const QString& t) { statusBar()->showMessage(t, 5000); });
    connect(sketch_, &SketchPage::exported, this, [this](int) { showPage(PageDraft); view_->zoomExtents(); });
    connect(interior_, &InteriorPage::exported, this, [this](int) { showPage(PageDraft); view_->zoomExtents(); });
    connect(sketch_, &SketchPage::dofChanged, this, [this](int dof) { dofLbl_->setText(dof >= 0 ? QStringLiteral("Свободных степеней: %1").arg(dof) : QString()); dofLbl_->setVisible(stack_->currentIndex() == PageSketch); });
    connect(sketch_, &SketchPage::sketchesChanged, this, [this] { if (tree_) tree_->refresh(); });
    connect(interior_, &InteriorPage::buildingChanged, this, [this] { if (tree_) tree_->refresh(); });
    connect(command_, &QLineEdit::returnPressed, this, &MainWindow::runCommandLine);
    connect(view_, &Viewport::cursorMoved, this, [this](Point p) { lastCursor_ = p; updateStatus(); });
    command_->installEventFilter(this);
    connect(view_, &Viewport::editRequested, this, &MainWindow::openInlineEditor);
    connect(command_, &QLineEdit::textEdited, this, [this] { tabIdx_ = -1; });
    connect(view_, &Viewport::promptChanged, this, [this](const QString& t) { prompt_->setText(t.isEmpty() ? QStringLiteral("Выберите инструмент") : t); });
    connect(view_, &Viewport::toolChanged, this, [this] {
        QString id = view_->toolId();
        if (id.isEmpty() || (view_->tool() && !view_->tool()->keepAfterFinish() && id != "select")) { /* ended */ }
        tools_->setActive(id);
        toolLbl_->setText(id.isEmpty() ? QString() : toolTitle(id));
    });
    connect(view_, &Viewport::deleteRequested, this, &MainWindow::deleteSelection);
    connect(view_, &Viewport::selectionChanged, this, &MainWindow::updateStatus);
    connect(&project_.commands(), &CommandStack::changed, this, [this] { updateUndoRedo(); updateTitle(); });
    connect(&project_, &Project::stateChanged, this, &MainWindow::updateTitle);
    connect(&project_, &Project::autosaved, this, [this](const QString& p) { statusBar()->showMessage(QStringLiteral("Автосохранение: %1").arg(QFileInfo(p).fileName()), 4000); });
    connect(&project_.document(), &Document::settingsChanged, this, &MainWindow::updateStatus);
    connect(&project_.document(), &Document::documentReset, this, [this] { view_->clearSelection(); view_->zoomExtents(); });

    showPage(PageWelcome);
    updateTitle(); updateUndoRedo(); updateStatus();
}

// ---------------------------------------------------------------- actions / menus
QAction* MainWindow::action(const QString& id, const QString& text, const QString& icon, const QKeySequence& key, const std::function<void()>& fn) {
    auto* a = new QAction(text, this);
    a->setObjectName(id);
    if (!icon.isEmpty()) a->setIcon(Icons::icon(icon, Theme::current().text));
    a->setShortcut(key);
    a->setShortcutContext(Qt::WindowShortcut);
    connect(a, &QAction::triggered, this, [fn] { fn(); });
    actions_[id] = a;
    addAction(a);
    return a;
}

void MainWindow::buildActions() {
    action("new", QStringLiteral("Новый проект"), "new", QKeySequence::New, [this] { newProject(); });
    action("open", QStringLiteral("Открыть…"), "open", QKeySequence::Open, [this] { openProject(); });
    action("save", QStringLiteral("Сохранить"), "save", QKeySequence::Save, [this] { saveProject(); });
    action("saveAs", QStringLiteral("Сохранить как…"), "", QKeySequence::SaveAs, [this] { saveProjectAs(); });
    action("importDxf", QStringLiteral("Импорт DXF…"), "", QKeySequence("Ctrl+I"), [this] { importDxf(QString()); });
    action("exportDxf", QStringLiteral("Экспорт DXF…"), "", QKeySequence("Ctrl+Shift+D"), [this] { exportDxf(); });
    action("exportPdf", QStringLiteral("Экспорт PDF…"), "pdf", QKeySequence("Ctrl+P"), [this] { std::vector<quint64> ids; for (const Sheet& s : project_.document().sheets()) ids.push_back(s.id); exportPdf(ids); });
    action("demo", QStringLiteral("Открыть демонстрацию"), "", QKeySequence(), [this] { loadDemo(); });
    action("quit", QStringLiteral("Выход"), "", QKeySequence::Quit, [this] { close(); });

    action("undo", QStringLiteral("Отменить"), "undo", QKeySequence::Undo, [this] {
        if (stack_->currentIndex() == PageSketch) sketch_->undo();
        else if (stack_->currentIndex() == PageInterior) interior_->undo();
        else project_.commands().undo();
        view_->invalidate();
    });
    action("redo", QStringLiteral("Повторить"), "redo", QKeySequence("Ctrl+Shift+Z"), [this] {
        if (stack_->currentIndex() == PageSketch) sketch_->redo();
        else if (stack_->currentIndex() == PageInterior) interior_->redo();
        else project_.commands().redo();
        view_->invalidate();
    });
    action("delete", QStringLiteral("Удалить"), "delete", QKeySequence::Delete, [this] { deleteSelection(); });
    action("selectAll", QStringLiteral("Выделить всё"), "", QKeySequence::SelectAll, [this] { view_->selectAll(); });
    action("deselect", QStringLiteral("Снять выделение"), "", QKeySequence("Ctrl+Shift+A"), [this] { view_->clearSelection(); });
    action("docSettings", QStringLiteral("Параметры документа…"), "settings", QKeySequence("Ctrl+Alt+D"), [this] { editDocumentSettings(); });
    action("appSettings", QStringLiteral("Настройки…"), "settings", QKeySequence::Preferences, [this] { editAppSettings(); });

    action("zoomExtents", QStringLiteral("Показать всё"), "zoom-extents", QKeySequence("Ctrl+Shift+E"), [this] { view_->zoomExtents(); });
    action("zoomWindow", QStringLiteral("Окно масштабирования"), "zoom-window", QKeySequence("Z"), [this] { view_->startZoomWindow(); });
    action("zoomIn", QStringLiteral("Увеличить"), "plus", QKeySequence::ZoomIn, [this] { view_->zoomBy(1.25); });
    action("zoomOut", QStringLiteral("Уменьшить"), "minus", QKeySequence::ZoomOut, [this] { view_->zoomBy(0.8); });
    action("grid", QStringLiteral("Сетка"), "grid", QKeySequence("F7"), [this] { toggleSetting([](DocumentSettings& s) { s.gridVisible = !s.gridVisible; }, QStringLiteral("Сетка")); });
    action("snap", QStringLiteral("Привязка"), "snap", QKeySequence("F3"), [this] { toggleSetting([](DocumentSettings& s) { s.snapEnabled = !s.snapEnabled; }, QStringLiteral("Привязка")); });
    action("ortho", QStringLiteral("Орто"), "ortho", QKeySequence("F8"), [this] { toggleSetting([](DocumentSettings& s) { s.ortho = !s.ortho; }, QStringLiteral("Орто")); });
    action("polar", QStringLiteral("Полярное отслеживание"), "polar", QKeySequence("F10"), [this] { toggleSetting([](DocumentSettings& s) { s.polar = !s.polar; }, QStringLiteral("Полярное отслеживание")); });
    action("gridSnap", QStringLiteral("Магнит к сетке"), "magnet", QKeySequence("F9"), [this] { toggleSetting([](DocumentSettings& s) { s.gridSnap = !s.gridSnap; }, QStringLiteral("Магнит к сетке")); });
    for (const char* id : {"grid", "snap", "gridSnap", "ortho", "polar"}) actions_[id]->setCheckable(true);

    const QMap<QString, QKeySequence> toolKeys{{"select", QKeySequence("Escape")}, {"line", QKeySequence("L")}, {"polyline", QKeySequence("P")}, {"rect", QKeySequence("R")},
                                               {"circle", QKeySequence("C")}, {"arc", QKeySequence("A")}, {"ellipse", QKeySequence("E")}, {"spline", QKeySequence("S")},
                                               {"text", QKeySequence("T")}, {"leader", QKeySequence("Shift+L")}, {"dim", QKeySequence("D")}, {"dim-h", QKeySequence("Shift+H")},
                                               {"dim-v", QKeySequence("Shift+V")}, {"dim-radius", QKeySequence("Shift+R")}, {"dim-diameter", QKeySequence("Shift+D")},
                                               {"dim-angle", QKeySequence("Shift+A")}, {"hatch", QKeySequence("H")}, {"move", QKeySequence("M")}, {"copy", QKeySequence("Shift+C")},
                                               {"rotate", QKeySequence("Shift+O")}, {"mirror", QKeySequence("Shift+M")}, {"scale", QKeySequence("Shift+S")}, {"offset", QKeySequence("O")},
                                               {"trim", QKeySequence("Shift+T")}, {"extend", QKeySequence("Shift+E")}, {"fillet", QKeySequence("F")}, {"chamfer", QKeySequence("Shift+F")},
                                               {"array", QKeySequence("Shift+Y")}, {"explode", QKeySequence("X")}, {"delete", QKeySequence()},
                                               {"join", QKeySequence("J")}, {"union", QKeySequence("Shift+U")}, {"subtract", QKeySequence("Shift+B")}, {"intersect", QKeySequence("Shift+I")},
                                               {"split", QKeySequence("Shift+X")}, {"cut", QKeySequence("Shift+K")}};
    for (const QString& id : toolIds()) {
        if (id == "delete") continue;
        QString tid = id;
        action("tool." + id, toolTitle(id), id.startsWith("dim") ? "dim" : id, toolKeys.value(id), [this, tid] { startTool(tid); });
    }
    actions_["tool.select"]->setShortcut(QKeySequence());   // Esc is handled by the viewport

    action("ws.draft", QStringLiteral("Черчение"), "draft", QKeySequence("Ctrl+1"), [this] { showPage(PageDraft); });
    action("ws.sheets", QStringLiteral("Листы"), "sheet", QKeySequence("Ctrl+2"), [this] { showPage(PageSheets); });
    action("ws.sketch", QStringLiteral("Эскиз"), "sketch", QKeySequence("Ctrl+3"), [this] { showPage(PageSketch); });
    action("ws.interior", QStringLiteral("Интерьер"), "interior", QKeySequence("Ctrl+4"), [this] { showPage(PageInterior); });
    action("ws.welcome", QStringLiteral("Начальный экран"), "", QKeySequence("Ctrl+0"), [this] { showPage(PageWelcome); });
    action("help", QStringLiteral("Справка по вводу"), "", QKeySequence::HelpContents, [this] {
        QMessageBox::information(this, QStringLiteral("Ввод координат"),
            QStringLiteral("<b>Точки</b>: «100,50» — абсолютные; «@40,10» — относительно последней; «@80<30» — длина и угол; «250» — длина вдоль курсора.<br>"
                           "<b>Единицы</b>: «80 cm», «2 m», «3'» (футы), «1 1/2 in». По умолчанию — единицы документа.<br>"
                           "<b>Инструменты</b>: буквы L, P, R, C, A, E, S, T, D, H, M, O, F, X; Enter / правая кнопка — завершить шаг; Esc — отмена.<br>"
                           "<b>Привязки</b>: конец, середина, центр, квадрант, пересечение, перпендикуляр, касательная, сетка (F3). Shift — орто."));
    });
    action("about", QStringLiteral("О программе"), "", QKeySequence(), [this] { showAbout(this); });
}

void MainWindow::buildMenus() {
    QMenuBar* mb = menuBar();
    auto add = [&](QMenu* m, std::initializer_list<const char*> ids) { for (const char* id : ids) { if (QString(id) == "-") m->addSeparator(); else m->addAction(actions_[id]); } };
    QMenu* file = mb->addMenu(QStringLiteral("Файл"));
    add(file, {"new", "open", "-", "save", "saveAs", "-", "importDxf", "exportDxf", "exportPdf", "-", "demo", "-", "quit"});
    QMenu* edit = mb->addMenu(QStringLiteral("Правка"));
    add(edit, {"undo", "redo", "-", "delete", "selectAll", "deselect", "-", "docSettings", "appSettings"});
    QMenu* viewM = mb->addMenu(QStringLiteral("Вид"));
    add(viewM, {"zoomExtents", "zoomWindow", "zoomIn", "zoomOut", "-", "grid", "snap", "gridSnap", "ortho", "polar"});
    QMenu* create = mb->addMenu(QStringLiteral("Создать"));
    for (const char* id : {"line", "polyline", "rect", "circle", "arc", "ellipse", "spline"}) create->addAction(actions_[QString("tool.") + id]);
    create->addSeparator();
    for (const char* id : {"text", "leader", "dim", "dim-h", "dim-v", "dim-radius", "dim-diameter", "dim-angle", "hatch"}) create->addAction(actions_[QString("tool.") + id]);
    QMenu* toolsM = mb->addMenu(QStringLiteral("Инструменты"));
    for (const char* id : {"move", "copy", "rotate", "mirror", "scale", "offset", "trim", "extend", "fillet", "chamfer", "array", "explode"}) toolsM->addAction(actions_[QString("tool.") + id]);
    toolsM->addSeparator();
    for (const char* id : {"join", "union", "subtract", "intersect", "split", "cut"}) toolsM->addAction(actions_[QString("tool.") + id]);
    QMenu* window = mb->addMenu(QStringLiteral("Окно"));
    add(window, {"ws.welcome", "ws.draft", "ws.sheets", "ws.sketch", "ws.interior"});
    window->addSeparator();
    // dock toggles are appended in buildDocks()
    QMenu* help = mb->addMenu(QStringLiteral("Справка"));
    add(help, {"help", "about"});
    window->setObjectName("menuWindow");
}

void MainWindow::buildWorkspaceBar() {
    toolbar_ = addToolBar(QStringLiteral("Рабочие области"));
    toolbar_->setObjectName("workspaces");
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(18, 18));
    toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    workspaces_ = new QButtonGroup(this);
    workspaces_->setExclusive(true);
    const Palette& pal = Theme::current();
    struct Ws { QString id, title, icon; bool enabled; QString tip; };
    const std::vector<Ws> list{
        {"draft", QStringLiteral("Черчение"), "draft", true, QStringLiteral("2D-черчение: линии, размеры, слои")},
        {"sheets", QStringLiteral("Листы"), "sheet", true, QStringLiteral("Листы, основная надпись, PDF")},
        {"sketch", QStringLiteral("Эскиз"), "sketch", true, QStringLiteral("Параметрический эскиз: примитивы, ограничения, решатель")},
        {"model", QStringLiteral("Модель"), "model", false, QStringLiteral("3D-моделирование — этап 03")},
        {"assembly", QStringLiteral("Сборка"), "assembly", false, QStringLiteral("Сборки — этап 04")},
        {"interior", QStringLiteral("Интерьер"), "interior", true, QStringLiteral("Интерьер: стены, проёмы, помещения, мебель, ведомости")},
        {"drawing", QStringLiteral("Чертёж 3D"), "drawing", false, QStringLiteral("Виды с модели — этап 06")}};
    auto* logo = new QLabel; logo->setPixmap(Icons::logo(22, pal.onAccent, pal.accent)); logo->setContentsMargins(8, 0, 10, 0);
    toolbar_->addWidget(logo);
    for (const Ws& w : list) {
        auto* b = new QToolButton;
        b->setText(w.title); b->setIcon(Icons::icon(w.icon, w.enabled ? pal.text : pal.faint));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); b->setCheckable(true); b->setAutoRaise(true); b->setEnabled(w.enabled);
        b->setToolTip(w.enabled ? w.tip : w.tip + QStringLiteral(" (недоступно)"));
        toolbar_->addWidget(b);
        workspaces_->addButton(b);
        wsButtons_[w.id] = b;
        if (w.id == "draft") connect(b, &QToolButton::clicked, this, [this] { showPage(PageDraft); });
        if (w.id == "sheets") connect(b, &QToolButton::clicked, this, [this] { showPage(PageSheets); });
        if (w.id == "sketch") connect(b, &QToolButton::clicked, this, [this] { showPage(PageSketch); });
        if (w.id == "interior") connect(b, &QToolButton::clicked, this, [this] { showPage(PageInterior); });
    }
    auto* spacer = new QWidget; spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(spacer);
    for (const char* id : {"undo", "redo", "zoomExtents", "grid", "snap", "gridSnap", "ortho", "polar", "exportPdf"}) toolbar_->addAction(actions_[id]);
}

void MainWindow::buildDocks() {
    auto mk = [this](const QString& title, QWidget* w, Qt::DockWidgetArea area, const QString& name) {
        auto* d = new QDockWidget(title, this);
        d->setObjectName(name);
        d->setWidget(w);
        d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
        addDockWidget(area, d);
        docks_.push_back(d);
        return d;
    };
    tools_ = new ToolsPanel;
    layers_ = new LayersPanel(project_);
    props_ = new PropertiesPanel(project_, *view_);
    tree_ = new TreePanel(project_, *view_);
    tools_->setMinimumWidth(230);
    props_->setMinimumWidth(280);
    layers_->setMinimumWidth(280);
    connect(tools_, &ToolsPanel::toolRequested, this, &MainWindow::startTool);
    for (const QString& id : toolIds()) if (QAction* a = actions_.value("tool." + id)) tools_->setShortcut(id, a->shortcut().toString(QKeySequence::NativeText));
    connect(tree_, &TreePanel::sheetActivated, this, [this](quint64 id) { showPage(PageSheets); sheets_->selectSheet(id); });
    QDockWidget* dTools = mk(QStringLiteral("Инструменты"), tools_, Qt::LeftDockWidgetArea, "dockTools");
    QDockWidget* dTree = mk(QStringLiteral("Дерево документа"), tree_, Qt::LeftDockWidgetArea, "dockTree");
    QDockWidget* dProps = mk(QStringLiteral("Свойства"), props_, Qt::RightDockWidgetArea, "dockProps");
    QDockWidget* dLayers = mk(QStringLiteral("Слои"), layers_, Qt::RightDockWidgetArea, "dockLayers");
    splitDockWidget(dTools, dTree, Qt::Vertical);
    splitDockWidget(dProps, dLayers, Qt::Vertical);
    resizeDocks({dTools, dTree}, {260, 260}, Qt::Horizontal);
    resizeDocks({dProps, dLayers}, {300, 300}, Qt::Horizontal);
    QMenu* window = findChild<QMenu*>("menuWindow");
    for (QDockWidget* d : docks_) window->addAction(d->toggleViewAction());
    QSettings st;
    restoreGeometry(st.value("ui/geometry").toByteArray());
    restoreState(st.value("ui/state").toByteArray());
}

void MainWindow::buildStatusBar() {
    coords_ = new QLabel; coords_->setMinimumWidth(220);
    unitLbl_ = new QLabel; snapLbl_ = new QLabel; toolLbl_ = new QLabel;
    for (QLabel* l : {coords_, unitLbl_, snapLbl_, toolLbl_}) l->setObjectName("Hint");
    statusBar()->addWidget(coords_);
    statusBar()->addWidget(toolLbl_, 1);
    dofLbl_ = new QLabel; dofLbl_->setObjectName("Hint"); dofLbl_->hide();
    statusBar()->addPermanentWidget(dofLbl_);
    gridBtn_ = new QToolButton; gridBtn_->setObjectName("gridMagnet"); gridBtn_->setAutoRaise(true); gridBtn_->setCheckable(true);
    gridBtn_->setToolTip(QStringLiteral("Магнит к круглым координатам (F9). Правая кнопка / стрелка — шаг привязки"));
    gridBtn_->setPopupMode(QToolButton::MenuButtonPopup);
    auto* stepMenu = new QMenu(gridBtn_);
    for (double st : {0.5, 1.0, 5.0, 10.0, 50.0, 100.0}) stepMenu->addAction(QStringLiteral("%1 мм").arg(st), this, [this, st] { setSnapStep(st); });
    stepMenu->addSeparator();
    stepMenu->addAction(QStringLiteral("Другой шаг…"), this, [this] {
        bool ok = false;
        double v = QInputDialog::getDouble(this, QStringLiteral("Шаг привязки"), QStringLiteral("Шаг магнита, мм:"), project_.document().settings().snapStep, 0.001, 100000, 3, &ok);
        if (ok) setSnapStep(v);
    });
    gridBtn_->setMenu(stepMenu);
    connect(gridBtn_, &QToolButton::clicked, this, [this] { actions_["gridSnap"]->trigger(); });
    statusBar()->addPermanentWidget(gridBtn_);
    statusBar()->addPermanentWidget(snapLbl_);
    statusBar()->addPermanentWidget(unitLbl_);
}

void MainWindow::applyHotkeys() {
    QSettings st;
    st.beginGroup("hotkeys");
    for (auto it = actions_.begin(); it != actions_.end(); ++it)
        if (st.contains(it.key())) it.value()->setShortcut(QKeySequence(st.value(it.key()).toString()));
    st.endGroup();
}

// ---------------------------------------------------------------- tools
ToolContext MainWindow::makeContext() {
    ToolContext c;
    c.project = &project_;
    c.view = view_;
    c.selection = &view_->selection();
    c.setPrompt = [this](const QString& t) { prompt_->setText(t); };
    c.message = [this](const QString& t) { statusBar()->showMessage(t, 5000); };
    c.askText = [this](const QString& title, const QString& label, const QString& def) -> std::optional<QString> {
        bool ok = false;
        QString t = QInputDialog::getText(this, title, label, QLineEdit::Normal, def, &ok);
        if (!ok) return std::nullopt;
        return t;
    };
    c.finished = [this] { QMetaObject::invokeMethod(this, [this] { startTool("select"); }, Qt::QueuedConnection); };
    return c;
}

void MainWindow::startTool(const QString& id) {
    if (stack_->currentIndex() != PageDraft) showPage(PageDraft);
    if (id == "delete") { deleteSelection(); return; }
    auto t = createTool(id, makeContext());
    if (!t) return;
    view_->setTool(std::move(t));
    view_->setFocus();
}

void MainWindow::deleteSelection() {
    if (view_->selection().isEmpty()) return;
    auto c = std::make_unique<EntityCommand>(QStringLiteral("Удалить"));
    for (quint64 id : view_->selection()) if (const Entity* e = project_.document().entity(id)) if (!e->locked) c->remove(*e);
    view_->clearSelection();
    if (!c->empty()) project_.commands().push(std::move(c));
}

void MainWindow::runCommandLine() {
    QString text = command_->text().trimmed();
    if (text.isEmpty()) { view_->setFocus(); if (view_->tool()) { if (view_->tool()->selecting()) view_->tool()->selectionDone(); else view_->tool()->enter(); } return; }
    command_->clear();
    // command aliases
    static const QMap<QString, QString> alias{{"l", "line"}, {"л", "line"}, {"line", "line"}, {"pl", "polyline"}, {"p", "polyline"}, {"rec", "rect"}, {"r", "rect"}, {"c", "circle"},
                                             {"a", "arc"}, {"el", "ellipse"}, {"e", "ellipse"}, {"spl", "spline"}, {"s", "spline"}, {"t", "text"}, {"le", "leader"}, {"d", "dim"}, {"dim", "dim"},
                                             {"dh", "dim-h"}, {"dv", "dim-v"}, {"dr", "dim-radius"}, {"dd", "dim-diameter"}, {"da", "dim-angle"}, {"h", "hatch"}, {"m", "move"}, {"co", "copy"},
                                             {"ro", "rotate"}, {"mi", "mirror"}, {"sc", "scale"}, {"o", "offset"}, {"tr", "trim"}, {"ex", "extend"}, {"f", "fillet"}, {"cha", "chamfer"},
                                             {"ar", "array"}, {"x", "explode"}, {"j", "join"}, {"un", "union"}, {"su", "subtract"}, {"in", "intersect"}, {"br", "split"}, {"cut", "cut"}, {"del", "delete"}, {"e!", "delete"}, {"u", "undo"}, {"z", "zoom"}, {"ze", "zoomExtents"}};
    QString low = text.toLower();
    Tool* tool = view_->tool();
    bool consumed = tool && tool->input(text);
    if (consumed) { view_->invalidate(); view_->update(); prompt_->setText(tool->prompt()); return; }
    if (alias.contains(low) || toolIds().contains(low)) {
        QString id = alias.value(low, low);
        if (id == "undo") { project_.commands().undo(); return; }
        if (id == "zoomExtents" || id == "zoom") { view_->zoomExtents(); return; }
        startTool(id); return;
    }
    statusBar()->showMessage(QStringLiteral("Не понял: «%1». Введите точку (100,50 · @40<30) или команду (l, c, r, m …)").arg(text), 5000);
}

// Double-click on an entity: inline editor over the viewport; Enter applies through an undoable command
void MainWindow::openInlineEditor(quint64 id, QPointF screen) {
    const Entity* e = project_.document().entity(id);
    if (!e || e->locked) return;
    std::vector<EditField> fields = editFields(project_.document(), *e);
    if (fields.empty()) { statusBar()->showMessage(QStringLiteral("Для этого объекта нет быстрого редактирования — см. панель «Свойства»"), 3000); return; }
    if (auto* old = view_->findChild<InlineEditor*>()) old->close();
    const DocumentSettings& s = project_.document().settings();
    auto* ed = new InlineEditor(fields, s.unit, s.precision, view_);
    connect(ed, &InlineEditor::applied, this, [this, id](const QMap<QString, double>& values, const QString& text) {
        const Entity* cur = project_.document().entity(id);
        if (!cur) return;
        if (auto cmd = applyEdit(project_.document(), *cur, values, text)) project_.commands().push(std::move(cmd));
        view_->invalidate(); view_->setFocus();
    });
    connect(ed, &InlineEditor::cancelled, this, [this] { view_->setFocus(); });
    ed->popup(screen);
}

void MainWindow::setSnapStep(double step) {
    if (step <= 0) return;
    toggleSetting([step](DocumentSettings& s) { s.snapStep = step; s.gridSnap = true; }, QStringLiteral("Шаг привязки"));
    statusBar()->showMessage(QStringLiteral("Шаг магнита: %1").arg(Units::format(step, project_.document().settings().unit, project_.document().settings().precision, true)), 2000);
}

// Tab in the command bar: round the typed number to the next step of the cycle (шаг привязки → 5 → 10 → 50 → 100 → 0.5 → 1 …)
void MainWindow::cycleRounding() {
    static const std::vector<double> steps{0.5, 1, 5, 10, 50, 100};
    const DocumentSettings& s = project_.document().settings();
    if (tabIdx_ < 0) {
        tabBase_ = command_->text().trimmed();
        auto it = std::find_if(steps.begin(), steps.end(), [&](double v) { return std::abs(v - s.snapStep) < 1e-9; });
        tabIdx_ = it == steps.end() ? 1 : static_cast<int>(it - steps.begin());
    } else {
        tabIdx_ = (tabIdx_ + 1) % static_cast<int>(steps.size());
    }
    QString t = tabBase_; t.replace(',', '.');
    bool ok = false; double v = t.toDouble(&ok);
    if (!ok) return;
    double step = steps[static_cast<size_t>(tabIdx_)];
    double r = std::round(v / step) * step;
    command_->setText(QString::number(r, 'f', step < 1 ? 1 : 0));
    statusBar()->showMessage(QStringLiteral("Округление до %1 мм (Tab — дальше)").arg(step), 1500);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == command_ && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) { cycleRounding(); return true; }
    }
    if (watched == view_ && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        const QString t = ke->text();
        if (!t.isEmpty() && ke->modifiers() == Qt::NoModifier && (t[0].isDigit() || t[0] == '@' || t[0] == '-' || t[0] == '.' || t[0] == ',')) {
            command_->setFocus();
            command_->setText(t);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::showPage(Page p) {
    const Page prev = static_cast<Page>(stack_->currentIndex());
    // Hiding the docks makes QMainWindow forget their sizes — keep the Draft dock layout and restore it on return.
    if (prev == PageDraft && p != PageDraft) draftDockState_ = saveState();
    stack_->setCurrentIndex(p);
    bool draft = p == PageDraft;
    for (QDockWidget* d : docks_) d->setVisible(draft && !d->property("userHidden").toBool());
    if (draft && prev != PageDraft && !draftDockState_.isEmpty()) restoreState(draftDockState_);
    if (wsButtons_.contains("draft")) wsButtons_["draft"]->setChecked(p == PageDraft);
    if (wsButtons_.contains("sheets")) wsButtons_["sheets"]->setChecked(p == PageSheets);
    if (wsButtons_.contains("sketch")) wsButtons_["sketch"]->setChecked(p == PageSketch);
    if (wsButtons_.contains("interior")) wsButtons_["interior"]->setChecked(p == PageInterior);
    if (dofLbl_) dofLbl_->setVisible(p == PageSketch);
    if (p == PageSheets) sheets_->refresh();
    if (p == PageSketch) sketch_->activate();
    if (p == PageInterior) interior_->activate();
    updateUndoRedo();
    if (p == PageDraft) { if (!view_->tool()) startTool("select"); view_->setFocus(); }
}

// ---------------------------------------------------------------- project ops
void MainWindow::newProject() {
    if (!maybeSave()) return;
    project_.newProject();
    view_->clearSelection();
    view_->setView(Point{100, 100}, 2.0);
    showPage(PageDraft);
    statusBar()->showMessage(QStringLiteral("Новый проект"), 3000);
}

void MainWindow::loadDemo() {
    if (!maybeSave()) return;
    Demo::build(project_);
    view_->clearSelection();
    view_->zoomExtents();
    showPage(PageDraft);
    statusBar()->showMessage(QStringLiteral("Демонстрационный проект: план комнаты 6000×4000, лист A3 1:50"), 6000);
}

void MainWindow::openProject() {
    if (!maybeSave()) return;
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Открыть"), QString(), QStringLiteral("Проекты Пси и DXF (*.psi *.dxf);;Проект Пси (*.psi);;DXF (*.dxf)"));
    if (!path.isEmpty()) openPath(path);
}

bool MainWindow::openPath(const QString& path) {
    if (path.endsWith(".dxf", Qt::CaseInsensitive)) { if (!maybeSave()) return false; project_.newProject(); importDxf(path); return true; }
    QString err; QStringList warnings;
    if (Project::hasRecovery(path)) {
        auto r = QMessageBox::question(this, QStringLiteral("Восстановление"), QStringLiteral("Найдено автосохранение новее файла «%1». Открыть его?").arg(QFileInfo(path).fileName()),
                                       QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel) return false;
        if (r == QMessageBox::Yes) {
            if (!project_.open(Project::autosavePath(path), &err, &warnings)) { QMessageBox::warning(this, QStringLiteral("Открытие"), err); return false; }
            project_.saveAs(path, &err);
            afterLoad(path, warnings);
            return true;
        }
        project_.discardRecovery(path);
    }
    if (!project_.open(path, &err, &warnings)) { QMessageBox::warning(this, QStringLiteral("Не удалось открыть"), err); return false; }
    afterLoad(path, warnings);
    return true;
}

void MainWindow::afterLoad(const QString& path, const QStringList& warnings) {
    Project::addRecent(path);
    welcome_->refresh();
    view_->clearSelection();
    view_->zoomExtents();
    showPage(PageDraft);
    if (!warnings.isEmpty()) QMessageBox::information(this, QStringLiteral("Предупреждения"), warnings.join('\n'));
    statusBar()->showMessage(QStringLiteral("Открыт %1").arg(QFileInfo(path).fileName()), 4000);
}

bool MainWindow::saveProject() {
    if (project_.path().isEmpty()) return saveProjectAs();
    QString err;
    if (!project_.save(&err)) { QMessageBox::warning(this, QStringLiteral("Сохранение"), err); return false; }
    Project::addRecent(project_.path());
    statusBar()->showMessage(QStringLiteral("Сохранено"), 3000);
    return true;
}

bool MainWindow::saveProjectAs() {
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Сохранить как"), project_.path().isEmpty() ? project_.meta().name + ".psi" : project_.path(), QStringLiteral("Проект Пси (*.psi)"));
    if (path.isEmpty()) return false;
    if (!path.endsWith(".psi", Qt::CaseInsensitive)) path += ".psi";
    QString err;
    if (!project_.saveAs(path, &err)) { QMessageBox::warning(this, QStringLiteral("Сохранение"), err); return false; }
    Project::addRecent(path);
    welcome_->refresh();
    statusBar()->showMessage(QStringLiteral("Сохранено: %1").arg(path), 3000);
    return true;
}

bool MainWindow::maybeSave() {
    if (!project_.isDirty() || (project_.document().entityCount() == 0 && project_.path().isEmpty())) return true;
    auto r = QMessageBox::question(this, QStringLiteral("Несохранённые изменения"), QStringLiteral("Сохранить изменения в «%1»?").arg(project_.displayName()),
                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) return saveProject();
    return true;
}

void MainWindow::importDxf(const QString& pathIn) {
    QString path = pathIn;
    if (path.isEmpty()) path = QFileDialog::getOpenFileName(this, QStringLiteral("Импорт DXF"), QString(), QStringLiteral("DXF (*.dxf)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, QStringLiteral("Импорт DXF"), QStringLiteral("Не удалось прочитать файл «%1».").arg(path)); return; }
    QByteArray bytes = f.readAll();

    struct Result { std::vector<Entity> entities; std::vector<Layer> layers; DxfReport report; QString error; bool ok = false; };
    auto result = std::make_shared<Result>();
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    auto* dlg = new QProgressDialog(QStringLiteral("Импорт DXF: %1…").arg(QFileInfo(path).fileName()), QStringLiteral("Отмена"), 0, 0, this);
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setMinimumDuration(300);
    connect(dlg, &QProgressDialog::canceled, this, [cancelled] { cancelled->store(true); });
    DxfOptions opt; opt.fallbackUnit = project_.document().settings().unit;
    QThread* th = QThread::create([bytes, result, cancelled, opt] {
        result->ok = Dxf::parse(bytes, &result->entities, &result->layers, &result->report, &result->error, opt, [cancelled] { return cancelled->load(); });
    });
    connect(th, &QThread::finished, this, [this, th, dlg, result, cancelled, path] {
        dlg->reset(); dlg->deleteLater(); th->deleteLater();
        if (cancelled->load()) { statusBar()->showMessage(QStringLiteral("Импорт отменён"), 3000); return; }
        if (!result->ok) { QMessageBox::warning(this, QStringLiteral("Импорт DXF"), result->error); return; }
        if (result->report.unitGuessed) {
            QStringList names; QList<Unit> units{Unit::Millimeter, Unit::Centimeter, Unit::Meter, Unit::Inch};
            for (Unit u : units) names << Units::name(u);
            bool ok = false;
            QString pick = QInputDialog::getItem(this, QStringLiteral("Единицы DXF"), QStringLiteral("В файле не указаны единицы. В каких единицах нарисован чертёж?"), names, 0, false, &ok);
            if (!ok) return;
            Unit u = units[names.indexOf(pick)];
            double k = Units::toMm(u);
            if (std::abs(k - 1.0) > 1e-12) for (Entity& e : result->entities) transformEntity(e, Transform::scale(k, k));
        }
        auto c = std::make_unique<EntityCommand>(QStringLiteral("Импорт DXF"));
        // layers go straight to the document (cheap, rarely undone); entities through a command for undo
        Document& doc = project_.document();
        QMap<quint32, quint32> layerMap;
        for (const Layer& l : result->layers) {
            quint32 id = 0;
            for (const Layer& d : doc.layers()) if (d.name == l.name) id = d.id;
            if (!id) { Layer n = l; n.id = 0; id = doc.addLayer(n); }
            layerMap[l.id] = id;
        }
        for (Entity e : result->entities) { e.id = 0; e.layer = layerMap.value(e.layer, 0); c->add(std::move(e)); }
        project_.commands().push(std::move(c));
        Project::addRecent(path);
        view_->zoomExtents();
        showPage(PageDraft);
        QMessageBox::information(this, QStringLiteral("Импорт DXF"), result->report.summary());
    });
    th->start();
}

void MainWindow::exportDxf() {
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Экспорт DXF"), project_.meta().name + ".dxf", QStringLiteral("DXF (*.dxf)"));
    if (path.isEmpty()) return;
    std::vector<quint64> ids;
    if (!view_->selection().isEmpty() && QMessageBox::question(this, QStringLiteral("Экспорт DXF"), QStringLiteral("Экспортировать только выделенные объекты?")) == QMessageBox::Yes)
        for (quint64 id : view_->selection()) ids.push_back(id);
    QByteArray bytes = Dxf::write(project_.document(), ids);
    QString err;
    if (!PsiFile::save(path, bytes, &err)) { QMessageBox::warning(this, QStringLiteral("Экспорт DXF"), err); return; }
    statusBar()->showMessage(QStringLiteral("DXF сохранён: %1").arg(path), 4000);
}

void MainWindow::exportPdf(const std::vector<quint64>& sheetIds) {
    if (sheetIds.empty()) return;
    QStringList clip;
    for (quint64 id : sheetIds) if (const Sheet* s = project_.document().sheet(id)) for (const QString& w : PdfExport::checkClipping(project_.document(), *s)) clip << s->name + QStringLiteral(": ") + w;
    if (!clip.isEmpty() && QMessageBox::warning(this, QStringLiteral("Экспорт PDF"), QStringLiteral("Часть содержимого будет обрезана:\n%1\n\nПродолжить?").arg(clip.join('\n')),
                                                 QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Экспорт PDF"), project_.meta().name + ".pdf", QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty()) return;
    QProgressDialog dlg(QStringLiteral("Экспорт PDF…"), QStringLiteral("Отмена"), 0, int(sheetIds.size()), this);
    dlg.setWindowModality(Qt::WindowModal); dlg.setMinimumDuration(200);
    QString err; QStringList warnings;
    bool ok = PdfExport::exportSheets(project_.document(), sheetIds, path, &err, &warnings, [&dlg](int done, int total) { dlg.setMaximum(total); dlg.setValue(done); QApplication::processEvents(); return !dlg.wasCanceled(); });
    dlg.reset();
    if (!ok) { if (!err.isEmpty()) QMessageBox::warning(this, QStringLiteral("Экспорт PDF"), err); return; }
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Экспорт PDF"));
    box.setText(QStringLiteral("PDF сохранён: %1 (%2 лист.)").arg(QFileInfo(path).fileName()).arg(sheetIds.size()));
    if (!warnings.isEmpty()) box.setInformativeText(warnings.join('\n'));
    QPushButton* openBtn = box.addButton(QStringLiteral("Открыть"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("Закрыть"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == openBtn) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// ---------------------------------------------------------------- settings
void MainWindow::toggleSetting(const std::function<void(DocumentSettings&)>& fn, const QString& text) {
    DocumentSettings before = project_.document().settings();
    DocumentSettings after = before; fn(after);
    project_.document().setSettings(after);   // view toggles are not undoable
    (void)text;
    view_->invalidate();
    updateStatus();
}

void MainWindow::editDocumentSettings() {
    DocumentSettingsDialog dlg(project_.document().settings(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    DocumentSettings after = dlg.settings();
    if (after == project_.document().settings()) return;
    project_.commands().push(std::make_unique<SettingsCommand>(project_.document().settings(), after, QStringLiteral("Параметры документа")));
    view_->invalidate();
}

void MainWindow::editAppSettings() {
    QList<QAction*> list;
    for (auto it = actions_.begin(); it != actions_.end(); ++it) if (!it.key().startsWith("ws.") && it.key() != "tool.select") list.push_back(it.value());
    AppSettingsDialog dlg(list, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QSettings st;
    view_->setCrosshair(st.value("ui/crosshair", true).toBool());
    project_.setAutosaveMinutes(st.value("autosave/minutes", 5).toInt());
    if (dlg.themeChanged()) statusBar()->showMessage(QStringLiteral("Тема применится после перезапуска Пси"), 6000);
}

// ---------------------------------------------------------------- status
void MainWindow::updateTitle() {
    setWindowTitle(project_.displayName() + QStringLiteral("[*] — Пси"));
    setWindowModified(project_.isDirty());
    if (!project_.path().isEmpty()) setWindowFilePath(project_.path());
}

void MainWindow::updateUndoRedo() {
    bool canUndo, canRedo; QString undoText, redoText;
    if (stack_ && stack_->currentIndex() == PageSketch) { canUndo = sketch_->canUndo(); canRedo = sketch_->canRedo(); undoText = sketch_->undoText(); redoText = sketch_->redoText(); }
    else if (stack_ && stack_->currentIndex() == PageInterior) { canUndo = interior_->canUndo(); canRedo = interior_->canRedo(); undoText = interior_->undoText(); redoText = interior_->redoText(); }
    else { CommandStack& s = project_.commands(); canUndo = s.canUndo(); canRedo = s.canRedo(); undoText = s.undoText(); redoText = s.redoText(); }
    actions_["undo"]->setEnabled(canUndo);
    actions_["redo"]->setEnabled(canRedo);
    actions_["undo"]->setText(canUndo ? QStringLiteral("Отменить: %1").arg(undoText) : QStringLiteral("Отменить"));
    actions_["redo"]->setText(canRedo ? QStringLiteral("Повторить: %1").arg(redoText) : QStringLiteral("Повторить"));
    view_->invalidate();
}

void MainWindow::updateStatus() {
    const DocumentSettings& s = project_.document().settings();
    actions_["grid"]->setChecked(s.gridVisible);
    actions_["snap"]->setChecked(s.snapEnabled);
    actions_["ortho"]->setChecked(s.ortho);
    actions_["polar"]->setChecked(s.polar);
    actions_["gridSnap"]->setChecked(s.gridSnap);
    if (lastCursor_) {   // the snapped point: exact round coordinates when the magnet holds
        Point w = *lastCursor_;
        coords_->setText(QStringLiteral("X %1   Y %2").arg(Units::format(w.x, s.unit, s.precision), Units::format(w.y, s.unit, s.precision)));
    }
    if (gridBtn_) {
        gridBtn_->setChecked(s.gridSnap);
        gridBtn_->setText(QStringLiteral("⌁ Сетка %1").arg(Units::format(s.snapStep, s.unit, s.precision, true)));
    }
    unitLbl_->setText(Units::name(s.unit) + QStringLiteral("  ·  сетка %1").arg(Units::format(s.gridSpacing, s.unit, s.precision, true)));
    QString sn = view_->snapLabel();
    snapLbl_->setText(sn.isEmpty() ? (s.snapEnabled ? QStringLiteral("привязка") : QStringLiteral("без привязки")) : sn);
    int n = view_->selection().size();
    if (n) toolLbl_->setText(QStringLiteral("Выбрано: %1").arg(n));
    else if (!view_->toolId().isEmpty()) toolLbl_->setText(toolTitle(view_->toolId()));
}

// ---------------------------------------------------------------- events
void MainWindow::closeEvent(QCloseEvent* e) {
    if (!maybeSave()) { e->ignore(); return; }
    QSettings st;
    st.setValue("ui/geometry", saveGeometry());
    st.setValue("ui/state", saveState());
    if (!project_.path().isEmpty()) project_.discardRecovery(project_.path());
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    for (const QUrl& u : e->mimeData()->urls()) { QString p = u.toLocalFile(); if (p.endsWith(".psi", Qt::CaseInsensitive) || p.endsWith(".dxf", Qt::CaseInsensitive)) { e->acceptProposedAction(); return; } }
}

void MainWindow::dropEvent(QDropEvent* e) {
    for (const QUrl& u : e->mimeData()->urls()) {
        QString p = u.toLocalFile();
        if (p.endsWith(".psi", Qt::CaseInsensitive)) { if (maybeSave()) openPath(p); return; }
        if (p.endsWith(".dxf", Qt::CaseInsensitive)) { importDxf(p); return; }
    }
}

} // namespace psi
