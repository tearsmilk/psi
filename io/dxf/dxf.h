#pragma once
#include "core/document/document.h"
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <functional>

namespace psi {

struct DxfReport {
    QMap<QString, int> counts;      // DXF entity type → count read
    QStringList unsupported;        // types skipped (unique)
    QStringList warnings;
    Unit unit = Unit::Millimeter;
    bool unitGuessed = false;       // $INSUNITS missing or unitless
    int imported = 0;
    int layers = 0;
    QString summary() const;        // Russian, multi-line
};

struct DxfOptions {
    Unit fallbackUnit = Unit::Millimeter;   // used when the file does not declare units
    bool mergeLayers = true;                // reuse document layers with the same name
};

// ASCII DXF (AC1015 / 2000 style) writer and reader with units detection.
struct Dxf {
    static QByteArray write(const Document& doc, const std::vector<quint64>& ids = {});       // empty ids = everything
    // Parses into `entities`/`layers` without touching a document (preview); returns false on a hard error.
    static bool parse(const QByteArray& bytes, std::vector<Entity>* entities, std::vector<Layer>* layers, DxfReport* report,
                      QString* error, const DxfOptions& opt = {}, const std::function<bool()>& cancelled = {});
    // Appends the parsed content to `doc` through plain document calls (callers wrap it in a command when needed).
    static int import(Document& doc, const std::vector<Entity>& entities, const std::vector<Layer>& layers, const DxfOptions& opt = {});
};

} // namespace psi
