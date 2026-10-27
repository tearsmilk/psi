#include "core/units/units.h"
#include "core/geometry/point.h"
#include <QLocale>
#include <QRegularExpression>
#include <cmath>

namespace psi {

double Units::toMm(Unit u) {
    switch (u) {
    case Unit::Millimeter: return 1.0;
    case Unit::Centimeter: return 10.0;
    case Unit::Meter: return 1000.0;
    case Unit::Inch: return 25.4;
    }
    return 1.0;
}

QString Units::suffix(Unit u) {
    switch (u) {
    case Unit::Millimeter: return QStringLiteral("мм");
    case Unit::Centimeter: return QStringLiteral("см");
    case Unit::Meter: return QStringLiteral("м");
    case Unit::Inch: return QStringLiteral("″");
    }
    return {};
}

QString Units::name(Unit u) {
    switch (u) {
    case Unit::Millimeter: return QStringLiteral("Миллиметры");
    case Unit::Centimeter: return QStringLiteral("Сантиметры");
    case Unit::Meter: return QStringLiteral("Метры");
    case Unit::Inch: return QStringLiteral("Дюймы");
    }
    return {};
}

QString Units::key(Unit u) {
    switch (u) {
    case Unit::Millimeter: return "mm";
    case Unit::Centimeter: return "cm";
    case Unit::Meter: return "m";
    case Unit::Inch: return "in";
    }
    return "mm";
}

std::optional<Unit> Units::fromKey(const QString& key) {
    const QString k = key.trimmed().toLower();
    if (k == "mm" || k == "мм") return Unit::Millimeter;
    if (k == "cm" || k == "см") return Unit::Centimeter;
    if (k == "m" || k == "м") return Unit::Meter;
    if (k == "in" || k == "inch" || k == "\"" || k == "″" || k == "дюйм") return Unit::Inch;
    return std::nullopt;
}

QString Units::format(double mm, Unit u, int precision, bool withSuffix) {
    double v = mm / toMm(u);
    if (std::abs(v) < 0.5 * std::pow(10.0, -precision)) v = 0;   // no «-0»
    QString s = QString::number(v, 'f', std::max(0, precision));
    if (s.contains('.')) {   // trim trailing zeros but keep at least one decimal when precision > 0
        while (s.endsWith('0')) s.chop(1);
        if (s.endsWith('.')) s.chop(1);
    }
    return withSuffix ? s + " " + suffix(u) : s;
}

std::optional<double> Units::parse(QString text, Unit defaultUnit) {
    text = text.trimmed().replace(',', '.');
    if (text.isEmpty()) return std::nullopt;
    static const QRegularExpression re(
        R"(^\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)(?:\s+(\d+)/(\d+))?\s*([a-zA-Zа-яА-Я″"']*)\s*$)");
    const auto m = re.match(text);
    if (!m.hasMatch()) return std::nullopt;
    bool ok = false;
    double v = m.captured(1).toDouble(&ok);
    if (!ok) return std::nullopt;
    if (!m.captured(2).isEmpty()) {
        double den = m.captured(3).toDouble();
        if (den > 0) v += (v < 0 ? -1 : 1) * m.captured(2).toDouble() / den;
    }
    QString suf = m.captured(4);
    Unit u = defaultUnit;
    if (suf == "'" ) return v * 304.8;   // feet
    if (!suf.isEmpty()) {
        auto fu = fromKey(suf);
        if (!fu) return std::nullopt;
        u = *fu;
    }
    return v * toMm(u);
}

QString Units::formatAngle(double radians, int precision) {
    QString s = QString::number(deg(radians), 'f', std::max(0, precision));
    if (s.contains('.')) { while (s.endsWith('0')) s.chop(1); if (s.endsWith('.')) s.chop(1); }
    return s + "°";
}

} // namespace psi
