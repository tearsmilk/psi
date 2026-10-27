#include "ui/icons.h"
#include "ui/theme.h"
#include "core/geometry/point.h"
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace psi {

QStringList Icons::names() {
    return {"select", "line", "polyline", "rect", "circle", "arc", "ellipse", "spline", "text", "leader", "dim", "dim-radius", "dim-angle",
            "hatch", "move", "copy", "rotate", "mirror", "scale", "offset", "trim", "extend", "fillet", "chamfer", "array", "explode",
            "delete", "zoom-extents", "zoom-window", "grid", "snap", "ortho", "polar", "layers", "sheet", "pdf", "undo", "redo", "new", "open",
            "save", "settings", "draft", "sketch", "model", "assembly", "interior", "drawing", "props", "tree", "tools", "eye", "lock", "plus", "minus", "print"};
}

static void draw(QPainter& p, const QString& n, double s) {
    // 20×20 design grid; pen width 1.5
    auto L = [&](double x1, double y1, double x2, double y2) { p.drawLine(QPointF(x1 * s, y1 * s), QPointF(x2 * s, y2 * s)); };
    auto C = [&](double x, double y, double r) { p.drawEllipse(QPointF(x * s, y * s), r * s, r * s); };
    auto R = [&](double x, double y, double w, double h) { p.drawRect(QRectF(x * s, y * s, w * s, h * s)); };
    auto dot = [&](double x, double y) { p.save(); p.setBrush(p.pen().color()); p.drawEllipse(QPointF(x * s, y * s), 1.4 * s, 1.4 * s); p.restore(); };
    auto arrow = [&](double x, double y, double dx, double dy) { double k = 2.4; L(x, y, x - dx * k + dy * 1.2, y - dy * k - dx * 1.2); L(x, y, x - dx * k - dy * 1.2, y - dy * k + dx * 1.2); };
    if (n == "select") { QPainterPath a; a.moveTo(5 * s, 3 * s); a.lineTo(5 * s, 15 * s); a.lineTo(8.5 * s, 12 * s); a.lineTo(11 * s, 17 * s); a.lineTo(13 * s, 16 * s); a.lineTo(10.5 * s, 11.5 * s); a.lineTo(15 * s, 11 * s); a.closeSubpath(); p.drawPath(a); }
    else if (n == "line") { L(4, 16, 16, 4); dot(4, 16); dot(16, 4); }
    else if (n == "polyline") { L(3, 15, 7, 6); L(7, 6, 11, 13); L(11, 13, 17, 4); dot(3, 15); dot(7, 6); dot(11, 13); dot(17, 4); }
    else if (n == "rect") { R(4, 5, 12, 10); dot(4, 5); dot(16, 15); }
    else if (n == "circle") { C(10, 10, 6.5); dot(10, 10); }
    else if (n == "arc") { QRectF r(3.5 * s, 3.5 * s, 13 * s, 13 * s); p.drawArc(r, 20 * 16, 140 * 16); dot(16, 8); dot(4, 8); }
    else if (n == "ellipse") { p.drawEllipse(QRectF(3 * s, 6 * s, 14 * s, 8 * s)); }
    else if (n == "spline") { QPainterPath a; a.moveTo(3 * s, 15 * s); a.cubicTo(6 * s, 2 * s, 11 * s, 20 * s, 17 * s, 5 * s); p.drawPath(a); dot(3, 15); dot(17, 5); }
    else if (n == "text") { L(5, 5, 15, 5); L(10, 5, 10, 16); L(8, 16, 12, 16); }
    else if (n == "leader") { L(4, 16, 11, 7); L(11, 7, 16, 7); arrow(4, 16, -0.7, 0.7); }
    else if (n == "dim") { L(4, 13, 16, 13); arrow(4, 13, -1, 0); arrow(16, 13, 1, 0); L(4, 16, 4, 10); L(16, 16, 16, 10); L(8, 6, 12, 6); }
    else if (n == "dim-radius") { C(10, 11, 6); L(10, 11, 15.2, 8); arrow(15.2, 8, 0.87, -0.5); dot(10, 11); }
    else if (n == "dim-angle") { L(4, 16, 16, 16); L(4, 16, 13, 6); QRectF r(-2 * s, 10 * s, 12 * s, 12 * s); p.drawArc(r, 0, 48 * 16); }
    else if (n == "hatch") { R(4, 4, 12, 12); L(4, 10, 10, 4); L(4, 16, 16, 4); L(10, 16, 16, 10); }
    else if (n == "move") { L(10, 3, 10, 17); L(3, 10, 17, 10); arrow(10, 3, 0, -1); arrow(10, 17, 0, 1); arrow(3, 10, -1, 0); arrow(17, 10, 1, 0); }
    else if (n == "copy") { R(4, 4, 9, 9); R(8, 8, 9, 9); }
    else if (n == "rotate") { QRectF r(4 * s, 4 * s, 12 * s, 12 * s); p.drawArc(r, 30 * 16, 270 * 16); arrow(15.2, 7, 0.5, -0.87); dot(10, 10); }
    else if (n == "mirror") { L(10, 3, 10, 17); QPainterPath a; a.moveTo(8 * s, 6 * s); a.lineTo(3 * s, 14 * s); a.lineTo(8 * s, 14 * s); a.closeSubpath(); p.drawPath(a); QPainterPath b; b.moveTo(12 * s, 6 * s); b.lineTo(17 * s, 14 * s); b.lineTo(12 * s, 14 * s); b.closeSubpath(); p.save(); p.setPen(QPen(p.pen().color(), p.pen().widthF(), Qt::DashLine)); p.drawPath(b); p.restore(); }
    else if (n == "scale") { R(4, 8, 8, 8); R(4, 3, 13, 13); L(12, 8, 17, 3); arrow(17, 3, 0.7, -0.7); }
    else if (n == "offset") { L(4, 14, 16, 14); p.save(); p.setPen(QPen(p.pen().color(), p.pen().widthF(), Qt::DashLine)); L(4, 8, 16, 8); p.restore(); L(10, 13, 10, 9); arrow(10, 9, 0, -1); }
    else if (n == "trim") { L(3, 10, 17, 10); L(10, 3, 10, 17); L(13, 4, 17, 8); L(17, 4, 13, 8); }
    else if (n == "extend") { L(3, 10, 11, 10); p.save(); p.setPen(QPen(p.pen().color(), p.pen().widthF(), Qt::DashLine)); L(11, 10, 16, 10); p.restore(); L(16, 4, 16, 16); }
    else if (n == "fillet") { L(4, 16, 4, 9); L(11, 4, 16, 4); QRectF r(4 * s, 4 * s, 14 * s, 10 * s); p.drawArc(r, 90 * 16, 90 * 16); }
    else if (n == "chamfer") { L(4, 16, 4, 9); L(4, 9, 9, 4); L(9, 4, 16, 4); }
    else if (n == "array") { for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) R(3.5 + i * 4.5, 3.5 + j * 4.5, 3, 3); }
    else if (n == "explode") { L(10, 10, 10, 4); L(10, 10, 15, 13); L(10, 10, 5, 13); L(10, 10, 15, 7); L(10, 10, 5, 7); L(10, 10, 10, 16); }
    else if (n == "delete") { L(5, 6, 15, 6); L(8, 6, 8, 4); L(12, 6, 12, 4); L(6, 6, 7, 17); L(14, 6, 13, 17); L(7, 17, 13, 17); }
    else if (n == "zoom-extents") { L(3, 7, 3, 3); L(3, 3, 7, 3); L(13, 3, 17, 3); L(17, 3, 17, 7); L(17, 13, 17, 17); L(17, 17, 13, 17); L(7, 17, 3, 17); L(3, 17, 3, 13); R(7, 7, 6, 6); }
    else if (n == "zoom-window") { C(9, 9, 5.5); L(13, 13, 17, 17); R(6.5, 6.5, 5, 5); }
    else if (n == "grid") { for (int i = 0; i < 4; ++i) { L(4 + i * 4, 4, 4 + i * 4, 16); L(4, 4 + i * 4, 16, 4 + i * 4); } }
    else if (n == "snap") { C(10, 10, 6); L(10, 2, 10, 6); L(10, 14, 10, 18); L(2, 10, 6, 10); L(14, 10, 18, 10); dot(10, 10); }
    else if (n == "ortho") { L(4, 16, 4, 4); L(4, 16, 16, 16); R(4, 12, 4, 4); }
    else if (n == "polar") { L(4, 16, 16, 16); L(4, 16, 13, 7); QRectF r(-4 * s, 8 * s, 16 * s, 16 * s); p.drawArc(r, 0, 45 * 16); }
    else if (n == "layers") { QPainterPath a; for (int i = 0; i < 3; ++i) { QPainterPath q; q.moveTo(10 * s, (3 + i * 4) * s); q.lineTo(17 * s, (6.5 + i * 4) * s); q.lineTo(10 * s, (10 + i * 4) * s); q.lineTo(3 * s, (6.5 + i * 4) * s); q.closeSubpath(); p.drawPath(q); } }
    else if (n == "sheet") { QPainterPath a; a.moveTo(5 * s, 3 * s); a.lineTo(12 * s, 3 * s); a.lineTo(15 * s, 6 * s); a.lineTo(15 * s, 17 * s); a.lineTo(5 * s, 17 * s); a.closeSubpath(); p.drawPath(a); L(12, 3, 12, 6); L(12, 6, 15, 6); L(8, 13, 12, 13); L(8, 10, 12, 10); }
    else if (n == "pdf") { QPainterPath a; a.moveTo(5 * s, 3 * s); a.lineTo(12 * s, 3 * s); a.lineTo(15 * s, 6 * s); a.lineTo(15 * s, 17 * s); a.lineTo(5 * s, 17 * s); a.closeSubpath(); p.drawPath(a); L(10, 8, 10, 14); arrow(10, 14, 0, 1); }
    else if (n == "print") { R(5, 8, 10, 6); L(7, 8, 7, 4); L(7, 4, 13, 4); L(13, 4, 13, 8); L(7, 14, 7, 17); L(7, 17, 13, 17); L(13, 17, 13, 14); }
    else if (n == "undo") { QRectF r(5 * s, 6 * s, 10 * s, 10 * s); p.drawArc(r, 0, 180 * 16); L(5, 11, 5, 16); arrow(5, 11, 0, -1); L(10, 16, 15, 16); }
    else if (n == "redo") { QRectF r(5 * s, 6 * s, 10 * s, 10 * s); p.drawArc(r, 0, 180 * 16); L(15, 11, 15, 16); arrow(15, 11, 0, -1); L(5, 16, 10, 16); }
    else if (n == "new") { QPainterPath a; a.moveTo(5 * s, 3 * s); a.lineTo(12 * s, 3 * s); a.lineTo(15 * s, 6 * s); a.lineTo(15 * s, 17 * s); a.lineTo(5 * s, 17 * s); a.closeSubpath(); p.drawPath(a); L(10, 8, 10, 14); L(7, 11, 13, 11); }
    else if (n == "open") { QPainterPath a; a.moveTo(3 * s, 5 * s); a.lineTo(8 * s, 5 * s); a.lineTo(10 * s, 7 * s); a.lineTo(17 * s, 7 * s); a.lineTo(17 * s, 16 * s); a.lineTo(3 * s, 16 * s); a.closeSubpath(); p.drawPath(a); }
    else if (n == "save") { QPainterPath a; a.moveTo(3 * s, 3 * s); a.lineTo(14 * s, 3 * s); a.lineTo(17 * s, 6 * s); a.lineTo(17 * s, 17 * s); a.lineTo(3 * s, 17 * s); a.closeSubpath(); p.drawPath(a); R(6, 3, 7, 4); R(6, 11, 8, 6); }
    else if (n == "settings") { C(10, 10, 3); for (int i = 0; i < 8; ++i) { double a = i * kPi / 4; L(10 + 5 * std::cos(a), 10 + 5 * std::sin(a), 10 + 7.5 * std::cos(a), 10 + 7.5 * std::sin(a)); } }
    else if (n == "draft") { L(4, 16, 16, 4); L(4, 16, 4, 11); L(4, 16, 9, 16); }
    else if (n == "sketch") { QPainterPath a; a.moveTo(3 * s, 14 * s); a.cubicTo(6 * s, 4 * s, 12 * s, 18 * s, 17 * s, 6 * s); p.drawPath(a); dot(3, 14); dot(17, 6); }
    else if (n == "model") { QPainterPath a; a.moveTo(10 * s, 3 * s); a.lineTo(17 * s, 7 * s); a.lineTo(17 * s, 14 * s); a.lineTo(10 * s, 18 * s); a.lineTo(3 * s, 14 * s); a.lineTo(3 * s, 7 * s); a.closeSubpath(); p.drawPath(a); L(10, 10, 10, 18); L(10, 10, 17, 7); L(10, 10, 3, 7); }
    else if (n == "assembly") { C(7, 7, 3.5); C(13.5, 12.5, 4); L(9.5, 9.5, 10.5, 10.5); }
    else if (n == "interior") { QPainterPath a; a.moveTo(3 * s, 10 * s); a.lineTo(10 * s, 3 * s); a.lineTo(17 * s, 10 * s); p.drawPath(a); R(5, 10, 10, 7); R(8, 12, 4, 5); }
    else if (n == "drawing") { R(3, 3, 14, 14); L(3, 12, 17, 12); L(10, 12, 10, 17); C(8, 7, 2.5); }
    else if (n == "props") { L(4, 6, 16, 6); L(4, 10, 16, 10); L(4, 14, 16, 14); dot(13, 6); dot(7, 10); dot(11, 14); }
    else if (n == "tree") { L(5, 4, 5, 16); L(5, 8, 10, 8); L(5, 12, 10, 12); L(5, 16, 10, 16); R(10, 6, 5, 4); R(10, 14, 5, 4); R(3, 2, 4, 4); }
    else if (n == "tools") { L(4, 16, 12, 8); C(13.5, 6.5, 3); L(10, 14, 13, 17); }
    else if (n == "eye") { QPainterPath a; a.moveTo(2 * s, 10 * s); a.quadTo(10 * s, 1 * s, 18 * s, 10 * s); a.quadTo(10 * s, 19 * s, 2 * s, 10 * s); p.drawPath(a); C(10, 10, 3); }
    else if (n == "lock") { R(5, 9, 10, 8); QRectF r(7 * s, 3 * s, 6 * s, 10 * s); p.drawArc(r, 0, 180 * 16); }
    else if (n == "plus") { L(10, 4, 10, 16); L(4, 10, 16, 10); }
    else if (n == "minus") { L(4, 10, 16, 10); }
}

QPixmap Icons::pixmap(const QString& name, const QColor& color, int size, qreal dpr) {
    QPixmap pm(static_cast<int>(size * dpr), static_cast<int>(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.5 * size / 20.0);
    pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    draw(p, name, size / 20.0);
    return pm;
}

QIcon Icons::icon(const QString& name, const QColor& color, int size) {
    QIcon ic;
    ic.addPixmap(pixmap(name, color, size, 1.0));
    ic.addPixmap(pixmap(name, color, size, 2.0));
    QColor dis = color; dis.setAlpha(80);
    ic.addPixmap(pixmap(name, dis, size, 1.0), QIcon::Disabled);
    ic.addPixmap(pixmap(name, dis, size, 2.0), QIcon::Disabled);
    return ic;
}

QPixmap Icons::logo(int size, const QColor& color, const QColor& bg) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(0, 0, size, size), size * 0.22, size * 0.22);
    QFont f(Theme::serifFamily());
    f.setPixelSize(static_cast<int>(size * 0.66));
    p.setFont(f);
    p.setPen(color);
    p.drawText(QRectF(0, -size * 0.04, size, size), Qt::AlignCenter, QStringLiteral("Ψ"));
    return pm;
}

} // namespace psi
