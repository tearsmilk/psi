#include "workspace_common.h"
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace psi {

QWidget* makeSection(const QString& title, QWidget* content, QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w); v->setContentsMargins(10, 8, 10, 8); v->setSpacing(6);
    auto* h = new QLabel(title); h->setObjectName("H2"); v->addWidget(h);
    v->addWidget(content, 1);
    return w;
}

QWidget* makeSideColumn(const std::vector<QWidget*>& sections, int width) {
    auto* inner = new QWidget; inner->setObjectName("Card");
    auto* v = new QVBoxLayout(inner); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
    for (QWidget* s : sections) v->addWidget(s);
    v->addStretch(1);
    auto* scroll = new QScrollArea; scroll->setWidget(inner); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedWidth(width);
    return scroll;
}

} // namespace psi
