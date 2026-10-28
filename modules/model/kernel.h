#pragma once
// Фасад геометрического ядра (ТЗ §13 GeometryCore / §5): остальная программа видит только эти типы,
// заголовки Open CASCADE подключаются исключительно в kernel.cpp (pimpl). Единицы — миллиметры.
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <memory>
#include <vector>

namespace psi::md {

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double k) const { return {x * k, y * k, z * k}; }
    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double length() const;
    Vec3 normalized() const;
};

// Плоскость построения: начало, нормаль и направление X (ось эскиза X). Оси эскиза: X → xdir, Y → normal × xdir.
struct Plane {
    Vec3 origin, normal{0, 0, 1}, xdir{1, 0, 0};
    static Plane XY(double offset = 0);
    static Plane XZ(double offset = 0);
    static Plane YZ(double offset = 0);
    Vec3 ydir() const { return normal.cross(xdir).normalized(); }
    Vec3 map(double u, double v) const { return origin + xdir * u + ydir() * v; }
};

struct Bbox { Vec3 min, max; bool valid = false; Vec3 size() const { return valid ? max - min : Vec3{}; } Vec3 center() const { return valid ? (min + max) * 0.5 : Vec3{}; } };

struct MassProps { double volume = 0, area = 0; Vec3 centroid; };

// Тесселяция тела для отображения: треугольники с нормалями и индексом грани, рёбра как отрезки.
struct Mesh {
    std::vector<float> positions;     // xyz · 3 на вершину
    std::vector<float> normals;       // xyz · 3 на вершину
    std::vector<uint32_t> indices;    // 3 на треугольник
    std::vector<uint32_t> faceOfTriangle;   // индекс грани (1..n) для каждого треугольника
    std::vector<float> edges;         // отрезки: x1 y1 z1 x2 y2 z2
    std::vector<uint32_t> edgeOfSegment;    // индекс ребра (1..n) для каждого отрезка
    size_t triangleCount() const { return indices.size() / 3; }
    size_t segmentCount() const { return edges.size() / 6; }
};

// Профиль: замкнутые контуры в плоскости (внешний + отверстия) из отрезков и дуг.
struct ProfileSegment { double x1, y1, x2, y2; double bulge = 0; };   // bulge = tan(θ/4) как в DXF; 0 = отрезок
struct ProfileLoop { std::vector<ProfileSegment> segments; };
struct ProfileCircle { double cx, cy, r; };
struct Profile { std::vector<ProfileLoop> loops; std::vector<ProfileCircle> circles; bool empty() const { return loops.empty() && circles.empty(); } };

enum class BoolOp { Union, Subtract, Intersect };

class Shape {
public:
    Shape();
    Shape(const Shape&);
    Shape(Shape&&) noexcept;
    Shape& operator=(const Shape&);
    Shape& operator=(Shape&&) noexcept;
    ~Shape();

    bool isNull() const;
    bool isValid() const;                 // BRepCheck
    int faceCount() const;
    int edgeCount() const;
    int solidCount() const;
    MassProps massProps() const;
    Bbox bbox() const;
    Mesh mesh(double deflection = 0.2, double angle = 0.35) const;
    Plane planeOfFace(int faceIndex, bool* ok = nullptr) const;   // плоская грань (1..faceCount) → её плоскость; иначе ok=false
    Vec3 faceNormalAt(int faceIndex, const Vec3& near) const;      // нормаль наружу вблизи точки
    QByteArray toBrep() const;
    static Shape fromBrep(const QByteArray& data, QString* error = nullptr);
    QString describe() const;             // «тело: 6 граней, 12 рёбер»

    struct Impl;
    Impl& impl() { return *impl_; }
    const Impl& impl() const { return *impl_; }
private:
    std::unique_ptr<Impl> impl_;
};

// Результат операции: тело или сообщение об ошибке по-русски.
struct Result {
    Shape shape;
    QString error;
    bool ok() const { return error.isEmpty() && !shape.isNull(); }
    static Result fail(const QString& e) { Result r; r.error = e; return r; }
};

struct Kernel {
    static Result makeFace(const Profile& profile, const Plane& plane);
    static Result extrude(const Profile& profile, const Plane& plane, double distance, double distance2 = 0, bool symmetric = false);
    static Result revolve(const Profile& profile, const Plane& plane, const Vec3& axisPoint, const Vec3& axisDir, double angleDeg);
    static Result cylinder(const Vec3& base, const Vec3& dir, double radius, double height);
    static Result cone(const Vec3& base, const Vec3& dir, double r1, double r2, double height);
    static Result box(const Vec3& corner, double dx, double dy, double dz);
    static Result boolean(const Shape& a, const Shape& b, BoolOp op);
    static Result fillet(const Shape& s, const std::vector<int>& edges, double radius);   // пусто = все рёбра
    static Result chamfer(const Shape& s, const std::vector<int>& edges, double distance);
    static Result translate(const Shape& s, const Vec3& d);
    static Result rotate(const Shape& s, const Vec3& axisPoint, const Vec3& axisDir, double angleDeg);
    static Result mirror(const Shape& s, const Plane& plane);
    static Result compound(const std::vector<Shape>& shapes);
    static Result shell(const Shape& s, const std::vector<int>& openFaces, double thickness);
    static Result fromMesh(const std::vector<float>& positions, const std::vector<uint32_t>& indices);   // сетка STL → непараметрическое тело-сетка (только показ / bbox / STL)
    static QString version();             // «Open CASCADE 7.6.3»
};

} // namespace psi::md
