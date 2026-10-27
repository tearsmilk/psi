#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace psi {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = 1e-9;

inline double deg(double rad) { return rad * 180.0 / kPi; }
inline double rad(double d) { return d * kPi / 180.0; }
inline double normAngle(double a) {           // [0, 2π)
    a = std::fmod(a, 2 * kPi);
    return a < 0 ? a + 2 * kPi : a;
}

struct Point {
    double x = 0, y = 0;
    constexpr Point() = default;
    constexpr Point(double x_, double y_) : x(x_), y(y_) {}
    Point operator+(const Point& o) const { return {x + o.x, y + o.y}; }
    Point operator-(const Point& o) const { return {x - o.x, y - o.y}; }
    Point operator*(double k) const { return {x * k, y * k}; }
    Point operator/(double k) const { return {x / k, y / k}; }
    Point& operator+=(const Point& o) { x += o.x; y += o.y; return *this; }
    bool operator==(const Point& o) const { return std::abs(x - o.x) < kEps && std::abs(y - o.y) < kEps; }
    bool operator!=(const Point& o) const { return !(*this == o); }
    double length() const { return std::hypot(x, y); }
    double dot(const Point& o) const { return x * o.x + y * o.y; }
    double cross(const Point& o) const { return x * o.y - y * o.x; }
    Point normalized() const { double l = length(); return l < kEps ? Point{} : Point{x / l, y / l}; }
    Point perp() const { return {-y, x}; }
    double angle() const { return std::atan2(y, x); }
    static Point polar(double len, double ang) { return {len * std::cos(ang), len * std::sin(ang)}; }
};
using Vec = Point;

inline double dist(const Point& a, const Point& b) { return (a - b).length(); }
inline Point mid(const Point& a, const Point& b) { return {(a.x + b.x) / 2, (a.y + b.y) / 2}; }
inline Point lerp(const Point& a, const Point& b, double t) { return a + (b - a) * t; }
inline Point rotateAround(const Point& p, const Point& c, double ang) {
    double s = std::sin(ang), co = std::cos(ang);
    Point d = p - c;
    return {c.x + d.x * co - d.y * s, c.y + d.x * s + d.y * co};
}

struct Rect {
    double x0 = std::numeric_limits<double>::infinity(), y0 = std::numeric_limits<double>::infinity();
    double x1 = -std::numeric_limits<double>::infinity(), y1 = -std::numeric_limits<double>::infinity();
    Rect() = default;
    Rect(double a, double b, double c, double d) : x0(std::min(a, c)), y0(std::min(b, d)), x1(std::max(a, c)), y1(std::max(b, d)) {}
    Rect(const Point& a, const Point& b) : Rect(a.x, a.y, b.x, b.y) {}
    bool valid() const { return x0 <= x1 && y0 <= y1; }
    double width() const { return valid() ? x1 - x0 : 0; }
    double height() const { return valid() ? y1 - y0 : 0; }
    Point center() const { return {(x0 + x1) / 2, (y0 + y1) / 2}; }
    void add(const Point& p) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
    void add(const Rect& r) { if (r.valid()) { add({r.x0, r.y0}); add({r.x1, r.y1}); } }
    Rect grown(double d) const { return valid() ? Rect(x0 - d, y0 - d, x1 + d, y1 + d) : *this; }
    bool contains(const Point& p) const { return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1; }
    bool contains(const Rect& r) const { return r.valid() && r.x0 >= x0 && r.x1 <= x1 && r.y0 >= y0 && r.y1 <= y1; }
    bool intersects(const Rect& r) const { return valid() && r.valid() && !(r.x0 > x1 || r.x1 < x0 || r.y0 > y1 || r.y1 < y0); }
};

// 2D affine transform: [a c e; b d f]
struct Transform {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    static Transform translate(double tx, double ty) { Transform t; t.e = tx; t.f = ty; return t; }
    static Transform scale(double sx, double sy) { Transform t; t.a = sx; t.d = sy; return t; }
    static Transform rotate(double ang) { Transform t; t.a = std::cos(ang); t.b = std::sin(ang); t.c = -t.b; t.d = t.a; return t; }
    static Transform rotateAround(const Point& c, double ang) { return translate(c.x, c.y) * rotate(ang) * translate(-c.x, -c.y); }
    static Transform scaleAround(const Point& c, double k) { return translate(c.x, c.y) * scale(k, k) * translate(-c.x, -c.y); }
    static Transform mirror(const Point& p1, const Point& p2) {   // reflection across the line p1-p2
        Vec d = (p2 - p1).normalized();
        Transform t;
        t.a = d.x * d.x - d.y * d.y; t.b = 2 * d.x * d.y; t.c = t.b; t.d = d.y * d.y - d.x * d.x;
        return translate(p1.x, p1.y) * t * translate(-p1.x, -p1.y);
    }
    Point map(const Point& p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
    Vec mapVec(const Vec& v) const { return {a * v.x + c * v.y, b * v.x + d * v.y}; }
    Transform operator*(const Transform& o) const {   // this ∘ o  (apply o first)
        Transform r;
        r.a = a * o.a + c * o.b; r.b = b * o.a + d * o.b;
        r.c = a * o.c + c * o.d; r.d = b * o.c + d * o.d;
        r.e = a * o.e + c * o.f + e; r.f = b * o.e + d * o.f + f;
        return r;
    }
    double det() const { return a * d - b * c; }
    bool mirrored() const { return det() < 0; }
    double uniformScale() const { return std::sqrt(std::abs(det())); }
    Transform inverted() const {
        double dt = det();
        if (std::abs(dt) < 1e-300) return Transform{};
        Transform r;
        r.a = d / dt; r.b = -b / dt; r.c = -c / dt; r.d = a / dt;
        r.e = -(r.a * e + r.c * f); r.f = -(r.b * e + r.d * f);
        return r;
    }
};

} // namespace psi
