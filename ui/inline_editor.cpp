#include "ui/inline_editor.h"
#include "ui/theme.h"
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>

namespace psi {

InlineEditor::InlineEditor(const std::vector<EditField>& fields, Unit unit, int precision, QWidget* parent)
    : QFrame(parent), spec_(fields), unit_(unit) {
    setObjectName("InlineEditor");
    setAttribute(Qt::WA_DeleteOnClose);
    const Palette& pal = Theme::current();
    setStyleSheet(QStringLiteral("QFrame#InlineEditor { background: %1; border: 1px solid %2; border-radius: 8px; }"
                                 "QLineEdit { min-width: 84px; padding: 2px 6px; border: 1px solid %2; border-radius: 5px; background: %3; color: %4; selection-background-color: %5; }"
                                 "QLabel { color: %6; }")
                      .arg(pal.card.name(), pal.accent.name(), pal.input.name(), pal.text.name(), pal.accent.name(), pal.muted.name()));
    auto* g = new QGridLayout(this);
    g->setContentsMargins(8, 6, 8, 6); g->setHorizontalSpacing(6); g->setVerticalSpacing(4);
    int row = 0;
    const bool single = fields.size() == 1 && !fields[0].isText;
    for (const EditField& f : fields) {
        auto* ed = new QLineEdit;
        if (f.isText) ed->setText(f.text);
        else if (f.angle) ed->setText(QString::number(f.value, 'f', std::min(precision, 2)) + QStringLiteral("°"));
        else ed->setText(Units::format(f.value, unit, precision, false));
        ed->installEventFilter(this);
        if (!single) g->addWidget(new QLabel(f.label), row, 0);
        g->addWidget(ed, row, single ? 0 : 1);
        fields_[f.key] = ed;
        ++row;
    }
    if (fields.size() > 1) {
        auto* hint = new QLabel(QStringLiteral("Enter — применить · Esc — отмена")); hint->setObjectName("Faint");
        QFont hf = hint->font(); hf.setPointSizeF(hf.pointSizeF() - 1); hint->setFont(hf);
        g->addWidget(hint, row, 0, 1, 2);
    }
    adjustSize();
}

void InlineEditor::popup(QPointF screen) {
    adjustSize();
    QPoint p(static_cast<int>(screen.x()) - 12, static_cast<int>(screen.y()) - height() / 2);
    if (parentWidget()) {
        p.setX(std::max(4, std::min(p.x(), parentWidget()->width() - width() - 4)));
        p.setY(std::max(4, std::min(p.y(), parentWidget()->height() - height() - 4)));
    }
    move(p);
    show(); raise();
    if (!fields_.isEmpty()) {
        QLineEdit* first = fields_.value(spec_.front().key);
        first->setFocus(); first->selectAll();
    }
}

void InlineEditor::setFieldText(const QString& key, const QString& text) { if (QLineEdit* e = fields_.value(key)) e->setText(text); }

QMap<QString, double> InlineEditor::values() const {
    QMap<QString, double> out;
    for (const EditField& f : spec_) {
        if (f.isText) continue;
        QString t = fields_.value(f.key)->text().trimmed();
        if (f.angle) {
            if (t.endsWith(QStringLiteral("°"))) t.chop(1);
            t.replace(',', '.');
            bool ok = false; double v = t.toDouble(&ok);
            if (ok) out[f.key] = v;
        } else if (auto v = Units::parse(t, unit_)) out[f.key] = *v;
    }
    return out;
}

QString InlineEditor::text() const {
    for (const EditField& f : spec_) if (f.isText) return fields_.value(f.key)->text();
    return {};
}

void InlineEditor::apply() {
    QMap<QString, double> v = values();
    // only fields the user changed are reported so that linked fields (radius / diameter) do not fight
    QMap<QString, double> changed;
    for (const EditField& f : spec_) {
        if (f.isText || !v.contains(f.key)) continue;
        if (std::abs(v[f.key] - f.value) > 1e-9) changed[f.key] = v[f.key];
    }
    if (changed.isEmpty() && !v.isEmpty()) changed = v;   // nothing differs: pass everything, applyEdit will no-op
    Q_EMIT applied(changed, text());
    close();
}

void InlineEditor::keyPressEvent(QKeyEvent* ev) {
    if (ev->key() == Qt::Key_Escape) { Q_EMIT cancelled(); close(); return; }
    if (ev->key() == Qt::Key_Return || ev->key() == Qt::Key_Enter) { apply(); return; }
    QFrame::keyPressEvent(ev);
}

bool InlineEditor::eventFilter(QObject* watched, QEvent* ev) {
    if (ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Escape) { Q_EMIT cancelled(); close(); return true; }
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) { apply(); return true; }
    }
    return QFrame::eventFilter(watched, ev);
}

} // namespace psi
