#pragma once
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVariant>

namespace psi {

// Typed user attributes (ТЗ §11). Quantity values are stored in millimetres (or the raw number with its unit label).
enum class AttrKind { String, Int, Double, Quantity, Bool, Enum, Ref, List, Material, Date, Note };

struct AttrValue {
    AttrKind kind = AttrKind::String;
    QVariant value;          // QString / qlonglong / double / bool / QStringList (List, Enum options in `options`)
    QString unit;            // Quantity: "mm" "kg" "m2" …; Enum: unused
    QStringList options;     // Enum: allowed values

    static AttrValue string(const QString& s) { AttrValue a; a.kind = AttrKind::String; a.value = s; return a; }
    static AttrValue integer(qlonglong v) { AttrValue a; a.kind = AttrKind::Int; a.value = v; return a; }
    static AttrValue number(double v) { AttrValue a; a.kind = AttrKind::Double; a.value = v; return a; }
    static AttrValue quantity(double v, const QString& unit) { AttrValue a; a.kind = AttrKind::Quantity; a.value = v; a.unit = unit; return a; }
    static AttrValue boolean(bool v) { AttrValue a; a.kind = AttrKind::Bool; a.value = v; return a; }
    static AttrValue enumeration(const QString& v, const QStringList& opts) { AttrValue a; a.kind = AttrKind::Enum; a.value = v; a.options = opts; return a; }
    static AttrValue ref(quint64 id) { AttrValue a; a.kind = AttrKind::Ref; a.value = static_cast<qulonglong>(id); return a; }
    static AttrValue list(const QStringList& v) { AttrValue a; a.kind = AttrKind::List; a.value = v; return a; }
    static AttrValue material(const QString& v) { AttrValue a; a.kind = AttrKind::Material; a.value = v; return a; }
    static AttrValue date(const QString& iso) { AttrValue a; a.kind = AttrKind::Date; a.value = iso; return a; }
    static AttrValue note(const QString& v) { AttrValue a; a.kind = AttrKind::Note; a.value = v; return a; }

    QString display() const;
    QJsonObject toJson() const;
    static AttrValue fromJson(const QJsonObject& o);
    bool operator==(const AttrValue& o) const { return kind == o.kind && value == o.value && unit == o.unit && options == o.options; }
};

using AttributeMap = QMap<QString, AttrValue>;

QString attrKindKey(AttrKind k);
AttrKind attrKindFromKey(const QString& k);
QString attrKindName(AttrKind k);   // Russian label
QJsonObject attributesToJson(const AttributeMap& m);
AttributeMap attributesFromJson(const QJsonObject& o);

} // namespace psi
