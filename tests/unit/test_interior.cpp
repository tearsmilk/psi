#include "core/project/project.h"
#include "io/native/psi_file.h"
#include "modules/interior/building.h"
#include <QJsonDocument>
#include <QtTest>
#include <cmath>

using namespace psi;
using namespace psi::in;

// 4 стены по осям 6200×4200 толщиной 200 → внутренняя комната 6000×4000 = 24 м²
static Building house() {
    Building b;
    Wall w; w.pts = {{0, 0}, {6200, 0}, {6200, 4200}, {0, 4200}, {0, 0}};
    b.addWall(0, w);
    return b;
}

class TestInterior : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void roomsFromWalls() {
        Building b = house();
        auto rooms = Building::rooms(b.floor(0));
        QCOMPARE(rooms.size(), size_t(1));
        QVERIFY2(std::abs(rooms[0].areaM2 - 24.0) < 0.01, qPrintable(QString::number(rooms[0].areaM2)));
        QVERIFY(std::abs(rooms[0].perimeterMm - 20000) < 1);
        auto outlines = Building::wallOutlines(b.floor(0));
        QCOMPARE(outlines.size(), size_t(4));
        // отдельные стены тоже образуют комнату
        Building c;
        Wall w1; w1.pts = {{0, 0}, {5000, 0}}; c.addWall(0, w1);
        Wall w2; w2.pts = {{5000, 0}, {5000, 3000}}; c.addWall(0, w2);
        Wall w3; w3.pts = {{5000, 3000}, {0, 3000}}; c.addWall(0, w3);
        Wall w4; w4.pts = {{0, 3000}, {0, 0}}; c.addWall(0, w4);
        Wall spur; spur.pts = {{2500, 0}, {2500, 1000}}; c.addWall(0, spur);   // ус не делит комнату
        QCOMPARE(Building::rooms(c.floor(0)).size(), size_t(1));
        Wall split; split.pts = {{2500, 0}, {2500, 3000}}; c.addWall(0, split);
        QCOMPARE(Building::rooms(c.floor(0)).size(), size_t(2));
    }
    void doorStaysOnWall() {
        Building b = house();
        const Wall& w = b.floor(0).walls[0];
        int seg = -1;
        double off = Building::projectOnWall(w, {3000, 350}, &seg);
        QCOMPARE(seg, 0);
        QVERIFY(std::abs(off - 3000) < 1e-6);
        Opening d; d.kind = OpeningKind::Door; d.wall = w.id; d.offset = off; d.width = 900;
        quint64 id = b.addOpening(0, d);
        Vec along, normal;
        Point c = Building::openingCenter(b.floor(0), *b.floor(0).opening(id), &along, &normal);
        QVERIFY(std::abs(c.x - 3000) < 1e-6 && std::abs(c.y) < 1e-6);
        QVERIFY(std::abs(along.x - 1) < 1e-9 && std::abs(normal.y) > 0.99);
        // окно на вертикальной стене (сегмент 1)
        off = Building::projectOnWall(w, {6500, 2000}, &seg);
        QCOMPARE(seg, 1);
        QVERIFY(std::abs(off - (6200 + 2000)) < 1e-6);
    }
    void scheduleAndCollisions() {
        Building b = house();
        Furniture f1 = Building::makeFurniture(FurnitureKind::Wardrobe); f1.pos = {700, 400};
        Furniture f2 = Building::makeFurniture(FurnitureKind::Wardrobe); f2.pos = {2500, 400};
        Furniture f3 = Building::makeFurniture(FurnitureKind::Table); f3.pos = {3000, 2000}; f3.price = 7500;
        b.addFurniture(0, f1); b.addFurniture(0, f2); b.addFurniture(0, f3);
        auto rows = Building::furnitureSchedule(b.floor(0));
        QCOMPARE(rows.size(), size_t(2));
        double total = 0; int count = 0;
        for (const auto& r : rows) { total += r.sum; count += r.count; }
        QCOMPARE(count, 3);
        QVERIFY(std::abs(total - (2 * 18000 + 7500)) < 1e-6);
        QString csv = Building::csv(rows);
        QVERIFY(csv.contains(QStringLiteral("Шкаф")));
        QCOMPARE(csv.count('\n'), 3);     // заголовок + 2 строки
        auto mats = Building::materialSchedule(b.floor(0));
        QVERIFY(!mats.empty());
        QVERIFY(Building::collisions(b.floor(0)).empty());
        Furniture f4 = Building::makeFurniture(FurnitureKind::Nightstand); f4.pos = {800, 500};   // внутри шкафа
        b.addFurniture(0, f4);
        auto col = Building::collisions(b.floor(0));
        QVERIFY(!col.empty());
        Furniture f5 = Building::makeFurniture(FurnitureKind::Bed); f5.pos = {3000, 50};          // пересекает стену
        quint64 f5id = b.addFurniture(0, f5);
        col = Building::collisions(b.floor(0));
        bool wallHit = false; for (const auto& c : col) if ((c.a == f5id || c.b == f5id) && c.what.contains(QStringLiteral("стен"), Qt::CaseInsensitive)) wallHit = true;
        QVERIFY(wallHit);
    }
    void psiRoundTrip() {
        Building b = house(); b.name = QStringLiteral("Дача");
        Opening d; d.kind = OpeningKind::Door; d.wall = b.floor(0).walls[0].id; d.offset = 1500;
        b.addOpening(0, d);
        Furniture f = Building::makeFurniture(FurnitureKind::Shelf); f.pos = {1000, 1000}; f.rotation = 90;
        b.addFurniture(0, f);
        b.addFloor(QStringLiteral("Этаж 2"));
        b.floor(0).roomInfo[Building::roomKey({3100, 2100})] = {QStringLiteral("Гостиная"), QStringLiteral("Паркет")};

        Document doc; ProjectMeta meta;
        QMap<QString, QByteArray> foreign;
        foreign[QStringLiteral("interior/%1.json").arg(b.id)] = QJsonDocument(b.toJson()).toJson(QJsonDocument::Compact);
        QByteArray bytes = PsiFile::write(doc, meta, foreign);
        Document doc2; ProjectMeta meta2; QMap<QString, QByteArray> foreign2; QString err;
        QVERIFY2(PsiFile::read(bytes, doc2, meta2, foreign2, &err), qPrintable(err));
        QCOMPARE(foreign2.size(), 1);
        Building c = Building::fromJson(QJsonDocument::fromJson(foreign2.first()).object());
        QCOMPARE(c.name, b.name);
        QCOMPARE(c.floors.size(), size_t(2));
        QCOMPARE(c.floor(0).walls.size(), size_t(1));
        QCOMPARE(c.floor(0).openings.size(), size_t(1));
        QCOMPARE(c.floor(0).furniture.size(), size_t(1));
        QCOMPARE(c.floor(0).furniture[0].rotation, 90.0);
        QCOMPARE(c.floor(0).roomInfo.size(), 1);
        auto rooms = Building::rooms(c.floor(0));
        QCOMPARE(rooms.size(), size_t(1));
        QCOMPARE(rooms[0].name, QStringLiteral("Гостиная"));
    }
    void exportToDraft() {
        Building b = house();
        Opening d; d.kind = OpeningKind::Door; d.wall = b.floor(0).walls[0].id; d.offset = 1500; b.addOpening(0, d);
        Opening w; w.kind = OpeningKind::Window; w.wall = b.floor(0).walls[0].id; w.offset = 6200 + 2000; b.addOpening(0, w);
        Furniture f = Building::makeFurniture(FurnitureKind::Wardrobe); f.pos = {700, 400}; b.addFurniture(0, f);
        Document doc;
        auto ents = b.toDraftEntities(0, doc);
        QVERIFY(ents.size() >= 8);
        bool text = false, poly = false;
        for (const auto& e : ents) { if (e.type == EntityType::Text) text = true; if (e.type == EntityType::Polyline) poly = true; }
        QVERIFY(text && poly);
        QVERIFY(doc.layers().size() >= 4);
    }
};

QTEST_GUILESS_MAIN(TestInterior)
#include "test_interior.moc"
