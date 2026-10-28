#include "selftest.h"
#include "interior_page.h"
#include "sketch_page.h"
#include "core/project/project.h"
#include "io/native/psi_file.h"
#include "modules/interior/building.h"
#include "modules/sketcher/sketch.h"
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <cmath>

namespace psi {

int runModulesSelfTest(const std::function<void(bool, const QString&)>& check) {
    int fails = 0;
    auto c = [&](bool ok, const QString& what) { check(ok, what); if (!ok) ++fails; };

    // ---- эскиз: прямоугольник 120×60 с размерами, закрепление угла → DOF 0, выдача в чертёж
    {
        using namespace sk;
        Sketch s; s.name = QStringLiteral("Самопроверка");
        auto line = [&](double x1, double y1, double x2, double y2) { SketchEntity e; e.kind = Kind::Line; e.p = {x1, y1, x2, y2}; return s.addEntity(e); };
        quint64 a = line(0, 0, 100, 2), b = line(101, 2, 103, 50), cc = line(103, 51, -1, 52), d = line(-1, 52, 1, 1);
        auto con = [&](CType t, std::vector<Ref> refs, double v = 0) { Constraint k; k.type = t; k.refs = std::move(refs); k.value = v; return s.addConstraint(k); };
        con(CType::Coincident, {{a, 1}, {b, 0}}); con(CType::Coincident, {{b, 1}, {cc, 0}}); con(CType::Coincident, {{cc, 1}, {d, 0}}); con(CType::Coincident, {{d, 1}, {a, 0}});
        con(CType::Horizontal, {{a, -1}}); con(CType::Horizontal, {{cc, -1}}); con(CType::Vertical, {{b, -1}}); con(CType::Vertical, {{d, -1}});
        quint64 len = con(CType::Length, {{a, -1}}, 120); con(CType::Length, {{b, -1}}, 60);
        Constraint fx; fx.type = CType::Fixed; fx.refs = {{a, 0}}; fx.fixedAt = {0, 0}; s.addConstraint(fx);
        QElapsedTimer t; t.start();
        SolveResult r = s.solve(true);
        c(r.converged && r.conflicts.empty(), QStringLiteral("эскиз: решатель сошёлся за %1 итераций (%2 мс)").arg(r.iterations).arg(t.elapsed()));
        c(r.dof == 0, QStringLiteral("эскиз: прямоугольник полностью определён (DOF %1)").arg(r.dof));
        const SketchEntity* la = s.entity(a);
        c(la && std::abs(dist(la->point(0), la->point(1)) - 120) < 1e-6, QStringLiteral("эскиз: длина стороны 120"));
        s.setValue(len, 200); r = s.solve();
        la = s.entity(a);
        c(r.converged && la && std::abs(dist(la->point(0), la->point(1)) - 200) < 1e-6, QStringLiteral("эскиз: изменение размера 120 → 200 перерешано"));
        con(CType::Length, {{a, -1}}, 150);
        r = s.solve(true);
        c(!r.converged && !r.conflicts.empty(), QStringLiteral("эскиз: конфликт ограничений обнаружен (%1 шт.)").arg(r.conflicts.size()));
        s.removeConstraint(s.constraints().back().id);
        c(s.solve(true).converged, QStringLiteral("эскиз: после удаления конфликта решается"));
        // JSON round-trip и выдача в чертёж
        Sketch copy = Sketch::fromJson(QJsonDocument::fromJson(QJsonDocument(s.toJson()).toJson()).object());
        c(copy.entities().size() == 4 && copy.constraints().size() == s.constraints().size(), QStringLiteral("эскиз: JSON round-trip"));
        Project project;
        auto ents = copy.toDraftEntities();
        auto cmd = std::make_unique<EntityCommand>(QStringLiteral("Выдать эскиз"));
        for (Entity& e : ents) cmd->add(std::move(e));
        project.commands().push(std::move(cmd));
        c(project.document().entityCount() == 4, QStringLiteral("эскиз: выдача в чертёж — %1 объектов").arg(project.document().entityCount()));
        project.commands().undo();
        c(project.document().entityCount() == 0, QStringLiteral("эскиз: выдача отменяется"));
    }

    // ---- интерьер: 4 стены + дверь + шкаф → помещение, ведомость, .psi, выдача в чертёж
    {
        using namespace in;
        Building b; b.name = QStringLiteral("Самопроверка");
        Wall w; w.pts = {{0, 0}, {6200, 0}, {6200, 4200}, {0, 4200}, {0, 0}}; w.thickness = 200;
        quint64 wid = b.addWall(0, w);
        Opening d; d.kind = OpeningKind::Door; d.wall = wid; d.offset = 1500; b.addOpening(0, d);
        Furniture f = Building::makeFurniture(FurnitureKind::Wardrobe); f.pos = {700, 400}; b.addFurniture(0, f);
        Furniture f2 = Building::makeFurniture(FurnitureKind::Wardrobe); f2.pos = {2000, 400}; b.addFurniture(0, f2);
        auto rooms = Building::rooms(b.floor(0));
        c(rooms.size() == 1 && std::abs(rooms[0].areaM2 - 24.0) < 0.01, QStringLiteral("интерьер: 4 стены → 1 помещение %1 м²").arg(rooms.empty() ? 0.0 : rooms[0].areaM2, 0, 'f', 2));
        auto sched = Building::furnitureSchedule(b.floor(0));
        double sum = 0; for (const auto& r : sched) sum += r.sum;
        c(sched.size() == 1 && sched[0].count == 2 && std::abs(sum - 36000) < 1e-6, QStringLiteral("интерьер: ведомость мебели — %1 поз., %2 ₽").arg(sched.size()).arg(sum, 0, 'f', 0));
        c(Building::collisions(b.floor(0)).empty(), QStringLiteral("интерьер: без коллизий"));
        c(Building::csv(sched).count('\n') == 2, QStringLiteral("интерьер: CSV ведомости"));
        Document doc; ProjectMeta meta; QMap<QString, QByteArray> foreign;
        foreign[QStringLiteral("interior/1.json")] = QJsonDocument(b.toJson()).toJson(QJsonDocument::Compact);
        QByteArray bytes = PsiFile::write(doc, meta, foreign);
        Document doc2; ProjectMeta meta2; QMap<QString, QByteArray> foreign2; QString err;
        bool ok = PsiFile::read(bytes, doc2, meta2, foreign2, &err);
        Building b2 = ok && foreign2.contains(QStringLiteral("interior/1.json")) ? Building::fromJson(QJsonDocument::fromJson(foreign2.value(QStringLiteral("interior/1.json"))).object()) : Building();
        c(ok && b2.floors.size() == 1 && b2.floor(0).walls.size() == 1 && b2.floor(0).openings.size() == 1 && b2.floor(0).furniture.size() == 2, QStringLiteral("интерьер: .psi round-trip (interior/1.json)"));
        Document target;
        auto ents = b2.toDraftEntities(0, target);
        c(ents.size() >= 10 && target.layers().size() >= 4, QStringLiteral("интерьер: выдача плана в чертёж — %1 объектов, %2 слоёв").arg(ents.size()).arg(target.layers().size()));
    }
    // ---- страницы рабочих пространств: создание, хранение в project.foreignParts(), отмена, выдача
    {
        Project project;
        SketchPage sp(project);
        sp.activate();                                        // создаёт первый эскиз
        c(sp.sketches().size() == 1 && project.foreignParts().contains(QStringLiteral("sketches/1.json")), QStringLiteral("страница «Эскиз»: эскиз сохранён в sketches/1.json"));
        sk::Sketch* s = sp.currentSketch();
        c(s != nullptr, QStringLiteral("страница «Эскиз»: текущий эскиз"));
        if (s) {
            sk::SketchEntity e; e.kind = sk::Kind::Line; e.p = {0, 0, 50, 0};
            sp.addSketch(QStringLiteral("Второй"));
            c(sp.sketches().size() == 2 && sp.canUndo(), QStringLiteral("страница «Эскиз»: второй эскиз, отмена доступна"));
            sp.undo();
            c(sp.sketches().size() == 1 && sp.canRedo(), QStringLiteral("страница «Эскиз»: отмена создания эскиза"));
            sp.redo();
            c(sp.sketches().size() == 2, QStringLiteral("страница «Эскиз»: повтор"));
            sp.currentSketch()->addEntity(e);
            int n = sp.exportToDraft();
            c(n == 1 && project.document().entityCount() == 1, QStringLiteral("страница «Эскиз»: выдача в чертёж через команду"));
        }
        InteriorPage ip(project);
        ip.activate();
        c(ip.building() && project.foreignParts().contains(QStringLiteral("interior/1.json")), QStringLiteral("страница «Интерьер»: здание сохранено в interior/1.json"));
        if (ip.building()) {
            in::Wall w; w.pts = {{0, 0}, {5000, 0}, {5000, 3000}, {0, 3000}, {0, 0}};
            ip.building()->addWall(0, w);
            in::Furniture f = in::Building::makeFurniture(in::FurnitureKind::Table); f.pos = {2500, 1500}; ip.building()->addFurniture(0, f);
            ip.saveToProject();
            int before = int(project.document().entityCount());
            int n = ip.exportToDraft();
            c(n > 0 && int(project.document().entityCount()) == before + n, QStringLiteral("страница «Интерьер»: план выдан в чертёж (%1 объектов)").arg(n));
            c(ip.scheduleCsv().contains(QStringLiteral("Стол")), QStringLiteral("страница «Интерьер»: CSV ведомости содержит стол"));
        }
        // сохранение и повторное открытие проекта возвращают эскизы и здание
        QTemporaryDir tmp; QString err;
        QString path = tmp.filePath(QStringLiteral("modules.psi"));
        c(project.saveAs(path, &err), QStringLiteral("страницы: проект сохранён (%1)").arg(err));
        Project reopened;
        SketchPage sp2(reopened); InteriorPage ip2(reopened);
        c(reopened.open(path, &err), QStringLiteral("страницы: проект открыт (%1)").arg(err));
        c(sp2.sketches().size() == 2 && ip2.building() && ip2.building()->floor(0).walls.size() == 1, QStringLiteral("страницы: после открытия 2 эскиза и здание с 1 стеной"));
    }
    return fails;
}

} // namespace psi
