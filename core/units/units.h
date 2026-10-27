#pragma once
#include <QString>
#include <optional>

namespace psi {

// All model coordinates are stored in millimetres; Unit only affects display and input.
enum class Unit { Millimeter, Centimeter, Meter, Inch };

struct Units {
    static double toMm(Unit u);                          // factor: 1 unit = toMm(u) mm
    static QString suffix(Unit u);                       // "мм", "см", "м", "″"
    static QString name(Unit u);                         // "Миллиметры" …
    static QString key(Unit u);                          // "mm" "cm" "m" "in" (for files)
    static std::optional<Unit> fromKey(const QString& key);
    static QString format(double mm, Unit u, int precision, bool withSuffix = false);
    // «800», «80 cm», «2'», «3 1/2"», «1,5м» → millimetres; nullopt when not a number
    static std::optional<double> parse(QString text, Unit defaultUnit);
    static QString formatAngle(double radians, int precision);
};

} // namespace psi
