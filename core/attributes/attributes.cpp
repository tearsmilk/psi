#include "core/attributes/attributes.h"
#include <QJsonArray>

namespace psi {

QString attrKindKey(AttrKind k) {
    switch (k) {
    case AttrKind::String: return "string";
    case AttrKind::Int: return "int";
    case AttrKind::Double: return "double";
    case AttrKind::Quantity: return "quantity";
    case AttrKind::Bool: return "bool";
    case AttrKind::Enum: return "enum";
    case AttrKind::Ref: return "ref";
    case AttrKind::List: return "list";
    case AttrKind::Material: return "material";
    case AttrKind::Date: return "date";
    case AttrKind::Note: return "note";
    }
    return "string";
}

AttrKind attrKindFromKey(const QString& k) {
    static const QMap<QString, AttrKind> m = {
        {"string", AttrKind::String}, {"int", AttrKind::Int}, {"double", AttrKind::Double}, {"quantity", AttrKind::Quantity},
        {"bool", AttrKind::Bool}, {"enum", AttrKind::Enum}, {"ref", AttrKind::Ref}, {"list", AttrKind::List},
        {"material", AttrKind::Material}, {"date", AttrKind::Date}, {"note", AttrKind::Note}};
    return m.value(k, AttrKind::String);
}

QString attrKindName(AttrKind k) {
    switch (k) {
    case AttrKind::String: return QStringLiteral("Строка");
    case AttrKind::Int: return QStringLiteral("Целое");
    case AttrKind::Double: return QStringLiteral("Число");
    case AttrKind::Quantity: return QStringLiteral("Величина с единицей");
    case AttrKind::Bool: return QStringLiteral("Да / нет");
    case AttrKind::Enum: return QStringLiteral("Перечисление");
    case AttrKind::Ref: return QStringLiteral("Ссылка на объект");
    case AttrKind::List: return QStringLiteral("Список");
    case AttrKind::Material: return QStringLiteral("Материал");
    case AttrKind::Date: return QStringLiteral("Дата");
    case AttrKind::Note: return QStringLiteral("Примечание");
    }
    return {};
}

QString AttrValue::display() const {
    switch (kind) {
    case AttrKind::Quantity: return QString::number(value.toDouble()) + (unit.isEmpty() ? "" : " " + unit);
    case AttrKind::Bool: return value.toBool() ? QStringLiteral("да") : QStringLiteral("нет");
    case AttrKind::List: return value.toStringList().join(", ");
    case AttrKind::Ref: return "#" + QString::number(value.toULongLong());
    default: return value.toString();
    }
}

QJsonObject AttrValue::toJson() const {
    QJsonObject o;
    o["kind"] = attrKindKey(kind);
    switch (kind) {
    case AttrKind::Int: o["value"] = value.toLongLong(); break;
    case AttrKind::Double:
    case AttrKind::Quantity: o["value"] = value.toDouble(); break;
    case AttrKind::Bool: o["value"] = value.toBool(); break;
    case AttrKind::Ref: o["value"] = QString::number(value.toULongLong()); break;
    case AttrKind::List: o["value"] = QJsonArray::fromStringList(value.toStringList()); break;
    default: o["value"] = value.toString(); break;
    }
    if (!unit.isEmpty()) o["unit"] = unit;
    if (!options.isEmpty()) o["options"] = QJsonArray::fromStringList(options);
    return o;
}

AttrValue AttrValue::fromJson(const QJsonObject& o) {
    AttrValue a;
    a.kind = attrKindFromKey(o.value("kind").toString());
    const QJsonValue v = o.value("value");
    switch (a.kind) {
    case AttrKind::Int: a.value = static_cast<qlonglong>(v.toDouble()); break;
    case AttrKind::Double:
    case AttrKind::Quantity: a.value = v.toDouble(); break;
    case AttrKind::Bool: a.value = v.toBool(); break;
    case AttrKind::Ref: a.value = static_cast<qulonglong>(v.toString().toULongLong()); break;
    case AttrKind::List: {
        QStringList l;
        for (const auto& x : v.toArray()) l << x.toString();
        a.value = l;
        break;
    }
    default: a.value = v.toString(); break;
    }
    a.unit = o.value("unit").toString();
    for (const auto& x : o.value("options").toArray()) a.options << x.toString();
    return a;
}

QJsonObject attributesToJson(const AttributeMap& m) {
    QJsonObject o;
    for (auto it = m.cbegin(); it != m.cend(); ++it) o[it.key()] = it.value().toJson();
    return o;
}

AttributeMap attributesFromJson(const QJsonObject& o) {
    AttributeMap m;
    for (auto it = o.begin(); it != o.end(); ++it) m[it.key()] = AttrValue::fromJson(it.value().toObject());
    return m;
}

} // namespace psi
