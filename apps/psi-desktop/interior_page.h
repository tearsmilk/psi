#pragma once
#include "core/project/project.h"
#include "modules/interior/building.h"
#include "workspace_common.h"
#include <QWidget>

class QComboBox;
class QTreeWidget;
class QTableWidget;
class QTabWidget;
class QLabel;
class QFormLayout;
class QToolButton;
class QListWidget;
class QDoubleSpinBox;

namespace psi {

namespace in { class InteriorView; }

// Рабочее пространство «Интерьер»: этажи и объекты + инструменты слева, план в центре,
// «Свойства», «Каталог мебели» и «Ведомости» справа. Здания хранятся в .psi как interior/<id>.json.
class InteriorPage : public QWidget {
    Q_OBJECT
public:
    explicit InteriorPage(Project& project, QWidget* parent = nullptr);
    void activate();
    void reload();
    in::Building* building() { return building_.id ? &building_ : nullptr; }
    in::Building& ensureBuilding();
    int exportToDraft();                        // количество выданных объектов
    QString scheduleCsv() const;
    void saveToProject() { store(); refreshTree(); refreshProperties(); refreshSchedules(); }   // после правок модели снаружи

    bool canUndo() const { return undo_.canUndo(); }
    bool canRedo() const { return undo_.canRedo(); }
    QString undoText() const { return undo_.undoText(); }
    QString redoText() const { return undo_.redoText(); }
    void undo();
    void redo();

Q_SIGNALS:
    void undoChanged();
    void statusMessage(const QString& text);
    void exported(int count);
    void buildingChanged();

private:
    QByteArray snapshot() const;
    void restore(const QByteArray& data);
    void store();
    QByteArray signature() const;
    void beginChange(const QString& what);
    void refreshTree();
    void refreshProperties();
    void refreshSchedules();
    void refreshFloors();
    void exportCsv();

    Project& project_;
    in::Building building_;
    SnapshotStack undo_;
    QByteArray loadedSig_;
    bool updating_ = false;

    in::InteriorView* view_ = nullptr;
    QComboBox* floorCombo_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QMap<QString, QToolButton*> toolBtns_;
    QDoubleSpinBox* thick_ = nullptr;
    QDoubleSpinBox* height_ = nullptr;
    QDoubleSpinBox* openW_ = nullptr;
    QLabel* propsHeader_ = nullptr;
    QFormLayout* props_ = nullptr;
    QListWidget* catalog_ = nullptr;
    QTabWidget* schedTabs_ = nullptr;
    QTableWidget* furnTable_ = nullptr;
    QTableWidget* matTable_ = nullptr;
    QLabel* totalLbl_ = nullptr;
    QLabel* prompt_ = nullptr;
};

} // namespace psi
