#pragma once
#include "core/project/project.h"
#include <QWidget>

class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QComboBox;
class QFormLayout;
class QLabel;
class QToolButton;

namespace psi {

class Viewport;

// Tool palette: grouped buttons, one per draft tool.
class ToolsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ToolsPanel(QWidget* parent = nullptr);
    void setActive(const QString& id);
Q_SIGNALS:
    void toolRequested(const QString& id);
private:
    QMap<QString, QToolButton*> buttons_;
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
