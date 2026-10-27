#include "modules/draft/demo.h"
#include "modules/drawing/sheet.h"
#include <QDate>

namespace psi {

void Demo::build(Project& project) {
    project.newProject();
    Document& doc = project.document();
    CommandStack& stack = project.commands();
    project.meta().name = QStringLiteral("Демонстрация — план комнаты");

    DocumentSettings s = doc.settings();
    s.unit = Unit::Millimeter; s.precision = 0; s.gridSpacing = 100; s.textHeight = 100;
    doc.setSettings(s);

    // Layers
    Layer walls; walls.name = QStringLiteral("Стены"); walls.color = 0x141413; walls.lineWeight = 0.5;
    Layer open; open.name = QStringLiteral("Проёмы"); open.color = 0x8A6D5A; open.lineWeight = 0.25;
    Layer dims; dims.name = QStringLiteral("Размеры"); dims.color = 0xD97757; dims.lineWeight = 0.18;
    Layer hatchL; hatchL.name = QStringLiteral("Штриховка"); hatchL.color = 0x9A9A96; hatchL.lineWeight = 0.13;
    Layer textL; textL.name = QStringLiteral("Текст"); textL.color = 0x141413; textL.lineWeight = 0.25;
    quint32 lWalls = doc.addLayer(walls), lOpen = doc.addLayer(open), lDims = doc.addLayer(dims), lHatch = doc.addLayer(hatchL), lText = doc.addLayer(textL);

    const double W = kRoomWidth, H = kRoomHeight, t = kWall;
    auto on = [](Entity e, quint32 layer) { e.layer = layer; return e; };

    stack.beginMacro(QStringLiteral("Демонстрация"));
    {
        auto c = std::make_unique<EntityCommand>(QStringLiteral("Стены"));
        // outer contour as four lines, inner contour as a closed polyline (offset by the wall thickness)
        c->add(on(makeLine({0, 0}, {W, 0}), lWalls));
        c->add(on(makeLine({W, 0}, {W, H}), lWalls));
        c->add(on(makeLine({W, H}, {0, H}), lWalls));
        c->add(on(makeLine({0, H}, {0, 0}), lWalls));
        // inner wall with a door opening (900 mm) at the bottom and a window (1500 mm) on the right
        c->add(on(makeLine({t, t}, {2000, t}), lWalls));
        c->add(on(makeLine({2900, t}, {W - t, t}), lWalls));
        c->add(on(makeLine({W - t, t}, {W - t, 1200}), lWalls));
        c->add(on(makeLine({W - t, 2700}, {W - t, H - t}), lWalls));
        c->add(on(makeLine({W - t, H - t}, {t, H - t}), lWalls));
        c->add(on(makeLine({t, H - t}, {t, t}), lWalls));
        // door: jambs, leaf and swing arc
        c->add(on(makeLine({2000, 0}, {2000, t}), lOpen));
        c->add(on(makeLine({2900, 0}, {2900, t}), lOpen));
        c->add(on(makeLine({2000, t}, {2000, t + 900}), lOpen));
        c->add(on(makeArc({2000, t}, 900, 0, kPi / 2), lOpen));
        // window: two lines across the wall + glazing
        c->add(on(makeLine({W - t, 1200}, {W, 1200}), lOpen));
        c->add(on(makeLine({W - t, 2700}, {W, 2700}), lOpen));
        c->add(on(makeLine({W - t / 2, 1200}, {W - t / 2, 2700}), lOpen));
        // hatch of the walls (left wall strip) — closed polyline + hatch
        std::vector<Point> strip{{0, 0}, {t, 0}, {t, H}, {0, H}};
        c->add(on(makeHatch(strip, HatchPattern::Lines, kPi / 4, 60), lHatch));
        std::vector<Point> top{{0, H - t}, {W, H - t}, {W, H}, {0, H}};
        c->add(on(makeHatch(top, HatchPattern::Lines, kPi / 4, 60), lHatch));
        // texts
        Entity title = makeText({W / 2, H / 2 + 200}, QStringLiteral("Комната 24 м²"), 180);
        title.attrs["align"] = AttrValue::string("center");
        c->add(on(title, lText));
        c->add(on(makeText({W / 2 - 700, H / 2 - 200}, QStringLiteral("Пол: ламинат"), 100), lText));
        c->add(on(makeLeader({{2450, t + 450}, {3200, 1300}, {3600, 1300}}, QStringLiteral("Дверь 900"), 100), lText));
        // dimensions
        Entity d1 = makeLinearDim({0, 0}, {W, 0}, {W / 2, -600}, DimKind::Horizontal); d1.textHeight = 120;
        Entity d2 = makeLinearDim({W, 0}, {W, H}, {W + 700, H / 2}, DimKind::Vertical); d2.textHeight = 120;
        Entity d3 = makeLinearDim({2000, 0}, {2900, 0}, {2450, -300}, DimKind::Horizontal); d3.textHeight = 120;
        Entity d4 = makeLinearDim({W, 1200}, {W, 2700}, {W + 350, 1950}, DimKind::Vertical); d4.textHeight = 120;
        c->add(on(d1, lDims)); c->add(on(d2, lDims)); c->add(on(d3, lDims)); c->add(on(d4, lDims));
        stack.push(std::move(c));
    }
    // radius dimension of the door swing
    {
        quint64 arcId = 0;
        for (quint64 id : doc.entityIds()) { const Entity* e = doc.entity(id); if (e && e->type == EntityType::Arc) { arcId = id; break; } }
        if (arcId) {
            auto c = std::make_unique<EntityCommand>(QStringLiteral("Размер радиуса"));
            Entity r = makeRadialDim(arcId, {2000, t}, 900, {2800, 950}, false); r.textHeight = 120;
            c->add(on(r, lDims));
            stack.push(std::move(c));
        }
    }
    stack.endMacro();

    // Sheet: A3 landscape, 1:50
    std::vector<Sheet> sheets = doc.sheets();
    Sheet sh = sheets.empty() ? Sheet{} : sheets.front();
    sh.name = QStringLiteral("План");
    sh.width = 297; sh.height = 420; sh.landscape = true;
    sh.title = TitleBlock::standard();
    sh.title.fields["name"] = AttrValue::string(QStringLiteral("План комнаты"));
    sh.title.fields["designation"] = AttrValue::string(QStringLiteral("ПСИ.ДЕМО.001"));
    sh.title.fields["scale"] = AttrValue::string(QStringLiteral("1:50"));
    sh.title.fields["author"] = AttrValue::string(QStringLiteral("Пси"));
    sh.title.fields["date"] = AttrValue::date(QDate::currentDate().toString(Qt::ISODate));
    sh.viewports.clear();
    SheetViewport vp;
    vp.frame = Rect{30, 70, 400, 285};
    vp.scale = 1.0 / 50.0;
    vp.modelCenter = Point{W / 2 + 200, H / 2 - 300};
    vp.border = false;
    sh.viewports.push_back(vp);
    if (sheets.empty()) doc.addSheet(sh); else doc.updateSheet(sh);

    doc.setCurrentLayer(lWalls);
    stack.setClean();
    project.markDirty();
}

} // namespace psi
