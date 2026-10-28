#pragma once
#include "core/units/units.h"
#include <QJsonObject>

namespace psi {

enum SnapFlag : unsigned {
    SnapEndpoint = 1, SnapMidpoint = 2, SnapCenter = 4, SnapQuadrant = 8, SnapIntersection = 16,
    SnapPerpendicular = 32, SnapTangent = 64, SnapGrid = 128, SnapAll = 255
};

struct DocumentSettings {
    Unit unit = Unit::Millimeter;
    int precision = 2;
    double gridSpacing = 10;      // mm
    bool gridVisible = true;
    bool snapEnabled = true;
    bool gridSnap = true;         // «магнит» к круглым координатам
    double snapStep = 1;          // шаг магнита, mm
    unsigned snapFlags = SnapAll & ~SnapGrid;
    bool ortho = false;
    bool polar = true;            // 45° tracking
    double polarStep = 45;        // degrees
    double textHeight = 3.5;      // default for new text / dimensions
    double tolerance = 1e-6;      // geometric tolerance, mm
    QString author;

    QJsonObject toJson() const;
    static DocumentSettings fromJson(const QJsonObject& o);
    bool operator==(const DocumentSettings& o) const {
        return unit == o.unit && precision == o.precision && gridSpacing == o.gridSpacing && gridVisible == o.gridVisible &&
               snapEnabled == o.snapEnabled && gridSnap == o.gridSnap && snapStep == o.snapStep && snapFlags == o.snapFlags && ortho == o.ortho && polar == o.polar &&
               polarStep == o.polarStep && textHeight == o.textHeight && tolerance == o.tolerance && author == o.author;
    }
};

} // namespace psi
