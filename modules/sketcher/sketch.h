#pragma once
#include "core/geometry/entity.h"
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

namespace psi {
class Document;
}

// Параметрический эскиз (ТЗ §3.2): примитивы с числовыми параметрами и ограничения, которые решатель
// (Гаусс–Ньютон с регуляризацией к текущему положению) сводит к нулю. Плоскость пока одна — XY.
namespace psi::sk {

enum class Kind { Point, Line, Circle, Arc };

// Параметры: Point x,y · Line x1,y1,x2,y2 · Circle cx,cy,r · Arc cx,cy,r,a0,a1 (радианы, против часовой)
struct SketchEntity {
    quint64 id = 0;
    Kind kind = Kind::Line;
    std::vector<double> p;
    bool construction = false;      // вспомогательная геометрия, не выдаётся в чертёж
    bool fixed = false;             // все параметры закреплены

    int paramCount() const;
    int pointCount() const;         // характерные точки: Point 1 · Line 2 (концы) · Circle 1 (центр) · Arc 3 (центр, начало, конец)
    Point point(int index) const;
    Point center() const { return {p[0], p[1]}; }
    Rect bbox() const;
    QJsonObject toJson() const;
    static SketchEntity fromJson(const QJsonObject& o);
    bool operator==(const SketchEntity& o) const { return id == o.id && kind == o.kind && p == o.p && construction == o.construction && fixed == o.fixed; }
};

enum class CType { Coincident, Horizontal, Vertical, Parallel, Perpendicular, Tangent, Equal, Fixed, Distance, Length, Radius, Diameter, Angle, PointOnLine, PointOnCircle, Symmetric };

struct Ref {
    quint64 entity = 0;
    int point = -1;                 // -1 = весь объект, иначе индекс характерной точки
    bool operator==(const Ref& o) const { return entity == o.entity && point == o.point; }
};

struct Constraint {
    quint64 id = 0;
    CType type = CType::Coincident;
    std::vector<Ref> refs;
    double value = 0;               // мм, или радианы для Angle
    Point fixedAt;                  // Fixed: куда закреплена точка
    Point label;                    // смещение подписи размерного ограничения
    bool conflict = false;          // выставляет решатель
    bool hasValue() const { return type == CType::Distance || type == CType::Length || type == CType::Radius || type == CType::Diameter || type == CType::Angle; }
    QJsonObject toJson() const;
    static Constraint fromJson(const QJsonObject& o);
};

struct SolveResult {
    bool converged = false;
    int iterations = 0;
    double maxResidual = 0;
    int dof = -1;                   // -1 = не вычислялось
    std::vector<quint64> conflicts;
};

QString constraintTypeKey(CType t);
CType constraintTypeFromKey(const QString& k);
QString constraintTypeName(CType t);     // русское имя
QString constraintGlyph(CType t);        // короткий значок для холста: ⟂, ∥, ═ …
QString kindKey(Kind k);
Kind kindFromKey(const QString& k);

class Sketch {
public:
    quint64 id = 0;
    QString name = QStringLiteral("Эскиз");
    QString plane = QStringLiteral("XY");

    // геометрия
    quint64 addEntity(SketchEntity e);
    bool removeEntity(quint64 id);                 // вместе с ограничениями на него
    SketchEntity* entity(quint64 id);
    const SketchEntity* entity(quint64 id) const;
    const std::vector<SketchEntity>& entities() const { return entities_; }
    std::vector<SketchEntity>& entitiesMut() { return entities_; }

    // ограничения
    quint64 addConstraint(Constraint c);
    bool removeConstraint(quint64 id);
    const std::vector<Constraint>& constraints() const { return constraints_; }
    std::vector<Constraint>& constraintsMut() { return constraints_; }
    Constraint* constraint(quint64 id);
    bool setValue(quint64 constraintId, double value);
    std::vector<quint64> constraintsOf(quint64 entityId) const;
    Point pointOf(const Ref& r) const;             // мировая точка ссылки (для подписи / отрисовки)
    QString describe(const Constraint& c) const;   // «Длина L3 = 120 мм»

    // решатель
    SolveResult solve(bool computeDof = false, int maxIterations = 60);
    int degreesOfFreedom();                        // ранг якобиана; дорого — вызывать не чаще раза в кадр
    double residual(const Constraint& c) const;    // суммарная невязка ограничения в текущем положении
    void dragPoint(const Ref& r, Point to);        // перенести точку и перерешать (мягко)
    void moveEntity(quint64 id, Vec d);

    // автопривязки при рисовании: совпадение концов и горизонталь/вертикаль в пределах допуска
    std::vector<quint64> autoConstrain(quint64 entityId, double tolerance, double angleTolDeg = 5.0);

    // выдача в чертёж: геометрия без вспомогательных объектов
    std::vector<Entity> toDraftEntities() const;

    Rect extents() const;
    quint64 nextId() const { return nextId_; }
    QJsonObject toJson() const;
    static Sketch fromJson(const QJsonObject& o);
    bool empty() const { return entities_.empty(); }

    struct Residuals;  // внутреннее: невязки для решателя
private:
    void collectParams(std::vector<double*>& params, std::vector<std::vector<int>>& byEntity);
    std::vector<SketchEntity> entities_;
    std::vector<Constraint> constraints_;
    quint64 nextId_ = 1;
    int dofCache_ = -1;
    quint64 dofRevision_ = 0, revision_ = 0;
};

} // namespace psi::sk
