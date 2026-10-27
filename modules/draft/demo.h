#pragma once
#include "core/project/project.h"

namespace psi {

// Demo scenario (ТЗ §22): a room plan 6000×4000 mm with walls, a door and a window, dimensions, texts, hatch,
// an A3 landscape sheet at 1:50 with a filled title block. Used by the welcome page, `psi --selftest` and tests.
struct Demo {
    static void build(Project& project);
    static constexpr double kRoomWidth = 6000, kRoomHeight = 4000, kWall = 200;
};

} // namespace psi
