#include "ui/theme.h"
#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleHints>

namespace psi {

static Palette g_palette;
static bool g_dark = false;

Palette Theme::palette(bool dark, const QColor& accent) {
    Palette p;
    if (!dark) {
        p.bg = QColor("#FAF9F5"); p.panel = QColor("#F0EEE6"); p.card = QColor("#FFFFFF"); p.border = QColor("#E3DFD3");
        p.text = QColor("#141413"); p.muted = QColor("#6F6E69"); p.faint = QColor("#A8A59C"); p.selected = QColor("#E6E2D6");
        p.hover = QColor("#E9E6DC"); p.input = QColor("#FFFFFF"); p.canvas = QColor("#FDFCF9"); p.grid = QColor("#EEEBE2");
        p.gridMajor = QColor("#DDD9CC"); p.crosshair = QColor("#8A8780");
    } else {
        p.bg = QColor("#262624"); p.panel = QColor("#1F1E1D"); p.card = QColor("#30302E"); p.border = QColor("#3E3D39");
        p.text = QColor("#F4F2EC"); p.muted = QColor("#A6A39A"); p.faint = QColor("#6E6C66"); p.selected = QColor("#3A3936");
        p.hover = QColor("#34332F"); p.input = QColor("#262624"); p.canvas = QColor("#1C1C1A"); p.grid = QColor("#262624");
        p.gridMajor = QColor("#33322F"); p.crosshair = QColor("#8A8780");
    }
    p.accent = accent.isValid() ? accent : defaultAccent();
    p.onAccent = QColor("#FFFFFF");
    return p;
}

const Palette& Theme::current() { return g_palette; }
bool Theme::isDark() { return g_dark; }

QString Theme::serifFamily() {
    for (const char* f : {"Source Serif 4", "Georgia", "Times New Roman", "Lora", "DejaVu Serif", "Liberation Serif"})
        if (QFontDatabase::hasFamily(QString::fromLatin1(f))) return QString::fromLatin1(f);
    return QStringLiteral("serif");
}
QString Theme::sansFamily() {
    for (const char* f : {"Inter", "SF Pro Text", "Helvetica Neue", "Segoe UI", "DejaVu Sans", "Liberation Sans"})
        if (QFontDatabase::hasFamily(QString::fromLatin1(f))) return QString::fromLatin1(f);
    return QStringLiteral("sans-serif");
}

void Theme::apply(QApplication& app, ThemeMode mode, const QColor& accent) {
    bool dark = mode == ThemeMode::Dark;
    if (mode == ThemeMode::System) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        dark = app.styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
        dark = app.palette().color(QPalette::Window).lightness() < 128;
#endif
    }
    g_dark = dark;
    g_palette = palette(dark, accent);
    const Palette& p = g_palette;
    auto c = [](const QColor& col) { return col.name(); };
    QString serif = serifFamily(), sans = sansFamily();
    QString ss = QString(R"(
QWidget { background: %1; color: %2; font-family: "%12"; font-size: 13px; }
QMainWindow::separator { background: %4; width: 1px; height: 1px; }
QMenuBar { background: %3; border-bottom: 1px solid %4; padding: 2px 6px; }
QMenuBar::item { padding: 4px 10px; border-radius: 6px; background: transparent; }
QMenuBar::item:selected { background: %6; }
QMenu { background: %5; border: 1px solid %4; border-radius: 8px; padding: 6px; }
QMenu::item { padding: 6px 24px 6px 12px; border-radius: 6px; }
QMenu::item:selected { background: %6; }
QMenu::separator { height: 1px; background: %4; margin: 6px 8px; }
QToolBar { background: %3; border: none; border-bottom: 1px solid %4; padding: 4px; spacing: 2px; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 7px; padding: 4px; }
QToolButton:hover { background: %6; }
QToolButton:checked { background: %7; border-color: %4; }
QToolButton:disabled { color: %9; }
QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; }
QDockWidget::title { background: %3; padding: 6px 10px; font-family: "%11"; font-size: 14px; border-bottom: 1px solid %4; }
QStatusBar { background: %3; border-top: 1px solid %4; color: %8; }
QStatusBar QLabel { padding: 0 6px; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QTextEdit, QPlainTextEdit { background: %10; border: 1px solid %4; border-radius: 7px; padding: 5px 8px; selection-background-color: %13; }
QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus { border-color: %13; }
QComboBox::drop-down { border: none; width: 22px; }
QPushButton { background: %5; border: 1px solid %4; border-radius: 8px; padding: 6px 14px; }
QPushButton:hover { background: %6; }
QPushButton:pressed { background: %7; }
QPushButton#Accent { background: %13; color: %14; border-color: %13; font-weight: 600; }
QPushButton#Accent:hover { background: %13; }
QPushButton:disabled { color: %9; }
QTreeView, QTableView, QListView { background: %5; border: 1px solid %4; border-radius: 8px; alternate-background-color: %3; }
QTreeView::item, QTableView::item, QListView::item { padding: 3px; }
QTreeView::item:selected, QTableView::item:selected, QListView::item:selected { background: %7; color: %2; }
QHeaderView::section { background: %3; border: none; border-bottom: 1px solid %4; padding: 4px 6px; color: %8; }
QTabWidget::pane { border: 1px solid %4; border-radius: 8px; }
QTabBar::tab { background: transparent; padding: 6px 12px; border-bottom: 2px solid transparent; color: %8; }
QTabBar::tab:selected { color: %2; border-bottom-color: %13; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: %4; border-radius: 4px; min-height: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: %4; border-radius: 4px; min-width: 24px; }
QCheckBox::indicator, QRadioButton::indicator { width: 15px; height: 15px; }
QGroupBox { border: 1px solid %4; border-radius: 8px; margin-top: 10px; padding-top: 10px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: %8; }
QLabel#Title { font-family: "%11"; font-size: 26px; }
QLabel#H2 { font-family: "%11"; font-size: 18px; }
QLabel#Hint { color: %8; }
QLabel#Faint { color: %9; }
QFrame#Card { background: %5; border: 1px solid %4; border-radius: 12px; }
QToolTip { background: %5; color: %2; border: 1px solid %4; border-radius: 6px; padding: 4px 8px; }
QSplitter::handle { background: %4; }
QProgressBar { border: 1px solid %4; border-radius: 6px; background: %10; text-align: center; }
QProgressBar::chunk { background: %13; border-radius: 5px; }
)").arg(c(p.bg), c(p.text), c(p.panel), c(p.border), c(p.card), c(p.hover), c(p.selected), c(p.muted), c(p.faint))
       .arg(c(p.input), serif, sans, c(p.accent), c(p.onAccent));
    app.setStyleSheet(ss);
}

} // namespace psi
