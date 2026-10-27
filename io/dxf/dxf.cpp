#include "io/dxf/dxf.h"
#include <QTextStream>
#include <cmath>

namespace psi {

// ---------------------------------------------------------------- helpers
static const int kAci[] = {0x000000, 0xFF0000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x0000FF, 0xFF00FF, 0x000000, 0x808080, 0xC0C0C0};

static int rgbToAci(int rgb) {
    if (rgb == 0x141413 || rgb == 0) return 7;
    int best = 7; double bd = 1e9;
    for (int i = 1; i <= 9; ++i) {
        int c = kAci[i];
        double d = std::pow(((rgb >> 16) & 255) - ((c >> 16) & 255), 2) + std::pow(((rgb >> 8) & 255) - ((c >> 8) & 255), 2) + std::pow((rgb & 255) - (c & 255), 2);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
static int aciToRgb(int aci) {
    if (aci >= 1 && aci <= 9) return aci == 7 ? 0x141413 : kAci[aci];
    return 0x141413;
}
static QString ltName(LineType t) {
    switch (t) {
    case LineType::Dashed: return "DASHED";
    case LineType::Dotted: return "DOT";
    case LineType::DashDot: return "DASHDOT";
    case LineType::ByLayer: return "BYLAYER";
    default: return "CONTINUOUS";
    }
}
static LineType ltFromName(const QString& n) {
    QString u = n.toUpper();
    if (u == "DASHED" || u == "HIDDEN" || u.startsWith("DASHED")) return LineType::Dashed;
    if (u == "DOT" || u.startsWith("DOT")) return LineType::Dotted;
    if (u == "DASHDOT" || u.startsWith("DASHDOT") || u == "CENTER") return LineType::DashDot;
    if (u == "BYLAYER") return LineType::ByLayer;
    return LineType::Solid;
}
static int insUnits(Unit u) {
    switch (u) { case Unit::Inch: return 1; case Unit::Millimeter: return 4; case Unit::Centimeter: return 5; case Unit::Meter: return 6; }
    return 4;
}

struct Writer {
    QString out;
    QTextStream ts{&out};
    void g(int code, const QString& v) { ts << code << "\n" << v << "\n"; }
    void g(int code, double v) { ts << code << "\n" << QString::number(v, 'g', 15) << "\n"; }
    void g(int code, int v) { ts << code << "\n" << v << "\n"; }
    void pt(int base, Point p) { g(base, p.x); g(base + 10, p.y); g(base + 20, 0.0); }
};

static void common(Writer& w, const Document& doc, const Entity& e, const QString& type) {
    w.g(0, type);
    w.g(8, doc.layer(e.layer) ? doc.layer(e.layer)->name : QStringLiteral("0"));
    if (e.color != kColorByLayer) { w.g(62, rgbToAci(e.color)); w.g(420, e.color); }
    if (e.lineType != LineType::ByLayer) w.g(6, ltName(e.lineType));
    if (e.lineWeight >= 0) w.g(370, static_cast<int>(std::lround(e.lineWeight * 100)));
}

static void writeText(Writer& w, const Document& doc, const Entity& src, Point pos, const QString& text, double h, double ang) {
    common(w, doc, src, "TEXT");
    w.g(100, "AcDbEntity"); w.g(100, "AcDbText");
    w.pt(10, pos); w.g(40, h); w.g(1, text);
    if (std::abs(ang) > 1e-12) w.g(50, deg(ang));
}

static void writeEntity(Writer& w, const Document& doc, const Entity& e) {
    switch (e.type) {
    case EntityType::Line:
        common(w, doc, e, "LINE"); w.g(100, "AcDbEntity"); w.g(100, "AcDbLine"); w.pt(10, e.pts[0]); w.pt(11, e.pts[1]); break;
    case EntityType::Polyline: {
        common(w, doc, e, "LWPOLYLINE"); w.g(100, "AcDbEntity"); w.g(100, "AcDbPolyline");
        w.g(90, static_cast<int>(e.pts.size())); w.g(70, e.closed ? 1 : 0);
        for (size_t i = 0; i < e.pts.size(); ++i) {
            w.g(10, e.pts[i].x); w.g(20, e.pts[i].y);
            if (i < e.bulges.size() && std::abs(e.bulges[i]) > 1e-12) w.g(42, e.bulges[i]);
        }
        break;
    }
    case EntityType::Circle: common(w, doc, e, "CIRCLE"); w.g(100, "AcDbEntity"); w.g(100, "AcDbCircle"); w.pt(10, e.center); w.g(40, e.radius); break;
    case EntityType::Arc:
        common(w, doc, e, "ARC"); w.g(100, "AcDbEntity"); w.g(100, "AcDbCircle"); w.pt(10, e.center); w.g(40, e.radius);
        w.g(100, "AcDbArc"); w.g(50, deg(e.startAngle)); w.g(51, deg(e.endAngle)); break;
    case EntityType::Ellipse: {
        common(w, doc, e, "ELLIPSE"); w.g(100, "AcDbEntity"); w.g(100, "AcDbEllipse");
        double major = std::max(e.rx, e.ry), minor = std::min(e.rx, e.ry);
        double rot = e.rx >= e.ry ? e.rotation : e.rotation + kPi / 2;
        w.pt(10, e.center); w.pt(11, Point::polar(major, rot)); w.g(40, major > 0 ? minor / major : 1.0);
        double a0 = e.startAngle, a1 = e.endAngle;
        if (e.rx < e.ry) { a0 -= kPi / 2; a1 -= kPi / 2; }
        w.g(41, a0); w.g(42, a1 - a0 >= 2 * kPi - 1e-9 ? 2 * kPi : a1);
        break;
    }
    case EntityType::Spline: {
        common(w, doc, e, "SPLINE"); w.g(100, "AcDbEntity"); w.g(100, "AcDbSpline");
        w.g(70, e.closed ? 11 : 8); w.g(71, 3); w.g(72, 0); w.g(73, 0); w.g(74, static_cast<int>(e.pts.size()));
        for (const Point& p : e.pts) w.pt(11, p);
        break;
    }
    case EntityType::Hatch: {
        common(w, doc, e, "HATCH"); w.g(100, "AcDbEntity"); w.g(100, "AcDbHatch");
        w.pt(10, Point{}); w.g(210, 0.0); w.g(220, 0.0); w.g(230, 1.0);
        bool solid = e.hatch == HatchPattern::Solid;
        w.g(2, solid ? "SOLID" : (e.hatch == HatchPattern::Cross ? "ANSI37" : "ANSI31"));
        w.g(70, solid ? 1 : 0); w.g(71, 0); w.g(91, 1);
        w.g(92, 2); w.g(72, 0); w.g(73, 1); w.g(93, static_cast<int>(e.pts.size()));
        for (const Point& p : e.pts) { w.g(10, p.x); w.g(20, p.y); }
        w.g(97, 0);
        w.g(75, 0); w.g(76, 1);
        if (!solid) {
            w.g(52, deg(e.hatchAngle)); w.g(41, 1.0); w.g(77, 0);
            int n = e.hatch == HatchPattern::Cross ? 2 : 1;
            w.g(78, n);
            for (int i = 0; i < n; ++i) {
                double a = e.hatchAngle + i * kPi / 2;
                w.g(53, deg(a)); w.g(43, 0.0); w.g(44, 0.0);
                Point off = Point::polar(e.hatchSpacing, a + kPi / 2);
                w.g(45, off.x); w.g(46, off.y); w.g(79, 0);
            }
        }
        w.g(98, 0);
        break;
    }
    case EntityType::Text: writeText(w, doc, e, e.pts[0], e.text, e.textHeight, e.textAngle); break;
    case EntityType::Leader: {
        for (size_t i = 1; i < e.pts.size(); ++i) { Entity l = makeLine(e.pts[i - 1], e.pts[i]); l.layer = e.layer; l.color = e.color; l.lineType = e.lineType; l.lineWeight = e.lineWeight; writeEntity(w, doc, l); }
        if (!e.pts.empty()) writeText(w, doc, e, e.pts.back() + Point{e.textHeight * 0.5, -e.textHeight * 0.5}, e.text, e.textHeight, 0);
        break;
    }
    case EntityType::Dimension: {
        // exported as plain geometry: dimension lines, arrows and the measured text
        DimGeometry g = dimensionGeometry(e, doc.lookup());
        auto line = [&](Point a, Point b) { Entity l = makeLine(a, b); l.layer = e.layer; l.color = e.color; writeEntity(w, doc, l); };
        for (auto& s : g.lines) line(s.a, s.b);
        for (auto& s : g.arrows) line(s.a, s.b);
        for (size_t i = 1; i < g.arc.size(); ++i) line(g.arc[i - 1], g.arc[i]);
        QString text = e.text;
        if (text.isEmpty()) {
            double v = dimensionValue(e, doc.lookup());
            text = e.dimKind == DimKind::Angular ? Units::formatAngle(v, doc.settings().precision)
                                                  : Units::format(v, doc.settings().unit, doc.settings().precision);
            if (e.dimKind == DimKind::Radius) text = "R" + text;
            if (e.dimKind == DimKind::Diameter) text = QStringLiteral("⌀") + text;
        }
        writeText(w, doc, e, g.textPos, text, e.textHeight, g.textAngle);
        break;
    }
    }
}

QByteArray Dxf::write(const Document& doc, const std::vector<quint64>& ids) {
    Writer w;
    std::vector<quint64> list = ids.empty() ? doc.entityIds() : ids;
    Rect ext;
    for (quint64 id : list) if (const Entity* e = doc.entity(id)) ext.add(entityBBox(*e));
    if (!ext.valid()) ext = Rect(0, 0, 0, 0);
    w.g(999, "PSI " PSI_VERSION " DXF export");
    w.g(0, "SECTION"); w.g(2, "HEADER");
    w.g(9, "$ACADVER"); w.g(1, "AC1015");
    w.g(9, "$INSUNITS"); w.g(70, insUnits(Unit::Millimeter));
    w.g(9, "$EXTMIN"); w.pt(10, {ext.x0, ext.y0});
    w.g(9, "$EXTMAX"); w.pt(10, {ext.x1, ext.y1});
    w.g(0, "ENDSEC");
    w.g(0, "SECTION"); w.g(2, "TABLES");
    w.g(0, "TABLE"); w.g(2, "LTYPE"); w.g(70, 4);
    struct LT { const char* n; const char* d; std::vector<double> p; };
    for (const LT& lt : {LT{"CONTINUOUS", "Solid line", {}}, LT{"DASHED", "__ __ __", {5, -2.5}}, LT{"DOT", ". . . .", {0, -2}}, LT{"DASHDOT", "__ . __ .", {5, -2, 0, -2}}}) {
        w.g(0, "LTYPE"); w.g(100, "AcDbSymbolTableRecord"); w.g(100, "AcDbLinetypeTableRecord"); w.g(2, lt.n); w.g(70, 0); w.g(3, lt.d); w.g(72, 65);
        w.g(73, static_cast<int>(lt.p.size()));
        double total = 0; for (double x : lt.p) total += std::abs(x);
        w.g(40, total);
        for (double x : lt.p) { w.g(49, x); w.g(74, 0); }
    }
    w.g(0, "ENDTAB");
    w.g(0, "TABLE"); w.g(2, "LAYER"); w.g(70, static_cast<int>(doc.layers().size()));
    for (const Layer& l : doc.layers()) {
        w.g(0, "LAYER"); w.g(100, "AcDbSymbolTableRecord"); w.g(100, "AcDbLayerTableRecord");
        w.g(2, l.name); w.g(70, l.locked ? 4 : 0); w.g(62, (l.visible ? 1 : -1) * rgbToAci(l.color)); w.g(420, l.color);
        w.g(6, ltName(l.lineType == LineType::ByLayer ? LineType::Solid : l.lineType)); w.g(370, static_cast<int>(std::lround(l.lineWeight * 100)));
        w.g(290, l.printable ? 1 : 0);
    }
    w.g(0, "ENDTAB"); w.g(0, "ENDSEC");
    w.g(0, "SECTION"); w.g(2, "ENTITIES");
    for (quint64 id : list) if (const Entity* e = doc.entity(id)) writeEntity(w, doc, *e);
    w.g(0, "ENDSEC"); w.g(0, "EOF");
    w.ts.flush();
    return w.out.toUtf8();
}

// ---------------------------------------------------------------- reader
namespace {
struct Pair { int code; QString value; };

struct Raw {   // one entity's group codes
    QString type;
    QList<Pair> pairs;
    QString str(int code, const QString& def = {}) const { for (auto& p : pairs) if (p.code == code) return p.value; return def; }
    double num(int code, double def = 0) const { for (auto& p : pairs) if (p.code == code) return p.value.toDouble(); return def; }
    int integer(int code, int def = 0) const { for (auto& p : pairs) if (p.code == code) return p.value.toInt(); return def; }
    bool has(int code) const { for (auto& p : pairs) if (p.code == code) return true; return false; }
    std::vector<double> all(int code) const { std::vector<double> v; for (auto& p : pairs) if (p.code == code) v.push_back(p.value.toDouble()); return v; }
};
} // namespace

QString DxfReport::summary() const {
    QStringList lines;
    lines << QStringLiteral("Единицы: %1%2").arg(Units::name(unit), unitGuessed ? QStringLiteral(" (в файле не указаны)") : QString());
    lines << QStringLiteral("Слоёв: %1").arg(layers);
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) lines << QStringLiteral("%1: %2").arg(it.key()).arg(it.value());
    if (!unsupported.isEmpty()) lines << QStringLiteral("Пропущено (не поддерживается): %1").arg(unsupported.join(", "));
    for (const QString& w : warnings) lines << QStringLiteral("⚠ %1").arg(w);
    return lines.join("\n");
}

bool Dxf::parse(const QByteArray& bytes, std::vector<Entity>* entities, std::vector<Layer>* layers, DxfReport* report, QString* error,
                const DxfOptions& opt, const std::function<bool()>& cancelled) {
    DxfReport rep;
    QString text = QString::fromUtf8(bytes);
    if (text.contains(QChar(0xFFFD))) text = QString::fromLatin1(bytes);
    const QStringList lines = text.split('\n');
    QList<Pair> pairs;
    for (int i = 0; i + 1 < lines.size(); i += 2) {
        bool ok = false;
        int code = lines[i].trimmed().toInt(&ok);
        if (!ok) { if (error) *error = QStringLiteral("Строка %1: ожидался код группы DXF").arg(i + 1); return false; }
        QString v = lines[i + 1];
        if (v.endsWith('\r')) v.chop(1);
        pairs.push_back({code, v.trimmed()});
    }
    if (pairs.isEmpty()) { if (error) *error = QStringLiteral("Файл пуст или это не DXF"); return false; }
    // split into sections / entities
    QString section;
    QList<Raw> raws;
    QMap<QString, Layer> layerTable;
    int unitsCode = -1;
    Raw cur; bool inEntity = false;
    auto flush = [&] { if (inEntity) raws.push_back(cur); cur = Raw{}; inEntity = false; };
    for (int i = 0; i < pairs.size(); ++i) {
        const Pair& p = pairs[i];
        if (cancelled && (i & 1023) == 0 && cancelled()) { if (error) *error = QStringLiteral("Импорт отменён"); return false; }
        if (p.code == 0) {
            if (p.value == "SECTION") { flush(); section = (i + 1 < pairs.size()) ? pairs[i + 1].value : QString(); continue; }
            if (p.value == "ENDSEC") { flush(); section.clear(); continue; }
            if (p.value == "EOF") { flush(); break; }
            if (section == "ENTITIES") { flush(); cur.type = p.value; inEntity = true; continue; }
            if (section == "TABLES") { flush(); if (p.value == "LAYER") { cur.type = "LAYER"; inEntity = true; } continue; }
            continue;
        }
        if (section == "HEADER" && p.code == 9 && p.value == "$INSUNITS" && i + 1 < pairs.size() && pairs[i + 1].code == 70) unitsCode = pairs[i + 1].value.toInt();
        if (inEntity) cur.pairs.push_back(p);
    }
    flush();
    // units
    switch (unitsCode) {
    case 1: rep.unit = Unit::Inch; break;
    case 4: rep.unit = Unit::Millimeter; break;
    case 5: rep.unit = Unit::Centimeter; break;
    case 6: rep.unit = Unit::Meter; break;
    case 2: rep.unit = Unit::Inch; rep.warnings << QStringLiteral("Единицы «футы» переведены через дюймы"); break;
    default: rep.unit = opt.fallbackUnit; rep.unitGuessed = true; break;
    }
    const double k = Units::toMm(rep.unit) * (unitsCode == 2 ? 12 : 1);
    // layers
    for (const Raw& r : raws) {
        if (r.type != "LAYER") continue;
        Layer l;
        l.name = r.str(2, "0");
        int aci = r.integer(62, 7);
        l.visible = aci >= 0;
        l.color = r.has(420) ? r.integer(420) : aciToRgb(std::abs(aci));
        l.lineType = ltFromName(r.str(6, "CONTINUOUS"));
        if (l.lineType == LineType::ByLayer) l.lineType = LineType::Solid;
        if (r.has(370) && r.integer(370) > 0) l.lineWeight = r.integer(370) / 100.0;
        l.locked = (r.integer(70) & 4) != 0;
        l.printable = r.integer(290, 1) != 0;
        layerTable[l.name] = l;
    }
    std::vector<Entity> out;
    QMap<QString, quint32> layerIds;   // name → temporary id = index+1 (0 = «0»)
    std::vector<Layer> layerList;
    auto layerIdFor = [&](const QString& name) -> quint32 {
        if (name.isEmpty() || name == "0") return 0;
        auto it = layerIds.find(name);
        if (it != layerIds.end()) return it.value();
        Layer l = layerTable.value(name);
        l.name = name;
        l.id = static_cast<quint32>(layerList.size() + 1);
        layerList.push_back(l);
        layerIds[name] = l.id;
        return l.id;
    };
    for (auto it = layerTable.cbegin(); it != layerTable.cend(); ++it) layerIdFor(it.key());
    QStringList unsupported;
    auto style = [&](const Raw& r, Entity& e) {
        e.layer = layerIdFor(r.str(8, "0"));
        if (r.has(420)) e.color = r.integer(420);
        else if (r.has(62)) { int aci = r.integer(62); e.color = (aci == 256 || aci == 0) ? kColorByLayer : aciToRgb(std::abs(aci)); }
        if (r.has(6)) e.lineType = ltFromName(r.str(6));
        if (r.has(370) && r.integer(370) > 0) e.lineWeight = r.integer(370) / 100.0;
        e.attrs.clear();
    };
    auto P = [&](const Raw& r, int xc, size_t idx = 0) { auto xs = r.all(xc), ys = r.all(xc + 10); return Point{idx < xs.size() ? xs[idx] * k : 0, idx < ys.size() ? ys[idx] * k : 0}; };
    std::vector<Point> polyPts; std::vector<double> polyBulges; bool polyClosed = false; bool inPoly = false; Raw polyRaw;
    for (const Raw& r : raws) {
        if (r.type == "LAYER") continue;
        rep.counts[r.type]++;
        Entity e;
        if (r.type == "LINE") { e = makeLine(P(r, 10), P(r, 11)); }
        else if (r.type == "LWPOLYLINE") {
            auto xs = r.all(10), ys = r.all(20);
            std::vector<Point> pts;
            for (size_t i = 0; i < std::min(xs.size(), ys.size()); ++i) pts.push_back({xs[i] * k, ys[i] * k});
            e = makePolyline(pts, (r.integer(70) & 1) != 0);
            // bulges are positional: walk pairs again to align 42 with its vertex
            std::vector<double> b(pts.size(), 0.0); int vi = -1;
            for (auto& p : r.pairs) { if (p.code == 10) ++vi; else if (p.code == 42 && vi >= 0 && vi < static_cast<int>(b.size())) b[vi] = p.value.toDouble(); }
            bool any = false; for (double x : b) if (std::abs(x) > 1e-12) any = true;
            if (any) e.bulges = b;
        }
        else if (r.type == "POLYLINE") { inPoly = true; polyPts.clear(); polyBulges.clear(); polyClosed = (r.integer(70) & 1) != 0; polyRaw = r; continue; }
        else if (r.type == "VERTEX") { if (inPoly) { polyPts.push_back(P(r, 10)); polyBulges.push_back(r.num(42, 0)); } continue; }
        else if (r.type == "SEQEND") {
            if (!inPoly) continue;
            inPoly = false;
            e = makePolyline(polyPts, polyClosed);
            bool any = false; for (double x : polyBulges) if (std::abs(x) > 1e-12) any = true;
            if (any) e.bulges = polyBulges;
            style(polyRaw, e);
            out.push_back(e);
            continue;
        }
        else if (r.type == "CIRCLE") { e = makeCircle(P(r, 10), r.num(40) * k); }
        else if (r.type == "ARC") { e = makeArc(P(r, 10), r.num(40) * k, rad(r.num(50)), rad(r.num(51))); }
        else if (r.type == "ELLIPSE") {
            Point c = P(r, 10); Point maj = P(r, 11);
            double major = maj.length(), ratio = r.num(40, 1);
            e = makeEllipse(c, major, major * ratio, maj.angle());
            e.startAngle = r.num(41, 0); e.endAngle = r.num(42, 2 * kPi);
            if (e.endAngle <= e.startAngle + 1e-9) e.endAngle += 2 * kPi;
        }
        else if (r.type == "SPLINE") {
            auto fx = r.all(11), fy = r.all(21);
            std::vector<Point> pts;
            if (!fx.empty()) for (size_t i = 0; i < std::min(fx.size(), fy.size()); ++i) pts.push_back({fx[i] * k, fy[i] * k});
            else { auto cx = r.all(10), cy = r.all(20); for (size_t i = 0; i < std::min(cx.size(), cy.size()); ++i) pts.push_back({cx[i] * k, cy[i] * k}); rep.warnings << QStringLiteral("SPLINE без точек прохождения: использованы контрольные точки"); }
            if (pts.size() < 2) continue;
            e = makeSpline(pts, (r.integer(70) & 1) != 0);
        }
        else if (r.type == "TEXT" || r.type == "MTEXT") {
            QString t = r.str(1);
            for (auto& p : r.pairs) if (p.code == 3) t.prepend(p.value);
            t.replace("\\P", "\n");
            e = makeText(P(r, 10), t, r.num(40, 3.5) * k, rad(r.num(50, 0)));
        }
        else if (r.type == "HATCH") {
            // first boundary path: polyline (72=0 within 92 bit 2) or line edges
            std::vector<Point> pts;
            bool pathPoly = (r.integer(92) & 2) != 0;
            if (pathPoly) { auto xs = r.all(10), ys = r.all(20); for (size_t i = 1; i < std::min(xs.size(), ys.size()); ++i) pts.push_back({xs[i] * k, ys[i] * k}); }
            else { auto xs = r.all(10), ys = r.all(20); for (size_t i = 1; i < std::min(xs.size(), ys.size()); ++i) pts.push_back({xs[i] * k, ys[i] * k}); }
            if (pts.size() < 3) { rep.warnings << QStringLiteral("HATCH со сложной границей пропущена"); continue; }
            HatchPattern pat = r.integer(70) == 1 ? HatchPattern::Solid : (r.integer(78, 1) >= 2 ? HatchPattern::Cross : HatchPattern::Lines);
            double ang = rad(r.num(52, 45)), sp = 3;
            auto p45 = r.all(45), p46 = r.all(46);
            if (!p45.empty() && !p46.empty()) sp = std::hypot(p45[0], p46[0]) * k;
            if (pat != HatchPattern::Solid) { auto a53 = r.all(53); if (!a53.empty()) ang = rad(a53[0]); }
            e = makeHatch(pts, pat, ang, sp > 0 ? sp : 3);
        }
        else { if (!unsupported.contains(r.type)) unsupported << r.type; continue; }
        style(r, e);
        out.push_back(e);
    }
    rep.unsupported = unsupported;
    rep.imported = static_cast<int>(out.size());
    rep.layers = static_cast<int>(layerList.size());
    if (entities) *entities = out;
    if (layers) *layers = layerList;
    if (report) *report = rep;
    return true;
}

int Dxf::import(Document& doc, const std::vector<Entity>& entities, const std::vector<Layer>& layers, const DxfOptions& opt) {
    QMap<quint32, quint32> map;   // temporary id → document id
    map[0] = 0;
    for (const Layer& l : layers) {
        quint32 id = 0; bool found = false;
        if (opt.mergeLayers) for (const Layer& d : doc.layers()) if (d.name == l.name) { id = d.id; found = true; break; }
        if (!found) { Layer n = l; n.id = 0; id = doc.addLayer(n); }
        map[l.id] = id;
    }
    int n = 0;
    for (Entity e : entities) {
        e.id = 0;
        e.layer = map.value(e.layer, 0);
        doc.addEntity(e);
        ++n;
    }
    return n;
}

} // namespace psi
