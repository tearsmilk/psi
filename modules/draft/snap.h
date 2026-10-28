#pragma once
#include "core/document/document.h"
#include <optional>

namespace psi {

struct SnapResult {
    Point p;
    SnapKind kind = SnapKind::Grid;
    quint64 entity = 0;
    bool valid = false;
};

struct SnapEngine {
    // `radius` in world units; `from` enables perpendicular / tangent candidates relative to the previous point
    static SnapResult snap(const Document& doc, Point cursor, double radius, const std::optional<Point>& from = std::nullopt,
                           unsigned flagsOverride = 0, double gridRadius = -1);
    // nearest multiple of `step` (per axis)
    static Point roundToStep(Point p, double step);
    // ortho / polar constraint of `p` relative to `from` (returns p when neither applies)
    static Point constrain(const DocumentSettings& s, Point from, Point p, bool orthoHeld);
    static QString kindName(SnapKind k);
};

} // namespace psi
