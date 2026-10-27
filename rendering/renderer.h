#pragma once
#include "core/document/document.h"
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>

namespace psi {

struct RenderOptions {
    QTransform world;            // world (mm) → device
    double pxPerMm = 1.0;        // device pixels per *paper* millimetre, for line weights and text
    bool forPrint = false;       // monochrome-ish, no selection, honour layer.printable
    QColor ink{0x14, 0x14, 0x13};
    QColor accent{0xD9, 0x77, 0x57};
    bool selected = false;
    bool hovered = false;
    double tol = 0.05;           // flattening tolerance in world mm
    double minWeightPx = 1.0;
};

struct Renderer {
    static QPainterPath worldPath(const Entity& e, double tol);                        // in world coordinates
    static void drawEntity(QPainter& p, const Document& doc, const Entity& e, const RenderOptions& o);
    static void drawText(QPainter& p, const RenderOptions& o, Point pos, const QString& text, double height, double angle, const QColor& color, int align = 0);
    static QString dimensionText(const Document& doc, const Entity& d);
    static QPen pen(const Document& doc, const Entity& e, const RenderOptions& o);
    // Sheet: frame, title block and every viewport's model content. `sheet` maps sheet mm → device.
    static void drawSheet(QPainter& p, const Document& doc, const Sheet& s, const QTransform& sheet, double pxPerMm, bool forPrint);
    static bool viewportFits(const Document& doc, const SheetViewport& vp, Rect* content = nullptr);
};

} // namespace psi
