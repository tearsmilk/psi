#include "dialogs.h"
#include "io/native/psi_file.h"
#include <QPainter>
#include "ui/icons.h"
#include "ui/theme.h"
#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace psi {

namespace {
QDialogButtonBox* buttons(QDialog* d) {
    auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    b->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Готово"));
    b->button(QDialogButtonBox::Ok)->setObjectName("Accent");
    b->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
    QObject::connect(b, &QDialogButtonBox::accepted, d, &QDialog::accept);
    QObject::connect(b, &QDialogButtonBox::rejected, d, &QDialog::reject);
    return b;
}
void setSwatch(QPushButton* b, const QColor& c) {
    QPixmap px(18, 18); px.fill(Qt::transparent);
    QPainter p(&px); p.setRenderHint(QPainter::Antialiasing); p.setPen(QPen(Theme::current().border, 1)); p.setBrush(c); p.drawRoundedRect(QRectF(0.5, 0.5, 17, 17), 4, 4);
    b->setIcon(QIcon(px)); b->setText(c.name().toUpper());
}
} // namespace

// ================================================================= LayerDialog
LayerDialog::LayerDialog(const Layer& l, QWidget* parent) : QDialog(parent), layer_(l) {
    setWindowTitle(QStringLiteral("Слой"));
    auto* v = new QVBoxLayout(this);
    auto* t = new QLabel(QStringLiteral("Слой")); t->setObjectName("H2"); v->addWidget(t);
    auto* f = new QFormLayout;
    name_ = new QLineEdit(l.name); f->addRow(QStringLiteral("Имя"), name_);
    color_ = new QPushButton; setSwatch(color_, QColor::fromRgb(l.color));
    connect(color_, &QPushButton::clicked, this, [this] { QColor c = QColorDialog::getColor(QColor::fromRgb(layer_.color), this, QStringLiteral("Цвет слоя")); if (c.isValid()) { layer_.color = c.rgb() & 0xFFFFFF; setSwatch(color_, c); } });
    f->addRow(QStringLiteral("Цвет"), color_);
    weight_ = new QComboBox;
    for (double w : {0.13, 0.18, 0.25, 0.35, 0.5, 0.7, 1.0, 1.4, 2.0}) weight_->addItem(QStringLiteral("%1 мм").arg(w, 0, 'f', 2), w);
    int wi = -1; for (int i = 0; i < weight_->count(); ++i) if (std::abs(weight_->itemData(i).toDouble() - l.lineWeight) < 1e-6) wi = i;
    if (wi < 0) { weight_->addItem(QStringLiteral("%1 мм").arg(l.lineWeight, 0, 'f', 2), l.lineWeight); wi = weight_->count() - 1; }
    weight_->setCurrentIndex(wi);
    f->addRow(QStringLiteral("Толщина"), weight_);
    lineType_ = new QComboBox;
    lineType_->addItem(QStringLiteral("Сплошная"), int(LineType::Solid)); lineType_->addItem(QStringLiteral("Штриховая"), int(LineType::Dashed));
    lineType_->addItem(QStringLiteral("Пунктирная"), int(LineType::Dotted)); lineType_->addItem(QStringLiteral("Штрихпунктирная"), int(LineType::DashDot));
    lineType_->setCurrentIndex(std::max(0, lineType_->findData(int(l.lineType))));
    f->addRow(QStringLiteral("Тип линии"), lineType_);
    visible_ = new QCheckBox(QStringLiteral("Видимый")); visible_->setChecked(l.visible);
    locked_ = new QCheckBox(QStringLiteral("Заблокирован")); locked_->setChecked(l.locked);
    printable_ = new QCheckBox(QStringLiteral("Печатается")); printable_->setChecked(l.printable);
    f->addRow(QString(), visible_); f->addRow(QString(), locked_); f->addRow(QString(), printable_);
    v->addLayout(f);
    v->addWidget(buttons(this));
    if (l.id == 0) name_->setEnabled(false);
}

Layer LayerDialog::layer() const {
    Layer l = layer_;
    l.name = name_->text().trimmed().isEmpty() ? layer_.name : name_->text().trimmed();
    l.lineWeight = weight_->currentData().toDouble();
    l.lineType = LineType(lineType_->currentData().toInt());
    l.visible = visible_->isChecked(); l.locked = locked_->isChecked(); l.printable = printable_->isChecked();
    return l;
}

// ================================================================= AttributeDialog
AttributeDialog::AttributeDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Атрибут"));
    auto* v = new QVBoxLayout(this);
    auto* t = new QLabel(QStringLiteral("Новый атрибут")); t->setObjectName("H2"); v->addWidget(t);
    auto* f = new QFormLayout;
    key_ = new QLineEdit; key_->setPlaceholderText(QStringLiteral("material, note, article…")); f->addRow(QStringLiteral("Имя"), key_);
    kind_ = new QComboBox;
    for (AttrKind k : {AttrKind::String, AttrKind::Int, AttrKind::Double, AttrKind::Quantity, AttrKind::Bool, AttrKind::Material, AttrKind::Date, AttrKind::Note})
        kind_->addItem(attrKindName(k), int(k));
    f->addRow(QStringLiteral("Тип"), kind_);
    value_ = new QLineEdit; f->addRow(QStringLiteral("Значение"), value_);
    unit_ = new QLineEdit; unit_->setPlaceholderText(QStringLiteral("мм, кг, м²…")); f->addRow(QStringLiteral("Единица"), unit_);
    v->addLayout(f);
    v->addWidget(buttons(this));
}

QString AttributeDialog::key() const { return key_->text().trimmed(); }

AttrValue AttributeDialog::value() const {
    auto k = AttrKind(kind_->currentData().toInt());
    QString v = value_->text().trimmed();
    switch (k) {
    case AttrKind::Int: return AttrValue::integer(v.toLongLong());
    case AttrKind::Double: return AttrValue::number(QString(v).replace(',', '.').toDouble());
    case AttrKind::Quantity: return AttrValue::quantity(QString(v).replace(',', '.').toDouble(), unit_->text().trimmed());
    case AttrKind::Bool: return AttrValue::boolean(v == "1" || v.toLower() == "да" || v.toLower() == "true" || v.toLower() == "yes");
    case AttrKind::Material: return AttrValue::material(v);
    case AttrKind::Date: return AttrValue::date(v);
    case AttrKind::Note: { AttrValue a = AttrValue::string(v); a.kind = AttrKind::Note; return a; }
    default: return AttrValue::string(v);
    }
}

// ================================================================= DocumentSettingsDialog
DocumentSettingsDialog::DocumentSettingsDialog(const DocumentSettings& s, QWidget* parent) : QDialog(parent), s_(s) {
    setWindowTitle(QStringLiteral("Параметры документа"));
    auto* v = new QVBoxLayout(this);
    auto* t = new QLabel(QStringLiteral("Параметры документа")); t->setObjectName("H2"); v->addWidget(t);
    auto* f = new QFormLayout;
    unit_ = new QComboBox;
    for (Unit u : {Unit::Millimeter, Unit::Centimeter, Unit::Meter, Unit::Inch}) unit_->addItem(Units::name(u), int(u));
    unit_->setCurrentIndex(unit_->findData(int(s.unit)));
    f->addRow(QStringLiteral("Единицы отображения"), unit_);
    precision_ = new QSpinBox; precision_->setRange(0, 6); precision_->setValue(s.precision); f->addRow(QStringLiteral("Знаков после запятой"), precision_);
    grid_ = new QDoubleSpinBox; grid_->setRange(0.01, 100000); grid_->setDecimals(2); grid_->setSuffix(QStringLiteral(" мм")); grid_->setValue(s.gridSpacing); f->addRow(QStringLiteral("Шаг сетки"), grid_);
    gridVisible_ = new QCheckBox(QStringLiteral("Показывать сетку")); gridVisible_->setChecked(s.gridVisible); f->addRow(QString(), gridVisible_);
    snap_ = new QCheckBox(QStringLiteral("Объектная привязка")); snap_->setChecked(s.snapEnabled); f->addRow(QString(), snap_);
    auto* grid = new QGridLayout;
    const std::vector<std::pair<unsigned, QString>> flags{{SnapEndpoint, QStringLiteral("Конечная точка")}, {SnapMidpoint, QStringLiteral("Середина")}, {SnapCenter, QStringLiteral("Центр")},
                                                        {SnapQuadrant, QStringLiteral("Квадрант")}, {SnapIntersection, QStringLiteral("Пересечение")}, {SnapPerpendicular, QStringLiteral("Перпендикуляр")},
                                                        {SnapTangent, QStringLiteral("Касательная")}, {SnapGrid, QStringLiteral("Сетка")}};
    int i = 0;
    for (const auto& fl : flags) { auto* cb = new QCheckBox(fl.second); cb->setChecked(s.snapFlags & fl.first); snapBoxes_[fl.first] = cb; grid->addWidget(cb, i / 2, i % 2); ++i; }
    f->addRow(QStringLiteral("Режимы привязки"), grid);
    ortho_ = new QCheckBox(QStringLiteral("Орто (Shift — временно)")); ortho_->setChecked(s.ortho); f->addRow(QString(), ortho_);
    polar_ = new QCheckBox(QStringLiteral("Полярное отслеживание")); polar_->setChecked(s.polar); f->addRow(QString(), polar_);
    polarStep_ = new QDoubleSpinBox; polarStep_->setRange(1, 180); polarStep_->setSuffix(QStringLiteral(" °")); polarStep_->setValue(s.polarStep); f->addRow(QStringLiteral("Шаг полярного угла"), polarStep_);
    textHeight_ = new QDoubleSpinBox; textHeight_->setRange(0.1, 10000); textHeight_->setDecimals(2); textHeight_->setSuffix(QStringLiteral(" мм")); textHeight_->setValue(s.textHeight); f->addRow(QStringLiteral("Высота текста по умолчанию"), textHeight_);
    author_ = new QLineEdit(s.author); f->addRow(QStringLiteral("Автор"), author_);
    v->addLayout(f);
    v->addWidget(buttons(this));
}

DocumentSettings DocumentSettingsDialog::settings() const {
    DocumentSettings s = s_;
    s.unit = Unit(unit_->currentData().toInt());
    s.precision = precision_->value();
    s.gridSpacing = grid_->value();
    s.gridVisible = gridVisible_->isChecked();
    s.snapEnabled = snap_->isChecked();
    s.snapFlags = 0; for (auto it = snapBoxes_.begin(); it != snapBoxes_.end(); ++it) if (it.value()->isChecked()) s.snapFlags |= it.key();
    s.ortho = ortho_->isChecked(); s.polar = polar_->isChecked(); s.polarStep = polarStep_->value();
    s.textHeight = textHeight_->value();
    s.author = author_->text().trimmed();
    return s;
}

// ================================================================= AppSettingsDialog
AppSettingsDialog::AppSettingsDialog(const QList<QAction*>& actions, QWidget* parent) : QDialog(parent), actions_(actions) {
    setWindowTitle(QStringLiteral("Настройки"));
    resize(560, 620);
    QSettings st;
    auto* v = new QVBoxLayout(this);
    auto* t = new QLabel(QStringLiteral("Настройки")); t->setObjectName("H2"); v->addWidget(t);
    auto* f = new QFormLayout;
    theme_ = new QComboBox;
    theme_->addItem(QStringLiteral("Как в системе"), "system"); theme_->addItem(QStringLiteral("Светлая"), "light"); theme_->addItem(QStringLiteral("Тёмная"), "dark");
    theme_->setCurrentIndex(std::max(0, theme_->findData(st.value("ui/theme", "system").toString())));
    f->addRow(QStringLiteral("Тема"), theme_);
    accentColor_ = QColor(st.value("ui/accent", Theme::defaultAccent().name()).toString());
    if (!accentColor_.isValid()) accentColor_ = Theme::defaultAccent();
    accent_ = new QPushButton; setSwatch(accent_, accentColor_);
    connect(accent_, &QPushButton::clicked, this, [this] { QColor c = QColorDialog::getColor(accentColor_, this, QStringLiteral("Акцентный цвет")); if (c.isValid()) { accentColor_ = c; setSwatch(accent_, c); } });
    auto* accRow = new QHBoxLayout; accRow->addWidget(accent_); auto* reset = new QPushButton(QStringLiteral("Терракота")); accRow->addWidget(reset); accRow->addStretch(1);
    connect(reset, &QPushButton::clicked, this, [this] { accentColor_ = Theme::defaultAccent(); setSwatch(accent_, accentColor_); });
    f->addRow(QStringLiteral("Акцент"), accRow);
    autosave_ = new QSpinBox; autosave_->setRange(0, 120); autosave_->setSuffix(QStringLiteral(" мин")); autosave_->setSpecialValueText(QStringLiteral("выключено")); autosave_->setValue(st.value("autosave/minutes", 5).toInt());
    f->addRow(QStringLiteral("Автосохранение"), autosave_);
    crosshair_ = new QCheckBox(QStringLiteral("Перекрестие курсора")); crosshair_->setChecked(st.value("ui/crosshair", true).toBool()); f->addRow(QString(), crosshair_);
    v->addLayout(f);

    auto* h = new QLabel(QStringLiteral("Горячие клавиши")); h->setObjectName("H2"); v->addWidget(h);
    keys_ = new QTableWidget(actions.size(), 2);
    keys_->setHorizontalHeaderLabels({QStringLiteral("Команда"), QStringLiteral("Клавиши")});
    keys_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    keys_->verticalHeader()->hide();
    keys_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    int r = 0;
    for (QAction* a : actions) {
        auto* name = new QTableWidgetItem(a->text().remove('&')); name->setData(Qt::UserRole, a->objectName());
        keys_->setItem(r, 0, name);
        auto* ed = new QKeySequenceEdit(a->shortcut());
        keys_->setCellWidget(r, 1, ed);
        ++r;
    }
    v->addWidget(keys_, 1);
    auto* hint = new QLabel(QStringLiteral("Тема и акцент применяются после перезапуска. Пустое поле — без сочетания.")); hint->setObjectName("Faint"); v->addWidget(hint);
    v->addWidget(buttons(this));
}

void AppSettingsDialog::accept() {
    QSettings st;
    QString oldTheme = st.value("ui/theme", "system").toString();
    QString oldAccent = st.value("ui/accent", Theme::defaultAccent().name()).toString();
    st.setValue("ui/theme", theme_->currentData().toString());
    st.setValue("ui/accent", accentColor_.name());
    st.setValue("autosave/minutes", autosave_->value());
    st.setValue("ui/crosshair", crosshair_->isChecked());
    themeChanged_ = oldTheme != theme_->currentData().toString() || QColor(oldAccent) != accentColor_;
    // hotkeys: detect duplicates
    QMap<QString, QString> used;
    for (int r = 0; r < keys_->rowCount(); ++r) {
        auto* ed = qobject_cast<QKeySequenceEdit*>(keys_->cellWidget(r, 1));
        QString seq = ed->keySequence().toString();
        if (seq.isEmpty()) continue;
        if (used.contains(seq)) { QMessageBox::warning(this, QStringLiteral("Горячие клавиши"), QStringLiteral("Сочетание %1 назначено и для «%2», и для «%3».").arg(seq, used[seq], keys_->item(r, 0)->text())); return; }
        used[seq] = keys_->item(r, 0)->text();
    }
    st.beginGroup("hotkeys");
    for (int r = 0; r < keys_->rowCount(); ++r) {
        auto* ed = qobject_cast<QKeySequenceEdit*>(keys_->cellWidget(r, 1));
        QString id = keys_->item(r, 0)->data(Qt::UserRole).toString();
        for (QAction* a : actions_) if (a->objectName() == id) { a->setShortcut(ed->keySequence()); st.setValue(id, ed->keySequence().toString()); }
    }
    st.endGroup();
    QDialog::accept();
}

// ================================================================= About
void showAbout(QWidget* parent) {
    QMessageBox box(parent);
    box.setWindowTitle(QStringLiteral("О программе"));
    box.setIconPixmap(Icons::logo(64, Theme::current().onAccent, Theme::current().accent));
    box.setText(QStringLiteral("<span style='font-size:20px'>Пси</span><br>Универсальная CAD-система · версия %1").arg(PSI_VERSION));
    box.setInformativeText(QStringLiteral("Этап 00–01: черчение, слои, листы с основной надписью, точный векторный PDF, DXF.<br>"
                                          "Собственный формат .psi (ZIP + JSON, схема %1).<br><br>Qt %2 · © tearsmilk").arg(kSchemaVersion).arg(qVersion()));
    box.setStandardButtons(QMessageBox::Ok);
    box.button(QMessageBox::Ok)->setText(QStringLiteral("Закрыть"));
    box.exec();
}

} // namespace psi
