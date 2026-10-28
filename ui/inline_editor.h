#pragma once
// Встроенный редактор над видовым экраном: поля «Длина / Угол», «Ширина × Высота», «Радиус», «Размер», «Текст».
// Enter — применить, Esc — отмена.
#include "modules/draft/edit.h"
#include <QFrame>
#include <QMap>
#include <vector>

class QLineEdit;

namespace psi {

class InlineEditor : public QFrame {
    Q_OBJECT
public:
    InlineEditor(const std::vector<EditField>& fields, Unit unit, int precision, QWidget* parent);
    // place the editor so that its first field sits over `screen` (clamped into the parent)
    void popup(QPointF screen);
    QLineEdit* field(const QString& key) const { return fields_.value(key); }
    void setFieldText(const QString& key, const QString& text);
    // parsed values (mm / degrees) and the text field
    QMap<QString, double> values() const;
    QString text() const;
Q_SIGNALS:
    void applied(const QMap<QString, double>& values, const QString& text);
    void cancelled();
protected:
    void keyPressEvent(QKeyEvent* ev) override;
    bool eventFilter(QObject* watched, QEvent* ev) override;
private:
    void apply();
    std::vector<EditField> spec_;
    Unit unit_;
    QMap<QString, QLineEdit*> fields_;
};

} // namespace psi
