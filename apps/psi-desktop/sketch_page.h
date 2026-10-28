#pragma once
#include "core/project/project.h"
#include "modules/sketcher/sketch.h"
#include "workspace_common.h"
#include <QWidget>

class QListWidget;
class QTableWidget;
class QLabel;
class QCheckBox;
class QToolButton;
class QFormLayout;
class QLineEdit;

namespace psi {

namespace sk { class SketchView; }

// Рабочее пространство «Эскиз»: список эскизов, инструменты и ограничения слева, холст в центре,
// панели «Ограничения» и «Свойства» справа. Эскизы хранятся в .psi как sketches/<id>.json.
class SketchPage : public QWidget {
    Q_OBJECT
public:
    explicit SketchPage(Project& project, QWidget* parent = nullptr);
    void activate();                            // при показе страницы
    void reload();                              // перечитать эскизы из проекта
    sk::Sketch* currentSketch();
    const std::vector<sk::Sketch>& sketches() const { return sketches_; }
    sk::Sketch* addSketch(const QString& name = QString());
    void selectSketch(quint64 id);
    int exportToDraft();                        // количество выданных объектов

    bool canUndo() const { return undo_.canUndo(); }
    bool canRedo() const { return undo_.canRedo(); }
    QString undoText() const { return undo_.undoText(); }
    QString redoText() const { return undo_.redoText(); }
    void undo();
    void redo();
    int dof() const;

Q_SIGNALS:
    void undoChanged();
    void statusMessage(const QString& text);
    void dofChanged(int dof);
    void exported(int count);
    void sketchesChanged();

private:
    QByteArray snapshot() const;
    void restore(const QByteArray& data);
    void store();                               // в project.foreignParts() + markDirty
    QByteArray signature() const;
    void refreshList();
    void refreshConstraints();
    void refreshProperties();
    void removeSketch();
    void renameSketch();

    Project& project_;
    std::vector<sk::Sketch> sketches_;
    quint64 currentId_ = 0;
    SnapshotStack undo_;
    QByteArray loadedSig_;
    bool updating_ = false;

    sk::SketchView* view_ = nullptr;
    QListWidget* list_ = nullptr;
    QTableWidget* cons_ = nullptr;
    QLabel* dofLbl_ = nullptr;
    QLabel* conflictLbl_ = nullptr;
    QCheckBox* autoCb_ = nullptr;
    QCheckBox* gridCb_ = nullptr;
    QMap<QString, QToolButton*> toolBtns_;
    QWidget* propsBox_ = nullptr;
    QFormLayout* props_ = nullptr;
    QLabel* propsHeader_ = nullptr;
    QLabel* prompt_ = nullptr;
};

} // namespace psi
