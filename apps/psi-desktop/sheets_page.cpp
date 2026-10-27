#include "sheets_page.h"
#include "io/pdf/pdf_export.h"
#include "modules/drawing/sheet.h"
#include "rendering/renderer.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace psi {

// ================================================================= SheetPreview
SheetPreview::SheetPreview(Project& project, QWidget* parent) : QWidget(parent), project_(project) {
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(&project_.document(), &Document::sheetsChanged, this, qOverload<>(&QWidget::update));
    connect(&project_.document(), &Document::entitiesChanged, this, [this] { update(); });
    connect(&project_.document(), &Document::layersChanged, this, qOverload<>(&QWidget::update));
}

QTransform SheetPreview::sheetToWidget(double* kOut) const {
    const Sheet* s = project_.document().sheet(sheetId_);
    if (!s) return {};
    const double pad = 24;
    double k = std::min((width() - 2 * pad) / s->pageWidth(), (height() - 2 * pad) / s->pageHeight());
    if (kOut) *kOut = k;
    double ox = (width() - s->pageWidth() * k) / 2, oy = (height() - s->pageHeight() * k) / 2;
    QTransform t;
    t.translate(ox, oy + s->pageHeight() * k);
    t.scale(k, -k);
    return t;
}

void SheetPreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const Palette& pal = Theme::current();
    p.fillRect(rect(), pal.bg);
    const Sheet* s = project_.document().sheet(sheetId_);
    if (!s) { p.setPen(pal.muted); p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Лист не выбран")); return; }
    double k = 1;
    QTransform t = sheetToWidget(&k);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF page = t.mapRect(QRectF(0, 0, s->pageWidth(), s->pageHeight()));
    p.fillRect(page.translated(3, 3), QColor(0, 0, 0, 40));
    p.fillRect(page, Qt::white);
    Renderer::drawSheet(p, project_.document(), *s, t, k, true);
    // viewport frames
    int i = 0;
    for (const SheetViewport& vp : s->viewports) {
        QRectF r = t.mapRect(QRectF(vp.frame.x0, vp.frame.y0, vp.frame.width(), vp.frame.height()));
        QPen pen(i == vpIndex_ ? pal.accent : pal.faint, i == vpIndex_ ? 1.5 : 1, Qt::DashLine);
        p.setPen(pen); p.setBrush(Qt::NoBrush); p.drawRect(r);
        if (i == vpIndex_) { p.setPen(pal.accent); p.setFont(QFont(Theme::sansFamily(), 9)); p.drawText(r.topLeft() + QPointF(4, -4), QStringLiteral("Вид %1 · %2").arg(i + 1).arg(scaleLabel(vp.scale))); }
        ++i;
    }
}

void SheetPreview::mousePressEvent(QMouseEvent* e) {
    const Sheet* s = project_.document().sheet(sheetId_);
    if (!s || e->button() != Qt::LeftButton) return;
    QTransform inv = sheetToWidget().inverted();
    QPointF mm = inv.map(e->position());
    for (int i = int(s->viewports.size()) - 1; i >= 0; --i) {
        const Rect& f = s->viewports[i].frame;
        if (f.contains(Point{mm.x(), mm.y()})) { vpIndex_ = i; dragging_ = true; dragStart_ = e->position(); update(); return; }
    }
}

void SheetPreview::mouseMoveEvent(QMouseEvent*) {}

void SheetPreview::mouseReleaseEvent(QMouseEvent* e) {
    if (!dragging_) return;
    dragging_ = false;
    double k = 1; sheetToWidget(&k);
    QPointF d = e->position() - dragStart_;
    if (std::hypot(d.x(), d.y()) < 2) return;
    Q_EMIT viewportMoved(vpIndex_, Point{d.x() / k, -d.y() / k});
}

// ================================================================= SheetsPage
SheetsPage::SheetsPage(Project& project, QWidget* parent) : QWidget(parent), project_(project) {
    const Palette& pal = Theme::current();
    auto* h = new QHBoxLayout(this); h->setContentsMargins(0, 0, 0, 0); h->setSpacing(0);

    // left: list
    auto* left = new QWidget; left->setFixedWidth(220); left->setObjectName("Card");
    auto* lv = new QVBoxLayout(left); lv->setContentsMargins(10, 10, 10, 10); lv->setSpacing(6);
    auto* lt = new QLabel(QStringLiteral("Листы")); lt->setObjectName("H2"); lv->addWidget(lt);
    list_ = new QListWidget; list_->setFrameShape(QFrame::NoFrame); lv->addWidget(list_, 1);
    auto* lrow = new QHBoxLayout;
    auto mkBtn = [&](const QString& icon, const QString& tip) { auto* b = new QToolButton; b->setIcon(Icons::icon(icon, pal.text)); b->setToolTip(tip); b->setAutoRaise(true); lrow->addWidget(b); return b; };
    connect(mkBtn("plus", QStringLiteral("Новый лист")), &QToolButton::clicked, this, &SheetsPage::addSheet);
    connect(mkBtn("minus", QStringLiteral("Удалить лист")), &QToolButton::clicked, this, &SheetsPage::removeSheet);
    connect(mkBtn("copy", QStringLiteral("Дублировать лист")), &QToolButton::clicked, this, &SheetsPage::duplicateSheet);
    lrow->addStretch(1);
    lv->addLayout(lrow);
    auto* pdfAll = new QPushButton(Icons::icon("pdf", pal.onAccent), QStringLiteral("Экспорт PDF…")); pdfAll->setObjectName("Accent");
    connect(pdfAll, &QPushButton::clicked, this, [this] { std::vector<quint64> ids; for (const Sheet& s : project_.document().sheets()) ids.push_back(s.id); Q_EMIT exportPdfRequested(ids); });
    lv->addWidget(pdfAll);
    auto* pdfOne = new QPushButton(QStringLiteral("Только этот лист…"));
    connect(pdfOne, &QPushButton::clicked, this, [this] { if (currentSheet()) Q_EMIT exportPdfRequested({currentSheet()}); });
    lv->addWidget(pdfOne);
    h->addWidget(left);

    // centre: preview
    preview_ = new SheetPreview(project_);
    h->addWidget(preview_, 1);

    // right: form in a scroll area
    auto* right = new QScrollArea; right->setFixedWidth(330); right->setWidgetResizable(true); right->setFrameShape(QFrame::NoFrame);
    auto* form = new QWidget; form->setObjectName("Card");
    auto* fv = new QVBoxLayout(form); fv->setContentsMargins(12, 12, 12, 12); fv->setSpacing(8);
    auto* ft = new QLabel(QStringLiteral("Лист")); ft->setObjectName("H2"); fv->addWidget(ft);
    auto* f1 = new QFormLayout; f1->setLabelAlignment(Qt::AlignRight);
    name_ = new QLineEdit; f1->addRow(QStringLiteral("Название"), name_);
    paper_ = new QComboBox; for (const PaperSize& ps : paperSizes()) paper_->addItem(QStringLiteral("%1 — %2×%3 мм").arg(ps.name).arg(ps.width).arg(ps.height), ps.name);
    paper_->addItem(QStringLiteral("Другой"), "custom");
    f1->addRow(QStringLiteral("Формат"), paper_);
    landscape_ = new QCheckBox(QStringLiteral("Альбомная ориентация")); f1->addRow(QString(), landscape_);
    frame_ = new QCheckBox(QStringLiteral("Рамка")); f1->addRow(QString(), frame_);
    auto mkSpin = [](double maxV, int dec = 1) { auto* s = new QDoubleSpinBox; s->setRange(0, maxV); s->setDecimals(dec); s->setSuffix(QStringLiteral(" мм")); s->setKeyboardTracking(false); return s; };
    mL_ = mkSpin(200); mT_ = mkSpin(200); mR_ = mkSpin(200); mB_ = mkSpin(200);
    auto* mrow = new QHBoxLayout; for (auto* s : {mL_, mT_, mR_, mB_}) mrow->addWidget(s);
    f1->addRow(QStringLiteral("Поля Л/В/П/Н"), mrow);
    fv->addLayout(f1);

    titleEnabled_ = new QCheckBox(QStringLiteral("Основная надпись (ГОСТ 185×55)")); fv->addWidget(titleEnabled_);
    titleBox_ = new QWidget; titleForm_ = new QFormLayout(titleBox_); titleForm_->setContentsMargins(0, 0, 0, 0); titleForm_->setLabelAlignment(Qt::AlignRight);
    fv->addWidget(titleBox_);

    auto* vt = new QLabel(QStringLiteral("Виды")); vt->setObjectName("H2"); fv->addWidget(vt);
    auto* vrow = new QHBoxLayout;
    vpList_ = new QComboBox; vrow->addWidget(vpList_, 1);
    auto* vAdd = new QToolButton; vAdd->setIcon(Icons::icon("plus", pal.text)); vAdd->setAutoRaise(true); vAdd->setToolTip(QStringLiteral("Добавить вид")); vrow->addWidget(vAdd);
    auto* vRem = new QToolButton; vRem->setIcon(Icons::icon("minus", pal.text)); vRem->setAutoRaise(true); vRem->setToolTip(QStringLiteral("Удалить вид")); vrow->addWidget(vRem);
    fv->addLayout(vrow);
    auto* f2 = new QFormLayout; f2->setLabelAlignment(Qt::AlignRight);
    vpX_ = mkSpin(5000); vpY_ = mkSpin(5000); vpW_ = mkSpin(5000); vpH_ = mkSpin(5000);
    auto* pos = new QHBoxLayout; pos->addWidget(vpX_); pos->addWidget(vpY_); f2->addRow(QStringLiteral("Положение X/Y"), pos);
    auto* size = new QHBoxLayout; size->addWidget(vpW_); size->addWidget(vpH_); f2->addRow(QStringLiteral("Размер Ш/В"), size);
    vpScale_ = new QComboBox; vpScale_->setEditable(true); for (const ScalePreset& sp : scalePresets()) vpScale_->addItem(sp.label, sp.factor);
    f2->addRow(QStringLiteral("Масштаб"), vpScale_);
    vpCx_ = new QDoubleSpinBox; vpCx_->setRange(-1e9, 1e9); vpCx_->setDecimals(1); vpCx_->setKeyboardTracking(false);
    vpCy_ = new QDoubleSpinBox; vpCy_->setRange(-1e9, 1e9); vpCy_->setDecimals(1); vpCy_->setKeyboardTracking(false);
    auto* cen = new QHBoxLayout; cen->addWidget(vpCx_); cen->addWidget(vpCy_); f2->addRow(QStringLiteral("Центр модели, мм"), cen);
    vpBorder_ = new QCheckBox(QStringLiteral("Рамка вида")); f2->addRow(QString(), vpBorder_);
    fv->addLayout(f2);
    auto* fit = new QPushButton(QStringLiteral("Вписать весь чертёж")); fv->addWidget(fit);
    vpWarn_ = new QLabel; vpWarn_->setObjectName("Hint"); vpWarn_->setWordWrap(true); fv->addWidget(vpWarn_);
    fv->addStretch(1);
    right->setWidget(form);
    h->addWidget(right);

    // wiring
    connect(list_, &QListWidget::currentRowChanged, this, [this](int) { if (!updating_) { preview_->setSheet(currentSheet()); rebuildForm(); } });
    auto onSheet = [this](const std::function<void(Sheet&)>& fn, const QString& text) {
        if (updating_) return;
        const Sheet* s = project_.document().sheet(currentSheet());
        if (!s) return;
        Sheet after = *s; fn(after);
        commit(after, text);
    };
    connect(name_, &QLineEdit::editingFinished, this, [=, this] { onSheet([this](Sheet& s) { s.name = name_->text().trimmed().isEmpty() ? s.name : name_->text().trimmed(); }, QStringLiteral("Название листа")); });
    connect(paper_, &QComboBox::activated, this, [=, this](int i) {
        QString key = paper_->itemData(i).toString();
        if (key == "custom") return;
        onSheet([key](Sheet& s) { for (const PaperSize& ps : paperSizes()) if (ps.name == key) { s.width = ps.width; s.height = ps.height; } }, QStringLiteral("Формат листа"));
    });
    connect(landscape_, &QCheckBox::toggled, this, [=, this](bool on) { onSheet([on](Sheet& s) { s.landscape = on; }, QStringLiteral("Ориентация листа")); });
    connect(frame_, &QCheckBox::toggled, this, [=, this](bool on) { onSheet([on](Sheet& s) { s.frame = on; }, QStringLiteral("Рамка листа")); });
    for (auto* sp : {mL_, mT_, mR_, mB_})
        connect(sp, &QDoubleSpinBox::editingFinished, this, [=, this] { onSheet([this](Sheet& s) { s.marginLeft = mL_->value(); s.marginTop = mT_->value(); s.marginRight = mR_->value(); s.marginBottom = mB_->value(); }, QStringLiteral("Поля листа")); });
    connect(titleEnabled_, &QCheckBox::toggled, this, [=, this](bool on) { onSheet([on](Sheet& s) { s.title.enabled = on; }, QStringLiteral("Основная надпись")); });
    connect(vAdd, &QToolButton::clicked, this, &SheetsPage::addViewport);
    connect(vRem, &QToolButton::clicked, this, &SheetsPage::removeViewport);
    connect(fit, &QPushButton::clicked, this, &SheetsPage::fitViewport);
    connect(vpList_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!updating_) { preview_->setSelectedViewport(i); refreshViewportForm(); } });
    auto onVp = [=, this](const std::function<void(SheetViewport&)>& fn, const QString& text) {
        int i = vpList_->currentIndex();
        onSheet([i, fn](Sheet& s) { if (i >= 0 && i < int(s.viewports.size())) fn(s.viewports[size_t(i)]); }, text);
    };
    for (auto* sp : {vpX_, vpY_, vpW_, vpH_})
        connect(sp, &QDoubleSpinBox::editingFinished, this, [=, this] { onVp([this](SheetViewport& v) { v.frame = Rect{vpX_->value(), vpY_->value(), vpX_->value() + std::max(1.0, vpW_->value()), vpY_->value() + std::max(1.0, vpH_->value())}; }, QStringLiteral("Рамка вида")); });
    auto applyScale = [=, this] {
        double f = parseScale(vpScale_->currentText());
        if (f <= 0) { int i = vpScale_->currentIndex(); if (i >= 0) f = vpScale_->itemData(i).toDouble(); }
        if (f > 0) onVp([f](SheetViewport& v) { v.scale = f; }, QStringLiteral("Масштаб вида"));
    };
    connect(vpScale_, &QComboBox::activated, this, [=, this](int) { applyScale(); });
    connect(vpScale_->lineEdit(), &QLineEdit::editingFinished, this, applyScale);
    for (auto* sp : {vpCx_, vpCy_})
        connect(sp, &QDoubleSpinBox::editingFinished, this, [=, this] { onVp([this](SheetViewport& v) { v.modelCenter = Point{vpCx_->value(), vpCy_->value()}; }, QStringLiteral("Центр вида")); });
    connect(vpBorder_, &QCheckBox::toggled, this, [=, this](bool on) { onVp([on](SheetViewport& v) { v.border = on; }, QStringLiteral("Рамка вида")); });
    connect(preview_, &SheetPreview::viewportMoved, this, [=, this](int i, Point d) {
        updating_ = true; vpList_->setCurrentIndex(i); updating_ = false;
        onSheet([i, d](Sheet& s) { if (i >= 0 && i < int(s.viewports.size())) { Rect& f = s.viewports[size_t(i)].frame; f = Rect{f.x0 + d.x, f.y0 + d.y, f.x1 + d.x, f.y1 + d.y}; } }, QStringLiteral("Перемещение вида"));
    });

    connect(&project_.document(), &Document::sheetsChanged, this, &SheetsPage::refresh);
    connect(&project_.document(), &Document::documentReset, this, &SheetsPage::refresh);
    connect(&project_.document(), &Document::entitiesChanged, this, [this] { refreshViewportForm(); });
    refresh();
}

quint64 SheetsPage::currentSheet() const {
    QListWidgetItem* it = list_->currentItem();
    return it ? it->data(Qt::UserRole).toULongLong() : 0;
}

void SheetsPage::selectSheet(quint64 id) {
    for (int i = 0; i < list_->count(); ++i) if (list_->item(i)->data(Qt::UserRole).toULongLong() == id) { list_->setCurrentRow(i); return; }
}

void SheetsPage::refresh() {
    quint64 cur = currentSheet();
    updating_ = true;
    list_->clear();
    int row = 0, curRow = 0;
    for (const Sheet& s : project_.document().sheets()) {
        auto* it = new QListWidgetItem(Icons::icon("sheet", Theme::current().muted), QStringLiteral("%1\n%2%3").arg(s.name, s.paperName(), s.landscape ? QStringLiteral(" · альбомный") : QString()));
        it->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(s.id));
        list_->addItem(it);
        if (s.id == cur) curRow = row;
        ++row;
    }
    updating_ = false;
    if (list_->count()) list_->setCurrentRow(curRow);
    preview_->setSheet(currentSheet());
    rebuildForm();
}

void SheetsPage::rebuildForm() {
    const Sheet* s = project_.document().sheet(currentSheet());
    updating_ = true;
    bool any = s != nullptr;
    for (QWidget* w : {static_cast<QWidget*>(name_), static_cast<QWidget*>(paper_), static_cast<QWidget*>(landscape_), static_cast<QWidget*>(frame_), titleBox_, static_cast<QWidget*>(titleEnabled_), static_cast<QWidget*>(vpList_)}) w->setEnabled(any);
    while (titleForm_->rowCount() > 0) titleForm_->removeRow(0);
    vpList_->clear();
    if (s) {
        name_->setText(s->name);
        int pi = paper_->findData(s->paperName()); paper_->setCurrentIndex(pi < 0 ? paper_->count() - 1 : pi);
        landscape_->setChecked(s->landscape); frame_->setChecked(s->frame);
        mL_->setValue(s->marginLeft); mT_->setValue(s->marginTop); mR_->setValue(s->marginRight); mB_->setValue(s->marginBottom);
        titleEnabled_->setChecked(s->title.enabled);
        titleBox_->setEnabled(s->title.enabled);
        const QList<QPair<QString, QString>> labels{{"name", QStringLiteral("Наименование")}, {"designation", QStringLiteral("Обозначение")}, {"material", QStringLiteral("Материал")},
                                                    {"scale", QStringLiteral("Масштаб")}, {"sheet", QStringLiteral("Лист")}, {"sheets", QStringLiteral("Листов")}, {"author", QStringLiteral("Разработал")},
                                                    {"checked", QStringLiteral("Проверил")}, {"date", QStringLiteral("Дата")}, {"org", QStringLiteral("Организация")}};
        for (const auto& l : labels) {
            QString key = l.first;
            auto* le = new QLineEdit(s->title.fields.contains(key) ? s->title.fields[key].display() : QString());
            connect(le, &QLineEdit::editingFinished, this, [this, key, le] {
                if (updating_) return;
                const Sheet* cs = project_.document().sheet(currentSheet()); if (!cs) return;
                Sheet after = *cs;
                AttrValue v = after.title.fields.value(key, AttrValue::string(QString()));
                if (v.kind == AttrKind::Int) v.value = le->text().toLongLong(); else v.value = le->text();
                if (after.title.fields.value(key).display() == v.display()) return;
                after.title.fields[key] = v;
                commit(after, QStringLiteral("Основная надпись"));
            });
            titleForm_->addRow(l.second, le);
        }
        for (size_t i = 0; i < s->viewports.size(); ++i) vpList_->addItem(QStringLiteral("Вид %1 · %2").arg(i + 1).arg(scaleLabel(s->viewports[i].scale)));
    }
    updating_ = false;
    preview_->setSelectedViewport(vpList_->currentIndex());
    refreshViewportForm();
}

void SheetsPage::refreshViewportForm() {
    const Sheet* s = project_.document().sheet(currentSheet());
    int i = vpList_->currentIndex();
    bool ok = s && i >= 0 && i < int(s->viewports.size());
    for (QWidget* w : {static_cast<QWidget*>(vpX_), static_cast<QWidget*>(vpY_), static_cast<QWidget*>(vpW_), static_cast<QWidget*>(vpH_), static_cast<QWidget*>(vpScale_), static_cast<QWidget*>(vpCx_), static_cast<QWidget*>(vpCy_), static_cast<QWidget*>(vpBorder_)}) w->setEnabled(ok);
    if (!ok) { vpWarn_->clear(); return; }
    updating_ = true;
    const SheetViewport& v = s->viewports[size_t(i)];
    vpX_->setValue(v.frame.x0); vpY_->setValue(v.frame.y0); vpW_->setValue(v.frame.width()); vpH_->setValue(v.frame.height());
    vpScale_->setCurrentText(scaleLabel(v.scale));
    vpCx_->setValue(v.modelCenter.x); vpCy_->setValue(v.modelCenter.y); vpBorder_->setChecked(v.border);
    updating_ = false;
    QStringList w = PdfExport::checkClipping(project_.document(), *s);
    vpWarn_->setText(w.isEmpty() ? QStringLiteral("Содержимое помещается в вид.") : w.join('\n'));
}

void SheetsPage::commit(const Sheet& after, const QString& text) {
    const Sheet* before = project_.document().sheet(after.id);
    if (!before) return;
    project_.commands().push(std::make_unique<SheetCommand>(SheetCommand::Update, *before, after, text));
}

void SheetsPage::addSheet() {
    Sheet s;
    s.name = QStringLiteral("Лист %1").arg(project_.document().sheets().size() + 1);
    s.title = TitleBlock::standard();
    s.title.fields["sheet"] = AttrValue::integer(qlonglong(project_.document().sheets().size() + 1));
    SheetViewport vp; vp.frame = Rect{30, 70, 190, 280}; vp.scale = 1.0;
    Rect ext = project_.document().extents();
    if (ext.width() > 0) { vp.modelCenter = ext.center(); double k = std::min(vp.frame.width() / std::max(ext.width(), 1e-9), vp.frame.height() / std::max(ext.height(), 1e-9)) * 0.9;
        for (auto it = scalePresets().rbegin(); it != scalePresets().rend(); ++it) if (it->factor <= k) { vp.scale = it->factor; break; } }
    s.viewports.push_back(vp);
    project_.commands().push(std::make_unique<SheetCommand>(SheetCommand::Add, Sheet{}, s, QStringLiteral("Новый лист")));
    list_->setCurrentRow(list_->count() - 1);
}

void SheetsPage::removeSheet() {
    const Sheet* s = project_.document().sheet(currentSheet());
    if (!s) return;
    if (project_.document().sheets().size() == 1) { QMessageBox::information(this, QStringLiteral("Листы"), QStringLiteral("В проекте должен остаться хотя бы один лист.")); return; }
    if (QMessageBox::question(this, QStringLiteral("Удалить лист"), QStringLiteral("Удалить лист «%1»?").arg(s->name)) != QMessageBox::Yes) return;
    project_.commands().push(std::make_unique<SheetCommand>(SheetCommand::Remove, *s, *s, QStringLiteral("Удалить лист")));
}

void SheetsPage::duplicateSheet() {
    const Sheet* s = project_.document().sheet(currentSheet());
    if (!s) return;
    Sheet c = *s; c.id = 0; c.name = s->name + QStringLiteral(" (копия)");
    for (auto& v : c.viewports) v.id = 0;
    project_.commands().push(std::make_unique<SheetCommand>(SheetCommand::Add, Sheet{}, c, QStringLiteral("Дублировать лист")));
    list_->setCurrentRow(list_->count() - 1);
}

void SheetsPage::addViewport() {
    const Sheet* s = project_.document().sheet(currentSheet());
    if (!s) return;
    Sheet after = *s;
    SheetViewport vp; vp.frame = Rect{s->marginLeft + 10, s->marginBottom + 65, s->marginLeft + 110, s->marginBottom + 145}; vp.scale = 1.0;
    Rect ext = project_.document().extents();
    if (ext.width() > 0) vp.modelCenter = ext.center();
    after.viewports.push_back(vp);
    commit(after, QStringLiteral("Добавить вид"));
    updating_ = true; vpList_->setCurrentIndex(vpList_->count() - 1); updating_ = false;
    preview_->setSelectedViewport(vpList_->currentIndex()); refreshViewportForm();
}

void SheetsPage::removeViewport() {
    const Sheet* s = project_.document().sheet(currentSheet());
    int i = vpList_->currentIndex();
    if (!s || i < 0 || i >= int(s->viewports.size())) return;
    Sheet after = *s; after.viewports.erase(after.viewports.begin() + i);
    commit(after, QStringLiteral("Удалить вид"));
}

void SheetsPage::fitViewport() {
    const Sheet* s = project_.document().sheet(currentSheet());
    int i = vpList_->currentIndex();
    if (!s || i < 0 || i >= int(s->viewports.size())) return;
    Rect ext = project_.document().extents();
    if (ext.width() <= 0 && ext.height() <= 0) return;
    Sheet after = *s;
    SheetViewport& v = after.viewports[size_t(i)];
    v.modelCenter = ext.center();
    double k = std::min(v.frame.width() / std::max(ext.width(), 1e-9), v.frame.height() / std::max(ext.height(), 1e-9)) * 0.92;
    double best = 0;
    for (const ScalePreset& sp : scalePresets()) if (sp.factor <= k && sp.factor > best) best = sp.factor;
    v.scale = best > 0 ? best : k;
    commit(after, QStringLiteral("Вписать вид"));
}

} // namespace psi
