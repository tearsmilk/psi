#include "sketch_page.h"
#include "core/units/units.h"
#include "modules/sketcher/sketch_view.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace psi {

static const QString kPrefix = QStringLiteral("sketches/");

SketchPage::SketchPage(Project& project, QWidget* parent) : QWidget(parent), project_(project) {
    const Palette& pal = Theme::current();
    view_ = new sk::SketchView;

    // ---- левая колонка: эскизы, инструменты, ограничения
    auto* listBox = new QWidget; auto* lv = new QVBoxLayout(listBox); lv->setContentsMargins(0, 0, 0, 0); lv->setSpacing(4);
    list_ = new QListWidget; list_->setMaximumHeight(140); lv->addWidget(list_);
    auto* lrow = new QHBoxLayout;
    auto* bAdd = new QToolButton; bAdd->setIcon(Icons::icon("plus", pal.text)); bAdd->setToolTip(QStringLiteral("Новый эскиз"));
    auto* bDel = new QToolButton; bDel->setIcon(Icons::icon("minus", pal.text)); bDel->setToolTip(QStringLiteral("Удалить эскиз"));
    auto* bRen = new QToolButton; bRen->setText(QStringLiteral("Имя…")); bRen->setToolTip(QStringLiteral("Переименовать"));
    lrow->addWidget(bAdd); lrow->addWidget(bDel); lrow->addWidget(bRen); lrow->addStretch(1); lv->addLayout(lrow);
    connect(bAdd, &QToolButton::clicked, this, [this] { addSketch(); });
    connect(bDel, &QToolButton::clicked, this, &SketchPage::removeSketch);
    connect(bRen, &QToolButton::clicked, this, &SketchPage::renameSketch);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) { if (updating_ || row < 0 || row >= int(sketches_.size())) return; selectSketch(sketches_[size_t(row)].id); });

    auto* toolsBox = new QWidget; auto* tg = new QGridLayout(toolsBox); tg->setContentsMargins(0, 0, 0, 0); tg->setSpacing(4);
    struct T { QString id, icon, title; sk::SketchView::Tool tool; };
    const std::vector<T> tools{{"select", "select", QStringLiteral("Выбор (Esc)"), sk::SketchView::Tool::Select}, {"point", "snap", QStringLiteral("Точка"), sk::SketchView::Tool::Point},
                               {"line", "line", QStringLiteral("Отрезок"), sk::SketchView::Tool::Line}, {"polyline", "polyline", QStringLiteral("Полилиния"), sk::SketchView::Tool::Polyline},
                               {"rect", "rect", QStringLiteral("Прямоугольник (авто H/V)"), sk::SketchView::Tool::Rectangle}, {"circle", "circle", QStringLiteral("Окружность"), sk::SketchView::Tool::Circle},
                               {"arc", "arc", QStringLiteral("Дуга: центр, начало, конец"), sk::SketchView::Tool::Arc}};
    int i = 0;
    for (const T& t : tools) {
        auto* b = new QToolButton; b->setIcon(Icons::icon(t.icon, pal.text)); b->setIconSize(QSize(20, 20)); b->setFixedSize(34, 34); b->setCheckable(true); b->setAutoRaise(true); b->setToolTip(t.title);
        connect(b, &QToolButton::clicked, this, [this, tool = t.tool] { view_->setTool(tool); view_->setFocus(); });
        tg->addWidget(b, i / 4, i % 4); ++i; toolBtns_[t.id] = b;
    }
    connect(view_, &sk::SketchView::toolChanged, this, [this, tools] { for (const T& t : tools) toolBtns_[t.id]->setChecked(view_->tool() == t.tool); });
    toolBtns_["select"]->setChecked(true);

    auto* consBox = new QWidget; auto* cg = new QGridLayout(consBox); cg->setContentsMargins(0, 0, 0, 0); cg->setSpacing(4);
    struct C { sk::CType type; QString key; };
    const std::vector<C> ctypes{{sk::CType::Coincident, "C"}, {sk::CType::Horizontal, "H"}, {sk::CType::Vertical, "V"}, {sk::CType::Parallel, "P"},
                                {sk::CType::Perpendicular, "N"}, {sk::CType::Tangent, "T"}, {sk::CType::Equal, "="}, {sk::CType::Fixed, "F"},
                                {sk::CType::Distance, "D"}, {sk::CType::Length, "L"}, {sk::CType::Radius, ""}, {sk::CType::Diameter, ""},
                                {sk::CType::Angle, ""}, {sk::CType::PointOnLine, ""}, {sk::CType::PointOnCircle, ""}, {sk::CType::Symmetric, ""}};
    i = 0;
    for (const C& c : ctypes) {
        auto* b = new QToolButton; b->setText(sk::constraintGlyph(c.type)); b->setFixedSize(34, 34); b->setAutoRaise(true);
        QFont f = b->font(); f.setPointSizeF(12); b->setFont(f);
        b->setToolTip(sk::constraintTypeName(c.type) + (c.key.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(c.key)));
        connect(b, &QToolButton::clicked, this, [this, t = c.type] { view_->addConstraint(t); view_->setFocus(); });
        cg->addWidget(b, i / 4, i % 4); ++i;
    }
    auto* optBox = new QWidget; auto* ov = new QVBoxLayout(optBox); ov->setContentsMargins(0, 0, 0, 0); ov->setSpacing(4);
    autoCb_ = new QCheckBox(QStringLiteral("Автопривязки")); autoCb_->setChecked(true); autoCb_->setToolTip(QStringLiteral("Совпадение концов и горизонталь/вертикаль при рисовании"));
    gridCb_ = new QCheckBox(QStringLiteral("Магнит к сетке")); gridCb_->setChecked(true);
    connect(autoCb_, &QCheckBox::toggled, view_, &sk::SketchView::setAutoConstrain);
    connect(gridCb_, &QCheckBox::toggled, view_, &sk::SketchView::setGridSnap);
    ov->addWidget(autoCb_); ov->addWidget(gridCb_);
    auto* bExport = new QPushButton(QStringLiteral("Выдать в чертёж")); bExport->setObjectName("Accent");
    connect(bExport, &QPushButton::clicked, this, [this] { exportToDraft(); });
    auto* bZoom = new QPushButton(QStringLiteral("Показать всё"));
    connect(bZoom, &QPushButton::clicked, view_, &sk::SketchView::zoomExtents);
    ov->addWidget(bZoom); ov->addWidget(bExport);
    auto* hint = new QLabel(QStringLiteral("Выделите объекты и нажмите ограничение. Двойной клик по размеру — изменить значение. Чёрное — закреплено, терракотовое — свободно."));
    hint->setObjectName("Hint"); hint->setWordWrap(true); ov->addWidget(hint);
    QWidget* left = makeSideColumn({makeSection(QStringLiteral("Эскизы"), listBox), makeSection(QStringLiteral("Инструменты"), toolsBox), makeSection(QStringLiteral("Ограничения"), consBox), makeSection(QStringLiteral("Параметры"), optBox)}, 230);

    // ---- правая колонка: ограничения и свойства
    auto* consList = new QWidget; auto* cv = new QVBoxLayout(consList); cv->setContentsMargins(0, 0, 0, 0); cv->setSpacing(4);
    dofLbl_ = new QLabel; dofLbl_->setObjectName("Hint"); cv->addWidget(dofLbl_);
    conflictLbl_ = new QLabel; conflictLbl_->setStyleSheet("color: #C0392B;"); conflictLbl_->setWordWrap(true); conflictLbl_->hide(); cv->addWidget(conflictLbl_);
    cons_ = new QTableWidget(0, 3); cons_->setHorizontalHeaderLabels({QStringLiteral("Тип"), QStringLiteral("Объекты"), QStringLiteral("Значение")});
    cons_->horizontalHeader()->setStretchLastSection(true); cons_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch); cons_->verticalHeader()->hide();
    cons_->setSelectionBehavior(QAbstractItemView::SelectRows); cons_->setSelectionMode(QAbstractItemView::SingleSelection); cons_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cons_->setMinimumHeight(180);
    cv->addWidget(cons_, 1);
    auto* crow = new QHBoxLayout;
    auto* bDelC = new QToolButton; bDelC->setIcon(Icons::icon("delete", pal.text)); bDelC->setToolTip(QStringLiteral("Удалить ограничение"));
    crow->addWidget(bDelC); crow->addStretch(1); cv->addLayout(crow);
    connect(cons_, &QTableWidget::itemSelectionChanged, this, [this] {
        if (updating_) return;
        int row = cons_->currentRow();
        if (row < 0 || !currentSketch()) return;
        quint64 id = cons_->item(row, 0)->data(Qt::UserRole).toULongLong();
        if (const sk::Constraint* c = currentSketch()->constraint(id)) { view_->select(c->refs); view_->selectConstraint(id); }
    });
    connect(cons_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { quint64 id = cons_->item(row, 0)->data(Qt::UserRole).toULongLong(); view_->editConstraintValue(id); });
    connect(bDelC, &QToolButton::clicked, this, [this] {
        int row = cons_->currentRow();
        if (row < 0 || !currentSketch()) return;
        quint64 id = cons_->item(row, 0)->data(Qt::UserRole).toULongLong();
        view_->select({}); view_->selectConstraint(id); view_->deleteSelection();
    });

    propsBox_ = new QWidget; auto* pv = new QVBoxLayout(propsBox_); pv->setContentsMargins(0, 0, 0, 0); pv->setSpacing(4);
    propsHeader_ = new QLabel(QStringLiteral("Ничего не выбрано")); propsHeader_->setObjectName("Hint"); pv->addWidget(propsHeader_);
    auto* formW = new QWidget; props_ = new QFormLayout(formW); props_->setContentsMargins(0, 0, 0, 0); pv->addWidget(formW);
    QWidget* right = makeSideColumn({makeSection(QStringLiteral("Ограничения"), consList), makeSection(QStringLiteral("Свойства"), propsBox_)}, 300);

    // ---- центр
    auto* center = new QWidget; auto* cvl = new QVBoxLayout(center); cvl->setContentsMargins(0, 0, 0, 0); cvl->setSpacing(0);
    cvl->addWidget(view_, 1);
    prompt_ = new QLabel; prompt_->setObjectName("Hint"); prompt_->setContentsMargins(10, 4, 10, 4); cvl->addWidget(prompt_);
    connect(view_, &sk::SketchView::promptChanged, prompt_, &QLabel::setText);

    auto* h = new QHBoxLayout(this); h->setContentsMargins(0, 0, 0, 0); h->setSpacing(0);
    h->addWidget(left); h->addWidget(center, 1); h->addWidget(right);

    // ---- модель
    connect(view_, &sk::SketchView::aboutToChange, this, [this](const QString& what) { undo_.push(what, snapshot()); Q_EMIT undoChanged(); });
    connect(view_, &sk::SketchView::changed, this, [this] { store(); refreshConstraints(); refreshProperties(); });
    connect(view_, &sk::SketchView::selectionChanged, this, &SketchPage::refreshProperties);
    connect(view_, &sk::SketchView::solved, this, [this] {
        const sk::SolveResult& r = view_->lastResult();
        dofLbl_->setText(r.dof >= 0 ? QStringLiteral("Свободных степеней: %1").arg(r.dof) : QString());
        if (!r.conflicts.empty()) { conflictLbl_->setText(QStringLiteral("Конфликт: %1 ограничений не выполняются (выделены красным)").arg(r.conflicts.size())); conflictLbl_->show(); }
        else conflictLbl_->hide();
        Q_EMIT dofChanged(r.dof);
        refreshConstraints();
    });
    connect(&project_.document(), &Document::documentReset, this, [this] { QTimer::singleShot(0, this, [this] { reload(); }); });
    connect(&project_, &Project::stateChanged, this, [this] { if (signature() != loadedSig_) reload(); });
    reload();
}

// ---------------------------------------------------------------- модель
sk::Sketch* SketchPage::currentSketch() { for (auto& s : sketches_) if (s.id == currentId_) return &s; return nullptr; }
int SketchPage::dof() const { return view_->lastResult().dof; }

QByteArray SketchPage::signature() const {
    QByteArray sig;
    const auto& parts = project_.foreignParts();
    for (auto it = parts.cbegin(); it != parts.cend(); ++it) if (it.key().startsWith(kPrefix)) { sig += it.key().toUtf8(); sig += ':'; sig += QByteArray::number(qHash(it.value())); sig += ';'; }
    return sig;
}

void SketchPage::reload() {
    sketches_.clear();
    const auto& parts = project_.foreignParts();
    for (auto it = parts.cbegin(); it != parts.cend(); ++it) {
        if (!it.key().startsWith(kPrefix)) continue;
        QJsonParseError err; QJsonDocument d = QJsonDocument::fromJson(it.value(), &err);
        if (err.error != QJsonParseError::NoError || !d.isObject()) continue;
        sketches_.push_back(sk::Sketch::fromJson(d.object()));
    }
    std::sort(sketches_.begin(), sketches_.end(), [](const sk::Sketch& a, const sk::Sketch& b) { return a.id < b.id; });
    loadedSig_ = signature();
    undo_.clear(); Q_EMIT undoChanged();
    if (!currentSketch()) currentId_ = sketches_.empty() ? 0 : sketches_.front().id;
    view_->setSketch(currentSketch());
    refreshList(); refreshConstraints(); refreshProperties();
    Q_EMIT sketchesChanged();
}

void SketchPage::store() {
    auto& parts = project_.foreignParts();
    for (auto it = parts.begin(); it != parts.end();) { if (it.key().startsWith(kPrefix)) it = parts.erase(it); else ++it; }
    for (const sk::Sketch& s : sketches_) parts[kPrefix + QString::number(s.id) + QStringLiteral(".json")] = QJsonDocument(s.toJson()).toJson(QJsonDocument::Compact);
    loadedSig_ = signature();
    project_.markDirty();
    Q_EMIT sketchesChanged();
}

QByteArray SketchPage::snapshot() const {
    QJsonArray arr; for (const sk::Sketch& s : sketches_) arr.push_back(s.toJson());
    QJsonObject o; o["current"] = double(currentId_); o["sketches"] = arr;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

void SketchPage::restore(const QByteArray& data) {
    QJsonObject o = QJsonDocument::fromJson(data).object();
    sketches_.clear();
    for (const QJsonValue& v : o["sketches"].toArray()) sketches_.push_back(sk::Sketch::fromJson(v.toObject()));
    currentId_ = quint64(o["current"].toDouble());
    if (!currentSketch()) currentId_ = sketches_.empty() ? 0 : sketches_.front().id;
    store();
    view_->setSketch(currentSketch());
    refreshList(); refreshConstraints(); refreshProperties();
}

void SketchPage::undo() { if (!undo_.canUndo()) return; restore(undo_.undo(snapshot())); Q_EMIT undoChanged(); }
void SketchPage::redo() { if (!undo_.canRedo()) return; restore(undo_.redo(snapshot())); Q_EMIT undoChanged(); }

sk::Sketch* SketchPage::addSketch(const QString& name) {
    undo_.push(QStringLiteral("Новый эскиз"), snapshot()); Q_EMIT undoChanged();
    quint64 id = 1; for (const sk::Sketch& s : sketches_) id = std::max(id, s.id + 1);
    sk::Sketch s; s.id = id; s.name = name.isEmpty() ? QStringLiteral("Эскиз %1").arg(id) : name;
    sketches_.push_back(s);
    currentId_ = id;
    store();
    view_->setSketch(currentSketch());
    refreshList(); refreshConstraints(); refreshProperties();
    Q_EMIT statusMessage(QStringLiteral("Создан %1").arg(s.name));
    return currentSketch();
}

void SketchPage::removeSketch() {
    if (!currentSketch()) return;
    undo_.push(QStringLiteral("Удалить эскиз"), snapshot()); Q_EMIT undoChanged();
    sketches_.erase(std::remove_if(sketches_.begin(), sketches_.end(), [this](const sk::Sketch& s) { return s.id == currentId_; }), sketches_.end());
    currentId_ = sketches_.empty() ? 0 : sketches_.front().id;
    store();
    view_->setSketch(currentSketch());
    refreshList(); refreshConstraints(); refreshProperties();
}

void SketchPage::renameSketch() {
    sk::Sketch* s = currentSketch(); if (!s) return;
    bool ok = false;
    QString n = QInputDialog::getText(this, QStringLiteral("Эскиз"), QStringLiteral("Название:"), QLineEdit::Normal, s->name, &ok);
    if (!ok || n.isEmpty()) return;
    undo_.push(QStringLiteral("Переименовать эскиз"), snapshot()); Q_EMIT undoChanged();
    s->name = n; store(); refreshList();
}

void SketchPage::selectSketch(quint64 id) {
    if (id == currentId_) return;
    currentId_ = id;
    view_->setSketch(currentSketch());
    refreshList(); refreshConstraints(); refreshProperties();
}

void SketchPage::activate() {
    if (sketches_.empty()) addSketch();
    view_->setFocus();
    if (view_->sketch() && view_->sketch()->empty()) view_->zoomExtents();
}

int SketchPage::exportToDraft() {
    sk::Sketch* s = currentSketch(); if (!s) return 0;
    auto ents = s->toDraftEntities();
    if (ents.empty()) { Q_EMIT statusMessage(QStringLiteral("Эскиз пуст — нечего выдавать")); return 0; }
    auto cmd = std::make_unique<EntityCommand>(QStringLiteral("Выдать эскиз «%1» в чертёж").arg(s->name));
    quint32 layer = project_.document().currentLayer();
    for (Entity& e : ents) { e.layer = layer; cmd->add(std::move(e)); }
    project_.commands().push(std::move(cmd));
    int n = int(ents.size());
    Q_EMIT statusMessage(QStringLiteral("В чертёж выдано %1 объектов из «%2»").arg(n).arg(s->name));
    Q_EMIT exported(n);
    return n;
}

// ---------------------------------------------------------------- панели
void SketchPage::refreshList() {
    updating_ = true;
    list_->clear();
    int cur = -1;
    for (size_t i = 0; i < sketches_.size(); ++i) {
        const sk::Sketch& s = sketches_[i];
        list_->addItem(QStringLiteral("%1  (%2 об., %3 огр.)").arg(s.name).arg(s.entities().size()).arg(s.constraints().size()));
        if (s.id == currentId_) cur = int(i);
    }
    if (cur >= 0) list_->setCurrentRow(cur);
    updating_ = false;
}

void SketchPage::refreshConstraints() {
    updating_ = true;
    cons_->setRowCount(0);
    if (sk::Sketch* s = currentSketch()) {
        const Palette& pal = Theme::current();
        for (const sk::Constraint& c : s->constraints()) {
            int row = cons_->rowCount(); cons_->insertRow(row);
            auto* t = new QTableWidgetItem(sk::constraintGlyph(c.type) + QStringLiteral("  ") + sk::constraintTypeName(c.type)); t->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(c.id));
            QStringList refs; for (const sk::Ref& r : c.refs) refs << (QStringLiteral("#%1").arg(r.entity) + (r.point >= 0 ? QStringLiteral(".%1").arg(r.point + 1) : QString()));
            auto* e = new QTableWidgetItem(refs.join(QStringLiteral(", ")));
            QString val = c.type == sk::CType::Angle ? QStringLiteral("%1°").arg(deg(c.value), 0, 'f', 1) : c.hasValue() ? Units::format(c.value, Unit::Millimeter, 2, true) : QStringLiteral("—");
            auto* v = new QTableWidgetItem(val);
            if (c.hasValue()) v->setToolTip(QStringLiteral("Двойной клик — изменить"));
            if (c.conflict) for (auto* it : {t, e, v}) it->setForeground(QColor(0xC0, 0x39, 0x2B));
            else for (auto* it : {t, e, v}) it->setForeground(pal.text);
            cons_->setItem(row, 0, t); cons_->setItem(row, 1, e); cons_->setItem(row, 2, v);
        }
    }
    updating_ = false;
}

void SketchPage::refreshProperties() {
    while (props_->rowCount()) props_->removeRow(0);
    sk::Sketch* s = currentSketch();
    if (!s) { propsHeader_->setText(QStringLiteral("Нет эскиза")); return; }
    auto ids = view_->selectedEntities();
    if (ids.empty()) {
        propsHeader_->setText(QStringLiteral("%1 · плоскость %2").arg(s->name, s->plane));
        props_->addRow(QStringLiteral("Объектов"), new QLabel(QString::number(s->entities().size())));
        props_->addRow(QStringLiteral("Ограничений"), new QLabel(QString::number(s->constraints().size())));
        return;
    }
    if (ids.size() > 1) { propsHeader_->setText(QStringLiteral("Выбрано объектов: %1").arg(ids.size())); return; }
    sk::SketchEntity* e = s->entity(ids[0]); if (!e) return;
    static const QMap<sk::Kind, QString> names{{sk::Kind::Point, QStringLiteral("Точка")}, {sk::Kind::Line, QStringLiteral("Отрезок")}, {sk::Kind::Circle, QStringLiteral("Окружность")}, {sk::Kind::Arc, QStringLiteral("Дуга")}};
    propsHeader_->setText(QStringLiteral("%1 #%2").arg(names[e->kind]).arg(e->id));
    auto num = [&](const QString& label, double v) { props_->addRow(label, new QLabel(Units::format(v, Unit::Millimeter, 2, true))); };
    switch (e->kind) {
    case sk::Kind::Point: num(QStringLiteral("X"), e->p[0]); num(QStringLiteral("Y"), e->p[1]); break;
    case sk::Kind::Line: num(QStringLiteral("X1"), e->p[0]); num(QStringLiteral("Y1"), e->p[1]); num(QStringLiteral("X2"), e->p[2]); num(QStringLiteral("Y2"), e->p[3]); num(QStringLiteral("Длина"), dist(e->point(0), e->point(1))); break;
    case sk::Kind::Circle: num(QStringLiteral("Центр X"), e->p[0]); num(QStringLiteral("Центр Y"), e->p[1]); num(QStringLiteral("Радиус"), e->p[2]); break;
    case sk::Kind::Arc: num(QStringLiteral("Центр X"), e->p[0]); num(QStringLiteral("Центр Y"), e->p[1]); num(QStringLiteral("Радиус"), e->p[2]); props_->addRow(QStringLiteral("Углы"), new QLabel(QStringLiteral("%1° … %2°").arg(deg(e->p[3]), 0, 'f', 1).arg(deg(e->p[4]), 0, 'f', 1))); break;
    }
    auto* cons = new QCheckBox(QStringLiteral("Вспомогательная")); cons->setChecked(e->construction);
    connect(cons, &QCheckBox::toggled, this, [this, id = e->id](bool on) { sk::Sketch* s = currentSketch(); if (!s) return; if (auto* e = s->entity(id)) { undo_.push(QStringLiteral("Вспомогательная геометрия"), snapshot()); Q_EMIT undoChanged(); e->construction = on; store(); view_->update(); } });
    props_->addRow(QString(), cons);
    auto* fixed = new QCheckBox(QStringLiteral("Закреплён целиком")); fixed->setChecked(e->fixed);
    connect(fixed, &QCheckBox::toggled, this, [this, id = e->id](bool on) { sk::Sketch* s = currentSketch(); if (!s) return; if (auto* e = s->entity(id)) { undo_.push(QStringLiteral("Закрепить объект"), snapshot()); Q_EMIT undoChanged(); e->fixed = on; store(); view_->resolve(); } });
    props_->addRow(QString(), fixed);
    auto consIds = s->constraintsOf(e->id);
    props_->addRow(QStringLiteral("Ограничений"), new QLabel(QString::number(consIds.size())));
}

} // namespace psi
