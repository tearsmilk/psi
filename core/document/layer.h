#pragma once
#include "core/geometry/entity.h"
#include <QJsonObject>
#include <QString>

namespace psi {

struct Layer {
    quint32 id = 0;
    QString name = QStringLiteral("0");
    int color = 0x141413;
    double lineWeight = 0.25;        // mm
    LineType lineType = LineType::Solid;
    bool visible = true;
    bool locked = false;
    bool printable = true;

    QJsonObject toJson() const;
    static Layer fromJson(const QJsonObject& o);
    bool operator==(const Layer& o) const {
        return id == o.id && name == o.name && color == o.color && lineWeight == o.lineWeight && lineType == o.lineType &&
               visible == o.visible && locked == o.locked && printable == o.printable;
    }
};

} // namespace psi
