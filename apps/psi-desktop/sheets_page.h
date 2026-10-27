#pragma once
#include "core/project/project.h"
#include <QWidget>

class QListWidget;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QFormLayout;
class QLabel;

namespace psi {

// Sheet preview: the sheet drawn with its viewports and title block, scaled to fit.
class SheetPreview : public QWidget {
    Q_OBJECT
public:
    explicit SheetPreview(Project& project, QWidget* parent = nullptr);
    void setSheet(quint64 id) { sheetId_ = id; update(); }
    void setSelectedViewport(int index) { vpIndex_ = index; update(); }
Q_SIGNALS:
    void viewportMoved(int index, Point delta);     // sheet mm
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    QTransform sheetToWidget(double* k = nullptr) const;
    Project& project_;
    quint64 sheetId_ = 0;
    int vpIndex_ = -1;
    bool dragging_ = false;
    QPointF dragStart_;
};

// «Листы»: list of sheets, preview, paper / margins / title block / viewports form, PDF export.
class SheetsPage : public QWidget {
    Q_OBJECT
public:
    explicit SheetsPage(Project& project, QWidget* parent = nullptr);
    void refresh();
    void selectSheet(quint64 id);
    quint64 currentSheet() const;
Q_SIGNALS:
    void exportPdfRequested(const std::vector<quint64>& sheetIds);
private:
    void addSheet();
    void removeSheet();
    void duplicateSheet();
    void commit(const Sheet& after, const QString& text);
    void rebuildForm();
    void addViewport();
    void removeViewport();
    void fitViewport();
    void refreshViewportForm();
    Project& project_;
    QListWidget* list_ = nullptr;
    SheetPreview* preview_ = nullptr;
    QLineEdit* name_ = nullptr;
    QComboBox* paper_ = nullptr;
    QCheckBox* landscape_ = nullptr;
    QCheckBox* frame_ = nullptr;
    QDoubleSpinBox* mL_ = nullptr; QDoubleSpinBox* mT_ = nullptr; QDoubleSpinBox* mR_ = nullptr; QDoubleSpinBox* mB_ = nullptr;
    QCheckBox* titleEnabled_ = nullptr;
    QWidget* titleBox_ = nullptr; QFormLayout* titleForm_ = nullptr;
    QComboBox* vpList_ = nullptr;
    QDoubleSpinBox* vpX_ = nullptr; QDoubleSpinBox* vpY_ = nullptr; QDoubleSpinBox* vpW_ = nullptr; QDoubleSpinBox* vpH_ = nullptr;
    QComboBox* vpScale_ = nullptr; QDoubleSpinBox* vpCx_ = nullptr; QDoubleSpinBox* vpCy_ = nullptr; QCheckBox* vpBorder_ = nullptr;
    QLabel* vpWarn_ = nullptr;
    bool updating_ = false;
};

} // namespace psi
