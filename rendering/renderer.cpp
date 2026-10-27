#include "rendering/renderer.h"
#include <QFont>
#include <QFontMetricsF>
#include <cmath>

namespace psi {

static QPointF q(const Point& p) { return {p.x, p.y}; }

QPainterPath Renderer::worldPath(const Entity& e, double tol) {
    QPainterPath path;
    switch (e.type) {
    case EntityType::Circle: path.addEllipse(q(e.center), e.radius, e.radius); return path;
    case EntityType::Arc: {
        QRectF r(e.center.x - e.radius, e.center.y - e.radius, 2 * e.radius, 2 * e.radius);
        path.arcMoveTo(r, deg(e.startAngle));
        path.arcTo(r, deg(e.startAngle), deg(arcSweep(e)));
        return path;
    }
    case EntityType::Dimension: {
        DimGeometry g = dimensionGeometry(e, nullptr);
        for (auto& s : g.lines) { path.moveTo(q(s.a)); path.lineTo(q(s.b)); }
        for (auto& s : g.arrows) { path.moveTo(q(s.a)); path.lineTo(q(s.b)); }
        if (!g.arc.empty()) { path.moveTo(q(g.arc[0])); for (size_t i = 1; i < g.arc.size(); ++i) path.lineTo(q(g.arc[i])); }
        return path;
    }
    default: break;
    }
    std::vector<Point> pts = flatten(e, tol);
    if (pts.empty()) return path;
    path.moveTo(q(pts[0]));
    for (size_t i = 1; i < pts.size(); ++i) path.lineTo(q(pts[i]));
    if ((e.type == EntityType::Polyline || e.type == EntityType::Spline || e.type == EntityType::Hatch) && e.closed) path.closeSubpath();
    return path;
}

QPen Renderer::pen(const Document& doc, const Entity& e, const RenderOptions& o) {
    QColor c = o.forPrint ? QColor(doc.effectiveColor(e)) : QColor(doc.effectiveColor(e));
    if (o.forPrint && c.lightness() > 200) c = Qt::black;   // white-on-dark colours still print
    if (!o.forPrint && (c == QColor(0x14, 0x14, 0x13) || c == QColor(Qt::black))) c = o.ink;
    if (o.selected) c = o.accent;
    double w = std::max(o.minWeightPx, doc.effectiveLineWeight(e) * o.pxPerMm);
    QPen pen(c, w);
    pen.setCosmetic(true);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    switch (doc.effectiveLineType(e)) {
    case LineType::Dashed: pen.setDashPattern({6, 3}); break;
    case LineType::Dotted: pen.setDashPattern({1, 2}); break;
    case LineType::DashDot: pen.setDashPattern({6, 2, 1, 2}); break;
    default: break;
    }
    if (o.hovered && !o.selected) { pen.setWidthF(w + 1.5); }
    return pen;
}

void Renderer::drawText(QPainter& p, const RenderOptions& o, Point pos, const QString& text, double height, double angle, const QColor& color, int align) {
    if (text.isEmpty()) return;
    const double s = std::sqrt(std::abs(o.world.determinant()));
    double px = height * s;
    if (px < 1.5 && !o.forPrint) {   // too small to read: a placeholder bar
        p.save(); p.setTransform(o.world, false); p.setPen(QPen(color, 0)); p.setBrush(Qt::NoBrush);
        p.drawLine(q(pos), q(pos + Point::polar(0.6 * height * std::max<qsizetype>(1, text.size()), angle)));
        p.restore();
        return;
    }
    QFont f("Helvetica");
    f.setPixelSize(std::max(1.0, px));
    QFontMetricsF fm(f);
    QPointF dev = o.world.map(q(pos));
    // device y is flipped relative to world y → rotate by -angle
    double devAngle = -deg(angle);
    if (o.world.m22() > 0) devAngle = deg(angle);
    p.save();
    p.resetTransform();
    p.translate(dev);
    p.rotate(devAngle);
    p.setFont(f);
    p.setPen(color);
    double w = fm.horizontalAdvance(text);
    double x = align == 1 ? -w / 2 : (align == 2 ? -w : 0);
    const QStringList lines = text.split('\n');
    double y = 0;
    for (const QString& line : lines) {
        double lw = fm.horizontalAdvance(line);
        double lx = align == 1 ? -lw / 2 : (align == 2 ? -lw : x);
        p.drawText(QPointF(lx, y), line);
        y += fm.height() * 1.1;
    }
    p.restore();
}

QString Renderer::dimensionText(const Document& doc, const Entity& d) {
    if (!d.text.isEmpty()) return d.text;
    double v = dimensionValue(d, doc.lookup());
    const auto& s = doc.settings();
    QString t = d.dimKind == DimKind::Angular ? Units::formatAngle(v, s.precision) : Units::format(v, s.unit, s.precision);
    if (d.dimKind == DimKind::Radius) t = "R" + t;
    if (d.dimKind == DimKind::Diameter) t = QStringLiteral("⌀") + t;
    return t;
}

void Renderer::drawEntity(QPainter& p, const Document& doc, const Entity& e, const RenderOptions& o) {
    if (!doc.entityVisible(e)) return;
    if (o.forPrint) { const Layer* l = doc.layer(e.layer); if (l && !l->printable) return; }
    QPen pn = pen(doc, e, o);
    const QColor color = pn.color();
    p.save();
    p.setTransform(o.world, false);
    p.setPen(pn);
    p.setBrush(Qt::NoBrush);
    switch (e.type) {
    case EntityType::Hatch: {
        QPainterPath path = worldPath(e, o.tol);
        if (e.hatch == HatchPattern::Solid) {
            QColor fill = color; fill.setAlpha(o.selected ? 120 : 90);
            p.fillPath(path, fill);
            p.drawPath(path);
        } else {
            p.drawPath(path);
            p.setClipPath(path);
            QRectF bb = path.boundingRect();
            double diag = std::hypot(bb.width(), bb.height());
            QPointF c = bb.center();
            int n = static_cast<int>(diag / std::max(e.hatchSpacing, 0.01)) + 2;
            if (n > 4000) n = 4000;
            int passes = e.hatch == HatchPattern::Cross ? 2 : 1;
            QPen hp = pn; hp.setStyle(Qt::SolidLine); p.setPen(hp);
            for (int pass = 0; pass < passes; ++pass) {
                double a = e.hatchAngle + pass * kPi / 2;
                Point dir = Point::polar(1, a), nrm = dir.perp();
                for (int i = -n; i <= n; ++i) {
                    Point base = Point{c.x(), c.y()} + nrm * (i * e.hatchSpacing);
                    p.drawLine(q(base - dir * diag), q(base + dir * diag));
                }
            }
        }
        break;
    }
    case EntityType::Text:
        p.restore();
        drawText(p, o, e.pts[0], e.text, e.textHeight, e.textAngle, color);
        return;
    case EntityType::Leader: {
        QPainterPath path = worldPath(e, o.tol);
        p.drawPath(path);
        if (e.pts.size() >= 2) {   // arrow head at the first point
            Vec d = (e.pts[0] - e.pts[1]).normalized() * e.textHeight;
            Vec n = d.perp() * 0.35;
            QPainterPath head; head.moveTo(q(e.pts[0])); head.lineTo(q(e.pts[0] - d + n)); head.lineTo(q(e.pts[0] - d - n)); head.closeSubpath();
            p.fillPath(head, color);
        }
        p.restore();
        if (!e.pts.empty()) drawText(p, o, e.pts.back() + Point{e.textHeight * 0.5, -e.textHeight * 0.35}, e.text, e.textHeight, 0, color);
        return;
    }
    case EntityType::Dimension: {
        DimGeometry g = dimensionGeometry(e, doc.lookup());
        QPen dp = pn; dp.setStyle(Qt::SolidLine); p.setPen(dp);
        for (auto& s : g.lines) p.drawLine(q(s.a), q(s.b));
        for (size_t i = 0; i + 1 < g.arrows.size(); i += 2) {
            QPainterPath head; head.moveTo(q(g.arrows[i].a)); head.lineTo(q(g.arrows[i].b)); head.lineTo(q(g.arrows[i + 1].b)); head.closeSubpath();
            p.fillPath(head, color);
        }
        if (!g.arc.empty()) { QPainterPath ap; ap.moveTo(q(g.arc[0])); for (size_t i = 1; i < g.arc.size(); ++i) ap.lineTo(q(g.arc[i])); p.drawPath(ap); }
        p.restore();
        drawText(p, o, g.textPos, dimensionText(doc, e), e.textHeight, g.textAngle, color, 1);
        return;
    }
    default:
        p.drawPath(worldPath(e, o.tol));
        break;
    }
    p.restore();
}

bool Renderer::viewportFits(const Document& doc, const SheetViewport& vp, Rect* content) {
    Rect ext = doc.extents();
    if (content) *content = ext;
    if (!ext.valid()) return true;
    return vp.modelWindow().contains(ext);
}

static void titleBlock(QPainter& p, const Sheet& s, const QTransform& sheet, double pxPerMm, bool forPrint) {
    // ГОСТ-like основная надпись 185×55 in the bottom-right corner of the frame
    const Rect fr = s.frameRect();
    const double W = std::min(185.0, fr.width()), H = std::min(55.0, fr.height());
    const double x0 = fr.x1 - W, y0 = fr.y0;
    QPen thick(forPrint ? Qt::black : QColor(0x14, 0x14, 0x13), std::max(1.0, 0.7 * pxPerMm));
    thick.setCosmetic(true);
    QPen thin = thick; thin.setWidthF(std::max(1.0, 0.25 * pxPerMm));
    p.save();
    p.setTransform(sheet, false);
    p.setBrush(Qt::NoBrush);
    p.setPen(thick);
    p.drawRect(QRectF(x0, y0, W, H));
    // columns: 65 (left table) | 120 (right block)
    const double L = W * 65.0 / 185.0;
    p.drawLine(QPointF(x0 + L, y0), QPointF(x0 + L, y0 + H));
    p.setPen(thin);
    for (int i = 1; i < 11; ++i) p.drawLine(QPointF(x0, y0 + H * i / 11.0), QPointF(x0 + L, y0 + H * i / 11.0));
    double cols[] = {7, 10, 23, 15, 10};
    double cx = x0;
    for (double c : cols) { cx += c * L / 65.0; p.drawLine(QPointF(cx, y0), QPointF(cx, y0 + H)); }
    p.setPen(thick);
    double rowH = H * 3 / 11.0;
    p.drawLine(QPointF(x0 + L, y0 + H - rowH), QPointF(x0 + W, y0 + H - rowH));       // designation row
    p.drawLine(QPointF(x0 + L, y0 + rowH), QPointF(x0 + W, y0 + rowH));               // bottom row
    double rx = x0 + L + (W - L) * 70.0 / 120.0;
    p.drawLine(QPointF(rx, y0 + rowH), QPointF(rx, y0 + H - rowH));
    double rx2 = rx + (W - L) * 20.0 / 120.0;
    p.drawLine(QPointF(rx2, y0 + rowH), QPointF(rx2, y0 + H - rowH));
    p.drawLine(QPointF(rx, y0 + rowH * 1.5), QPointF(x0 + W, y0 + rowH * 1.5));
    p.drawLine(QPointF(rx + (W - L) * 10.0 / 120.0, y0 + rowH * 1.5), QPointF(rx + (W - L) * 10.0 / 120.0, y0 + rowH));
    p.restore();

    RenderOptions o; o.world = sheet; o.pxPerMm = pxPerMm; o.forPrint = forPrint;
    QColor ink = forPrint ? Qt::black : QColor(0x14, 0x14, 0x13);
    auto field = [&](const char* key) { auto it = s.title.fields.find(QString::fromLatin1(key)); return it == s.title.fields.end() ? QString() : it->display(); };
    Renderer::drawText(p, o, {x0 + L + (W - L) / 2, y0 + H - rowH + rowH * 0.35}, field("designation"), 5, 0, ink, 1);
    Renderer::drawText(p, o, {x0 + L + (W - L) * 35.0 / 120.0, y0 + rowH + rowH * 0.75}, field("name"), 4, 0, ink, 1);
    Renderer::drawText(p, o, {x0 + L + (W - L) * 35.0 / 120.0, y0 + rowH * 0.35}, field("material"), 3, 0, ink, 1);
    Renderer::drawText(p, o, {rx + 1.5, y0 + rowH * 1.75}, QStringLiteral("Масса"), 2, 0, ink, 0);
    Renderer::drawText(p, o, {rx + (W - L) * 10.0 / 120.0 + 1.5, y0 + rowH * 1.75}, QStringLiteral("Масштаб"), 2, 0, ink, 0);
    Renderer::drawText(p, o, {rx + (W - L) * 15.0 / 120.0, y0 + rowH * 1.1}, field("scale"), 3.5, 0, ink, 1);
    Renderer::drawText(p, o, {rx + 1.5, y0 + rowH * 0.6}, QStringLiteral("Лист %1").arg(field("sheet")), 2.5, 0, ink, 0);
    Renderer::drawText(p, o, {rx2 + 1.5, y0 + rowH * 0.6}, QStringLiteral("Листов %1").arg(field("sheets")), 2.5, 0, ink, 0);
    Renderer::drawText(p, o, {rx2 + (W - L) * 15.0 / 120.0, y0 + rowH * 0.35}, field("org"), 2.5, 0, ink, 1);
    const char* rows[] = {"Разраб.", "Пров.", "Т.контр.", "", "Н.контр.", "Утв."};
    const QString vals[] = {field("author"), field("checked"), "", "", "", ""};
    for (int i = 0; i < 6; ++i) {
        double y = y0 + H * (5 - i) / 11.0 + H / 11.0 * 0.25;
        Renderer::drawText(p, o, {x0 + 0.8, y}, QString::fromUtf8(rows[i]), 2.2, 0, ink, 0);
        Renderer::drawText(p, o, {x0 + 7 * L / 65.0 + 0.8, y}, vals[i], 2.2, 0, ink, 0);
    }
    Renderer::drawText(p, o, {x0 + (7 + 10 + 23 + 15 + 5) * L / 65.0, y0 + H * 10 / 11.0 + H / 11.0 * 0.25}, field("date"), 2, 0, ink, 1);
}

void Renderer::drawSheet(QPainter& p, const Document& doc, const Sheet& s, const QTransform& sheet, double pxPerMm, bool forPrint) {
    // frame
    if (s.frame) {
        p.save();
        p.setTransform(sheet, false);
        QPen pen(forPrint ? Qt::black : QColor(0x14, 0x14, 0x13), std::max(1.0, 0.7 * pxPerMm));
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        Rect f = s.frameRect();
        p.drawRect(QRectF(f.x0, f.y0, f.width(), f.height()));
        p.restore();
    }
    // viewports
    for (const SheetViewport& vp : s.viewports) {
        QTransform m2s;
        Transform t = vp.modelToSheet();
        m2s.setMatrix(t.a, t.b, 0, t.c, t.d, 0, t.e, t.f, 1);
        RenderOptions o;
        o.world = m2s * sheet;
        o.pxPerMm = pxPerMm;
        o.forPrint = forPrint;
        o.tol = 0.02 / std::max(vp.scale, 1e-6);
        p.save();
        p.setTransform(sheet, false);
        p.setClipRect(QRectF(vp.frame.x0, vp.frame.y0, vp.frame.width(), vp.frame.height()));
        if (vp.border) { QPen pen(QColor(0x14, 0x14, 0x13), std::max(1.0, 0.25 * pxPerMm)); pen.setCosmetic(true); p.setPen(pen); p.setBrush(Qt::NoBrush); p.drawRect(QRectF(vp.frame.x0, vp.frame.y0, vp.frame.width(), vp.frame.height())); }
        Rect win = vp.modelWindow();
        for (quint64 id : doc.query(win)) if (const Entity* e = doc.entity(id)) drawEntity(p, doc, *e, o);
        p.restore();
    }
    if (s.title.enabled && s.frame) titleBlock(p, s, sheet, pxPerMm, forPrint);
}

} // namespace psi
