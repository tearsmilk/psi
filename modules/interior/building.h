#pragma once
#include "core/geometry/entity.h"
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <vector>

namespace psi {
class Document;
}

// Интерьер (ТЗ §3.5, §7): здание → этажи → стены (осевая полилиния + толщина), проёмы на стенах, помещения (замкнутые
// контуры из осевых линий), параметрическая мебель. Всё в миллиметрах, план — вид сверху.
namespace psi::in {

struct Wall {
    quint64 id = 0;
    std::vector<Point> pts;         // осевая линия (≥ 2 точки)
    double thickness = 200;         // мм
    double height = 2700;           // мм
    QString material = QStringLiteral("Кирпич");
    double length() const;
    int segmentCount() const { return int(pts.size()) - 1; }
    QJsonObject toJson() const;
    static Wall fromJson(const QJsonObject& o);
};

enum class OpeningKind { Door, Window };

struct Opening {
    quint64 id = 0;
    OpeningKind kind = OpeningKind::Door;
    quint64 wall = 0;
    double offset = 0;              // мм от начала стены вдоль оси до центра проёма
    double width = 900, height = 2100;
    double sill = 900;              // окно: высота подоконника
    bool swingLeft = false;         // дверь: сторона петель
    bool swingInside = true;        // дверь: открывается внутрь (в сторону левой нормали оси стены)
    QJsonObject toJson() const;
    static Opening fromJson(const QJsonObject& o);
};

enum class FurnitureKind { Wardrobe, Table, Shelf, Bed, Nightstand };

struct Furniture {
    quint64 id = 0;
    FurnitureKind kind = FurnitureKind::Wardrobe;
    QString name;                   // по умолчанию — имя вида
    Point pos;                      // центр в плане
    double rotation = 0;            // радианы
    double width = 1200, height = 2000, depth = 600;   // Ш × В × Г, мм
    double thickness = 16;          // материал панели
    int shelves = 4, doors = 2, drawers = 0;
    QString material = QStringLiteral("ЛДСП");
    QString color = QStringLiteral("Белый");
    QString article, vendor;
    double price = 0;
    std::vector<Point> footprint() const;   // 4 угла в плане (с поворотом)
    Rect bbox() const;
    QJsonObject toJson() const;
    static Furniture fromJson(const QJsonObject& o);
};

struct Room {                       // вычисляется из стен; имя/материал хранятся по ключу-центроиду
    quint64 id = 0;
    QString name;
    QString floorMaterial;
    std::vector<Point> axis;        // контур по осям стен
    std::vector<Point> inner;       // внутренний контур (с учётом толщин)
    double areaM2 = 0;              // внутренняя площадь, м²
    double perimeterMm = 0;         // внутренний периметр
    Point centroid;
};

struct RoomInfo { QString name, floorMaterial; };

struct Floor {
    quint64 id = 0;
    QString name = QStringLiteral("Этаж 1");
    double level = 0;               // отметка, мм
    double height = 2700;
    std::vector<Wall> walls;
    std::vector<Opening> openings;
    std::vector<Furniture> furniture;
    QMap<QString, RoomInfo> roomInfo;   // ключ «x,y» центроида с округлением до 100 мм

    const Wall* wall(quint64 id) const;
    Wall* wallMut(quint64 id);
    const Opening* opening(quint64 id) const;
    const Furniture* furnitureAt(quint64 id) const;
    Furniture* furnitureMut(quint64 id);
    QJsonObject toJson() const;
    static Floor fromJson(const QJsonObject& o);
};

struct ScheduleRow { QString name, article, size, material; int count = 0; double price = 0, sum = 0; };
struct MaterialRow { QString material, kind; double area = 0; };   // kind: «Стены» / «Пол»
struct Collision { quint64 a = 0, b = 0; QString what; };

struct WallOutline { quint64 wall = 0; int segment = 0; std::vector<Point> polygon; };   // по сегментам, с усами на стыках

class Building {
public:
    quint64 id = 0;
    QString name = QStringLiteral("Дом");
    std::vector<Floor> floors;

    Building();
    Floor& floor(size_t i) { return floors[i]; }
    Floor* floorById(quint64 id);
    quint64 addFloor(const QString& name);
    quint64 addWall(size_t floor, Wall w);
    quint64 addOpening(size_t floor, Opening o);
    quint64 addFurniture(size_t floor, Furniture f);
    bool remove(size_t floor, quint64 id);   // стена (с проёмами), проём или мебель по id
    quint64 nextId() { return nextId_++; }

    // геометрия плана
    static std::vector<WallOutline> wallOutlines(const Floor& f);
    static std::vector<Room> rooms(const Floor& f);              // замкнутые контуры из осевых линий
    static Point openingCenter(const Floor& f, const Opening& o, Vec* along = nullptr, Vec* normal = nullptr);
    static double projectOnWall(const Wall& w, Point p, int* segment = nullptr);   // расстояние вдоль оси до ближайшей точки
    static std::vector<Collision> collisions(const Floor& f);
    static std::vector<ScheduleRow> furnitureSchedule(const Floor& f);
    static std::vector<MaterialRow> materialSchedule(const Floor& f);
    static QString csv(const std::vector<ScheduleRow>& rows);
    static QString roomKey(Point centroid);
    static QString kindName(FurnitureKind k);
    static Furniture makeFurniture(FurnitureKind k);             // параметры по умолчанию для вида

    // выдача плана в чертёж (слои Стены / Проёмы / Мебель / Помещения создаются при необходимости)
    std::vector<Entity> toDraftEntities(size_t floor, Document& doc) const;

    Rect extents(size_t floor) const;
    QJsonObject toJson() const;
    static Building fromJson(const QJsonObject& o);

private:
    quint64 nextId_ = 1;
};

QString furnitureKindKey(FurnitureKind k);
FurnitureKind furnitureKindFromKey(const QString& s);
double polygonArea(const std::vector<Point>& poly);
std::vector<Point> offsetPolygon(const std::vector<Point>& poly, double d);   // внутрь на d для контура против часовой

} // namespace psi::in
