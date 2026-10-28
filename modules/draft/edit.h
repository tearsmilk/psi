#pragma once
// Редактирование размеров объектов по значению (двойной щелчок → встроенный редактор).
#include "core/command-system/command.h"
#include "core/document/document.h"
#include <QMap>
#include <QString>
#include <memory>
#include <vector>

namespace psi {

// Одно поле редактора: число (мм или градусы — `angle`) или текст.
struct EditField {
    QString key;      // "value", "length", "angle", "width", "height", "radius", "diameter", "text"
    QString label;    // Russian label
    double value = 0; // mm (or degrees when angle)
    bool angle = false;
    bool isText = false;
    QString text;
};

// Fields the inline editor shows for an entity; empty when the entity is not editable this way
// (dimension → value; line → length / angle; rectangle → width × height; circle → radius / diameter; text / leader → text).
std::vector<EditField> editFields(const Document& doc, const Entity& e);

// Build the undoable command applying `values` (key → mm / degrees / text). nullptr if nothing changes.
// A linear dimension moves the geometry it measures: every entity with a vertex at the second measured point
// has its vertices lying at or beyond that point along the dimension direction shifted (a rectangle widens);
// a radial dimension changes the circle / arc radius.
std::unique_ptr<EntityCommand> applyEdit(const Document& doc, const Entity& e, const QMap<QString, double>& values, const QString& text = QString());

// closed 4-point polyline with right angles
bool isRectangle(const Entity& e);

} // namespace psi
