#pragma once
#include <QString>
#include <functional>

namespace psi {
// Headless end-to-end check (used by CI after the .dmg build): demo scenario, .psi / DXF round trips, PDF page size,
// viewport render. Returns 0 on success.
int runSelfTest();
// Проверки рабочих пространств «Эскиз» и «Интерьер» (modules/sketcher, modules/interior). Возвращает число ошибок.
int runModulesSelfTest(const std::function<void(bool, const QString&)>& check);
}
