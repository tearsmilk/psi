#pragma once
// STEP AP214 (ISO 10303-21) через STEPControl_Reader / Writer: тела в миллиметрах с именами продуктов.
// Отчёт по образцу DXF: единицы, пропущенное, предупреждения, отмена.
#include "modules/model/kernel.h"
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

namespace psi {

struct StepBody { QString name; md::Shape shape; };

struct StepReport {
    int roots = 0;                  // корней в файле
    int bodies = 0;                 // перенесённых тел
    int faces = 0;
    QString unit = QStringLiteral("мм");
    QStringList unsupported;        // пропущенные сущности (например, сетки без геометрии)
    QStringList warnings;
    bool cancelled = false;
    QString summary() const;        // по-русски, многострочно
};

struct Step {
    // Запись: все тела в один файл, имена → PRODUCT. Возвращает false и error при неудаче.
    static bool write(const QString& path, const std::vector<StepBody>& bodies, QString* error = nullptr, StepReport* report = nullptr);
    // Чтение: каждый корень → тело (имена «Тело N»: PRODUCT-имена без XDE не переносятся — указано в отчёте).
    static std::vector<StepBody> read(const QString& path, StepReport* report, QString* error = nullptr, const std::function<bool()>& cancelled = {});
};

} // namespace psi
