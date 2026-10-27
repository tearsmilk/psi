#include "modules/draft/input.h"
#include <QStringList>

namespace psi {

std::optional<double> InputParser::angleDeg(const QString& text) {
    QString t = text.trimmed();
    if (t.endsWith(QStringLiteral("°"))) t.chop(1);
    t.replace(',', '.');
    bool ok = false;
    double v = t.toDouble(&ok);
    if (!ok) return std::nullopt;
    return v;
}

std::optional<Point> InputParser::point(const QString& textIn, Unit unit, const std::optional<Point>& last, const std::optional<Point>& cursor) {
    QString text = textIn.trimmed();
    if (text.isEmpty()) return std::nullopt;
    bool relative = text.startsWith('@');
    if (relative) text = text.mid(1).trimmed();
    // decimal comma vs separator comma: «12,5» alone is a number, «12,5;7» → use ; or whitespace; «12,7» with two parts → x,y
    QStringList parts;
    if (text.contains('<')) {
        parts = text.split('<');
        if (parts.size() != 2) return std::nullopt;
        auto len = Units::parse(parts[0], unit);
        auto ang = angleDeg(parts[1]);
        if (!len || !ang) return std::nullopt;
        Point base = relative && last ? *last : Point{};
        return base + Point::polar(*len, rad(*ang));
    }
    if (text.contains(';')) parts = text.split(';');
    else if (text.count(',') == 1 && !text.contains('.')) parts = text.split(',');
    else if (text.contains(' ')) parts = text.split(' ', Qt::SkipEmptyParts);
    else if (text.count(',') >= 1 && text.contains('.')) parts = text.split(',');
    if (parts.size() == 2) {
        auto x = Units::parse(parts[0], unit), y = Units::parse(parts[1], unit);
        if (!x || !y) return std::nullopt;
        Point p{*x, *y};
        if (relative && last) p = *last + p;
        return p;
    }
    // single number: distance along the cursor direction from the last point
    auto len = Units::parse(text, unit);
    if (!len || !last) return std::nullopt;
    if (!cursor) return std::nullopt;
    Vec d = (*cursor - *last).normalized();
    if (d.length() < kEps) d = {1, 0};
    return *last + d * *len;
}

} // namespace psi
