#include "welcome.h"
#include "core/project/project.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace psi {

WelcomePage::WelcomePage(QWidget* parent) : QWidget(parent) {
    const Palette& pal = Theme::current();
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addStretch(1);

    auto* card = new QWidget; card->setObjectName("Card"); card->setFixedWidth(560);
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(40, 40, 40, 40);
    v->setSpacing(14);

    auto* logo = new QLabel; logo->setPixmap(Icons::logo(72, pal.onAccent, pal.accent)); logo->setAlignment(Qt::AlignLeft);
    v->addWidget(logo);
    auto* title = new QLabel(QStringLiteral("Пси")); title->setObjectName("Title");
    v->addWidget(title);
    auto* sub = new QLabel(QStringLiteral("Универсальная CAD-система. Этап 00–01: черчение, листы, PDF, DXF."));
    sub->setObjectName("Hint"); sub->setWordWrap(true);
    v->addWidget(sub);
    v->addSpacing(10);

    auto* row = new QHBoxLayout; row->setSpacing(10);
    auto mk = [&](const QString& text, const QString& icon, bool accent) {
        auto* b = new QPushButton(Icons::icon(icon, accent ? pal.onAccent : pal.text), text);
        if (accent) b->setObjectName("Accent");
        b->setMinimumHeight(36);
        row->addWidget(b);
        return b;
    };
    connect(mk(QStringLiteral("Новый проект"), "new", true), &QPushButton::clicked, this, &WelcomePage::newRequested);
    connect(mk(QStringLiteral("Открыть…"), "open", false), &QPushButton::clicked, this, [this] { Q_EMIT openRequested(QString()); });
    connect(mk(QStringLiteral("Импорт DXF…"), "draft", false), &QPushButton::clicked, this, &WelcomePage::importDxfRequested);
    connect(mk(QStringLiteral("Демонстрация"), "sheet", false), &QPushButton::clicked, this, &WelcomePage::demoRequested);
    v->addLayout(row);

    v->addSpacing(16);
    auto* h2 = new QLabel(QStringLiteral("Недавние")); h2->setObjectName("H2");
    v->addWidget(h2);
    recent_ = new QListWidget;
    recent_->setFrameShape(QFrame::NoFrame);
    recent_->setMinimumHeight(180);
    connect(recent_, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) { Q_EMIT openRequested(it->data(Qt::UserRole).toString()); });
    v->addWidget(recent_, 1);
    auto* hint = new QLabel(QStringLiteral("Файлы .psi и .dxf можно перетащить в окно. Ввод координат: «100,50», «@40<30», длина вдоль курсора."));
    hint->setObjectName("Faint"); hint->setWordWrap(true);
    v->addWidget(hint);

    outer->addWidget(card, 0, Qt::AlignVCenter);
    outer->addStretch(1);
    refresh();
}

void WelcomePage::refresh() {
    recent_->clear();
    for (const QString& p : Project::recentFiles()) {
        QFileInfo fi(p);
        auto* it = new QListWidgetItem(Icons::icon("sheet", Theme::current().muted), fi.fileName() + QStringLiteral("   ") + fi.absolutePath());
        it->setData(Qt::UserRole, p);
        it->setToolTip(p);
        recent_->addItem(it);
    }
    if (recent_->count() == 0) { auto* it = new QListWidgetItem(QStringLiteral("Пока пусто")); it->setFlags(Qt::NoItemFlags); recent_->addItem(it); }
}

} // namespace psi
