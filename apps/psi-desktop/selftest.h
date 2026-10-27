#pragma once

namespace psi {
// Headless end-to-end check (used by CI after the .dmg build): demo scenario, .psi / DXF round trips, PDF page size,
// viewport render. Returns 0 on success.
int runSelfTest();
}
