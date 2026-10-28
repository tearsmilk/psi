#include "panels.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "dialogs.h"
#include "modules/draft/tool.h"
#include "rendering/viewport.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QShowEvent>
#include <QSettings>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace psi {

namespace {
const QList<QPair<QString, QString>>& toolIcons() {
    static const QList<QPair<QString, QString>> m{
        {"select", "select"}, {"line", "line"}, {"polyline", "polyline"}, {"rect", "rect"}, {"circle", "circle"}, {"arc", "arc"},
        {"ellipse", "ellipse"}, {"spline", "spline"}, {"text", "text"}, {"leader", "leader"}, {"dim", "dim"}, {"dim-radius", "dim-radius"}, {"dim-angle", "dim-angle"}, {"hatch", "hatch"},
        {"move", "move"}, {"copy", "copy"}, {"rotate", "rotate"}, {"mirror", "mirror"}, {"scale", "scale"}, {"offset", "offset"},
        {"trim", "trim"}, {"extend", "extend"}, {"fillet", "fillet"}, {"chamfer", "chamfer"}, {"array", "array"}, {"explode", "explode"},
        {"delete", "delete"}, {"join", "join"}, {"union", "union"}, {"subtract", "subtract"}, {"intersect", "intersect"}, {"split", "split"}, {"cut", "cut"}, {"dim-h", "dim-h"}, {"dim-v", "dim-v"}, {"dim-diameter", "dim-diameter"}};
    return m;
}
QString iconFor(const QString& tool) { for (const auto& p : toolIcons()) if (p.first == tool) return p.second; return tool; }

QString weightLabel(double w) { return w < 0 ? QStringLiteral("По слою") : QStringLiteral("%1 мм").arg(w, 0, 'f', 2); }
const std::vector<double>& weights() { static const std::vector<double> w{0.13, 0.18, 0.25, 0.35, 0.5, 0.7, 1.0, 1.4, 2.0}; return w; }
QString lineTypeName(LineType t) {
    switch (t) { case LineType::ByLayer: return QStringLiteral("По слою"); case LineType::Solid: return QStringLiteral("Сплошная");
                 case LineType::Dashed: return QStringLiteral("Штриховая"); case LineType::Dotted: return QStringLiteral("Пунктирная"); case LineType::DashDot: return QStringLiteral("Штрихпунктирная"); }
    return {};
}
QPixmap swatch(int rgb, int size = 14) {
    QPixmap px(size, size); px.fill(Qt::transparent);
    QPainter p(&px); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Theme::current().border, 1)); p.setBrush(QColor::fromRgb(rgb)); p.drawRoundedRect(QRectF(0.5, 0.5, size - 1, size - 1), 3, 3);
    return px;
}
} // namespace

// ================================================================= ToolsPanel
namespace {
const int kFavourites = 8;
QString groupKey(const QString& title) { QString k = title; k.replace(' ', '_'); return "tools/collapsed/" + k; }
}

ToolsPanel::ToolsPanel(QWidget* parent) : QWidget(parent) {
    QSettings st;
    compact_ = st.value("tools/compact", false).toBool();
    recent_ = st.value("tools/recent").toStringList();
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(8, 8, 8, 8); v->setSpacing(6);
    // search + compact toggle
    auto* top = new QHBoxLayout; top->setSpacing(4);
    search_ = new QLineEdit; search_->setPlaceholderText(QStringLiteral("Поиск инструмента…")); search_->setClearButtonEnabled(true);
    search_->setObjectName("toolSearch");
    connect(search_, &QLineEdit::textChanged, this, &ToolsPanel::setFilter);
    top->addWidget(search_, 1);
    compactBtn_ = new QToolButton; compactBtn_->setCheckable(true); compactBtn_->setAutoRaise(true); compactBtn_->setChecked(compact_);
    compactBtn_->setToolTip(QStringLiteral("Компактный режим: только иконки")); compactBtn_->setFixedSize(28, 28); compactBtn_->setObjectName("toolCompact");
    connect(compactBtn_, &QToolButton::toggled, this, &ToolsPanel::setCompact);
    top->addWidget(compactBtn_);
    v->addLayout(top);
    groups_ = {
        {QStringLiteral("Избранное"), {}},
        {QStringLiteral("Рисование"), {"select", "line", "polyline", "rect", "circle", "arc", "ellipse", "spline"}},
        {QStringLiteral("Текст и размеры"), {"text", "leader", "dim", "dim-h", "dim-v", "dim-radius", "dim-diameter", "dim-angle"}},
        {QStringLiteral("Правка"), {"move", "copy", "rotate", "mirror", "scale", "offset", "trim", "extend", "fillet", "chamfer", "array", "explode", "delete"}},
        {QStringLiteral("Объединение"), {"join", "union", "subtract", "intersect", "split", "cut"}},
        {QStringLiteral("Штриховка"), {"hatch"}}};
    for (Group& g : groups_) {
        g.header = new QToolButton; g.header->setText(g.title); g.header->setCheckable(true); g.header->setAutoRaise(true);
        g.header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); g.header->setArrowType(Qt::DownArrow);
        g.header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); g.header->setObjectName("toolGroup");
        g.header->setStyleSheet("QToolButton#toolGroup { text-align: left; font-weight: 600; padding: 2px 4px; }");
        g.collapsed = st.value(groupKey(g.title), false).toBool();
        g.header->setChecked(!g.collapsed); g.header->setArrowType(g.collapsed ? Qt::RightArrow : Qt::DownArrow);
        g.body = new QWidget; g.grid = new QGridLayout(g.body); g.grid->setContentsMargins(0, 0, 0, 0); g.grid->setSpacing(3);
        g.body->setVisible(!g.collapsed);
        const QString title = g.title;
        connect(g.header, &QToolButton::toggled, this, [this, title](bool open) { setGroupCollapsed(title, !open); });
        v->addWidget(g.header); v->addWidget(g.body);
        if (g.title != QStringLiteral("Избранное")) relayout(g);
    }
    rebuildFavourites();
    v->addStretch(1);
}

QToolButton* ToolsPanel::button(const QString& id) {
    if (QToolButton* b = buttons_.value(id)) return b;
    auto* b = new QToolButton;
    b->setCheckable(true); b->setAutoRaise(true); b->setIconSize(QSize(20, 20));
    b->setText(toolTitle(id)); b->setProperty("toolId", id);
    b->setFocusPolicy(Qt::NoFocus);
    connect(b, &QToolButton::clicked, this, [this, id] { Q_EMIT toolRequested(id); });
    buttons_[id] = b;
    return b;
}

void ToolsPanel::relayout(Group& g) {
    while (QLayoutItem* it = g.grid->takeAt(0)) { if (it->widget()) it->widget()->setParent(nullptr); delete it; }
    const bool fav = g.title == QStringLiteral("Избранное");
    const int cols = compact_ ? 4 : 2;
    int i = 0;
    for (const QString& id : g.ids) {
        QToolButton* b = fav ? favButtons_.value(id) : button(id);
        if (!b) continue;
        b->setParent(g.body);
        b->setToolButtonStyle(compact_ ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        if (compact_) { b->setFixedSize(36, 36); b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); }
        else { b->setMinimumSize(0, 36); b->setMaximumSize(QWIDGETSIZE_MAX, 36); b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); }
        QString tip = toolTitle(id);
        if (!shortcuts_.value(id).isEmpty()) tip += QStringLiteral("  ·  %1").arg(shortcuts_.value(id));
        b->setToolTip(tip);
        b->setChecked(id == active_);
        b->setVisible(filter_.isEmpty() || toolTitle(id).contains(filter_, Qt::CaseInsensitive) || id.contains(filter_, Qt::CaseInsensitive) ||
                      shortcuts_.value(id).contains(filter_, Qt::CaseInsensitive));
        if (b->isVisibleTo(g.body) || filter_.isEmpty()) { g.grid->addWidget(b, i / cols, i % cols); ++i; }
    }
    if (compact_) g.grid->setColumnStretch(cols, 1); else for (int c = 0; c <= cols; ++c) g.grid->setColumnStretch(c, c < cols ? 1 : 0);
    const bool any = i > 0;
    g.header->setVisible(any && !(fav && !filter_.isEmpty()));
    g.body->setVisible(any && !g.collapsed && !(fav && !filter_.isEmpty()));
    g.grid->activate(); g.body->updateGeometry();
    if (layout()) layout()->activate();
}

void ToolsPanel::rebuildFavourites() {
    Group& g = groups_.front();
    QStringList ids;
    for (const QString& id : recent_) if (id != "select" && toolIds().contains(id) && ids.size() < kFavourites) ids.push_back(id);
    g.ids = ids;
    // favourites get their own buttons (a widget can live in one layout only)
    for (const QString& id : ids) {
        if (favButtons_.contains(id)) continue;
        auto* b = new QToolButton; b->setCheckable(true); b->setAutoRaise(true); b->setIconSize(QSize(20, 20));
        b->setText(toolTitle(id)); b->setProperty("toolId", id); b->setFocusPolicy(Qt::NoFocus);
        if (iconsReady_) b->setIcon(Icons::icon(iconFor(id), Theme::current().text));
        connect(b, &QToolButton::clicked, this, [this, id] { Q_EMIT toolRequested(id); });
        favButtons_[id] = b;
    }
    for (auto it = favButtons_.begin(); it != favButtons_.end();) {
        if (!ids.contains(it.key())) { it.value()->deleteLater(); it = favButtons_.erase(it); } else ++it;
    }
    relayout(g);
}

void ToolsPanel::ensureIcons() {
    if (iconsReady_) return;
    iconsReady_ = true;
    const QColor c = Theme::current().text;
    QHash<QString, QIcon> cache;
    auto iconOf = [&](const QString& id) { QString n = iconFor(id); if (!cache.contains(n)) cache[n] = Icons::icon(n, c); return cache[n]; };
    for (auto it = buttons_.begin(); it != buttons_.end(); ++it) it.value()->setIcon(iconOf(it.key()));
    for (auto it = favButtons_.begin(); it != favButtons_.end(); ++it) it.value()->setIcon(iconOf(it.key()));
    search_->addAction(Icons::icon("search", Theme::current().muted), QLineEdit::LeadingPosition);
    compactBtn_->setIcon(Icons::icon("compact", c));
}

void ToolsPanel::showEvent(QShowEvent* ev) { ensureIcons(); QWidget::showEvent(ev); }

void ToolsPanel::setActive(const QString& id) {
    active_ = id;
    for (auto it = buttons_.begin(); it != buttons_.end(); ++it) it.value()->setChecked(it.key() == id);
    for (auto it = favButtons_.begin(); it != favButtons_.end(); ++it) it.value()->setChecked(it.key() == id);
    if (id.isEmpty() || id == "select" || !toolIds().contains(id)) return;
    recent_.removeAll(id); recent_.prepend(id);
    while (recent_.size() > kFavourites) recent_.removeLast();
    rebuildFavourites();
    saveState();
}

void ToolsPanel::setShortcut(const QString& id, const QString& keys) {
    shortcuts_[id] = keys;
    QString tip = toolTitle(id); if (!keys.isEmpty()) tip += QStringLiteral("  ·  %1").arg(keys);
    if (QToolButton* b = buttons_.value(id)) b->setToolTip(tip);
    if (QToolButton* b = favButtons_.value(id)) b->setToolTip(tip);
}

void ToolsPanel::setCompact(bool on) {
    if (compact_ == on && compactBtn_->isChecked() == on) { for (Group& g : groups_) relayout(g); return; }
    compact_ = on;
    if (compactBtn_->isChecked() != on) compactBtn_->setChecked(on);
    for (Group& g : groups_) relayout(g);
    saveState();
}

void ToolsPanel::setFilter(const QString& text) {
    filter_ = text.trimmed();
    if (search_->text() != text) search_->setText(text);
    for (Group& g : groups_) relayout(g);
}

QStringList ToolsPanel::visibleTools() const {
    QStringList out;
    for (const Group& g : groups_) {
        if (g.title == QStringLiteral("Избранное") || !g.header->isVisibleTo(const_cast<ToolsPanel*>(this))) continue;
        for (int i = 0; i < g.grid->count(); ++i) if (QWidget* w = g.grid->itemAt(i)->widget()) if (w->isVisibleTo(g.body)) out << w->property("toolId").toString();
    }
    return out;
}

bool ToolsPanel::groupCollapsed(const QString& title) const { for (const Group& g : groups_) if (g.title == title) return g.collapsed; return false; }

void ToolsPanel::setGroupCollapsed(const QString& title, bool collapsed) {
    for (Group& g : groups_) {
        if (g.title != title) continue;
        g.collapsed = collapsed;
        g.header->setArrowType(collapsed ? Qt::RightArrow : Qt::DownArrow);
        if (g.header->isChecked() == collapsed) g.header->setChecked(!collapsed);
        relayout(g);
    }
    saveState();
}

void ToolsPanel::saveState() const {
    QSettings st;
    st.setValue("tools/compact", compact_);
    st.setValue("tools/recent", recent_);
    for (const Group& g : groups_) st.setValue(groupKey(g.title), g.collapsed);
}

// ================================================================= LayersPanel
LayersPanel::LayersPanel(Project& project, QWidget* parent) : QWidget(parent), project_(project) {
    auto* v = new QVBoxLayout(this); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(4);
    table_ = new QTableWidget(0, 5);
    table_->setHorizontalHeaderLabels({QString(), QString(), QStringLiteral("Слой"), QStringLiteral("Цвет"), QStringLiteral("Толщина")});
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->addWidget(table_, 1);
    auto* row = new QHBoxLayout;
    const Palette& pal = Theme::current();
    auto* add = new QToolButton; add->setIcon(Icons::icon("plus", pal.text)); add->setToolTip(QStringLiteral("Новый слой")); add->setAutoRaise(true);
    auto* rem = new QToolButton; rem->setIcon(Icons::icon("minus", pal.text)); rem->setToolTip(QStringLiteral("Удалить слой")); rem->setAutoRaise(true);
    auto* edit = new QToolButton; edit->setIcon(Icons::icon("settings", pal.text)); edit->setToolTip(QStringLiteral("Свойства слоя")); edit->setAutoRaise(true);
    row->addWidget(add); row->addWidget(rem); row->addWidget(edit); row->addStretch(1);
    auto* hint = new QLabel(QStringLiteral("Двойной щелчок — текущий слой")); hint->setObjectName("Faint"); row->addWidget(hint);
    v->addLayout(row);
    connect(add, &QToolButton::clicked, this, &LayersPanel::addLayer);
    connect(rem, &QToolButton::clicked, this, &LayersPanel::removeLayer);
    connect(edit, &QToolButton::clicked, this, [this] { int r = table_->currentRow(); if (r >= 0) editLayer(table_->item(r, 2)->data(Qt::UserRole).toUInt()); });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int r, int col) {
        quint32 id = table_->item(r, 2)->data(Qt::UserRole).toUInt();
        if (col == 2) { project_.document().setCurrentLayer(id); refresh(); }
        else if (col >= 3) editLayer(id);
    });
    connect(table_, &QTableWidget::cellClicked, this, [this](int r, int col) {
        if (updating_ || col > 1) return;
        quint32 id = table_->item(r, 2)->data(Qt::UserRole).toUInt();
        const Layer* l = project_.document().layer(id);
        if (!l) return;
        Layer after = *l;
        if (col == 0) after.visible = !after.visible; else after.locked = !after.locked;
        project_.commands().push(std::make_unique<LayerCommand>(LayerCommand::Update, *l, after, col == 0 ? QStringLiteral("Видимость слоя") : QStringLiteral("Блокировка слоя")));
    });
    connect(&project_.document(), &Document::layersChanged, this, &LayersPanel::refresh);
    connect(&project_.document(), &Document::documentReset, this, &LayersPanel::refresh);
    refresh();
}

void LayersPanel::refresh() {
    updating_ = true;
    const Document& doc = project_.document();
    const Palette& pal = Theme::current();
    table_->setRowCount(int(doc.layers().size()));
    int r = 0;
    for (const Layer& l : doc.layers()) {
        auto* eye = new QTableWidgetItem; eye->setIcon(Icons::icon("eye", l.visible ? pal.text : pal.faint)); eye->setToolTip(QStringLiteral("Видимость"));
        auto* lock = new QTableWidgetItem; lock->setIcon(Icons::icon("lock", l.locked ? pal.accent : pal.faint)); lock->setToolTip(QStringLiteral("Блокировка"));
        auto* name = new QTableWidgetItem(l.name); name->setData(Qt::UserRole, l.id);
        if (l.id == doc.currentLayer()) { QFont f = name->font(); f.setBold(true); name->setFont(f); name->setText(QStringLiteral("● ") + l.name); }
        auto* col = new QTableWidgetItem; col->setIcon(QIcon(swatch(l.color))); col->setText(QColor::fromRgb(l.color).name().toUpper());
        auto* w = new QTableWidgetItem(weightLabel(l.lineWeight));
        table_->setItem(r, 0, eye); table_->setItem(r, 1, lock); table_->setItem(r, 2, name); table_->setItem(r, 3, col); table_->setItem(r, 4, w);
        ++r;
    }
    updating_ = false;
}

void LayersPanel::addLayer() {
    Layer l; l.name = QStringLiteral("Слой %1").arg(project_.document().layers().size());
    LayerDialog dlg(l, this);
    if (dlg.exec() != QDialog::Accepted) return;
    project_.commands().push(std::make_unique<LayerCommand>(LayerCommand::Add, Layer{}, dlg.layer(), QStringLiteral("Новый слой")));
}

void LayersPanel::removeLayer() {
    int r = table_->currentRow();
    if (r < 0) return;
    quint32 id = table_->item(r, 2)->data(Qt::UserRole).toUInt();
    const Layer* l = project_.document().layer(id);
    if (!l) return;
    if (id == 0) { QMessageBox::information(this, QStringLiteral("Слои"), QStringLiteral("Слой «0» удалить нельзя.")); return; }
    if (project_.document().layerInUse(id)) { QMessageBox::information(this, QStringLiteral("Слои"), QStringLiteral("На слое «%1» есть объекты. Сначала перенесите или удалите их.").arg(l->name)); return; }
    project_.commands().push(std::make_unique<LayerCommand>(LayerCommand::Remove, *l, *l, QStringLiteral("Удалить слой")));
}

void LayersPanel::editLayer(quint32 id) {
    const Layer* l = project_.document().layer(id);
    if (!l) return;
    LayerDialog dlg(*l, this);
    if (dlg.exec() != QDialog::Accepted) return;
    Layer after = dlg.layer(); after.id = id;
    if (after == *l) return;
    project_.commands().push(std::make_unique<LayerCommand>(LayerCommand::Update, *l, after, QStringLiteral("Свойства слоя")));
}

// ================================================================= PropertiesPanel
PropertiesPanel::PropertiesPanel(Project& project, Viewport& view, QWidget* parent) : QWidget(parent), project_(project), view_(view) {
    auto* v = new QVBoxLayout(this); v->setContentsMargins(8, 8, 8, 8); v->setSpacing(8);
    header_ = new QLabel(QStringLiteral("Ничего не выбрано")); header_->setObjectName("H2"); v->addWidget(header_);
    auto* form = new QFormLayout; form->setLabelAlignment(Qt::AlignRight);
    layer_ = new QComboBox; color_ = new QComboBox; weight_ = new QComboBox; lineType_ = new QComboBox;
    form->addRow(QStringLiteral("Слой"), layer_);
    form->addRow(QStringLiteral("Цвет"), color_);
    form->addRow(QStringLiteral("Толщина"), weight_);
    form->addRow(QStringLiteral("Тип линии"), lineType_);
    v->addLayout(form);
    geomBox_ = new QWidget; geom_ = new QFormLayout(geomBox_); geom_->setContentsMargins(0, 0, 0, 0); geom_->setLabelAlignment(Qt::AlignRight);
    v->addWidget(geomBox_);
    auto* h = new QLabel(QStringLiteral("Атрибуты")); h->setObjectName("H2"); v->addWidget(h);
    attrs_ = new QTableWidget(0, 2);
    attrs_->setHorizontalHeaderLabels({QStringLiteral("Имя"), QStringLiteral("Значение")});
    attrs_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    attrs_->verticalHeader()->hide();
    v->addWidget(attrs_, 1);
    auto* row = new QHBoxLayout;
    const Palette& pal = Theme::current();
    auto* add = new QToolButton; add->setIcon(Icons::icon("plus", pal.text)); add->setAutoRaise(true); add->setToolTip(QStringLiteral("Добавить атрибут"));
    auto* rem = new QToolButton; rem->setIcon(Icons::icon("minus", pal.text)); rem->setAutoRaise(true); rem->setToolTip(QStringLiteral("Удалить атрибут"));
    row->addWidget(add); row->addWidget(rem); row->addStretch(1);
    v->addLayout(row);
    connect(add, &QToolButton::clicked, this, &PropertiesPanel::addAttribute);
    connect(rem, &QToolButton::clicked, this, &PropertiesPanel::removeAttribute);

    connect(layer_, &QComboBox::activated, this, [this](int i) { quint32 id = layer_->itemData(i).toUInt(); apply([id](Entity& e) { e.layer = id; }, QStringLiteral("Слой объекта")); });
    connect(color_, &QComboBox::activated, this, [this](int i) {
        if (color_->itemData(i).toString() == "pick") {
            QColor c = QColorDialog::getColor(Qt::black, this, QStringLiteral("Цвет объекта"));
            if (!c.isValid()) { refresh(); return; }
            int rgb = c.rgb() & 0xFFFFFF;
            apply([rgb](Entity& e) { e.color = rgb; }, QStringLiteral("Цвет объекта"));
        } else { int c = color_->itemData(i).toInt(); apply([c](Entity& e) { e.color = c; }, QStringLiteral("Цвет объекта")); }
    });
    connect(weight_, &QComboBox::activated, this, [this](int i) { double w = weight_->itemData(i).toDouble(); apply([w](Entity& e) { e.lineWeight = w; }, QStringLiteral("Толщина линии")); });
    connect(lineType_, &QComboBox::activated, this, [this](int i) { auto t = LineType(lineType_->itemData(i).toInt()); apply([t](Entity& e) { e.lineType = t; }, QStringLiteral("Тип линии")); });
    connect(attrs_, &QTableWidget::cellChanged, this, [this](int r, int c) {
        if (updating_ || c != 1) return;
        QString key = attrs_->item(r, 0)->text(); QString val = attrs_->item(r, 1)->text();
        apply([key, val](Entity& e) { auto it = e.attrs.find(key); if (it != e.attrs.end()) { AttrValue a = it.value(); a.value = val; e.attrs[key] = a; } else e.attrs[key] = AttrValue::string(val); }, QStringLiteral("Атрибут"));
    });

    connect(&view_, &Viewport::selectionChanged, this, &PropertiesPanel::refresh);
    connect(&project_.document(), &Document::entitiesChanged, this, &PropertiesPanel::refresh);
    connect(&project_.document(), &Document::layersChanged, this, &PropertiesPanel::refresh);
    connect(&project_.document(), &Document::documentReset, this, &PropertiesPanel::refresh);
    refresh();
}

void PropertiesPanel::apply(const std::function<void(Entity&)>& fn, const QString& text) {
    if (updating_) return;
    auto c = std::make_unique<EntityCommand>(text);
    for (quint64 id : view_.selection()) {
        const Entity* e = project_.document().entity(id);
        if (!e || e->locked) continue;
        Entity after = *e; fn(after);
        if (!(after == *e)) c->modify(*e, after);
    }
    if (!c->empty()) project_.commands().push(std::move(c));
}

void PropertiesPanel::refresh() {
    updating_ = true;
    const Document& doc = project_.document();
    const QSet<quint64>& sel = view_.selection();
    std::vector<const Entity*> es;
    for (quint64 id : sel) if (const Entity* e = doc.entity(id)) es.push_back(e);
    bool any = !es.empty();
    for (QWidget* w : {static_cast<QWidget*>(layer_), static_cast<QWidget*>(color_), static_cast<QWidget*>(weight_), static_cast<QWidget*>(lineType_), geomBox_, static_cast<QWidget*>(attrs_)}) w->setEnabled(any);

    // header
    if (!any) header_->setText(QStringLiteral("Ничего не выбрано"));
    else if (es.size() == 1) header_->setText(entityTypeName(es[0]->type) + QStringLiteral("  ·  #%1").arg(es[0]->id));
    else {
        bool same = true; for (auto* e : es) if (e->type != es[0]->type) { same = false; break; }
        header_->setText(same ? QStringLiteral("%1 × %2").arg(entityTypeName(es[0]->type)).arg(es.size()) : QStringLiteral("Объекты: %1").arg(es.size()));
    }

    // layer combo
    layer_->clear();
    for (const Layer& l : doc.layers()) layer_->addItem(QIcon(swatch(l.color)), l.name, l.id);
    if (any) { bool same = true; for (auto* e : es) if (e->layer != es[0]->layer) same = false; layer_->setCurrentIndex(same ? layer_->findData(es[0]->layer) : -1); }

    // color
    color_->clear();
    color_->addItem(QStringLiteral("По слою"), kColorByLayer);
    const int presets[] = {0x141413, 0xD97757, 0x8A6D5A, 0x3B6FB6, 0x4C8C4A, 0xB8431F, 0x9A9A96, 0xE0B341};
    for (int c : presets) color_->addItem(QIcon(swatch(c)), QColor::fromRgb(c).name().toUpper(), c);
    color_->addItem(QStringLiteral("Другой…"), "pick");
    if (any) {
        int c = es[0]->color; bool same = true; for (auto* e : es) if (e->color != c) same = false;
        int idx = same ? color_->findData(c) : -1;
        if (same && idx < 0 && c >= 0) { color_->insertItem(1, QIcon(swatch(c)), QColor::fromRgb(c).name().toUpper(), c); idx = 1; }
        color_->setCurrentIndex(idx);
    }

    // weight
    weight_->clear();
    weight_->addItem(QStringLiteral("По слою"), -1.0);
    for (double w : weights()) weight_->addItem(weightLabel(w), w);
    if (any) { double w = es[0]->lineWeight; bool same = true; for (auto* e : es) if (e->lineWeight != w) same = false; int idx = -1; if (same) for (int i = 0; i < weight_->count(); ++i) if (std::abs(weight_->itemData(i).toDouble() - w) < 1e-6) idx = i; weight_->setCurrentIndex(idx); }

    // line type
    lineType_->clear();
    for (LineType t : {LineType::ByLayer, LineType::Solid, LineType::Dashed, LineType::Dotted, LineType::DashDot}) lineType_->addItem(lineTypeName(t), int(t));
    if (any) { bool same = true; for (auto* e : es) if (e->lineType != es[0]->lineType) same = false; lineType_->setCurrentIndex(same ? lineType_->findData(int(es[0]->lineType)) : -1); }

    // geometry (single entity)
    while (geom_->rowCount() > 0) geom_->removeRow(0);
    if (es.size() == 1) {
        const Entity& e = *es[0];
        Unit u = doc.settings().unit; int prec = doc.settings().precision;
        auto ro = [&](const QString& label, const QString& val) { auto* l = new QLabel(val); l->setTextInteractionFlags(Qt::TextSelectableByMouse); geom_->addRow(label, l); };
        auto spin = [&](const QString& label, double value, double minV, const std::function<void(Entity&, double)>& set) {
            auto* s = new QDoubleSpinBox; s->setRange(minV, 1e9); s->setDecimals(3); s->setValue(value); s->setKeyboardTracking(false);
            connect(s, &QDoubleSpinBox::editingFinished, this, [this, s, set] { double v = s->value(); apply([v, set](Entity& en) { set(en, v); }, QStringLiteral("Геометрия")); });
            geom_->addRow(label, s);
        };
        auto text = [&](const QString& label, const QString& value, const std::function<void(Entity&, const QString&)>& set) {
            auto* le = new QLineEdit(value);
            connect(le, &QLineEdit::editingFinished, this, [this, le, set] { QString v = le->text(); apply([v, set](Entity& en) { set(en, v); }, QStringLiteral("Текст")); });
            geom_->addRow(label, le);
        };
        Rect bb = entityBBox(e);
        ro(QStringLiteral("Габариты"), Units::format(bb.width(), u, prec) + QStringLiteral(" × ") + Units::format(bb.height(), u, prec, true));
        switch (e.type) {
        case EntityType::Line:
            ro(QStringLiteral("Длина"), Units::format(dist(e.pts[0], e.pts[1]), u, prec, true));
            ro(QStringLiteral("Угол"), Units::formatAngle((e.pts[1] - e.pts[0]).angle(), 2));
            break;
        case EntityType::Circle:
            spin(QStringLiteral("Радиус"), e.radius, 1e-6, [](Entity& en, double v) { en.radius = v; });
            ro(QStringLiteral("Центр"), Units::format(e.center.x, u, prec) + ", " + Units::format(e.center.y, u, prec));
            break;
        case EntityType::Arc:
            spin(QStringLiteral("Радиус"), e.radius, 1e-6, [](Entity& en, double v) { en.radius = v; });
            ro(QStringLiteral("Развёртка"), Units::formatAngle(arcSweep(e), 2));
            break;
        case EntityType::Ellipse:
            spin(QStringLiteral("Полуось A"), e.rx, 1e-6, [](Entity& en, double v) { en.rx = v; });
            spin(QStringLiteral("Полуось B"), e.ry, 1e-6, [](Entity& en, double v) { en.ry = v; });
            break;
        case EntityType::Polyline: case EntityType::Spline: {
            double len = 0; auto segs = flatten(e, 0.05); for (size_t i = 1; i < segs.size(); ++i) len += dist(segs[i - 1], segs[i]);
            ro(QStringLiteral("Вершин"), QString::number(e.pts.size()));
            ro(QStringLiteral("Длина"), Units::format(len, u, prec, true));
            ro(QStringLiteral("Замкнута"), e.closed ? QStringLiteral("да") : QStringLiteral("нет"));
            break;
        }
        case EntityType::Text: case EntityType::Leader:
            text(QStringLiteral("Текст"), e.text, [](Entity& en, const QString& v) { en.text = v; });
            spin(QStringLiteral("Высота"), e.textHeight, 0.1, [](Entity& en, double v) { en.textHeight = v; });
            break;
        case EntityType::Dimension:
            ro(QStringLiteral("Значение"), e.dimKind == DimKind::Angular ? Units::formatAngle(dimensionValue(e, doc.lookup()), 1) : Units::format(dimensionValue(e, doc.lookup()), u, prec, true));
            text(QStringLiteral("Переопределение"), e.text, [](Entity& en, const QString& v) { en.text = v; });
            spin(QStringLiteral("Высота текста"), e.textHeight, 0.1, [](Entity& en, double v) { en.textHeight = v; });
            break;
        case EntityType::Hatch:
            spin(QStringLiteral("Шаг"), e.hatchSpacing, 0.01, [](Entity& en, double v) { en.hatchSpacing = v; });
            spin(QStringLiteral("Угол, °"), deg(e.hatchAngle), -360, [](Entity& en, double v) { en.hatchAngle = rad(v); });
            break;
        }
    }

    // attributes
    attrs_->setRowCount(0);
    if (es.size() == 1) {
        const AttributeMap& m = es[0]->attrs;
        attrs_->setRowCount(m.size());
        int r = 0;
        for (auto it = m.begin(); it != m.end(); ++it, ++r) {
            auto* k = new QTableWidgetItem(it.key()); k->setFlags(k->flags() & ~Qt::ItemIsEditable); k->setToolTip(attrKindName(it.value().kind));
            attrs_->setItem(r, 0, k);
            attrs_->setItem(r, 1, new QTableWidgetItem(it.value().display()));
        }
    }
    updating_ = false;
}

void PropertiesPanel::addAttribute() {
    if (view_.selection().isEmpty()) return;
    AttributeDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    QString key = dlg.key(); AttrValue v = dlg.value();
    if (key.isEmpty()) return;
    apply([key, v](Entity& e) { e.attrs[key] = v; }, QStringLiteral("Атрибут"));
}

void PropertiesPanel::removeAttribute() {
    int r = attrs_->currentRow();
    if (r < 0) return;
    QString key = attrs_->item(r, 0)->text();
    apply([key](Entity& e) { e.attrs.remove(key); }, QStringLiteral("Удалить атрибут"));
}

// ================================================================= TreePanel
TreePanel::TreePanel(Project& project, Viewport& view, QWidget* parent) : QWidget(parent), project_(project), view_(view) {
    auto* v = new QVBoxLayout(this); v->setContentsMargins(0, 0, 0, 0);
    tree_ = new QTreeWidget; tree_->setHeaderHidden(true); tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    v->addWidget(tree_);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (updating_) return;
        QSet<quint64> sel;
        for (QTreeWidgetItem* it : tree_->selectedItems()) {
            if (it->data(0, Qt::UserRole).toString() == "entity") sel.insert(it->data(0, Qt::UserRole + 1).toULongLong());
            else if (it->data(0, Qt::UserRole).toString() == "layer") { quint32 lid = it->data(0, Qt::UserRole + 1).toUInt(); for (quint64 id : project_.document().entityIds()) { const Entity* e = project_.document().entity(id); if (e && e->layer == lid) sel.insert(id); } }
        }
        if (!sel.isEmpty()) view_.setSelection(sel);
    });
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it, int) {
        if (it->data(0, Qt::UserRole).toString() == "sheet") Q_EMIT sheetActivated(it->data(0, Qt::UserRole + 1).toULongLong());
        else if (it->data(0, Qt::UserRole).toString() == "entity") { const Entity* e = project_.document().entity(it->data(0, Qt::UserRole + 1).toULongLong()); if (e) view_.zoomTo(entityBBox(*e).grown(std::max(entityBBox(*e).width(), entityBBox(*e).height()) * 0.5 + 1)); }
    });
    connect(&project_.document(), &Document::entitiesChanged, this, &TreePanel::refresh);
    connect(&project_.document(), &Document::layersChanged, this, &TreePanel::refresh);
    connect(&project_.document(), &Document::sheetsChanged, this, &TreePanel::refresh);
    connect(&project_.document(), &Document::documentReset, this, &TreePanel::refresh);
    connect(&project_, &Project::stateChanged, this, &TreePanel::refresh);
    refresh();
}

void TreePanel::refresh() {
    updating_ = true;
    const Document& doc = project_.document();
    const Palette& pal = Theme::current();
    QSet<quint32> expandedLayers;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = tree_->topLevelItem(i);
        for (int j = 0; j < top->childCount(); ++j) { QTreeWidgetItem* c = top->child(j); if (c->isExpanded() && c->data(0, Qt::UserRole).toString() == "layer") expandedLayers.insert(c->data(0, Qt::UserRole + 1).toUInt()); }
    }
    tree_->clear();
    auto* root = new QTreeWidgetItem({project_.displayName()}); root->setIcon(0, Icons::icon("tree", pal.accent));
    auto* drawing = new QTreeWidgetItem({QStringLiteral("Чертёж  (%1)").arg(doc.entityCount())}); drawing->setIcon(0, Icons::icon("draft", pal.text));
    std::map<quint32, std::vector<const Entity*>> byLayer;
    for (quint64 id : doc.entityIds()) if (const Entity* e = doc.entity(id)) byLayer[e->layer].push_back(e);
    for (const Layer& l : doc.layers()) {
        auto& es = byLayer[l.id];
        auto* li = new QTreeWidgetItem({QStringLiteral("%1  (%2)").arg(l.name).arg(es.size())});
        li->setIcon(0, Icons::icon("layers", l.visible ? pal.text : pal.faint));
        li->setData(0, Qt::UserRole, "layer"); li->setData(0, Qt::UserRole + 1, l.id);
        const size_t limit = 500;
        for (size_t i = 0; i < es.size() && i < limit; ++i) {
            const Entity* e = es[i];
            QString label = entityTypeName(e->type);
            if (!e->text.isEmpty()) label += QStringLiteral(": ") + e->text.left(24);
            auto* ei = new QTreeWidgetItem({label + QStringLiteral("  #%1").arg(e->id)});
            ei->setData(0, Qt::UserRole, "entity"); ei->setData(0, Qt::UserRole + 1, QVariant::fromValue<qulonglong>(e->id));
            if (e->locked) ei->setIcon(0, Icons::icon("lock", pal.faint));
            li->addChild(ei);
        }
        if (es.size() > limit) li->addChild(new QTreeWidgetItem({QStringLiteral("… ещё %1").arg(es.size() - limit)}));
        li->setExpanded(expandedLayers.contains(l.id));
        drawing->addChild(li);
    }
    root->addChild(drawing);
    auto* sheets = new QTreeWidgetItem({QStringLiteral("Листы  (%1)").arg(doc.sheets().size())}); sheets->setIcon(0, Icons::icon("sheet", pal.text));
    for (const Sheet& s : doc.sheets()) {
        auto* si = new QTreeWidgetItem({QStringLiteral("%1 — %2%3").arg(s.name, s.paperName(), s.landscape ? QStringLiteral(" альбомный") : QString())});
        si->setData(0, Qt::UserRole, "sheet"); si->setData(0, Qt::UserRole + 1, QVariant::fromValue<qulonglong>(s.id));
        sheets->addChild(si);
    }
    root->addChild(sheets);
    // эскизы и здания хранятся как части .psi (sketches/*.json, interior/*.json)
    auto partsNode = [&](const QString& prefix, const QString& title, const QString& icon, const QString& kind) {
        const auto& parts = project_.foreignParts();
        std::vector<std::pair<QString, QString>> items;
        for (auto it = parts.cbegin(); it != parts.cend(); ++it) {
            if (!it.key().startsWith(prefix)) continue;
            QJsonObject o = QJsonDocument::fromJson(it.value()).object();
            QString label = o.value("name").toString(it.key().mid(prefix.size()));
            if (kind == "sketch") label += QStringLiteral("  (%1 об., %2 огр.)").arg(o.value("entities").toArray().size()).arg(o.value("constraints").toArray().size());
            else label += QStringLiteral("  (этажей: %1)").arg(o.value("floors").toArray().size());
            items.push_back({it.key(), label});
        }
        if (items.empty()) return;
        auto* node = new QTreeWidgetItem({QStringLiteral("%1  (%2)").arg(title).arg(items.size())}); node->setIcon(0, Icons::icon(icon, pal.text));
        for (const auto& [key, label] : items) { auto* it = new QTreeWidgetItem({label}); it->setData(0, Qt::UserRole, kind); it->setData(0, Qt::UserRole + 1, key); node->addChild(it); }
        root->addChild(node); node->setExpanded(true);
    };
    partsNode(QStringLiteral("sketches/"), QStringLiteral("Эскизы"), "sketch", "sketch");
    partsNode(QStringLiteral("interior/"), QStringLiteral("Интерьер"), "interior", "interior");
    tree_->addTopLevelItem(root);
    root->setExpanded(true); drawing->setExpanded(true); sheets->setExpanded(true);
    updating_ = false;
}

} // namespace psi
