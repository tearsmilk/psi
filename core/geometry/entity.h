#pragma once
#include "core/attributes/attributes.h"
#include "core/geometry/point.h"
#include <QJsonObject>
#include <QString>
#include <functional>
#include <optional>
#include <vector>

namespace psi {

enum class EntityType { Line, Polyline, Circle, Arc, Ellipse, Spline, Hatch, Text, Leader, Dimension };
enum class LineType { ByLayer, Solid, Dashed, Dotted, DashDot };
enum class DimKind { Aligned, Horizontal, Vertical, Radius, Diameter, Angular };
enum class HatchPattern { Solid, Lines, Cross };
enum class SnapKind { Endpoint, Midpoint, Center, Quadrant, Intersection, Perpendicular, Tangent, Grid, Node };

constexpr int kColorByLayer = -1;   // otherwise 0xRRGGBB

struct Entity {
    quint64 id = 0;
    EntityType type = EntityType::Line;
    quint32 layer = 0;
    int color = kColorByLayer;
    double lineWeight = -1;             // mm; -1 = by layer
    LineType lineType = LineType::ByLayer;
    bool visible = true;
    bool locked = false;

    std::vector<Point> pts;             // Line: 2 · Polyline/Spline/Hatch boundary/Leader: n · Text: [pos] · Dimension: see below
    std::vector<double> bulges;         // Polyline: per segment, tan(θ/4); empty = straight
    bool closed = false;

    Point center;                       // Circle, Arc, Ellipse
    double radius = 0;                  // Circle, Arc, Radius/Diameter dimension without reference
    double startAngle = 0, endAngle = 0;// Arc: CCW from start to end (radians); Ellipse: parameter range (0..2π = full)
    double rx = 0, ry = 0, rotation = 0;// Ellipse

    QString text;                       // Text, Leader, Dimension override (empty = measured value)
    double textHeight = 3.5;
    double textAngle = 0;

    DimKind dimKind = DimKind::Aligned; // Dimension: pts[0],pts[1] measured, pts[2] on the dimension line;
    std::vector<quint64> refs;          //   Radius/Diameter: refs[0] circle/arc or center+radius, pts[0] = text point;
                                        //   Angular: pts[0] vertex, pts[1], pts[2] rays, pts[3] arc point
    HatchPattern hatch = HatchPattern::Solid;
    double hatchAngle = kPi / 4, hatchSpacing = 3;

    AttributeMap attrs;

    bool operator==(const Entity& o) const;
};

struct SnapPoint { Point p; SnapKind kind; quint64 entity = 0; };

// polyline / flattened segment
struct Segment { Point a, b; };

QString entityTypeKey(EntityType t);
EntityType entityTypeFromKey(const QString& k);
QString entityTypeName(EntityType t);   // Russian
QString lineTypeKey(LineType t);
LineType lineTypeFromKey(const QString& k);
QString dimKindKey(DimKind k);
DimKind dimKindFromKey(const QString& k);

// factories
Entity makeLine(Point a, Point b);
Entity makePolyline(std::vector<Point> pts, bool closed);
Entity makeRectangle(Point a, Point b);
Entity makeCircle(Point c, double r);
Entity makeArc(Point c, double r, double a0, double a1);
Entity makeArc3(Point p1, Point p2, Point p3);               // through three points
Entity makeEllipse(Point c, double rx, double ry, double rot);
Entity makeSpline(std::vector<Point> fit, bool closed = false);
Entity makeText(Point pos, const QString& text, double height, double angle = 0);
Entity makeLeader(std::vector<Point> pts, const QString& text, double height = 3.5);
Entity makeHatch(std::vector<Point> boundary, HatchPattern p, double angle = kPi / 4, double spacing = 3);
Entity makeLinearDim(Point a, Point b, Point line, DimKind kind = DimKind::Aligned);
Entity makeRadialDim(quint64 circleRef, Point center, double r, Point textPt, bool diameter);
Entity makeAngularDim(Point vertex, Point ray1, Point ray2, Point arcPt);

// geometry queries
Rect entityBBox(const Entity& e);
std::vector<Point> flatten(const Entity& e, double tol = 0.05);       // polyline approximation (closed shapes repeat the first point)
std::vector<Segment> segments(const Entity& e, double tol = 0.05);
double hitDistance(const Entity& e, Point p, double tol = 0.05);
std::vector<SnapPoint> snapPoints(const Entity& e);
std::vector<Point> intersections(const Entity& a, const Entity& b, double tol = 0.01);
std::optional<Point> perpendicularPoint(const Entity& e, Point from);   // foot of the perpendicular from `from`
std::vector<Point> tangentPoints(const Entity& e, Point from);          // tangents from `from` to a circle/arc
Point closestPoint(const Entity& e, Point p);
bool isClosedShape(const Entity& e);

// edits
void transformEntity(Entity& e, const Transform& t);
std::vector<Entity> explode(const Entity& e);
std::optional<Entity> offsetEntity(const Entity& e, double d, Point side);
// cut `e` by `cutters`; returns the pieces of `e` with the piece nearest `pick` removed (Line/Arc/Circle/Polyline)
std::vector<Entity> trimEntity(const Entity& e, const std::vector<Entity>& cutters, Point pick);
// extend the end of `e` nearest `pick` up to the nearest intersection with `boundary`
std::optional<Entity> extendEntity(const Entity& e, const Entity& boundary, Point pick);
struct Fillet { Entity a, b; std::optional<Entity> arc; };
std::optional<Fillet> filletLines(const Entity& a, const Entity& b, double r, Point pickA, Point pickB);
std::optional<Fillet> chamferLines(const Entity& a, const Entity& b, double d1, double d2, Point pickA, Point pickB);

// dimensions
using EntityLookup = std::function<const Entity*(quint64)>;
double dimensionValue(const Entity& d, const EntityLookup& lookup);   // mm or radians (Angular)
struct DimGeometry { std::vector<Segment> lines; std::vector<Segment> arrows; Point textPos; double textAngle; std::vector<Point> arc; };
DimGeometry dimensionGeometry(const Entity& d, const EntityLookup& lookup);

// serialization
QJsonObject entityToJson(const Entity& e);
Entity entityFromJson(const QJsonObject& o);

// helpers shared by tools
Point arcPoint(const Entity& arc, double angle);
double arcSweep(const Entity& arc);
std::optional<std::pair<Point, double>> circleFrom3Points(Point a, Point b, Point c);
std::optional<Point> lineIntersection(Point a1, Point a2, Point b1, Point b2, bool segmentsOnly, double tol = 1e-9);
std::vector<Point> catmullRom(const std::vector<Point>& fit, bool closed, int perSpan = 12);

} // namespace psi
