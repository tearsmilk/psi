#pragma once
// Рабочее пространство «Модель» (этап 02, ⌘5): параметрическое 3D на Open CASCADE.
// Слева — детали, дерево построения, операции, вид; в центре — View3D; справа — свойства операции,
// материал, массовые характеристики. Детали хранятся в .psi как models/<id>.json (+ кэш assets/model_<id>.brep).
#include "core/project/project.h"
#include "modules/model/part.h"
#include "rendering/view3d.h"
#include "workspace_common.h"
#include <QWidget>

class QListWidget;
class QLabel;
class QFormLayout;
class QToolButton;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;

namespace psi {

class ModelPage : public QWidget {
    Q_OBJECT
public:
    explicit ModelPage(Project& project, QWidget* parent = nullptr);
    void activate();
    void reload();

    const std::vector<md::Part>& parts() const { return parts_; }
    md::Part* currentPart();
    md::Part* addPart(const QString& name = QString());
    void selectPart(quint64 id);
    View3D* view() const { return view_; }

    // операции с отменой, перестроением и сохранением в проект
    quint64 addFeature(md::FeatureType type, const QJsonObject& params, const QString& name = QString());
    bool editFeature(quint64 id, const QJsonObject& params);
    bool removeFeature(quint64 id);
    bool suppressFeature(quint64 id, bool on);
    bool moveFeature(quint64 id, int newIndex);
    md::RebuildReport rebuild();
    QString lastReport() const { return lastReport_; }

    bool exportStep(const QString& path, QString* error = nullptr);
    bool exportStl(const QString& path, bool binary = true, QString* error = nullptr);
    bool importFile(const QString& path, QString* report = nullptr, QString* error = nullptr);   // .step / .stp / .stl

    bool canUndo() const { return undo_.canUndo(); }
    bool canRedo() const { return undo_.canRedo(); }
    QString undoText() const { return undo_.undoText(); }
    QString redoText() const { return undo_.redoText(); }
    void undo();
    void redo();

Q_SIGNALS:
    void undoChanged();
    void statusMessage(const QString& text);
    void modelsChanged();
    void exported(int bodies);

private:
    QByteArray snapshot() const;
    void restore(const QByteArray& data);
    void store();
    QByteArray signature() const;
    void beginChange(const QString& text);
    void afterChange();
    void refreshParts();
    void refreshTree();
    void refreshView();
    void refreshProps();
    void refreshMass();
    void runFeatureDialog(md::FeatureType type, quint64 editId = 0);
    void treeReordered();
    void removePart();
    void renamePart();
    void chooseExport(bool step);
    void chooseImport();
    quint64 selectedFeature() const;

    Project& project_;
    std::vector<md::Part> parts_;
    quint64 currentId_ = 0;
    SnapshotStack undo_;
    QByteArray loadedSig_;
    bool updating_ = false;
    QString lastReport_;

    View3D* view_ = nullptr;
    QListWidget* partsList_ = nullptr;
    QListWidget* tree_ = nullptr;
    QLabel* prompt_ = nullptr;
    QLabel* errorLbl_ = nullptr;
    QLabel* propsHeader_ = nullptr;
    QWidget* propsForm_ = nullptr;
    QFormLayout* props_ = nullptr;
    QLineEdit* matName_ = nullptr;
    QDoubleSpinBox* matDensity_ = nullptr;
    QPushButton* matColor_ = nullptr;
    QLabel* massLbl_ = nullptr;
    QMap<QString, QToolButton*> pickBtns_;
    QToolButton* perspBtn_ = nullptr;
};

} // namespace psi
