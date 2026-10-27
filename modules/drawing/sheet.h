#pragma once
#include "core/attributes/attributes.h"
#include "core/geometry/point.h"
#include <QJsonObject>
#include <QString>
#include <vector>

namespace psi {

struct PaperSize { QString name; double width, height; };   // portrait, mm
const std::vector<PaperSize>& paperSizes();                   // A0…A6
struct ScalePreset { QString label; double factor; };         // factor = sheet mm per model mm (1:5 → 0.2)
const std::vector<ScalePreset>& scalePresets();
QString scaleLabel(double factor);
double parseScale(const QString& text);                       // «1:5», «2:1», «0.2» → factor, 0 when invalid

// A window onto model space placed on the sheet. Sheet coordinates: mm, origin bottom-left, y up.
struct SheetViewport {
    quint64 id = 0;
    Rect frame{10, 10, 100, 100};      // on the sheet, mm
    Point modelCenter;                 // model point shown at the frame centre
    double scale = 1.0;                // sheet mm per model mm
    bool border = false;
    QJsonObject toJson() const;
    static SheetViewport fromJson(const QJsonObject& o);
    Rect modelWindow() const;          // model-space rectangle visible in the frame
    Transform modelToSheet() const;
};

struct TitleBlock {
    bool enabled = true;
    AttributeMap fields;               // name, designation, material, scale, sheet, sheets, author, checked, date, org
    static TitleBlock standard();
    QJsonObject toJson() const;
    static TitleBlock fromJson(const QJsonObject& o);
};

struct Sheet {
    quint64 id = 0;
    QString name = QStringLiteral("Лист 1");
    double width = 210, height = 297;  // portrait dimensions, mm
    bool landscape = false;
    double marginLeft = 20, marginTop = 5, marginRight = 5, marginBottom = 5;   // ГОСТ-like default
    bool frame = true;
    TitleBlock title;
    std::vector<SheetViewport> viewports;

    double pageWidth() const { return landscape ? height : width; }
    double pageHeight() const { return landscape ? width : height; }
    Rect frameRect() const { return {marginLeft, marginBottom, pageWidth() - marginRight, pageHeight() - marginTop}; }
    QString paperName() const;
    QJsonObject toJson() const;
    static Sheet fromJson(const QJsonObject& o);
};

} // namespace psi
