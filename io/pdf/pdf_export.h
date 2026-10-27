#pragma once
#include "core/document/document.h"
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

namespace psi {

struct PdfExport {
    // Vector PDF with the exact physical page size of every sheet (1200 dpi, zero margins, 1 mm = 1 mm).
    static bool exportSheets(const Document& doc, const std::vector<quint64>& sheetIds, const QString& path, QString* error,
                             QStringList* warnings = nullptr, const std::function<bool(int, int)>& progress = {});
    // Warnings for clipped viewports (content outside the viewport window or the viewport outside the page).
    static QStringList checkClipping(const Document& doc, const Sheet& s);
    static bool fixMediaBoxes(const QString& path, const std::vector<QSizeF>& pagesMm, QString* error);
};

} // namespace psi
