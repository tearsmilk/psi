#include "model_page.h"
#include "io/step/step.h"
#include "io/stl/stl.h"
#include "modules/sketcher/sketch.h"
#include "modules/sketcher/sketch_view.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace psi {

using md::FeatureType;
static const QString kPrefix = QStringLiteral("models/");
static const QString kBrepPrefix = QStringLiteral("assets/model_");

// ---------------------------------------------------------------- схема параметров операций
namespace {
struct Choice { QString label; QJsonValue value; };
struct Field {
    enum Kind { Double, Int, Bool, Choice, Vec3, Text };
    QString key, label; Kind kind = Double; QJsonValue def; QString suffix; std::vector<struct Choice> choices; double min = -1e9, max = 1e9;
    Field() = default;
    Field(QString k, QString l, Kind kd, QJsonValue d, QString sfx = {}, std::vector<struct Choice> ch = {}, double mn = -1e9, double mx = 1e9)
        : key(std::move(k)), label(std::move(l)), kind(kd), def(std::move(d)), suffix(std::move(sfx)), choices(std::move(ch)), min(mn), max(mx) {}
};

std::vector<Choice> sketchChoices(const md::Part& p, int before) { std::vector<Choice> c; int i = 0; for (const md::Feature& f : p.features()) { if (i++ >= before && before >= 0) break; if (f.type == FeatureType::Sketch) c.push_back({f.name, double(f.id)}); } return c; }
std::vector<Choice> toolChoices(const md::Part& p, int before) { std::vector<Choice> c; int i = 0; for (const md::Feature& f : p.features()) { if (i++ >= before && before >= 0) break; if (f.type == FeatureType::Extrude || f.type == FeatureType::Revolve || f.type == FeatureType::Hole) c.push_back({f.name, double(f.id)}); } return c; }
std::vector<Choice> datumChoices(const md::Part& p, int before, FeatureType t) { std::vector<Choice> c; int i = 0; for (const md::Feature& f : p.features()) { if (i++ >= before && before >= 0) break; if (f.type == t) c.push_back({f.name, double(f.id)}); } return c; }
std::vector<Choice> bodyChoices(const md::Part& p) { std::vector<Choice> c; c.push_back({QStringLiteral("последнее"), -1}); for (size_t i = 0; i < p.bodies().size(); ++i) c.push_back({QStringLiteral("%1 — %2").arg(i + 1).arg(p.bodies()[i].name), int(i)}); return c; }
std::vector<Choice> planeChoices(const md::Part& p, int before, bool withFace) {
    std::vector<Choice> c{{QStringLiteral("XY (горизонтальная)"), "XY"}, {QStringLiteral("XZ (фронтальная)"), "XZ"}, {QStringLiteral("YZ (боковая)"), "YZ"}};
    if (withFace) c.push_back({QStringLiteral("выбранная грань"), "face"});
    for (const Choice& d : datumChoices(p, before, FeatureType::DatumPlane)) c.push_back({QStringLiteral("плоскость «%1»").arg(d.label), QStringLiteral("datum:%1").arg(d.value.toDouble())});
    return c;
}
const QStringList kModes{"add", "cut", "new"};
const QStringList kModeLabels{QStringLiteral("добавить к телу"), QStringLiteral("вырезать из тела"), QStringLiteral("новое тело")};

std::vector<Field> schemaFor(FeatureType t, const md::Part& p, int before) {
    std::vector<Field> f;
    auto add = [&](Field x) { f.push_back(std::move(x)); };
    auto modes = [&] { Field m; m.key = "mode"; m.label = QStringLiteral("Режим"); m.kind = Field::Choice; m.def = "add"; for (int i = 0; i < kModes.size(); ++i) m.choices.push_back({kModeLabels[i], kModes[i]}); add(m); };
    auto body = [&] { Field b; b.key = "body"; b.label = QStringLiteral("Тело"); b.kind = Field::Choice; b.def = -1; b.choices = bodyChoices(p); add(b); };
    auto edges = [&] { Field e; e.key = "edges"; e.label = QStringLiteral("Рёбра"); e.kind = Field::Text; e.def = ""; e.suffix = QStringLiteral("номера через запятую; пусто — все"); add(e); };
    switch (t) {
    case FeatureType::Extrude: { Field s; s.key = "sketch"; s.label = QStringLiteral("Эскиз"); s.kind = Field::Choice; s.choices = sketchChoices(p, before); add(s); add({"distance", QStringLiteral("Расстояние"), Field::Double, 10, QStringLiteral("мм"), {}, -1e6, 1e6}); add({"distance2", QStringLiteral("Вторая сторона"), Field::Double, 0, QStringLiteral("мм (0 — нет)"), {}, 0, 1e6}); add({"symmetric", QStringLiteral("Симметрично"), Field::Bool, false, {}, {}}); modes(); body(); break; }
    case FeatureType::Revolve: { Field s; s.key = "sketch"; s.label = QStringLiteral("Эскиз"); s.kind = Field::Choice; s.choices = sketchChoices(p, before); add(s); add({"angle", QStringLiteral("Угол"), Field::Double, 360, QStringLiteral("°"), {}, -360, 360}); Field a; a.key = "axis"; a.label = QStringLiteral("Ось"); a.kind = Field::Choice; a.def = "Y"; a.choices = {{QStringLiteral("ось Y эскиза"), "Y"}, {QStringLiteral("ось X эскиза"), "X"}}; for (const Choice& d : datumChoices(p, before, FeatureType::DatumAxis)) a.choices.push_back({QStringLiteral("ось «%1»").arg(d.label), QStringLiteral("datum:%1").arg(d.value.toDouble())}); add(a); modes(); body(); break; }
    case FeatureType::Hole: { add({"point", QStringLiteral("Точка"), Field::Vec3, QJsonArray{0, 0, 0}, QStringLiteral("мм")}); add({"dir", QStringLiteral("Нормаль"), Field::Vec3, QJsonArray{0, 0, 1}, {}}); add({"diameter", QStringLiteral("Диаметр"), Field::Double, 10, QStringLiteral("мм"), {}, 0.01, 1e5}); add({"through", QStringLiteral("Сквозное"), Field::Bool, true}); add({"depth", QStringLiteral("Глубина"), Field::Double, 20, QStringLiteral("мм"), {}, 0.01, 1e6}); add({"countersink", QStringLiteral("Зенковка"), Field::Bool, false}); add({"csDiameter", QStringLiteral("Диаметр зенковки"), Field::Double, 16, QStringLiteral("мм"), {}, 0.01, 1e5}); add({"csAngle", QStringLiteral("Угол зенковки"), Field::Double, 90, QStringLiteral("°"), {}, 10, 170}); body(); break; }
    case FeatureType::Fillet: add({"radius", QStringLiteral("Радиус"), Field::Double, 2, QStringLiteral("мм"), {}, 0.001, 1e5}); edges(); body(); break;
    case FeatureType::Chamfer: add({"distance", QStringLiteral("Размер"), Field::Double, 1, QStringLiteral("мм"), {}, 0.001, 1e5}); edges(); body(); break;
    case FeatureType::LinearPattern: { Field s; s.key = "feature"; s.label = QStringLiteral("Операция"); s.kind = Field::Choice; s.choices = toolChoices(p, before); add(s); add({"count", QStringLiteral("Количество"), Field::Int, 3, {}, {}, 1, 2000}); add({"step", QStringLiteral("Шаг"), Field::Vec3, QJsonArray{50, 0, 0}, QStringLiteral("мм")}); add({"count2", QStringLiteral("Количество 2"), Field::Int, 1, {}, {}, 1, 2000}); add({"step2", QStringLiteral("Шаг 2"), Field::Vec3, QJsonArray{0, 50, 0}, QStringLiteral("мм")}); break; }
    case FeatureType::CircularPattern: { Field s; s.key = "feature"; s.label = QStringLiteral("Операция"); s.kind = Field::Choice; s.choices = toolChoices(p, before); add(s); add({"count", QStringLiteral("Количество"), Field::Int, 6, {}, {}, 2, 720}); add({"angle", QStringLiteral("Полный угол"), Field::Double, 360, QStringLiteral("°"), {}, -360, 360}); add({"axisPoint", QStringLiteral("Точка оси"), Field::Vec3, QJsonArray{0, 0, 0}, QStringLiteral("мм")}); add({"axisDir", QStringLiteral("Направление"), Field::Vec3, QJsonArray{0, 0, 1}, {}}); break; }
    case FeatureType::Mirror: { Field s; s.key = "feature"; s.label = QStringLiteral("Операция"); s.kind = Field::Choice; s.choices = toolChoices(p, before); s.choices.insert(s.choices.begin(), {QStringLiteral("всё тело"), 0}); add(s); Field pl; pl.key = "plane"; pl.label = QStringLiteral("Плоскость"); pl.kind = Field::Choice; pl.def = "YZ"; pl.choices = planeChoices(p, before, false); add(pl); add({"offset", QStringLiteral("Смещение"), Field::Double, 0, QStringLiteral("мм"), {}, -1e6, 1e6}); body(); break; }
    case FeatureType::Boolean: { Field a; a.key = "a"; a.label = QStringLiteral("Тело A"); a.kind = Field::Choice; a.def = 0; Field b = a; b.key = "b"; b.label = QStringLiteral("Тело B"); b.def = 1; for (size_t i = 0; i < p.bodies().size(); ++i) { Choice c{QStringLiteral("%1 — %2").arg(i + 1).arg(p.bodies()[i].name), int(i)}; a.choices.push_back(c); b.choices.push_back(c); } add(a); add(b); Field op; op.key = "op"; op.label = QStringLiteral("Операция"); op.kind = Field::Choice; op.def = "union"; op.choices = {{QStringLiteral("объединение"), "union"}, {QStringLiteral("вычитание A − B"), "subtract"}, {QStringLiteral("пересечение"), "intersect"}}; add(op); break; }
    case FeatureType::DatumPlane: { Field b; b.key = "base"; b.label = QStringLiteral("Основа"); b.kind = Field::Choice; b.def = "XY"; b.choices = planeChoices(p, before, true); add(b); add({"offset", QStringLiteral("Смещение"), Field::Double, 0, QStringLiteral("мм"), {}, -1e6, 1e6}); break; }
    case FeatureType::DatumAxis: { Field b; b.key = "base"; b.label = QStringLiteral("Основа"); b.kind = Field::Choice; b.def = "Z"; b.choices = {{QStringLiteral("ось X"), "X"}, {QStringLiteral("ось Y"), "Y"}, {QStringLiteral("ось Z"), "Z"}, {QStringLiteral("точка и направление"), "custom"}}; add(b); add({"point", QStringLiteral("Точка"), Field::Vec3, QJsonArray{0, 0, 0}, QStringLiteral("мм")}); add({"dir", QStringLiteral("Направление"), Field::Vec3, QJsonArray{0, 0, 1}, {}}); break; }
    case FeatureType::Shell: add({"thickness", QStringLiteral("Толщина"), Field::Double, 2, QStringLiteral("мм"), {}, 0.001, 1e5}); { Field e; e.key = "faces"; e.label = QStringLiteral("Открытые грани"); e.kind = Field::Text; e.def = ""; e.suffix = QStringLiteral("номера через запятую"); add(e); } body(); break;
    case FeatureType::Sketch: { Field pl; pl.key = "plane"; pl.label = QStringLiteral("Плоскость"); pl.kind = Field::Choice; pl.def = "XY"; pl.choices = planeChoices(p, before, true); add(pl); add({"offset", QStringLiteral("Смещение"), Field::Double, 0, QStringLiteral("мм"), {}, -1e6, 1e6}); break; }
    case FeatureType::MeshBody: case FeatureType::ImportBody: { Field n; n.key = "name"; n.label = QStringLiteral("Имя тела"); n.kind = Field::Text; n.def = ""; add(n); break; }
    }
    return f;
}

QJsonArray intsToArray(const QString& text) { QJsonArray a; for (const QString& s : text.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts)) { bool ok; int v = s.toInt(&ok); if (ok && v > 0) a.append(v); } return a; }
QString arrayToInts(const QJsonValue& v) { QStringList l; for (const QJsonValue& x : v.toArray()) l << QString::number(x.toInt()); return l.join(QStringLiteral(", ")); }

// Диалог параметров операции: форма по схеме; для эскиза — встроенный холст SketchView.
class FeatureDialog : public QDialog {
public:
    FeatureDialog(FeatureType type, const md::Part& part, int before, QJsonObject params, QWidget* parent)
        : QDialog(parent), type_(type), params_(std::move(params)) {
        setWindowTitle(md::featureTypeName(type));
        setMinimumWidth(type == FeatureType::Sketch ? 760 : 380);
        auto* v = new QVBoxLayout(this);
        auto* title = new QLabel(md::featureTypeName(type)); title->setObjectName("H2"); v->addWidget(title);
        auto* formW = new QWidget; auto* form = new QFormLayout(formW); form->setContentsMargins(0, 0, 0, 0);
        fields_ = schemaFor(type, part, before);
        for (const Field& f : fields_) {
            QJsonValue cur = params_.contains(f.key) ? params_[f.key] : f.def;
            QWidget* w = nullptr;
            switch (f.kind) {
            case Field::Double: { auto* s = new QDoubleSpinBox; s->setDecimals(3); s->setRange(f.min, f.max); s->setValue(cur.toDouble(f.def.toDouble())); if (!f.suffix.isEmpty()) s->setSuffix(QStringLiteral(" ") + f.suffix); w = s; break; }
            case Field::Int: { auto* s = new QSpinBox; s->setRange(int(f.min), int(f.max)); s->setValue(cur.toInt(f.def.toInt())); w = s; break; }
            case Field::Bool: { auto* c = new QCheckBox; c->setChecked(cur.toBool(f.def.toBool())); w = c; break; }
            case Field::Text: { auto* e = new QLineEdit(cur.isArray() ? arrayToInts(cur) : cur.toString()); if (!f.suffix.isEmpty()) e->setPlaceholderText(f.suffix); w = e; break; }
            case Field::Choice: {
                auto* c = new QComboBox;
                QString curKey = cur.isString() ? cur.toString() : QString::number(cur.toDouble());
                // «datum:<id>» склеивается из plane + datum / axis + datum
                if ((f.key == "plane" || f.key == "base" || f.key == "axis") && curKey == QLatin1String("datum")) curKey = QStringLiteral("datum:%1").arg(params_["datum"].toDouble());
                int sel = 0;
                for (size_t i = 0; i < f.choices.size(); ++i) { const QJsonValue& cv = f.choices[i].value; c->addItem(f.choices[i].label, cv.toVariant()); QString k = cv.isString() ? cv.toString() : QString::number(cv.toDouble()); if (k == curKey) sel = int(i); }
                c->setCurrentIndex(sel);
                w = c; break;
            }
            case Field::Vec3: {
                auto* row = new QWidget; auto* h = new QHBoxLayout(row); h->setContentsMargins(0, 0, 0, 0); h->setSpacing(4);
                QJsonArray a = cur.toArray(); if (a.size() != 3) a = f.def.toArray();
                for (int i = 0; i < 3; ++i) { auto* s = new QDoubleSpinBox; s->setDecimals(3); s->setRange(-1e7, 1e7); s->setValue(a[i].toDouble()); s->setPrefix(QStringLiteral("%1 ").arg(QChar('X' + i))); h->addWidget(s); s->setObjectName(QStringLiteral("v%1").arg(i)); }
                w = row; break;
            }
            }
            editors_[f.key] = w;
            form->addRow(f.label, w);
        }
        v->addWidget(formW);
        if (type == FeatureType::Sketch) {
            sketch_ = sk::Sketch::fromJson(params_["sketch"].toObject());
            if (sketch_.name == QStringLiteral("Эскиз") && params_.isEmpty()) sketch_.name = QStringLiteral("Профиль");
            auto* bar = new QHBoxLayout;
            struct T { QString icon, title; sk::SketchView::Tool tool; };
            const std::vector<T> tools{{"select", QStringLiteral("Выбор"), sk::SketchView::Tool::Select}, {"line", QStringLiteral("Отрезок"), sk::SketchView::Tool::Line}, {"polyline", QStringLiteral("Полилиния"), sk::SketchView::Tool::Polyline},
                                       {"rect", QStringLiteral("Прямоугольник"), sk::SketchView::Tool::Rectangle}, {"circle", QStringLiteral("Окружность"), sk::SketchView::Tool::Circle}, {"arc", QStringLiteral("Дуга"), sk::SketchView::Tool::Arc}};
            sketchView_ = new sk::SketchView;
            for (const T& t : tools) { auto* b = new QToolButton; b->setIcon(Icons::icon(t.icon, Theme::current().text)); b->setToolTip(t.title); b->setAutoRaise(true); b->setFixedSize(32, 32); connect(b, &QToolButton::clicked, this, [this, tool = t.tool] { sketchView_->setTool(tool); sketchView_->setFocus(); }); bar->addWidget(b); }
            auto* zoom = new QToolButton; zoom->setIcon(Icons::icon("zoom-extents", Theme::current().text)); zoom->setAutoRaise(true); zoom->setToolTip(QStringLiteral("Показать всё")); connect(zoom, &QToolButton::clicked, sketchView_, &sk::SketchView::zoomExtents); bar->addWidget(zoom);
            bar->addStretch(1);
            auto* hint = new QLabel(QStringLiteral("Замкнутый контур — профиль; окружность внутри — отверстие. Оси эскиза: X вправо, Y вверх в выбранной плоскости.")); hint->setObjectName("Hint"); hint->setWordWrap(true); bar->addWidget(hint, 1);
            v->addLayout(bar);
            sketchView_->setSketch(&sketch_);
            sketchView_->setMinimumHeight(360);
            v->addWidget(sketchView_, 1);
        }
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Готово")); bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept); connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        v->addWidget(bb);
    }
    QJsonObject result() const {
        QJsonObject o = params_;
        for (const Field& f : fields_) {
            QWidget* w = editors_[f.key];
            switch (f.kind) {
            case Field::Double: o[f.key] = static_cast<QDoubleSpinBox*>(w)->value(); break;
            case Field::Int: o[f.key] = static_cast<QSpinBox*>(w)->value(); break;
            case Field::Bool: o[f.key] = static_cast<QCheckBox*>(w)->isChecked(); break;
            case Field::Text: { QString t = static_cast<QLineEdit*>(w)->text(); if (f.key == "edges" || f.key == "faces") o[f.key] = intsToArray(t); else o[f.key] = t; break; }
            case Field::Choice: {
                QVariant v = static_cast<QComboBox*>(w)->currentData();
                if (v.typeId() == QMetaType::QString) {
                    QString s = v.toString();
                    if (s.startsWith(QLatin1String("datum:"))) { o[f.key] = QStringLiteral("datum"); o["datum"] = s.mid(6).toDouble(); }
                    else o[f.key] = s;
                } else o[f.key] = v.toDouble();
                break;
            }
            case Field::Vec3: { QJsonArray a; for (int i = 0; i < 3; ++i) a.append(w->findChild<QDoubleSpinBox*>(QStringLiteral("v%1").arg(i))->value()); o[f.key] = a; break; }
            }
        }
        if (type_ == FeatureType::Sketch) o["sketch"] = sketch_.toJson();
        return o;
    }
private:
    FeatureType type_;
    QJsonObject params_;
    std::vector<Field> fields_;
    QMap<QString, QWidget*> editors_;
    sk::Sketch sketch_;
    sk::SketchView* sketchView_ = nullptr;
};
} // namespace

// ---------------------------------------------------------------- страница
ModelPage::ModelPage(Project& project, QWidget* parent) : QWidget(parent), project_(project) {
    const Palette& pal = Theme::current();
    view_ = new View3D;

    // ---- левая колонка: детали, дерево построения, операции, вид
    auto* partsBox = new QWidget; auto* pv = new QVBoxLayout(partsBox); pv->setContentsMargins(0, 0, 0, 0); pv->setSpacing(4);
    partsList_ = new QListWidget; partsList_->setMaximumHeight(96); pv->addWidget(partsList_);
    auto* prow = new QHBoxLayout;
    auto* bAdd = new QToolButton; bAdd->setIcon(Icons::icon("plus", pal.text)); bAdd->setToolTip(QStringLiteral("Новая деталь"));
    auto* bDel = new QToolButton; bDel->setIcon(Icons::icon("minus", pal.text)); bDel->setToolTip(QStringLiteral("Удалить деталь"));
    auto* bRen = new QToolButton; bRen->setText(QStringLiteral("Имя…"));
    prow->addWidget(bAdd); prow->addWidget(bDel); prow->addWidget(bRen); prow->addStretch(1); pv->addLayout(prow);
    connect(bAdd, &QToolButton::clicked, this, [this] { addPart(); });
    connect(bDel, &QToolButton::clicked, this, &ModelPage::removePart);
    connect(bRen, &QToolButton::clicked, this, &ModelPage::renamePart);
    connect(partsList_, &QListWidget::currentRowChanged, this, [this](int row) { if (updating_ || row < 0 || row >= int(parts_.size())) return; selectPart(parts_[size_t(row)].id); });

    auto* treeBox = new QWidget; auto* tv = new QVBoxLayout(treeBox); tv->setContentsMargins(0, 0, 0, 0); tv->setSpacing(4);
    tree_ = new QListWidget; tree_->setDragDropMode(QAbstractItemView::InternalMove); tree_->setDefaultDropAction(Qt::MoveAction); tree_->setMinimumHeight(160);
    tree_->setToolTip(QStringLiteral("Перетащите, чтобы изменить порядок; флажок — подавить; двойной щелчок — параметры"));
    tv->addWidget(tree_, 1);
    auto* trow = new QHBoxLayout;
    auto* bUp = new QToolButton; bUp->setIcon(Icons::icon("up", pal.text)); bUp->setToolTip(QStringLiteral("Выше"));
    auto* bDown = new QToolButton; bDown->setIcon(Icons::icon("down", pal.text)); bDown->setToolTip(QStringLiteral("Ниже"));
    auto* bEdit = new QToolButton; bEdit->setIcon(Icons::icon("props", pal.text)); bEdit->setToolTip(QStringLiteral("Параметры…"));
    auto* bRem = new QToolButton; bRem->setIcon(Icons::icon("delete", pal.text)); bRem->setToolTip(QStringLiteral("Удалить операцию"));
    auto* bRebuild = new QToolButton; bRebuild->setIcon(Icons::icon("rebuild", pal.text)); bRebuild->setToolTip(QStringLiteral("Перестроить всё"));
    for (auto* b : {bUp, bDown, bEdit, bRem, bRebuild}) trow->addWidget(b);
    trow->addStretch(1); tv->addLayout(trow);
    connect(bUp, &QToolButton::clicked, this, [this] { quint64 id = selectedFeature(); md::Part* p = currentPart(); if (p && id) moveFeature(id, p->indexOf(id) - 1); });
    connect(bDown, &QToolButton::clicked, this, [this] { quint64 id = selectedFeature(); md::Part* p = currentPart(); if (p && id) moveFeature(id, p->indexOf(id) + 1); });
    connect(bEdit, &QToolButton::clicked, this, [this] { quint64 id = selectedFeature(); md::Part* p = currentPart(); if (p && id) runFeatureDialog(p->feature(id)->type, id); });
    connect(bRem, &QToolButton::clicked, this, [this] { quint64 id = selectedFeature(); if (id) removeFeature(id); });
    connect(bRebuild, &QToolButton::clicked, this, [this] { if (md::Part* p = currentPart()) { p->setParams(p->features().empty() ? 0 : p->features().front().id, p->features().empty() ? QJsonObject() : p->features().front().params); } rebuild(); refreshTree(); refreshView(); refreshProps(); refreshMass(); });
    connect(tree_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { md::Part* p = currentPart(); quint64 id = it->data(Qt::UserRole).toULongLong(); if (p && p->feature(id)) runFeatureDialog(p->feature(id)->type, id); });
    connect(tree_, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) { if (updating_) return; quint64 id = it->data(Qt::UserRole).toULongLong(); suppressFeature(id, it->checkState() == Qt::Unchecked); });
    connect(tree_, &QListWidget::currentRowChanged, this, [this](int) { if (!updating_) refreshProps(); });
    connect(tree_->model(), &QAbstractItemModel::rowsMoved, this, [this] { if (!updating_) treeReordered(); });

    auto* opsBox = new QWidget; auto* og = new QGridLayout(opsBox); og->setContentsMargins(0, 0, 0, 0); og->setSpacing(4);
    struct Op { FeatureType t; QString tip; };
    const std::vector<Op> ops{{FeatureType::Sketch, QStringLiteral("Эскиз на плоскости или грани")}, {FeatureType::Extrude, QStringLiteral("Выдавить эскиз")}, {FeatureType::Revolve, QStringLiteral("Вращать эскиз вокруг оси")},
                              {FeatureType::Hole, QStringLiteral("Отверстие в выбранной грани")}, {FeatureType::Fillet, QStringLiteral("Скругление рёбер")}, {FeatureType::Chamfer, QStringLiteral("Фаска рёбер")},
                              {FeatureType::LinearPattern, QStringLiteral("Линейный массив операции")}, {FeatureType::CircularPattern, QStringLiteral("Круговой массив операции")}, {FeatureType::Mirror, QStringLiteral("Зеркало операции или тела")},
                              {FeatureType::Boolean, QStringLiteral("Булева операция между телами")}, {FeatureType::DatumPlane, QStringLiteral("Опорная плоскость")}, {FeatureType::DatumAxis, QStringLiteral("Опорная ось")}, {FeatureType::Shell, QStringLiteral("Оболочка (тонкостенное тело)")}};
    int i = 0;
    for (const Op& op : ops) {
        auto* b = new QToolButton; b->setIcon(Icons::icon(md::featureIcon(op.t), pal.text)); b->setIconSize(QSize(20, 20)); b->setFixedSize(34, 34); b->setAutoRaise(true); b->setToolTip(md::featureTypeName(op.t) + QStringLiteral(" — ") + op.tip);
        connect(b, &QToolButton::clicked, this, [this, t = op.t] { runFeatureDialog(t); });
        og->addWidget(b, i / 5, i % 5); ++i;
    }

    auto* viewBox = new QWidget; auto* vg = new QGridLayout(viewBox); vg->setContentsMargins(0, 0, 0, 0); vg->setSpacing(4);
    struct Vw { QString title; View3D::StdView v; };
    const std::vector<Vw> views{{QStringLiteral("Изо"), View3D::StdView::Iso}, {QStringLiteral("Спереди"), View3D::StdView::Front}, {QStringLiteral("Сверху"), View3D::StdView::Top}, {QStringLiteral("Справа"), View3D::StdView::Right},
                                {QStringLiteral("Сзади"), View3D::StdView::Back}, {QStringLiteral("Снизу"), View3D::StdView::Bottom}, {QStringLiteral("Слева"), View3D::StdView::Left}};
    i = 0;
    for (const Vw& vw : views) { auto* b = new QToolButton; b->setText(vw.title); b->setAutoRaise(true); connect(b, &QToolButton::clicked, this, [this, v = vw.v] { view_->setStandardView(v, false); }); vg->addWidget(b, i / 4, i % 4); ++i; }
    auto* bFit = new QToolButton; bFit->setIcon(Icons::icon("zoom-extents", pal.text)); bFit->setAutoRaise(true); bFit->setToolTip(QStringLiteral("Показать всё")); connect(bFit, &QToolButton::clicked, view_, &View3D::zoomExtents); vg->addWidget(bFit, i / 4, i % 4); ++i;
    perspBtn_ = new QToolButton; perspBtn_->setIcon(Icons::icon("persp", pal.text)); perspBtn_->setCheckable(true); perspBtn_->setChecked(true); perspBtn_->setAutoRaise(true); perspBtn_->setToolTip(QStringLiteral("Перспектива / ортогональ"));
    connect(perspBtn_, &QToolButton::toggled, view_, &View3D::setPerspective);
    auto* gridBtn = new QToolButton; gridBtn->setIcon(Icons::icon("grid", pal.text)); gridBtn->setCheckable(true); gridBtn->setChecked(true); gridBtn->setAutoRaise(true); gridBtn->setToolTip(QStringLiteral("Сетка плоскости XY")); connect(gridBtn, &QToolButton::toggled, view_, &View3D::setShowGrid);
    auto* edgeBtn = new QToolButton; edgeBtn->setIcon(Icons::icon("edge", pal.text)); edgeBtn->setCheckable(true); edgeBtn->setChecked(true); edgeBtn->setAutoRaise(true); edgeBtn->setToolTip(QStringLiteral("Рёбра")); connect(edgeBtn, &QToolButton::toggled, view_, &View3D::setShowEdges);
    int row = (i + 3) / 4;
    vg->addWidget(perspBtn_, row, 0); vg->addWidget(gridBtn, row, 1); vg->addWidget(edgeBtn, row, 2);
    struct Pk { QString id, icon, title; View3D::PickMode m; };
    const std::vector<Pk> picks{{"face", "face", QStringLiteral("Выбор граней"), View3D::PickMode::Face}, {"edge", "edge", QStringLiteral("Выбор рёбер"), View3D::PickMode::Edge}, {"vertex", "vertex", QStringLiteral("Выбор вершин"), View3D::PickMode::Vertex}, {"body", "body", QStringLiteral("Выбор тел"), View3D::PickMode::Body}};
    ++row; int col = 0;
    for (const Pk& pk : picks) {
        auto* b = new QToolButton; b->setIcon(Icons::icon(pk.icon, pal.text)); b->setCheckable(true); b->setAutoRaise(true); b->setToolTip(pk.title); b->setChecked(pk.m == View3D::PickMode::Face);
        connect(b, &QToolButton::clicked, this, [this, m = pk.m, id = pk.id] { view_->setPickMode(m); for (auto it = pickBtns_.begin(); it != pickBtns_.end(); ++it) it.value()->setChecked(it.key() == id); });
        vg->addWidget(b, row, col++); pickBtns_[pk.id] = b;
    }

    auto* fileBox = new QWidget; auto* fv = new QVBoxLayout(fileBox); fv->setContentsMargins(0, 0, 0, 0); fv->setSpacing(4);
    auto* bImport = new QPushButton(QStringLiteral("Импорт STEP / STL…")); connect(bImport, &QPushButton::clicked, this, &ModelPage::chooseImport);
    auto* bStep = new QPushButton(QStringLiteral("Экспорт STEP…")); bStep->setObjectName("Accent"); connect(bStep, &QPushButton::clicked, this, [this] { chooseExport(true); });
    auto* bStl = new QPushButton(QStringLiteral("Экспорт STL…")); connect(bStl, &QPushButton::clicked, this, [this] { chooseExport(false); });
    fv->addWidget(bImport); fv->addWidget(bStep); fv->addWidget(bStl);
    auto* hint = new QLabel(QStringLiteral("ЛКМ — выбор и орбита, Shift+ЛКМ / ПКМ — панорама, колесо — зум. Выберите грань, затем «Отверстие» или «Эскиз» — параметры подставятся."));
    hint->setObjectName("Hint"); hint->setWordWrap(true); fv->addWidget(hint);
    QWidget* left = makeSideColumn({makeSection(QStringLiteral("Детали"), partsBox), makeSection(QStringLiteral("Дерево построения"), treeBox), makeSection(QStringLiteral("Операции"), opsBox), makeSection(QStringLiteral("Вид"), viewBox), makeSection(QStringLiteral("Файлы"), fileBox)}, 250);

    // ---- правая колонка: свойства, материал, масса
    auto* propsBox = new QWidget; auto* rv = new QVBoxLayout(propsBox); rv->setContentsMargins(0, 0, 0, 0); rv->setSpacing(4);
    propsHeader_ = new QLabel(QStringLiteral("Выберите операцию в дереве")); propsHeader_->setObjectName("Hint"); propsHeader_->setWordWrap(true); rv->addWidget(propsHeader_);
    errorLbl_ = new QLabel; errorLbl_->setStyleSheet("color: #C0392B;"); errorLbl_->setWordWrap(true); errorLbl_->hide(); rv->addWidget(errorLbl_);
    propsForm_ = new QWidget; props_ = new QFormLayout(propsForm_); props_->setContentsMargins(0, 0, 0, 0); rv->addWidget(propsForm_);
    auto* matBox = new QWidget; auto* mf = new QFormLayout(matBox); mf->setContentsMargins(0, 0, 0, 0);
    matName_ = new QLineEdit; matDensity_ = new QDoubleSpinBox; matDensity_->setDecimals(3); matDensity_->setRange(0.001, 30); matDensity_->setSuffix(QStringLiteral(" г/см³"));
    matColor_ = new QPushButton(QStringLiteral("Цвет…"));
    mf->addRow(QStringLiteral("Материал"), matName_); mf->addRow(QStringLiteral("Плотность"), matDensity_); mf->addRow(QStringLiteral("Цвет"), matColor_);
    auto applyMaterial = [this] {
        md::Part* p = currentPart(); if (!p || updating_) return;
        if (p->material.name == matName_->text() && std::abs(p->material.density - matDensity_->value()) < 1e-9) return;
        beginChange(QStringLiteral("Материал"));
        p->material.name = matName_->text(); p->material.density = matDensity_->value();
        store(); refreshMass(); Q_EMIT modelsChanged();
    };
    connect(matName_, &QLineEdit::editingFinished, this, applyMaterial);
    connect(matDensity_, &QDoubleSpinBox::editingFinished, this, applyMaterial);
    connect(matColor_, &QPushButton::clicked, this, [this] {
        md::Part* p = currentPart(); if (!p) return;
        QColor c = QColorDialog::getColor(QColor(p->material.color), this, QStringLiteral("Цвет детали"));
        if (!c.isValid()) return;
        beginChange(QStringLiteral("Цвет детали")); p->material.color = c.name(); store(); refreshView(); refreshMass();
    });
    massLbl_ = new QLabel; massLbl_->setWordWrap(true); massLbl_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QWidget* right = makeSideColumn({makeSection(QStringLiteral("Свойства"), propsBox), makeSection(QStringLiteral("Материал"), matBox), makeSection(QStringLiteral("Масса и объём"), massLbl_)}, 300);

    // ---- центр
    auto* center = new QWidget; auto* cv = new QVBoxLayout(center); cv->setContentsMargins(0, 0, 0, 0); cv->setSpacing(0);
    cv->addWidget(view_, 1);
    prompt_ = new QLabel(QStringLiteral("Создайте деталь и эскиз, затем выдавите его")); prompt_->setObjectName("Hint"); prompt_->setContentsMargins(10, 4, 10, 4); cv->addWidget(prompt_);
    connect(view_, &View3D::hoverChanged, this, [this](const View3D::Pick& pk) {
        md::Part* p = currentPart();
        if (!pk.valid() || !p || pk.item > p->bodies().size()) { prompt_->setText(view_->selection().valid() ? QStringLiteral("Выбрано: %1").arg(view_->selection().face ? QStringLiteral("грань %1").arg(view_->selection().face) : view_->selection().edge ? QStringLiteral("ребро %1").arg(view_->selection().edge) : QStringLiteral("тело")) : QStringLiteral("Наведите на тело")); return; }
        const QString body = p->bodies()[pk.item - 1].name;
        QString what = pk.face ? QStringLiteral("Грань %1").arg(pk.face) : pk.edge ? QStringLiteral("Ребро %1").arg(pk.edge) : view_->pickMode() == View3D::PickMode::Vertex ? QStringLiteral("Вершина") : QStringLiteral("Тело");
        prompt_->setText(QStringLiteral("%1 · «%2» · %3, %4, %5 мм").arg(what, body).arg(pk.point.x, 0, 'f', 1).arg(pk.point.y, 0, 'f', 1).arg(pk.point.z, 0, 'f', 1));
    });
    connect(view_, &View3D::picked, this, [this](const View3D::Pick& pk) { Q_EMIT statusMessage(pk.valid() ? QStringLiteral("Выбрано: %1").arg(pk.face ? QStringLiteral("грань %1").arg(pk.face) : pk.edge ? QStringLiteral("ребро %1").arg(pk.edge) : QStringLiteral("тело %1").arg(pk.item)) : QStringLiteral("Выбор снят")); });
    connect(view_, &View3D::doubleClicked, this, [this](const View3D::Pick& pk) { if (pk.valid() && pk.face) runFeatureDialog(FeatureType::Hole); });

    auto* h = new QHBoxLayout(this); h->setContentsMargins(0, 0, 0, 0); h->setSpacing(0);
    h->addWidget(left); h->addWidget(center, 1); h->addWidget(right);

    connect(&project_, &Project::stateChanged, this, [this] { if (signature() != loadedSig_) reload(); });
    connect(&project_.document(), &Document::documentReset, this, [this] { reload(); });
    reload();
}

// ---------------------------------------------------------------- хранение
QByteArray ModelPage::signature() const {
    QByteArray sig;
    const auto& parts = project_.foreignParts();
    for (auto it = parts.cbegin(); it != parts.cend(); ++it) if (it.key().startsWith(kPrefix)) { sig += it.key().toUtf8(); sig += it.value(); }
    return sig;
}

void ModelPage::reload() {
    parts_.clear();
    const auto& parts = project_.foreignParts();
    for (auto it = parts.cbegin(); it != parts.cend(); ++it) {
        if (!it.key().startsWith(kPrefix)) continue;
        QJsonParseError err; QJsonDocument d = QJsonDocument::fromJson(it.value(), &err);
        if (err.error != QJsonParseError::NoError || !d.isObject()) continue;
        parts_.push_back(md::Part::fromJson(d.object()));
    }
    std::sort(parts_.begin(), parts_.end(), [](const md::Part& a, const md::Part& b) { return a.id < b.id; });
    for (md::Part& p : parts_) {
        md::RebuildReport r = p.rebuild();
        if (!r.ok()) lastReport_ = r.errors.join(QLatin1Char('\n'));
    }
    loadedSig_ = signature();
    undo_.clear(); Q_EMIT undoChanged();
    if (!currentPart()) currentId_ = parts_.empty() ? 0 : parts_.front().id;
    refreshParts(); refreshTree(); refreshView(); refreshProps(); refreshMass();
    view_->zoomExtents();
    Q_EMIT modelsChanged();
}

void ModelPage::store() {
    auto& parts = project_.foreignParts();
    for (auto it = parts.begin(); it != parts.end();) { if (it.key().startsWith(kPrefix) || it.key().startsWith(kBrepPrefix)) it = parts.erase(it); else ++it; }
    for (const md::Part& p : parts_) {
        parts[kPrefix + QString::number(p.id) + QStringLiteral(".json")] = QJsonDocument(p.toJson()).toJson(QJsonDocument::Compact);
        QByteArray brep = p.brepCache();
        if (!brep.isEmpty()) parts[kBrepPrefix + QString::number(p.id) + QStringLiteral(".brep")] = brep;   // кэш для просмотра без перестроения
    }
    loadedSig_ = signature();
    project_.markDirty();
    Q_EMIT modelsChanged();
}

QByteArray ModelPage::snapshot() const {
    QJsonArray arr; for (const md::Part& p : parts_) arr.push_back(p.toJson());
    QJsonObject o; o["current"] = double(currentId_); o["parts"] = arr;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

void ModelPage::restore(const QByteArray& data) {
    QJsonObject o = QJsonDocument::fromJson(data).object();
    parts_.clear();
    for (const QJsonValue& v : o["parts"].toArray()) parts_.push_back(md::Part::fromJson(v.toObject()));
    for (md::Part& p : parts_) p.rebuild();
    currentId_ = quint64(o["current"].toDouble());
    if (!currentPart()) currentId_ = parts_.empty() ? 0 : parts_.front().id;
    store();
    refreshParts(); refreshTree(); refreshView(); refreshProps(); refreshMass();
}

void ModelPage::undo() { if (!undo_.canUndo()) return; restore(undo_.undo(snapshot())); Q_EMIT undoChanged(); }
void ModelPage::redo() { if (!undo_.canRedo()) return; restore(undo_.redo(snapshot())); Q_EMIT undoChanged(); }
void ModelPage::beginChange(const QString& text) { undo_.push(text, snapshot()); Q_EMIT undoChanged(); }

void ModelPage::afterChange() {
    md::RebuildReport r = rebuild();
    store();
    refreshTree(); refreshView(); refreshProps(); refreshMass();
    if (!r.ok()) Q_EMIT statusMessage(QStringLiteral("Ошибка построения: %1").arg(r.errors.join(QStringLiteral("; "))));
}

void ModelPage::activate() { if (signature() != loadedSig_) reload(); view_->setFocus(); if (parts_.empty()) prompt_->setText(QStringLiteral("Нажмите «+» в разделе «Детали», затем «Эскиз» и «Выдавливание»")); }

// ---------------------------------------------------------------- детали
md::Part* ModelPage::currentPart() { for (md::Part& p : parts_) if (p.id == currentId_) return &p; return nullptr; }

md::Part* ModelPage::addPart(const QString& name) {
    beginChange(QStringLiteral("Новая деталь"));
    quint64 id = 1; for (const md::Part& p : parts_) id = std::max(id, p.id + 1);
    md::Part p; p.id = id; p.name = name.isEmpty() ? QStringLiteral("Деталь %1").arg(id) : name;
    parts_.push_back(p);
    currentId_ = id;
    store();
    refreshParts(); refreshTree(); refreshView(); refreshProps(); refreshMass();
    Q_EMIT statusMessage(QStringLiteral("Создана %1").arg(p.name));
    return currentPart();
}

void ModelPage::selectPart(quint64 id) {
    currentId_ = id;
    refreshParts(); refreshTree(); refreshView(); refreshProps(); refreshMass();
    view_->zoomExtents();
}

void ModelPage::removePart() {
    md::Part* p = currentPart(); if (!p) return;
    if (QMessageBox::question(this, QStringLiteral("Удалить деталь"), QStringLiteral("Удалить «%1» со всеми операциями?").arg(p->name)) != QMessageBox::Yes) return;
    beginChange(QStringLiteral("Удалить деталь"));
    parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [&](const md::Part& q) { return q.id == currentId_; }), parts_.end());
    currentId_ = parts_.empty() ? 0 : parts_.front().id;
    store(); refreshParts(); refreshTree(); refreshView(); refreshProps(); refreshMass();
}

void ModelPage::renamePart() {
    md::Part* p = currentPart(); if (!p) return;
    bool ok; QString n = QInputDialog::getText(this, QStringLiteral("Имя детали"), QStringLiteral("Имя:"), QLineEdit::Normal, p->name, &ok);
    if (!ok || n.trimmed().isEmpty()) return;
    beginChange(QStringLiteral("Имя детали")); p->name = n.trimmed(); store(); refreshParts();
}

// ---------------------------------------------------------------- операции
quint64 ModelPage::addFeature(FeatureType type, const QJsonObject& params, const QString& name) {
    md::Part* p = currentPart();
    if (!p) p = addPart();
    beginChange(md::featureTypeName(type));
    quint64 id = p->addFeature(type, params, name);
    afterChange();
    if (p->feature(id) && p->feature(id)->ok()) Q_EMIT statusMessage(QStringLiteral("%1: %2").arg(p->feature(id)->name, p->shape().describe()));
    return id;
}

bool ModelPage::editFeature(quint64 id, const QJsonObject& params) {
    md::Part* p = currentPart(); if (!p || !p->feature(id)) return false;
    beginChange(QStringLiteral("Параметры: %1").arg(p->feature(id)->name));
    p->setParams(id, params);
    afterChange();
    return true;
}

bool ModelPage::removeFeature(quint64 id) {
    md::Part* p = currentPart(); if (!p || !p->feature(id)) return false;
    beginChange(QStringLiteral("Удалить %1").arg(p->feature(id)->name));
    p->removeFeature(id);
    afterChange();
    return true;
}

bool ModelPage::suppressFeature(quint64 id, bool on) {
    md::Part* p = currentPart(); if (!p || !p->feature(id) || p->feature(id)->suppressed == on) return false;
    beginChange(on ? QStringLiteral("Подавить %1").arg(p->feature(id)->name) : QStringLiteral("Включить %1").arg(p->feature(id)->name));
    p->setSuppressed(id, on);
    afterChange();
    return true;
}

bool ModelPage::moveFeature(quint64 id, int newIndex) {
    md::Part* p = currentPart(); if (!p || !p->feature(id) || newIndex < 0 || newIndex >= int(p->features().size())) return false;
    beginChange(QStringLiteral("Порядок операций"));
    p->moveFeature(id, newIndex);
    afterChange();
    for (int i = 0; i < tree_->count(); ++i) if (tree_->item(i)->data(Qt::UserRole).toULongLong() == id) tree_->setCurrentRow(i);
    return true;
}

md::RebuildReport ModelPage::rebuild() {
    md::Part* p = currentPart();
    if (!p) return {};
    md::RebuildReport r = p->rebuild();
    lastReport_ = r.ok() ? QStringLiteral("Перестроено операций: %1").arg(r.rebuilt) : r.errors.join(QLatin1Char('\n'));
    return r;
}

void ModelPage::treeReordered() {
    md::Part* p = currentPart(); if (!p) return;
    std::vector<quint64> order;
    for (int i = 0; i < tree_->count(); ++i) order.push_back(tree_->item(i)->data(Qt::UserRole).toULongLong());
    beginChange(QStringLiteral("Порядок операций"));
    for (size_t i = 0; i < order.size(); ++i) if (p->indexOf(order[i]) != int(i)) p->moveFeature(order[i], int(i));
    afterChange();
}

quint64 ModelPage::selectedFeature() const { QListWidgetItem* it = tree_->currentItem(); return it ? it->data(Qt::UserRole).toULongLong() : 0; }

void ModelPage::runFeatureDialog(FeatureType type, quint64 editId) {
    md::Part* p = currentPart();
    if (!p) p = addPart();
    QJsonObject params;
    int before = -1;
    if (editId) { if (const md::Feature* f = p->feature(editId)) { params = f->params; before = p->indexOf(editId); } }
    else {
        // подстановка из выбора в 3D-виде
        const View3D::Pick sel = view_->selection();
        const int bodyIdx = sel.valid() && sel.item <= p->bodies().size() ? int(sel.item) - 1 : -1;
        const quint64 lastId = p->features().empty() ? 0 : p->features().back().id;
        if (type == FeatureType::Hole && sel.valid() && sel.face) { params["point"] = QJsonArray{sel.point.x, sel.point.y, sel.point.z}; params["dir"] = QJsonArray{sel.normal.x, sel.normal.y, sel.normal.z}; params["faceIndex"] = sel.face; params["body"] = bodyIdx; }
        else if (type == FeatureType::Hole && !p->bodies().empty()) { md::Bbox b = p->bodies().back().shape.bbox(); params["point"] = QJsonArray{b.center().x, b.center().y, b.max.z}; params["dir"] = QJsonArray{0, 0, 1}; }
        if ((type == FeatureType::Sketch || type == FeatureType::DatumPlane) && sel.valid() && sel.face) { params[type == FeatureType::Sketch ? "plane" : "base"] = QStringLiteral("face"); params["faceFeature"] = double(lastId); params["faceIndex"] = sel.face; }
        if ((type == FeatureType::Fillet || type == FeatureType::Chamfer) && sel.valid()) { if (sel.edge) params["edges"] = QJsonArray{sel.edge}; params["body"] = bodyIdx; }
        if (type == FeatureType::Shell && sel.valid() && sel.face) { params["faces"] = QJsonArray{sel.face}; params["body"] = bodyIdx; }
        if ((type == FeatureType::Extrude || type == FeatureType::Revolve) && bodyIdx >= 0) params["body"] = bodyIdx;
        if (type == FeatureType::Extrude || type == FeatureType::Revolve) { bool any = false; for (const md::Feature& f : p->features()) if (f.type == FeatureType::Sketch) any = true; if (!any) { QMessageBox::information(this, md::featureTypeName(type), QStringLiteral("Сначала создайте эскиз с замкнутым контуром.")); return; } if (p->bodies().empty()) params["mode"] = QStringLiteral("new"); }
        if ((type == FeatureType::LinearPattern || type == FeatureType::CircularPattern) && toolChoices(*p, -1).empty()) { QMessageBox::information(this, md::featureTypeName(type), QStringLiteral("Массив повторяет выдавливание, вращение или отверстие — сначала создайте одну из них.")); return; }
        if (type == FeatureType::Boolean && p->bodies().size() < 2) { QMessageBox::information(this, md::featureTypeName(type), QStringLiteral("Нужны два тела: выдавите второй эскиз в режиме «новое тело».")); return; }
        if (type != FeatureType::Sketch && type != FeatureType::DatumPlane && type != FeatureType::DatumAxis && type != FeatureType::Extrude && type != FeatureType::Revolve && p->bodies().empty()) { QMessageBox::information(this, md::featureTypeName(type), QStringLiteral("Сначала создайте тело: эскиз и выдавливание.")); return; }
    }
    FeatureDialog dlg(type, *p, before, params, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QJsonObject result = dlg.result();
    if (type == FeatureType::Sketch) {
        // параметры грани сохраняем, если выбрана «грань»
        if (result["plane"].toString() == QLatin1String("face") && !result.contains("faceFeature")) { QMessageBox::information(this, QStringLiteral("Эскиз"), QStringLiteral("Для плоскости «выбранная грань» выберите грань в 3D-виде перед созданием эскиза.")); return; }
    }
    if (editId) editFeature(editId, result); else addFeature(type, result);
    view_->clearSelection();
}

// ---------------------------------------------------------------- файлы
bool ModelPage::exportStep(const QString& path, QString* error) {
    md::Part* p = currentPart(); if (!p) { if (error) *error = QStringLiteral("Нет детали"); return false; }
    std::vector<StepBody> bodies;
    for (const md::Body& b : p->bodies()) bodies.push_back({b.name.isEmpty() ? p->name : b.name, b.shape});
    StepReport rep;
    bool ok = Step::write(path, bodies, error, &rep);
    lastReport_ = rep.summary();
    if (ok) { Q_EMIT exported(rep.bodies); Q_EMIT statusMessage(QStringLiteral("STEP: %1 тел → %2").arg(rep.bodies).arg(QFileInfo(path).fileName())); }
    return ok;
}

bool ModelPage::exportStl(const QString& path, bool binary, QString* error) {
    md::Part* p = currentPart(); if (!p) { if (error) *error = QStringLiteral("Нет детали"); return false; }
    std::vector<md::Shape> shapes; for (const md::Body& b : p->bodies()) shapes.push_back(b.shape);
    StlOptions opt; opt.binary = binary;
    StlReport rep;
    bool ok = Stl::write(path, shapes, opt, error, &rep);
    lastReport_ = rep.summary();
    if (ok) { Q_EMIT exported(int(shapes.size())); Q_EMIT statusMessage(QStringLiteral("STL: %1 треугольников → %2").arg(rep.triangles).arg(QFileInfo(path).fileName())); }
    return ok;
}

bool ModelPage::importFile(const QString& path, QString* report, QString* error) {
    const QString ext = QFileInfo(path).suffix().toLower();
    md::Part* p = currentPart();
    if (!p) p = addPart(QFileInfo(path).completeBaseName());
    if (ext == QLatin1String("stl")) {
        StlReport rep; QString err;
        StlMesh m = Stl::read(path, &rep, &err);
        if (report) *report = rep.summary();
        if (m.indices.empty()) { if (error) *error = err; return false; }
        addFeature(FeatureType::MeshBody, {{"name", QFileInfo(path).completeBaseName()}, {"mesh", QString::fromLatin1(Stl::encodeMesh(m).toBase64())}}, QStringLiteral("STL: %1").arg(QFileInfo(path).fileName()));
        lastReport_ = rep.summary();
        view_->zoomExtents();
        return true;
    }
    if (ext == QLatin1String("step") || ext == QLatin1String("stp")) {
        StepReport rep; QString err;
        auto bodies = Step::read(path, &rep, &err);
        if (report) *report = rep.summary();
        if (bodies.empty()) { if (error) *error = err; return false; }
        beginChange(QStringLiteral("Импорт STEP"));
        for (const StepBody& b : bodies) p->addFeature(FeatureType::ImportBody, {{"name", b.name}, {"brep", QString::fromLatin1(b.shape.toBrep().toBase64())}}, QStringLiteral("STEP: %1").arg(b.name));
        afterChange();
        lastReport_ = rep.summary();
        view_->zoomExtents();
        return true;
    }
    if (error) *error = QStringLiteral("Неизвестный формат: %1").arg(ext);
    return false;
}

void ModelPage::chooseExport(bool step) {
    md::Part* p = currentPart();
    if (!p || p->bodies().empty()) { QMessageBox::information(this, QStringLiteral("Экспорт"), QStringLiteral("Нет тел для экспорта.")); return; }
    QString path = QFileDialog::getSaveFileName(this, step ? QStringLiteral("Экспорт STEP") : QStringLiteral("Экспорт STL"), p->name + (step ? QStringLiteral(".step") : QStringLiteral(".stl")), step ? QStringLiteral("STEP (*.step *.stp)") : QStringLiteral("STL (*.stl)"));
    if (path.isEmpty()) return;
    QString err;
    bool ok = step ? exportStep(path, &err) : exportStl(path, true, &err);
    if (!ok) QMessageBox::warning(this, QStringLiteral("Экспорт"), err);
    else QMessageBox::information(this, QStringLiteral("Экспорт выполнен"), lastReport_);
}

void ModelPage::chooseImport() {
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Импорт модели"), QString(), QStringLiteral("STEP и STL (*.step *.stp *.stl);;STEP (*.step *.stp);;STL (*.stl)"));
    if (path.isEmpty()) return;
    QString rep, err;
    if (!importFile(path, &rep, &err)) QMessageBox::warning(this, QStringLiteral("Импорт"), err + (rep.isEmpty() ? QString() : QStringLiteral("\n\n") + rep));
    else QMessageBox::information(this, QStringLiteral("Импорт выполнен"), rep);
}

// ---------------------------------------------------------------- обновление панелей
void ModelPage::refreshParts() {
    updating_ = true;
    partsList_->clear();
    int cur = -1;
    for (size_t i = 0; i < parts_.size(); ++i) {
        auto* it = new QListWidgetItem(QStringLiteral("%1  (операций: %2, тел: %3)").arg(parts_[i].name).arg(parts_[i].features().size()).arg(parts_[i].bodies().size()));
        it->setIcon(Icons::icon("model", Theme::current().text));
        partsList_->addItem(it);
        if (parts_[i].id == currentId_) cur = int(i);
    }
    partsList_->setCurrentRow(cur);
    md::Part* p = currentPart();
    matName_->setText(p ? p->material.name : QString()); matDensity_->setValue(p ? p->material.density : 7.85);
    matName_->setEnabled(p); matDensity_->setEnabled(p); matColor_->setEnabled(p);
    if (p) matColor_->setStyleSheet(QStringLiteral("QPushButton { border-left: 10px solid %1; }").arg(p->material.color));
    updating_ = false;
}

void ModelPage::refreshTree() {
    updating_ = true;
    quint64 sel = selectedFeature();
    tree_->clear();
    const Palette& pal = Theme::current();
    if (md::Part* p = currentPart()) {
        for (const md::Feature& f : p->features()) {
            auto* it = new QListWidgetItem(f.name);
            it->setData(Qt::UserRole, double(f.id));
            it->setIcon(Icons::icon(md::featureIcon(f.type), f.ok() ? pal.text : QColor(0xC0, 0x39, 0x2B)));
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
            it->setCheckState(f.suppressed ? Qt::Unchecked : Qt::Checked);
            if (!f.ok()) { it->setForeground(QColor(0xC0, 0x39, 0x2B)); it->setToolTip(f.error); }
            else if (f.suppressed) it->setForeground(pal.faint);
            else it->setToolTip(md::featureTypeName(f.type));
            tree_->addItem(it);
            if (f.id == sel) tree_->setCurrentItem(it);
        }
    }
    updating_ = false;
}

void ModelPage::refreshView() {
    std::vector<View3DItem> items;
    if (md::Part* p = currentPart()) {
        for (size_t i = 0; i < p->bodies().size(); ++i) {
            const md::Body& b = p->bodies()[i];
            md::Bbox bb = b.shape.bbox();
            double defl = bb.valid ? std::clamp(bb.size().length() / 1500.0, 0.02, 2.0) : 0.2;
            items.push_back({quint64(i + 1), b.shape.mesh(defl, 0.3), QColor(p->material.color), b.name});
        }
    }
    bool wasEmpty = view_->items().empty();
    view_->setItems(std::move(items));
    if (wasEmpty && !view_->items().empty()) view_->zoomExtents();
}

void ModelPage::refreshProps() {
    // форма параметров выбранной операции: числа редактируются на месте (Enter — применить)
    while (props_->rowCount() > 0) props_->removeRow(0);
    md::Part* p = currentPart();
    quint64 id = selectedFeature();
    const md::Feature* f = p ? p->feature(id) : nullptr;
    if (!f) { propsHeader_->setText(p ? QStringLiteral("Выберите операцию в дереве") : QStringLiteral("Нет детали")); errorLbl_->hide(); return; }
    propsHeader_->setText(QStringLiteral("%1 · %2%3").arg(f->name, md::featureTypeName(f->type), f->suppressed ? QStringLiteral(" · подавлена") : QString()));
    errorLbl_->setVisible(!f->ok()); errorLbl_->setText(f->error);
    auto* nameEd = new QLineEdit(f->name);
    connect(nameEd, &QLineEdit::editingFinished, this, [this, id, nameEd] { md::Part* q = currentPart(); if (!q || !q->feature(id) || q->feature(id)->name == nameEd->text()) return; beginChange(QStringLiteral("Имя операции")); q->rename(id, nameEd->text()); store(); refreshTree(); refreshProps(); });
    props_->addRow(QStringLiteral("Имя"), nameEd);
    for (const Field& fld : schemaFor(f->type, *p, p->indexOf(id))) {
        if (!f->params.contains(fld.key)) continue;
        const QJsonValue v = f->params[fld.key];
        if (fld.kind == Field::Double || fld.kind == Field::Int) {
            auto* s = new QDoubleSpinBox; s->setDecimals(fld.kind == Field::Int ? 0 : 3); s->setRange(fld.min, fld.max); s->setValue(v.toDouble()); if (!fld.suffix.isEmpty()) s->setSuffix(QStringLiteral(" ") + fld.suffix);
            connect(s, &QDoubleSpinBox::editingFinished, this, [this, id, key = fld.key, isInt = fld.kind == Field::Int, s] {
                md::Part* q = currentPart(); if (!q || !q->feature(id)) return;
                QJsonObject prm = q->feature(id)->params;
                if (std::abs(prm[key].toDouble() - s->value()) < 1e-12) return;
                if (isInt) prm[key] = int(s->value()); else prm[key] = s->value();
                editFeature(id, prm);
            });
            props_->addRow(fld.label, s);
        } else if (fld.kind == Field::Bool) {
            auto* c = new QCheckBox; c->setChecked(v.toBool());
            connect(c, &QCheckBox::toggled, this, [this, id, key = fld.key](bool on) { md::Part* q = currentPart(); if (!q || !q->feature(id)) return; QJsonObject prm = q->feature(id)->params; prm[key] = on; editFeature(id, prm); });
            props_->addRow(fld.label, c);
        } else {
            QString text;
            if (fld.kind == Field::Vec3) { QJsonArray a = v.toArray(); text = a.size() == 3 ? QStringLiteral("%1, %2, %3").arg(a[0].toDouble()).arg(a[1].toDouble()).arg(a[2].toDouble()) : QString(); }
            else if (v.isArray()) text = arrayToInts(v).isEmpty() ? QStringLiteral("все") : arrayToInts(v);
            else if (fld.kind == Field::Choice) { text = v.isString() ? v.toString() : QString::number(v.toDouble()); for (const Choice& c : fld.choices) { QString k = c.value.isString() ? c.value.toString() : QString::number(c.value.toDouble()); if (k == text) text = c.label; } if (text == QLatin1String("datum")) text = QStringLiteral("опорный элемент"); }
            else text = v.toString();
            auto* l = new QLabel(text); l->setObjectName("Hint"); l->setWordWrap(true);
            props_->addRow(fld.label, l);
        }
    }
    auto* bEdit = new QPushButton(QStringLiteral("Все параметры…"));
    connect(bEdit, &QPushButton::clicked, this, [this, id, t = f->type] { runFeatureDialog(t, id); });
    props_->addRow(QString(), bEdit);
}

void ModelPage::refreshMass() {
    md::Part* p = currentPart();
    if (!p || p->bodies().empty()) { massLbl_->setText(QStringLiteral("Нет тел")); return; }
    md::MassProps m = p->massProps();
    double mass = p->mass();
    QString massText = mass >= 1000 ? QStringLiteral("%1 кг").arg(mass / 1000, 0, 'f', 3) : QStringLiteral("%1 г").arg(mass, 0, 'f', 2);
    massLbl_->setText(QStringLiteral("Тел: %1\nОбъём: %2 см³\nПлощадь: %3 см²\nМасса: %4 (%5, %6 г/см³)\nЦентр масс: %7, %8, %9 мм\n%10")
                          .arg(p->bodies().size()).arg(m.volume / 1000, 0, 'f', 3).arg(m.area / 100, 0, 'f', 2).arg(massText, p->material.name).arg(p->material.density)
                          .arg(m.centroid.x, 0, 'f', 2).arg(m.centroid.y, 0, 'f', 2).arg(m.centroid.z, 0, 'f', 2).arg(p->shape().describe()));
}

} // namespace psi
