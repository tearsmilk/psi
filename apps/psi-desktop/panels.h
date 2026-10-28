#pragma once
#include "core/project/project.h"
#include <QWidget>

class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QComboBox;
class QFormLayout;
class QGridLayout;
class QLineEdit;
class QLabel;
class QToolButton;

namespace psi {

class Viewport;

// Tool palette: search box, collapsible groups («Избранное» = 8 most recent, Рисование, Текст и размеры, Правка,
// Объединение, Штриховка), 36 px buttons with labels (2 columns) or icons only (compact mode). Icons are built lazily on first show.
class ToolsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ToolsPanel(QWidget* parent = nullptr);
    void setActive(const QString& id);                       // highlights the tool and records it in «Избранное»
    void setShortcut(const QString& id, const QString& keys);// shown in tooltips and searchable
    void setCompact(bool on);
    bool compact() const { return compact_; }
    void setFilter(const QString& text);
    QStringList recent() const { return recent_; }
    QStringList visibleTools() const;                        // ids of buttons currently shown (tests)
    bool groupCollapsed(const QString& title) const;
    void setGroupCollapsed(const QString& title, bool collapsed);
Q_SIGNALS:
    void toolRequested(const QString& id);
protected:
    void showEvent(QShowEvent* ev) override;
private:
    struct Group { QString title; QStringList ids; QToolButton* header = nullptr; QWidget* body = nullptr; QGridLayout* grid = nullptr; bool collapsed = false; };
    QToolButton* button(const QString& id);                 // one button per tool, shared between groups
    void relayout(Group& g);
    void rebuildFavourites();
    void ensureIcons();
    void saveState() const;
    std::vector<Group> groups_;
    QMap<QString, QToolButton*> buttons_;
    QMap<QString, QToolButton*> favButtons_;
    QMap<QString, QString> shortcuts_;
    QStringList recent_;
    QLineEdit* search_ = nullptr;
    QToolButton* compactBtn_ = nullptr;
    QString filter_;
    QString active_;
    bool compact_ = false;
    bool iconsReady_ = false;
};

// Layers: visibility, lock, name, colour, weight; current layer; add / remove / edit via LayerCommand.
class LayersPanel : public QWidget {
    Q_OBJECT
public:
    LayersPanel(Project& project, QWidget* parent = nullptr);
    void refresh();
private:
    void addLayer();
    void removeLayer();
    void editLayer(quint32 id);
    Project& project_;
    QTableWidget* table_ = nullptr;
    bool updating_ = false;
};

// Properties of the selection: common style fields, geometry, custom attributes.
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    PropertiesPanel(Project& project, Viewport& view, QWidget* parent = nullptr);
    void refresh();
private:
    void apply(const std::function<void(Entity&)>& fn, const QString& text);
    void addAttribute();
    void removeAttribute();
    Project& project_;
    Viewport& view_;
    QLabel* header_ = nullptr;
    QComboBox* layer_ = nullptr;
    QComboBox* color_ = nullptr;
    QComboBox* weight_ = nullptr;
    QComboBox* lineType_ = nullptr;
    QWidget* geomBox_ = nullptr;
    QFormLayout* geom_ = nullptr;
    QTableWidget* attrs_ = nullptr;
    bool updating_ = false;
};

// Document tree: project → drawing (layers → entities) → sheets.
class TreePanel : public QWidget {
    Q_OBJECT
public:
    TreePanel(Project& project, Viewport& view, QWidget* parent = nullptr);
    void refresh();
Q_SIGNALS:
    void sheetActivated(quint64 id);
private:
    Project& project_;
    Viewport& view_;
    QTreeWidget* tree_ = nullptr;
    bool updating_ = false;
};

} // namespace psi
