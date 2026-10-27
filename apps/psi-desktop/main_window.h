#pragma once
#include "core/project/project.h"
#include "modules/draft/tool.h"
#include <QMainWindow>
#include <QMap>

class QLabel;
class QLineEdit;
class QStackedWidget;
class QDockWidget;
class QToolButton;
class QButtonGroup;
class QToolBar;

namespace psi {

class Viewport;
class WelcomePage;
class SheetsPage;
class ToolsPanel;
class LayersPanel;
class PropertiesPanel;
class TreePanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    bool openPath(const QString& path);        // .psi or .dxf
    void importDxf(const QString& path);
    void loadDemo();

protected:
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum Page { PageWelcome = 0, PageDraft = 1, PageSheets = 2 };
    void buildActions();
    void buildMenus();
    void buildWorkspaceBar();
    void buildDocks();
    void buildStatusBar();
    void applyHotkeys();
    QAction* action(const QString& id, const QString& text, const QString& icon, const QKeySequence& key, const std::function<void()>& fn);
    ToolContext makeContext();
    void startTool(const QString& id);
    void showPage(Page p);

    void newProject();
    void openProject();
    bool saveProject();
    bool saveProjectAs();
    bool maybeSave();
    void exportDxf();
    void exportPdf(const std::vector<quint64>& sheetIds);
    void deleteSelection();
    void runCommandLine();
    void editDocumentSettings();
    void editAppSettings();
    void toggleSetting(const std::function<void(DocumentSettings&)>& fn, const QString& text);
    void updateTitle();
    void updateUndoRedo();
    void updateStatus();
    void afterLoad(const QString& path, const QStringList& warnings);

    Project project_;
    QStackedWidget* stack_ = nullptr;
    WelcomePage* welcome_ = nullptr;
    Viewport* view_ = nullptr;
    SheetsPage* sheets_ = nullptr;
    QWidget* draftPage_ = nullptr;
    ToolsPanel* tools_ = nullptr;
    LayersPanel* layers_ = nullptr;
    PropertiesPanel* props_ = nullptr;
    TreePanel* tree_ = nullptr;
    QList<QDockWidget*> docks_;
    QToolBar* toolbar_ = nullptr;
    QLineEdit* command_ = nullptr;
    QLabel* prompt_ = nullptr;
    QLabel* coords_ = nullptr;
    QLabel* unitLbl_ = nullptr;
    QLabel* snapLbl_ = nullptr;
    QLabel* toolLbl_ = nullptr;
    QMap<QString, QAction*> actions_;
    QButtonGroup* workspaces_ = nullptr;
    QMap<QString, QToolButton*> wsButtons_;
};

} // namespace psi
