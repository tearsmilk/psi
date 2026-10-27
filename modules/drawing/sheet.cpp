#include "modules/drawing/sheet.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <cmath>

namespace psi {

const std::vector<PaperSize>& paperSizes() {
    static const std::vector<PaperSize> s = {{"A0", 841, 1189}, {"A1", 594, 841}, {"A2", 420, 594}, {"A3", 297, 420},
                                             {"A4", 210, 297}, {"A5", 148, 210}, {"A6", 105, 148}};
    return s;
}

const std::vector<ScalePreset>& scalePresets() {
    static const std::vector<ScalePreset> s = {{"1:1", 1}, {"1:2", 0.5}, {"1:5", 0.2}, {"1:10", 0.1}, {"1:20", 0.05}, {"1:50", 0.02},
                                               {"1:100", 0.01}, {"2:1", 2}, {"5:1", 5}, {"10:1", 10}};
    return s;
}

QString scaleLabel(double factor) {
    for (const auto& p : scalePresets()) if (std::abs(p.factor - factor) < 1e-9) return p.label;
    if (factor <= 0) return "—";
    if (factor >= 1) return QString::number(factor, 'g', 4) + ":1";
    return "1:" + QString::number(1.0 / factor, 'g', 4);
}

double parseScale(const QString& text) {
    static const QRegularExpression re(R"(^\s*([\d.,]+)\s*:\s*([\d.,]+)\s*$)");
    QString t = text.trimmed();
    for (const auto& p : scalePresets()) if (p.label == t) return p.factor;
    auto m = re.match(t);
    if (m.hasMatch()) {
        double a = m.captured(1).replace(',', '.').toDouble(), b = m.captured(2).replace(',', '.').toDouble();
        return (a > 0 && b > 0) ? a / b : 0;
    }
    bool ok = false;
    double v = QString(t).replace(',', '.').toDouble(&ok);
    return ok && v > 0 ? v : 0;
}

Rect SheetViewport::modelWindow() const {
    double w = frame.width() / scale, h = frame.height() / scale;
    return {modelCenter.x - w / 2, modelCenter.y - h / 2, modelCenter.x + w / 2, modelCenter.y + h / 2};
}

Transform SheetViewport::modelToSheet() const {
    Point fc = frame.center();
    return Transform::translate(fc.x, fc.y) * Transform::scale(scale, scale) * Transform::translate(-modelCenter.x, -modelCenter.y);
}

QJsonObject SheetViewport::toJson() const {
    QJsonObject o;
    o["id"] = QString::number(id);
    o["frame"] = QJsonArray{frame.x0, frame.y0, frame.x1, frame.y1};
    o["center"] = QJsonArray{modelCenter.x, modelCenter.y};
    o["scale"] = scale;
    o["border"] = border;
    return o;
}

SheetViewport SheetViewport::fromJson(const QJsonObject& o) {
    SheetViewport v;
    v.id = o.value("id").toString().toULongLong();
    auto f = o.value("frame").toArray();
    if (f.size() == 4) v.frame = Rect(f[0].toDouble(), f[1].toDouble(), f[2].toDouble(), f[3].toDouble());
    auto c = o.value("center").toArray();
    if (c.size() == 2) v.modelCenter = {c[0].toDouble(), c[1].toDouble()};
    v.scale = o.value("scale").toDouble(1);
    if (v.scale <= 0) v.scale = 1;
    v.border = o.value("border").toBool(false);
    return v;
}

TitleBlock TitleBlock::standard() {
    TitleBlock t;
    t.fields["name"] = AttrValue::string(QStringLiteral("Деталь"));
    t.fields["designation"] = AttrValue::string(QStringLiteral("ПСИ.000.001"));
    t.fields["material"] = AttrValue::material(QStringLiteral(""));
    t.fields["scale"] = AttrValue::string(QStringLiteral("1:1"));
    t.fields["sheet"] = AttrValue::integer(1);
    t.fields["sheets"] = AttrValue::integer(1);
    t.fields["author"] = AttrValue::string(QStringLiteral(""));
    t.fields["checked"] = AttrValue::string(QStringLiteral(""));
    t.fields["date"] = AttrValue::date(QStringLiteral(""));
    t.fields["org"] = AttrValue::string(QStringLiteral(""));
    return t;
}

QJsonObject TitleBlock::toJson() const {
    QJsonObject o;
    o["enabled"] = enabled;
    o["fields"] = attributesToJson(fields);
    return o;
}

TitleBlock TitleBlock::fromJson(const QJsonObject& o) {
    TitleBlock t = standard();
    t.enabled = o.value("enabled").toBool(true);
    AttributeMap f = attributesFromJson(o.value("fields").toObject());
    for (auto it = f.cbegin(); it != f.cend(); ++it) t.fields[it.key()] = it.value();
    return t;
}

QString Sheet::paperName() const {
    for (const auto& p : paperSizes()) if (std::abs(p.width - width) < 0.01 && std::abs(p.height - height) < 0.01) return p.name;
    return QStringLiteral("%1 × %2 мм").arg(width).arg(height);
}

QJsonObject Sheet::toJson() const {
    QJsonObject o;
    o["id"] = QString::number(id);
    o["name"] = name;
    o["width"] = width; o["height"] = height; o["landscape"] = landscape;
    o["margins"] = QJsonArray{marginLeft, marginTop, marginRight, marginBottom};
    o["frame"] = frame;
    o["title"] = title.toJson();
    QJsonArray vps;
    for (const auto& v : viewports) vps.push_back(v.toJson());
    o["viewports"] = vps;
    return o;
}

Sheet Sheet::fromJson(const QJsonObject& o) {
    Sheet s;
    s.id = o.value("id").toString().toULongLong();
    s.name = o.value("name").toString(s.name);
    s.width = o.value("width").toDouble(210); s.height = o.value("height").toDouble(297);
    s.landscape = o.value("landscape").toBool(false);
    auto m = o.value("margins").toArray();
    if (m.size() == 4) { s.marginLeft = m[0].toDouble(); s.marginTop = m[1].toDouble(); s.marginRight = m[2].toDouble(); s.marginBottom = m[3].toDouble(); }
    s.frame = o.value("frame").toBool(true);
    s.title = TitleBlock::fromJson(o.value("title").toObject());
    for (const auto& v : o.value("viewports").toArray()) s.viewports.push_back(SheetViewport::fromJson(v.toObject()));
    return s;
}

} // namespace psi
