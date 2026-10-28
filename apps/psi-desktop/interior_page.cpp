#include "interior_page.h"
#include "core/units/units.h"
#include "modules/interior/interior_view.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QFile>
#include <QTextStream>

namespace psi {

using in::Building; using in::Floor; using in::Wall; using in::Opening; using in::Furniture; using in::InteriorView;

static const QString kPrefix = QStringLiteral("interior/");

InteriorPage::InteriorPage(Project& project, QWidget* parent) : QWidget(parent), project_(project) {
    const Palette& pal = Theme::current();
    view_ = new InteriorView;

    // ---- левая колонка
    auto* floorsBox = new QWidget; auto* fv = new QVBoxLayout(floorsBox); fv->setContentsMargins(0, 0, 0, 0); fv->setSpacing(4);
    auto* frow = new QHBoxLayout;
    floorCombo_ = new QComboBox; frow->addWidget(floorCombo_, 1);
    auto* bAddF = new QToolButton; bAddF->setIcon(Icons::icon("plus", pal.text)); bAddF->setToolTip(QStringLiteral("Добавить этаж"));
    auto* bDelF = new QToolButton; bDelF->setIcon(Icons::icon("minus", pal.text)); bDelF->setToolTip(QStringLiteral("Удалить этаж"));
    frow->addWidget(bAddF); frow->addWidget(bDelF); fv->addLayout(frow);
    tree_ = new QTreeWidget; tree_->setHeaderHidden(true); tree_->setMinimumHeight(200); fv->addWidget(tree_, 1);
    connect(floorCombo_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!updating_ && i >= 0) view_->setFloor(size_t(i)); });
    connect(bAddF, &QToolButton::clicked, this, [this] {
        bool ok = false; QString n = QInputDialog::getText(this, QStringLiteral("Этаж"), QStringLiteral("Название:"), QLineEdit::Normal, QStringLiteral("Этаж %1").arg(ensureBuilding().floors.size() + 1), &ok);
        if (!ok || n.isEmpty()) return;
        beginChange(QStringLiteral("Добавить этаж")); ensureBuilding().addFloor(n); building_.floors.back().level = building_.floors.size() > 1 ? building_.floors[building_.floors.size() - 2].level + building_.floors[building_.floors.size() - 2].height : 0;
        store(); refreshFloors(); view_->setFloor(building_.floors.size() - 1);
    });
    connect(bDelF, &QToolButton::clicked, this, [this] {
        if (building_.floors.size() <= 1) { Q_EMIT statusMessage(QStringLiteral("Последний этаж удалить нельзя")); return; }
        beginChange(QStringLiteral("Удалить этаж")); building_.floors.erase(building_.floors.begin() + long(view_->floorIndex()));
        store(); refreshFloors(); view_->setFloor(0);
    });
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (updating_) return;
        auto items = tree_->selectedItems();
        if (items.isEmpty()) return;
        QTreeWidgetItem* it = items.first(); QString kind = it->data(0, Qt::UserRole).toString();
        InteriorView::Selection s;
        if (kind == "wall") s = {InteriorView::Selection::Wall, it->data(0, Qt::UserRole + 1).toULongLong(), {}};
        else if (kind == "opening") s = {InteriorView::Selection::Opening, it->data(0, Qt::UserRole + 1).toULongLong(), {}};
        else if (kind == "furniture") s = {InteriorView::Selection::Furniture, it->data(0, Qt::UserRole + 1).toULongLong(), {}};
        else if (kind == "room") s = {InteriorView::Selection::Room, it->data(0, Qt::UserRole + 1).toULongLong(), it->data(0, Qt::UserRole + 2).toString()};
        else return;
        view_->select(s);
    });

    auto* toolsBox = new QWidget; auto* tv = new QVBoxLayout(toolsBox); tv->setContentsMargins(0, 0, 0, 0); tv->setSpacing(6);
    auto* tg = new QGridLayout; tg->setSpacing(4);
    struct T { QString id, icon, title; InteriorView::Tool tool; };
    const std::vector<T> tools{{"select", "select", QStringLiteral("Выбор / перенос (Esc)"), InteriorView::Tool::Select}, {"wall", "polyline", QStringLiteral("Стена по оси (двойной клик — закончить)"), InteriorView::Tool::Wall},
                               {"door", "interior", QStringLiteral("Дверь — щёлкните по стене"), InteriorView::Tool::Door}, {"window", "rect", QStringLiteral("Окно — щёлкните по стене"), InteriorView::Tool::Window},
                               {"room", "text", QStringLiteral("Помещение — имя и материал пола"), InteriorView::Tool::Room}, {"furniture", "hatch", QStringLiteral("Мебель из каталога (R — повернуть)"), InteriorView::Tool::Furniture},
                               {"measure", "dim", QStringLiteral("Измерить длину / площадь"), InteriorView::Tool::Measure}, {"delete", "delete", QStringLiteral("Удалить выбранное (Delete)"), InteriorView::Tool::Select}};
    int i = 0;
    for (const T& t : tools) {
        auto* b = new QToolButton; b->setIcon(Icons::icon(t.icon, pal.text)); b->setIconSize(QSize(20, 20)); b->setFixedSize(34, 34); b->setCheckable(t.id != "delete"); b->setAutoRaise(true); b->setToolTip(t.title);
        if (t.id == "delete") connect(b, &QToolButton::clicked, view_, &InteriorView::deleteSelection);
        else connect(b, &QToolButton::clicked, this, [this, tool = t.tool] { view_->setTool(tool); view_->setFocus(); });
        tg->addWidget(b, i / 4, i % 4); ++i; toolBtns_[t.id] = b;
    }
    connect(view_, &InteriorView::toolChanged, this, [this, tools] { for (const T& t : tools) if (t.id != "delete") toolBtns_[t.id]->setChecked(view_->tool() == t.tool); });
    toolBtns_["select"]->setChecked(true);
    tv->addLayout(tg);
    auto* pf = new QFormLayout; pf->setContentsMargins(0, 0, 0, 0);
    thick_ = new QDoubleSpinBox; thick_->setRange(20, 1000); thick_->setValue(200); thick_->setSuffix(QStringLiteral(" мм")); thick_->setDecimals(0);
    height_ = new QDoubleSpinBox; height_->setRange(500, 10000); height_->setValue(2700); height_->setSuffix(QStringLiteral(" мм")); height_->setDecimals(0);
    openW_ = new QDoubleSpinBox; openW_->setRange(300, 5000); openW_->setValue(900); openW_->setSuffix(QStringLiteral(" мм")); openW_->setDecimals(0);
    connect(thick_, &QDoubleSpinBox::valueChanged, this, [this](double v) { view_->wallThickness = v; });
    connect(height_, &QDoubleSpinBox::valueChanged, this, [this](double v) { view_->wallHeight = v; });
    connect(openW_, &QDoubleSpinBox::valueChanged, this, [this](double v) { view_->openingWidth = v; });
    pf->addRow(QStringLiteral("Толщина стены"), thick_); pf->addRow(QStringLiteral("Высота стены"), height_); pf->addRow(QStringLiteral("Ширина проёма"), openW_);
    tv->addLayout(pf);
    auto* bZoom = new QPushButton(QStringLiteral("Показать всё")); connect(bZoom, &QPushButton::clicked, view_, &InteriorView::zoomExtents);
    auto* bExport = new QPushButton(QStringLiteral("Выдать план в чертёж")); bExport->setObjectName("Accent"); connect(bExport, &QPushButton::clicked, this, [this] { exportToDraft(); });
    tv->addWidget(bZoom); tv->addWidget(bExport);
    QWidget* left = makeSideColumn({makeSection(QStringLiteral("Этажи и объекты"), floorsBox), makeSection(QStringLiteral("Инструменты"), toolsBox)}, 240);

    // ---- правая колонка
    auto* propsW = new QWidget; auto* pv = new QVBoxLayout(propsW); pv->setContentsMargins(0, 0, 0, 0); pv->setSpacing(4);
    propsHeader_ = new QLabel(QStringLiteral("Ничего не выбрано")); propsHeader_->setObjectName("Hint"); propsHeader_->setWordWrap(true); pv->addWidget(propsHeader_);
    auto* formW = new QWidget; props_ = new QFormLayout(formW); props_->setContentsMargins(0, 0, 0, 0); pv->addWidget(formW);

    catalog_ = new QListWidget; catalog_->setMinimumHeight(130);
    for (in::FurnitureKind k : {in::FurnitureKind::Wardrobe, in::FurnitureKind::Table, in::FurnitureKind::Shelf, in::FurnitureKind::Bed, in::FurnitureKind::Nightstand}) {
        Furniture f = Building::makeFurniture(k);
        auto* it = new QListWidgetItem(QStringLiteral("%1 — %2×%3×%4, %5 ₽").arg(f.name).arg(qRound(f.width)).arg(qRound(f.height)).arg(qRound(f.depth)).arg(f.price, 0, 'f', 0));
        it->setData(Qt::UserRole, in::furnitureKindKey(k)); catalog_->addItem(it);
    }
    connect(catalog_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { view_->setPlacing(Building::makeFurniture(in::furnitureKindFromKey(it->data(Qt::UserRole).toString()))); view_->setFocus(); });
    auto* catBox = new QWidget; auto* cv = new QVBoxLayout(catBox); cv->setContentsMargins(0, 0, 0, 0); cv->setSpacing(4); cv->addWidget(catalog_);
    auto* catHint = new QLabel(QStringLiteral("Щёлкните по позиции, затем по плану. Размеры и цену меняйте в «Свойствах».")); catHint->setObjectName("Hint"); catHint->setWordWrap(true); cv->addWidget(catHint);

    auto* schedW = new QWidget; auto* sv = new QVBoxLayout(schedW); sv->setContentsMargins(0, 0, 0, 0); sv->setSpacing(4);
    schedTabs_ = new QTabWidget;
    furnTable_ = new QTableWidget(0, 6); furnTable_->setHorizontalHeaderLabels({QStringLiteral("Наименование"), QStringLiteral("Артикул"), QStringLiteral("Размер"), QStringLiteral("Материал"), QStringLiteral("Кол-во"), QStringLiteral("Сумма")});
    matTable_ = new QTableWidget(0, 3); matTable_->setHorizontalHeaderLabels({QStringLiteral("Материал"), QStringLiteral("Где"), QStringLiteral("Площадь, м²")});
    for (QTableWidget* t : {furnTable_, matTable_}) { t->verticalHeader()->hide(); t->horizontalHeader()->setStretchLastSection(true); t->setEditTriggers(QAbstractItemView::NoEditTriggers); t->setMinimumHeight(160); }
    schedTabs_->addTab(furnTable_, QStringLiteral("Мебель")); schedTabs_->addTab(matTable_, QStringLiteral("Материалы"));
    sv->addWidget(schedTabs_, 1);
    totalLbl_ = new QLabel; totalLbl_->setObjectName("Hint"); sv->addWidget(totalLbl_);
    auto* bCsv = new QPushButton(QStringLiteral("Экспорт CSV")); connect(bCsv, &QPushButton::clicked, this, &InteriorPage::exportCsv); sv->addWidget(bCsv);
    QWidget* right = makeSideColumn({makeSection(QStringLiteral("Свойства"), propsW), makeSection(QStringLiteral("Каталог мебели"), catBox), makeSection(QStringLiteral("Ведомости"), schedW)}, 320);

    // ---- центр
    auto* center = new QWidget; auto* cl = new QVBoxLayout(center); cl->setContentsMargins(0, 0, 0, 0); cl->setSpacing(0);
    cl->addWidget(view_, 1);
    prompt_ = new QLabel; prompt_->setObjectName("Hint"); prompt_->setContentsMargins(10, 4, 10, 4); cl->addWidget(prompt_);
    connect(view_, &InteriorView::promptChanged, prompt_, &QLabel::setText);

    auto* h = new QHBoxLayout(this); h->setContentsMargins(0, 0, 0, 0); h->setSpacing(0);
    h->addWidget(left); h->addWidget(center, 1); h->addWidget(right);

    // ---- модель
    connect(view_, &InteriorView::aboutToChange, this, &InteriorPage::beginChange);
    connect(view_, &InteriorView::changed, this, [this] { store(); refreshTree(); refreshProperties(); refreshSchedules(); });
    connect(view_, &InteriorView::selectionChanged, this, [this] { refreshProperties(); });
    connect(view_, &InteriorView::floorChanged, this, [this] { updating_ = true; floorCombo_->setCurrentIndex(int(view_->floorIndex())); updating_ = false; refreshTree(); refreshSchedules(); });
    connect(&project_.document(), &Document::documentReset, this, [this] { QTimer::singleShot(0, this, [this] { reload(); }); });
    connect(&project_, &Project::stateChanged, this, [this] { if (signature() != loadedSig_) reload(); });
    reload();
}

// ---------------------------------------------------------------- модель
QByteArray InteriorPage::signature() const {
    QByteArray sig;
    const auto& parts = project_.foreignParts();
    for (auto it = parts.cbegin(); it != parts.cend(); ++it) if (it.key().startsWith(kPrefix)) { sig += it.key().toUtf8(); sig += ':'; sig += QByteArray::number(qHash(it.value())); sig += ';'; }
    return sig;
}

Building& InteriorPage::ensureBuilding() {
    if (!building_.id) { building_ = Building(); building_.id = 1; building_.name = QStringLiteral("Дом"); if (building_.floors.empty()) building_.addFloor(QStringLiteral("Этаж 1")); }
    return building_;
}

void InteriorPage::reload() {
    building_ = Building(); building_.id = 0; building_.floors.clear();
    const auto& parts = project_.foreignParts();
    for (auto it = parts.cbegin(); it != parts.cend(); ++it) {
        if (!it.key().startsWith(kPrefix)) continue;
        QJsonParseError err; QJsonDocument d = QJsonDocument::fromJson(it.value(), &err);
        if (err.error != QJsonParseError::NoError || !d.isObject()) continue;
        building_ = Building::fromJson(d.object());
        if (!building_.id) building_.id = 1;
        if (building_.floors.empty()) building_.addFloor(QStringLiteral("Этаж 1"));
        break;   // одно здание на проект (этап 05-MVP)
    }
    loadedSig_ = signature();
    undo_.clear(); Q_EMIT undoChanged();
    view_->setBuilding(building_.id ? &building_ : nullptr);
    refreshFloors(); refreshTree(); refreshProperties(); refreshSchedules();
    Q_EMIT buildingChanged();
}

void InteriorPage::store() {
    auto& parts = project_.foreignParts();
    for (auto it = parts.begin(); it != parts.end();) { if (it.key().startsWith(kPrefix)) it = parts.erase(it); else ++it; }
    if (building_.id) parts[kPrefix + QString::number(building_.id) + QStringLiteral(".json")] = QJsonDocument(building_.toJson()).toJson(QJsonDocument::Compact);
    loadedSig_ = signature();
    project_.markDirty();
    Q_EMIT buildingChanged();
}

QByteArray InteriorPage::snapshot() const {
    QJsonObject o = building_.id ? building_.toJson() : QJsonObject(); o["__floor"] = double(view_->floorIndex());
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

void InteriorPage::restore(const QByteArray& data) {
    QJsonObject o = QJsonDocument::fromJson(data).object();
    size_t fl = size_t(o.value("__floor").toDouble());
    o.remove("__floor");
    if (o.isEmpty()) { building_ = Building(); building_.id = 0; building_.floors.clear(); }
    else { building_ = Building::fromJson(o); if (building_.floors.empty()) building_.addFloor(QStringLiteral("Этаж 1")); }
    store();
    view_->setBuilding(building_.id ? &building_ : nullptr);
    if (fl < building_.floors.size()) view_->setFloor(fl);
    refreshFloors(); refreshTree(); refreshProperties(); refreshSchedules();
}

void InteriorPage::beginChange(const QString& what) { undo_.push(what, snapshot()); Q_EMIT undoChanged(); }
void InteriorPage::undo() { if (!undo_.canUndo()) return; restore(undo_.undo(snapshot())); Q_EMIT undoChanged(); }
void InteriorPage::redo() { if (!undo_.canRedo()) return; restore(undo_.redo(snapshot())); Q_EMIT undoChanged(); }

void InteriorPage::activate() {
    if (!building_.id) { beginChange(QStringLiteral("Новое здание")); ensureBuilding(); store(); view_->setBuilding(&building_); refreshFloors(); refreshTree(); refreshSchedules(); view_->zoomExtents(); }
    view_->setFocus();
}

int InteriorPage::exportToDraft() {
    if (!building_.id) return 0;
    Document& doc = project_.document();
    auto ents = building_.toDraftEntities(view_->floorIndex(), doc);
    if (ents.empty()) { Q_EMIT statusMessage(QStringLiteral("На этаже нет объектов")); return 0; }
    auto cmd = std::make_unique<EntityCommand>(QStringLiteral("Выдать план «%1» в чертёж").arg(building_.floors[view_->floorIndex()].name));
    for (Entity& e : ents) cmd->add(std::move(e));
    project_.commands().push(std::move(cmd));
    int n = int(ents.size());
    Q_EMIT statusMessage(QStringLiteral("В чертёж выдано %1 объектов плана").arg(n));
    Q_EMIT exported(n);
    return n;
}

QString InteriorPage::scheduleCsv() const {
    if (!building_.id || view_->floorIndex() >= building_.floors.size()) return {};
    return Building::csv(Building::furnitureSchedule(building_.floors[view_->floorIndex()]));
}

void InteriorPage::exportCsv() {
    if (!building_.id) return;
    const Floor& f = building_.floors[view_->floorIndex()];
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Экспорт ведомости"), QStringLiteral("%1 — %2.csv").arg(building_.name, schedTabs_->currentIndex() == 0 ? QStringLiteral("мебель") : QStringLiteral("материалы")), QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return;
    QString text;
    if (schedTabs_->currentIndex() == 0) text = Building::csv(Building::furnitureSchedule(f));
    else {
        text = QStringLiteral("Материал;Где;Площадь, м²\n");
        for (const in::MaterialRow& r : Building::materialSchedule(f)) text += QStringLiteral("%1;%2;%3\n").arg(r.material, r.kind).arg(r.area, 0, 'f', 2);
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { Q_EMIT statusMessage(QStringLiteral("Не удалось записать %1").arg(path)); return; }
    file.write("\xEF\xBB\xBF"); file.write(text.toUtf8());
    Q_EMIT statusMessage(QStringLiteral("Ведомость сохранена: %1").arg(path));
}

// ---------------------------------------------------------------- панели
void InteriorPage::refreshFloors() {
    updating_ = true;
    floorCombo_->clear();
    for (const Floor& f : building_.floors) floorCombo_->addItem(QStringLiteral("%1  (отм. %2)").arg(f.name, Units::format(f.level, Unit::Millimeter, 0, true)));
    floorCombo_->setCurrentIndex(int(view_->floorIndex()));
    updating_ = false;
}

void InteriorPage::refreshTree() {
    updating_ = true;
    tree_->clear();
    const Palette& pal = Theme::current();
    if (building_.id && view_->floorIndex() < building_.floors.size()) {
        const Floor& f = building_.floors[view_->floorIndex()];
        auto* walls = new QTreeWidgetItem({QStringLiteral("Стены  (%1)").arg(f.walls.size())}); walls->setIcon(0, Icons::icon("polyline", pal.text));
        for (const Wall& w : f.walls) { auto* it = new QTreeWidgetItem({QStringLiteral("Стена #%1 · %2 · %3").arg(w.id).arg(Units::format(w.length(), Unit::Millimeter, 0, true), w.material)}); it->setData(0, Qt::UserRole, "wall"); it->setData(0, Qt::UserRole + 1, QVariant::fromValue<qulonglong>(w.id)); walls->addChild(it); }
        auto* ops = new QTreeWidgetItem({QStringLiteral("Проёмы  (%1)").arg(f.openings.size())}); ops->setIcon(0, Icons::icon("interior", pal.text));
        for (const Opening& o : f.openings) { auto* it = new QTreeWidgetItem({QStringLiteral("%1 #%2 · %3").arg(o.kind == in::OpeningKind::Door ? QStringLiteral("Дверь") : QStringLiteral("Окно")).arg(o.id).arg(Units::format(o.width, Unit::Millimeter, 0, true))}); it->setData(0, Qt::UserRole, "opening"); it->setData(0, Qt::UserRole + 1, QVariant::fromValue<qulonglong>(o.id)); ops->addChild(it); }
        auto* furn = new QTreeWidgetItem({QStringLiteral("Мебель  (%1)").arg(f.furniture.size())}); furn->setIcon(0, Icons::icon("hatch", pal.text));
        for (const Furniture& m : f.furniture) { auto* it = new QTreeWidgetItem({QStringLiteral("%1 #%2").arg(m.name).arg(m.id)}); it->setData(0, Qt::UserRole, "furniture"); it->setData(0, Qt::UserRole + 1, QVariant::fromValue<qulonglong>(m.id)); furn->addChild(it); }
        auto* rooms = new QTreeWidgetItem({QStringLiteral("Помещения  (%1)").arg(view_->rooms().size())}); rooms->setIcon(0, Icons::icon("text", pal.text));
        for (const in::Room& r : view_->rooms()) { auto* it = new QTreeWidgetItem({QStringLiteral("%1 · %2 м²").arg(r.name.isEmpty() ? QStringLiteral("Помещение %1").arg(r.id) : r.name).arg(r.areaM2, 0, 'f', 2)}); it->setData(0, Qt::UserRole, "room"); it->setData(0, Qt::UserRole + 1, QVariant::fromValue<qulonglong>(r.id)); it->setData(0, Qt::UserRole + 2, Building::roomKey(r.centroid)); rooms->addChild(it); }
        for (auto* top : {walls, ops, furn, rooms}) { tree_->addTopLevelItem(top); top->setExpanded(true); }
        if (!view_->collisions().empty()) {
            auto* col = new QTreeWidgetItem({QStringLiteral("Коллизии  (%1)").arg(view_->collisions().size())}); col->setForeground(0, QColor(0xC0, 0x39, 0x2B));
            for (const in::Collision& c : view_->collisions()) col->addChild(new QTreeWidgetItem({c.what}));
            tree_->addTopLevelItem(col); col->setExpanded(true);
        }
    }
    updating_ = false;
}

void InteriorPage::refreshProperties() {
    while (props_->rowCount()) props_->removeRow(0);
    if (!building_.id || view_->floorIndex() >= building_.floors.size()) { propsHeader_->setText(QStringLiteral("Нет здания")); return; }
    Floor& f = building_.floors[view_->floorIndex()];
    const InteriorView::Selection& s = view_->selection();
    auto spin = [&](double v, double lo, double hi, const QString& suffix, const std::function<void(double)>& fn) {
        auto* sb = new QDoubleSpinBox; sb->setRange(lo, hi); sb->setDecimals(0); sb->setSuffix(suffix); sb->setValue(v); sb->setKeyboardTracking(false);
        connect(sb, &QDoubleSpinBox::valueChanged, this, fn); return sb;
    };
    auto text = [&](const QString& v, const std::function<void(const QString&)>& fn) {
        auto* le = new QLineEdit(v); connect(le, &QLineEdit::editingFinished, this, [le, fn] { fn(le->text()); }); return le;
    };
    auto commit = [this](const QString& what, const std::function<void()>& fn) { beginChange(what); fn(); store(); view_->refresh(); refreshTree(); refreshSchedules(); };
    const QString mm = QStringLiteral(" мм");
    switch (s.kind) {
    case InteriorView::Selection::None:
        propsHeader_->setText(QStringLiteral("%1 · %2").arg(building_.name, f.name));
        props_->addRow(QStringLiteral("Здание"), text(building_.name, [this, commit](const QString& v) { if (v != building_.name) commit(QStringLiteral("Имя здания"), [this, v] { building_.name = v; }); }));
        props_->addRow(QStringLiteral("Этаж"), text(f.name, [this, commit, &f](const QString& v) { if (v != f.name) commit(QStringLiteral("Имя этажа"), [&f, v] { f.name = v; }); refreshFloors(); }));
        props_->addRow(QStringLiteral("Высота этажа"), spin(f.height, 500, 10000, mm, [commit, &f](double v) { commit(QStringLiteral("Высота этажа"), [&f, v] { f.height = v; }); }));
        break;
    case InteriorView::Selection::Wall: {
        Wall* w = f.wallMut(s.id); if (!w) break;
        propsHeader_->setText(QStringLiteral("Стена #%1 · длина %2 · %3 сегм.").arg(w->id).arg(Units::format(w->length(), Unit::Millimeter, 0, true)).arg(w->segmentCount()));
        props_->addRow(QStringLiteral("Толщина"), spin(w->thickness, 20, 1000, mm, [commit, w](double v) { commit(QStringLiteral("Толщина стены"), [w, v] { w->thickness = v; }); }));
        props_->addRow(QStringLiteral("Высота"), spin(w->height, 500, 10000, mm, [commit, w](double v) { commit(QStringLiteral("Высота стены"), [w, v] { w->height = v; }); }));
        props_->addRow(QStringLiteral("Материал"), text(w->material, [commit, w](const QString& v) { if (v != w->material) commit(QStringLiteral("Материал стены"), [w, v] { w->material = v; }); }));
        break;
    }
    case InteriorView::Selection::Opening: {
        Opening* o = nullptr; for (Opening& k : f.openings) if (k.id == s.id) o = &k; if (!o) break;
        bool door = o->kind == in::OpeningKind::Door;
        propsHeader_->setText(QStringLiteral("%1 #%2 на стене #%3").arg(door ? QStringLiteral("Дверь") : QStringLiteral("Окно")).arg(o->id).arg(o->wall));
        props_->addRow(QStringLiteral("Ширина"), spin(o->width, 300, 5000, mm, [commit, o](double v) { commit(QStringLiteral("Ширина проёма"), [o, v] { o->width = v; }); }));
        props_->addRow(QStringLiteral("Высота"), spin(o->height, 300, 5000, mm, [commit, o](double v) { commit(QStringLiteral("Высота проёма"), [o, v] { o->height = v; }); }));
        props_->addRow(QStringLiteral("Отступ по стене"), spin(o->offset, 0, 100000, mm, [commit, o](double v) { commit(QStringLiteral("Положение проёма"), [o, v] { o->offset = v; }); }));
        if (door) {
            auto* left = new QCheckBox(QStringLiteral("Петли слева")); left->setChecked(o->swingLeft);
            connect(left, &QCheckBox::toggled, this, [commit, o](bool v) { commit(QStringLiteral("Сторона открывания"), [o, v] { o->swingLeft = v; }); });
            auto* inside = new QCheckBox(QStringLiteral("Открывается внутрь")); inside->setChecked(o->swingInside);
            connect(inside, &QCheckBox::toggled, this, [commit, o](bool v) { commit(QStringLiteral("Направление открывания"), [o, v] { o->swingInside = v; }); });
            props_->addRow(QString(), left); props_->addRow(QString(), inside);
        } else props_->addRow(QStringLiteral("Подоконник"), spin(o->sill, 0, 3000, mm, [commit, o](double v) { commit(QStringLiteral("Высота подоконника"), [o, v] { o->sill = v; }); }));
        break;
    }
    case InteriorView::Selection::Furniture: {
        Furniture* m = f.furnitureMut(s.id); if (!m) break;
        propsHeader_->setText(QStringLiteral("%1 #%2 · %3").arg(Building::kindName(m->kind)).arg(m->id).arg(m->name));
        props_->addRow(QStringLiteral("Название"), text(m->name, [commit, m](const QString& v) { if (v != m->name) commit(QStringLiteral("Название мебели"), [m, v] { m->name = v; }); }));
        props_->addRow(QStringLiteral("Ширина"), spin(m->width, 100, 10000, mm, [commit, m](double v) { commit(QStringLiteral("Размер мебели"), [m, v] { m->width = v; }); }));
        props_->addRow(QStringLiteral("Высота"), spin(m->height, 50, 5000, mm, [commit, m](double v) { commit(QStringLiteral("Размер мебели"), [m, v] { m->height = v; }); }));
        props_->addRow(QStringLiteral("Глубина"), spin(m->depth, 50, 10000, mm, [commit, m](double v) { commit(QStringLiteral("Размер мебели"), [m, v] { m->depth = v; }); }));
        props_->addRow(QStringLiteral("Поворот"), spin(deg(m->rotation), -360, 360, QStringLiteral("°"), [commit, m](double v) { commit(QStringLiteral("Поворот мебели"), [m, v] { m->rotation = rad(v); }); }));
        props_->addRow(QStringLiteral("Материал"), text(m->material, [commit, m](const QString& v) { if (v != m->material) commit(QStringLiteral("Материал мебели"), [m, v] { m->material = v; }); }));
        props_->addRow(QStringLiteral("Цвет"), text(m->color, [commit, m](const QString& v) { if (v != m->color) commit(QStringLiteral("Цвет мебели"), [m, v] { m->color = v; }); }));
        props_->addRow(QStringLiteral("Артикул"), text(m->article, [commit, m](const QString& v) { if (v != m->article) commit(QStringLiteral("Артикул"), [m, v] { m->article = v; }); }));
        props_->addRow(QStringLiteral("Цена"), spin(m->price, 0, 1e7, QStringLiteral(" ₽"), [commit, m](double v) { commit(QStringLiteral("Цена"), [m, v] { m->price = v; }); }));
        auto ispin = [&](int v, int hi, const std::function<void(int)>& fn) { auto* sb = new QSpinBox; sb->setRange(0, hi); sb->setValue(v); sb->setKeyboardTracking(false); connect(sb, &QSpinBox::valueChanged, this, fn); return sb; };
        props_->addRow(QStringLiteral("Полки"), ispin(m->shelves, 50, [commit, m](int v) { commit(QStringLiteral("Полки"), [m, v] { m->shelves = v; }); }));
        props_->addRow(QStringLiteral("Двери"), ispin(m->doors, 20, [commit, m](int v) { commit(QStringLiteral("Двери"), [m, v] { m->doors = v; }); }));
        props_->addRow(QStringLiteral("Ящики"), ispin(m->drawers, 50, [commit, m](int v) { commit(QStringLiteral("Ящики"), [m, v] { m->drawers = v; }); }));
        break;
    }
    case InteriorView::Selection::Room: {
        const in::Room* r = nullptr; for (const in::Room& k : view_->rooms()) if (k.id == s.id) r = &k; if (!r) break;
        propsHeader_->setText(QStringLiteral("Помещение · %1 м² · периметр %2").arg(r->areaM2, 0, 'f', 2).arg(Units::format(r->perimeterMm, Unit::Millimeter, 0, true)));
        QString key = s.roomKey;
        props_->addRow(QStringLiteral("Название"), text(r->name, [commit, &f, key](const QString& v) { commit(QStringLiteral("Название помещения"), [&f, key, v] { f.roomInfo[key].name = v; }); }));
        props_->addRow(QStringLiteral("Материал пола"), text(r->floorMaterial, [commit, &f, key](const QString& v) { commit(QStringLiteral("Материал пола"), [&f, key, v] { f.roomInfo[key].floorMaterial = v; }); }));
        break;
    }
    }
}

void InteriorPage::refreshSchedules() {
    furnTable_->setRowCount(0); matTable_->setRowCount(0); totalLbl_->clear();
    if (!building_.id || view_->floorIndex() >= building_.floors.size()) return;
    const Floor& f = building_.floors[view_->floorIndex()];
    double total = 0; int count = 0;
    for (const in::ScheduleRow& r : Building::furnitureSchedule(f)) {
        int row = furnTable_->rowCount(); furnTable_->insertRow(row);
        const QStringList cells{r.name, r.article, r.size, r.material, QString::number(r.count), QStringLiteral("%1 ₽").arg(r.sum, 0, 'f', 0)};
        for (int c = 0; c < cells.size(); ++c) furnTable_->setItem(row, c, new QTableWidgetItem(cells[c]));
        total += r.sum; count += r.count;
    }
    for (const in::MaterialRow& r : Building::materialSchedule(f)) {
        int row = matTable_->rowCount(); matTable_->insertRow(row);
        matTable_->setItem(row, 0, new QTableWidgetItem(r.material)); matTable_->setItem(row, 1, new QTableWidgetItem(r.kind)); matTable_->setItem(row, 2, new QTableWidgetItem(QString::number(r.area, 'f', 2)));
    }
    totalLbl_->setText(QStringLiteral("Позиций: %1 · итого %2 ₽").arg(count).arg(total, 0, 'f', 0));
}

} // namespace psi
