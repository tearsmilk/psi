#pragma once
#include "core/geometry/point.h"
#include "core/units/units.h"
#include <QString>
#include <optional>

namespace psi {

// Keyboard coordinate entry (AutoCAD-like):
//   x,y        absolute point           @dx,dy    relative to the last point
//   @len<ang   polar from the last point (angle in degrees)
//   len        a distance along the cursor direction from the last point
struct InputParser {
    static std::optional<Point> point(const QString& text, Unit unit, const std::optional<Point>& last, const std::optional<Point>& cursor);
    static std::optional<double> length(const QString& text, Unit unit) { return Units::parse(text, unit); }
    static std::optional<double> angleDeg(const QString& text);
};

} // namespace psi
