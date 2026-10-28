#pragma once
// Дерево построения детали (ТЗ §5 «Модель»): упорядоченные операции, перестроение с изменённой операции,
// несколько тел, массовые характеристики, JSON (чужая часть `models/<id>.json`) и кэш BREP.
//
// Параметры операций — QJsonObject с ключами:
//   Sketch     plane "XY"|"XZ"|"YZ"|"face"|"datum", offset, faceFeature + faceIndex (plane=face), datum (id плоскости), sketch (psi::sk::Sketch JSON)
//   Extrude    sketch (id), distance, distance2 (вторая сторона, 0 = нет), symmetric, mode "add"|"cut"|"new", body (индекс тела)
//   Revolve    sketch, angle (°), axis "X"|"Y" (ось эскиза) | axisPoint/axisDir [x,y,z], mode, body
//   Hole       body, faceIndex (0 = по направлению dir), point [x,y,z], dir [x,y,z], diameter, depth, through, countersink, csDiameter, csAngle (°)
//   Fillet     body, radius, edges [индексы рёбер, пусто = все]
//   Chamfer    body, distance, edges
//   LinearPattern   feature (id Extrude/Revolve/Hole), count, step [x,y,z], count2, step2
//   CircularPattern feature, count, angle (общий угол, 360 = полный), axisPoint, axisDir
//   Mirror     feature | body, plane "XY"|"XZ"|"YZ"|"datum", offset, datum
//   Boolean    a, b (индексы тел), op "union"|"subtract"|"intersect"
//   DatumPlane base "XY"|"XZ"|"YZ"|"face", offset, faceFeature, faceIndex
//   DatumAxis  base "X"|"Y"|"Z" | point/dir
//   Shell      body, thickness, faces [индексы открытых граней]
//   MeshBody   name, mesh (base64: uint32 n, uint32 t, float·3n, uint32·3t) — импорт STL, непараметрическое тело
#include "modules/model/kernel.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <vector>

namespace psi::sk { class Sketch; }

namespace psi::md {

enum class FeatureType { Sketch, Extrude, Revolve, Hole, Fillet, Chamfer, LinearPattern, CircularPattern, Mirror, Boolean, DatumPlane, DatumAxis, Shell, MeshBody };

QString featureTypeName(FeatureType t);        // «Выдавливание»
QString featureTypeKey(FeatureType t);         // "extrude"
FeatureType featureTypeFromKey(const QString& key, bool* ok = nullptr);
QString featureIcon(FeatureType t);            // имя иконки ui/icons

struct Feature {
    quint64 id = 0;
    FeatureType type = FeatureType::Sketch;
    QString name;
    bool suppressed = false;
    QJsonObject params;
    // состояние последнего перестроения
    QString error;                 // пусто = успех
    bool built = false;
    bool ok() const { return error.isEmpty(); }
    QJsonObject toJson() const;
    static Feature fromJson(const QJsonObject& o);
};

struct Material {
    QString name = QStringLiteral("Сталь");
    double density = 7.85;         // г/см³
    QString color = QStringLiteral("#B8B4AD");   // hex, без QtGui в psi_model
    QJsonObject toJson() const;
    static Material fromJson(const QJsonObject& o);
};

struct Body {
    QString name;
    Shape shape;
    bool mesh = false;             // тело-сетка (STL): без булевых операций
    quint64 origin = 0;            // операция, создавшая тело
};

struct RebuildReport {
    int rebuilt = 0;
    QStringList errors;            // «Скругление 2: радиус 50 мм слишком велик…»
    std::vector<quint64> failed;
    bool ok() const { return errors.isEmpty(); }
};

// Профиль из эскиза: окружности и замкнутые цепочки отрезков / дуг (без вспомогательной геометрии).
Profile profileFromSketch(const sk::Sketch& sketch, QString* warning = nullptr);

class Part {
public:
    Part();
    quint64 id = 0;
    QString name = QStringLiteral("Деталь");
    Material material;

    const std::vector<Feature>& features() const { return features_; }
    Feature* feature(quint64 id);
    const Feature* feature(quint64 id) const;
    int indexOf(quint64 id) const;
    quint64 addFeature(FeatureType type, const QJsonObject& params, const QString& name = QString());
    bool removeFeature(quint64 id);
    bool moveFeature(quint64 id, int newIndex);
    bool setParams(quint64 id, const QJsonObject& params);
    bool setSuppressed(quint64 id, bool on);
    bool rename(quint64 id, const QString& name);

    // Перестроение: с первой изменённой операции (fromIndex < 0) или с fromIndex (кэш состояния перед ней); 0 = всё.
    RebuildReport rebuild(int fromIndex = -1);
    bool dirty() const { return dirtyFrom_ >= 0; }

    const std::vector<Body>& bodies() const { return bodies_; }
    Shape shape() const;                   // все тела как одно составное
    MassProps massProps() const;          // суммарно по телам
    double mass() const;                   // г, по плотности материала
    Plane planeOf(quint64 datumId, bool* ok = nullptr) const;   // плоскость DatumPlane / плоскость эскиза
    bool axisOf(quint64 datumId, Vec3& point, Vec3& dir) const;
    Plane sketchPlane(const Feature& f, QString* error = nullptr) const;
    Shape toolShape(quint64 featureId) const;   // тело-инструмент операции (для массивов / зеркала)

    QJsonObject toJson() const;
    static Part fromJson(const QJsonObject& o);
    QByteArray brepCache() const { return shape().toBrep(); }
    bool restoreFromBrep(const QByteArray& data);   // показать тело из кэша без перестроения (OCCT-кэш в assets/)

    static std::vector<Plane> standardPlanes() { return {Plane::XY(), Plane::XZ(), Plane::YZ()}; }
    static Plane namedPlane(const QString& name, double offset = 0);

private:
    struct State { std::vector<Body> bodies; };
    bool applyFeature(Feature& f, State& st);
    bool applyTool(const Feature& f, const Shape& tool, const QString& mode, int bodyIndex, State& st, bool meshOk = false);
    Shape buildTool(const Feature& f, const State& st, QString* error, QString* mode, int* bodyIndex) const;
    Body* bodyAt(State& st, int index) const;
    std::vector<Feature> features_;
    std::vector<Body> bodies_;
    std::vector<State> states_;            // состояние после каждой операции
    std::vector<Shape> tools_;             // тело-инструмент каждой операции (пусто, если нет)
    int dirtyFrom_ = 0;
    quint64 nextId_ = 1;
};

} // namespace psi::md
