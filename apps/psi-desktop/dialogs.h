#pragma once
#include "core/attributes/attributes.h"
#include "core/document/document.h"
#include <QDialog>
#include <QKeySequence>
#include <QMap>

class QComboBox;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QTableWidget;
class QAction;

namespace psi {

// Layer properties.
class LayerDialog : public QDialog {
    Q_OBJECT
public:
    explicit LayerDialog(const Layer& l, QWidget* parent = nullptr);
    Layer layer() const;
private:
    Layer layer_;
    QLineEdit* name_; QPushButton* color_; QComboBox* weight_; QComboBox* lineType_; QCheckBox* visible_; QCheckBox* locked_; QCheckBox* printable_;
};

// New attribute: name, kind, value.
class AttributeDialog : public QDialog {
    Q_OBJECT
public:
    explicit AttributeDialog(QWidget* parent = nullptr);
    QString key() const;
    AttrValue value() const;
private:
    QLineEdit* key_; QComboBox* kind_; QLineEdit* value_; QLineEdit* unit_;
};

// Document settings: units, precision, grid, snaps, text height, author.
class DocumentSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit DocumentSettingsDialog(const DocumentSettings& s, QWidget* parent = nullptr);
    DocumentSettings settings() const;
private:
    DocumentSettings s_;
    QComboBox* unit_; QSpinBox* precision_; QDoubleSpinBox* grid_; QCheckBox* gridVisible_; QCheckBox* snap_; QCheckBox* ortho_; QCheckBox* polar_;
    QDoubleSpinBox* polarStep_; QDoubleSpinBox* textHeight_; QLineEdit* author_;
    QMap<unsigned, QCheckBox*> snapBoxes_;
};

// Application settings: theme, accent, autosave, hotkeys (QSettings, applied on restart for the theme).
class AppSettingsDialog : public QDialog {
    Q_OBJECT
public:
    AppSettingsDialog(const QList<QAction*>& actions, QWidget* parent = nullptr);
    bool themeChanged() const { return themeChanged_; }
private:
    void accept() override;
    QList<QAction*> actions_;
    QComboBox* theme_; QPushButton* accent_; QSpinBox* autosave_; QTableWidget* keys_; QCheckBox* crosshair_;
    QColor accentColor_;
    bool themeChanged_ = false;
};

// About.
void showAbout(QWidget* parent);

} // namespace psi
