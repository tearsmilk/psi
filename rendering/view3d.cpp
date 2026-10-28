#include "rendering/view3d.h"
#include "ui/theme.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace psi {

using md::Vec3;
static constexpr double kDeg = M_PI / 180.0;
static constexpr uint32_t kItemShift = 20;

View3D::View3D(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void View3D::setItems(std::vector<View3DItem> items) {
    items_ = std::move(items);
    if (selection_.item) { bool found = false; for (const auto& it : items_) if (it.id == selection_.item) found = true; if (!found) selection_ = {}; }
    hover_ = {};
    invalidate();
}

void View3D::clear() { items_.clear(); selection_ = {}; hover_ = {}; invalidate(); }

md::Bbox View3D::sceneBbox() const {
    md::Bbox b;
    for (const View3DItem& it : items_) {
        const auto& p = it.mesh.positions;
        for (size_t i = 0; i + 2 < p.size(); i += 3) {
            Vec3 v{p[i], p[i + 1], p[i + 2]};
            if (!b.valid) { b.min = b.max = v; b.valid = true; }
            else { b.min = {std::min(b.min.x, v.x), std::min(b.min.y, v.y), std::min(b.min.z, v.z)}; b.max = {std::max(b.max.x, v.x), std::max(b.max.y, v.y), std::max(b.max.z, v.z)}; }
        }
    }
    return b;
}

void View3D::zoomExtents() {
    md::Bbox b = sceneBbox();
    if (!b.valid) { target_ = {0, 0, 0}; dist_ = 1500; }
    else {
        target_ = b.center();
        double r = std::max(b.size().length() / 2, 1.0);
        dist_ = r / std::sin(fovDeg_ / 2 * kDeg) * 1.15;
    }
    invalidate();
    Q_EMIT viewChanged();
}

void View3D::setStandardView(StdView v, bool fit) {
    switch (v) {
    case StdView::Front: yaw_ = -90; pitch_ = 0; break;
    case StdView::Back: yaw_ = 90; pitch_ = 0; break;
    case StdView::Right: yaw_ = 0; pitch_ = 0; break;
    case StdView::Left: yaw_ = 180; pitch_ = 0; break;
    case StdView::Top: yaw_ = -90; pitch_ = 90; break;
    case StdView::Bottom: yaw_ = -90; pitch_ = -90; break;
    case StdView::Iso: yaw_ = -45; pitch_ = 35.264; break;
    }
    if (fit) zoomExtents(); else { invalidate(); Q_EMIT viewChanged(); }
}

void View3D::setPerspective(bool on) { if (perspective_ == on) return; perspective_ = on; invalidate(); Q_EMIT viewChanged(); }

void View3D::orbit(double dYaw, double dPitch) {
    yaw_ = std::fmod(yaw_ + dYaw, 360.0);
    pitch_ = std::clamp(pitch_ + dPitch, -90.0, 90.0);
    invalidate();
    Q_EMIT viewChanged();
}

void View3D::pan(double dx, double dy) {
    Camera c = camera(std::max(1, width()), std::max(1, height()));
    double mmPerPx = perspective_ ? dist_ / c.focal : dist_ * std::tan(fovDeg_ / 2 * kDeg) * 2 / std::max(1, height());
    target_ = target_ - c.right * (dx * mmPerPx) + c.up * (dy * mmPerPx);
    invalidate();
    Q_EMIT viewChanged();
}

void View3D::zoomBy(double factor) { dist_ = std::clamp(dist_ / factor, 0.1, 1e7); invalidate(); Q_EMIT viewChanged(); }

void View3D::setSelection(const Pick& p) { if (selection_ == p) return; selection_ = p; update(); }

// ---------------------------------------------------------------- камера и проекция
View3D::Camera View3D::camera(int w, int h) const {
    Camera c;
    const double y = yaw_ * kDeg, p = pitch_ * kDeg;
    Vec3 dir{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};   // от цели к глазу, Z вверх
    c.eye = target_ + dir * dist_;
    c.forward = dir * -1;
    Vec3 up{0, 0, 1};
    if (std::abs(std::cos(p)) < 1e-4) up = {-std::cos(y), -std::sin(y), 0};         // вид сверху / снизу: Y эскиза вверх
    c.right = c.forward.cross(up).normalized();
    c.up = c.right.cross(c.forward).normalized();
    c.focal = h / 2.0 / std::tan(fovDeg_ / 2 * kDeg);
    Q_UNUSED(w);
    return c;
}

bool View3D::toScreen(const Camera& c, const Vec3& p, double& sx, double& sy, double& depth) const {
    Vec3 d = p - c.eye;
    double x = d.dot(c.right), y = d.dot(c.up), z = d.dot(c.forward);
    depth = z;
    if (perspective_) {
        if (z < 1e-3) return false;
        sx = x / z * c.focal; sy = -y / z * c.focal;
    } else {
        const double s = c.focal / dist_;
        sx = x * s; sy = -y * s;
    }
    return true;
}

QPointF View3D::project(const Vec3& p) const {
    Camera c = camera(std::max(1, width()), std::max(1, height()));
    double sx, sy, z;
    if (!toScreen(c, p, sx, sy, z)) return {-1e9, -1e9};
    return {sx + width() / 2.0, sy + height() / 2.0};
}

// ---------------------------------------------------------------- растеризация
namespace {
struct Vtx { double x, y, z, shade; };

inline QRgb shadeColor(const QColor& base, double k) {
    return qRgb(std::clamp(int(base.red() * k), 0, 255), std::clamp(int(base.green() * k), 0, 255), std::clamp(int(base.blue() * k), 0, 255));
}
}

void View3D::rasterTriangles(Frame& f, const Camera& cam) const {
    const Palette& pal = Theme::current();
    const int W = f.w, H = f.h;
    const double cx = W / 2.0, cy = H / 2.0;
    // ключевой свет сверху-слева от камеры, заполняющий — спереди
    Vec3 light = (cam.right * -0.35 + cam.up * 0.55 + cam.forward * -0.75).normalized();
    for (size_t ii = 0; ii < items_.size() && ii < (1u << 11); ++ii) {
        const View3DItem& it = items_[ii];
        const md::Mesh& m = it.mesh;
        QColor base = it.color.isValid() ? it.color : (Theme::isDark() ? QColor(0x8C, 0x8A, 0x86) : QColor(0xB8, 0xB4, 0xAD));
        const size_t nv = m.positions.size() / 3;
        std::vector<Vtx> vs(nv);
        std::vector<bool> ok(nv);
        for (size_t v = 0; v < nv; ++v) {
            Vec3 p{m.positions[v * 3], m.positions[v * 3 + 1], m.positions[v * 3 + 2]};
            double sx, sy, z;
            ok[v] = toScreen(cam, p, sx, sy, z);
            double shade = 0.55;
            if (v * 3 + 2 < m.normals.size()) {
                Vec3 n{m.normals[v * 3], m.normals[v * 3 + 1], m.normals[v * 3 + 2]};
                double d = std::abs(n.dot(light));
                shade = 0.42 + 0.62 * d;
            }
            vs[v] = {sx * f.dpr + cx, sy * f.dpr + cy, z, shade};
        }
        const uint32_t idBase = uint32_t(ii + 1) << kItemShift;
        for (size_t t = 0; t < m.triangleCount(); ++t) {
            uint32_t a = m.indices[t * 3], b = m.indices[t * 3 + 1], c = m.indices[t * 3 + 2];
            if (a >= nv || b >= nv || c >= nv || !ok[a] || !ok[b] || !ok[c]) continue;
            const uint32_t id = idBase | (t < m.faceOfTriangle.size() ? (m.faceOfTriangle[t] & ((1u << kItemShift) - 1)) : 0);
            const Vtx& A = vs[a]; const Vtx& B = vs[b]; const Vtx& C = vs[c];
            double minx = std::floor(std::min({A.x, B.x, C.x})), maxx = std::ceil(std::max({A.x, B.x, C.x}));
            double miny = std::floor(std::min({A.y, B.y, C.y})), maxy = std::ceil(std::max({A.y, B.y, C.y}));
            if (maxx < 0 || maxy < 0 || minx >= W || miny >= H) continue;
            int x0 = std::max(0, int(minx)), x1 = std::min(W - 1, int(maxx)), y0 = std::max(0, int(miny)), y1 = std::min(H - 1, int(maxy));
            double area = (B.x - A.x) * (C.y - A.y) - (C.x - A.x) * (B.y - A.y);
            if (std::abs(area) < 1e-9) continue;
            const double inv = 1.0 / area;
            for (int y = y0; y <= y1; ++y) {
                const double py = y + 0.5;
                QRgb* row = reinterpret_cast<QRgb*>(f.color.scanLine(y));
                for (int x = x0; x <= x1; ++x) {
                    const double px = x + 0.5;
                    double w0 = ((B.x - px) * (C.y - py) - (C.x - px) * (B.y - py)) * inv;
                    double w1 = ((C.x - px) * (A.y - py) - (A.x - px) * (C.y - py)) * inv;
                    double w2 = 1 - w0 - w1;
                    if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
                    double z = w0 * A.z + w1 * B.z + w2 * C.z;
                    float& dz = f.depth[size_t(y) * W + x];
                    if (z >= dz) continue;
                    dz = float(z);
                    f.ids[size_t(y) * W + x] = id;
                    row[x] = shadeColor(base, w0 * A.shade + w1 * B.shade + w2 * C.shade);
                }
            }
        }
    }
    Q_UNUSED(pal);
}

// Отрезки с проверкой глубины по z-буферу (скрытые линии не рисуются), сглаживание QPainter.
void View3D::drawSegments(QPainter& p, const Frame& f, const Camera& cam, const std::vector<Vec3>& seg, const QColor& color, double width, bool depthTest) const {
    const double cx = f.w / 2.0, cy = f.h / 2.0;
    p.setPen(QPen(color, width * f.dpr, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    auto visible = [&](double sx, double sy, double z) {
        if (!depthTest) return true;
        int x = int(sx), y = int(sy);
        if (x < 0 || y < 0 || x >= f.w || y >= f.h) return false;
        float d = f.depth[size_t(y) * f.w + x];
        double bias = perspective_ ? std::max(0.002 * z, 0.05) : std::max(0.0015 * dist_, 0.05);
        return z <= d + bias;
    };
    QVector<QLineF> lines;
    for (size_t i = 0; i + 1 < seg.size(); i += 2) {
        double ax, ay, az, bx, by, bz;
        if (!toScreen(cam, seg[i], ax, ay, az) || !toScreen(cam, seg[i + 1], bx, by, bz)) continue;
        ax = ax * f.dpr + cx; ay = ay * f.dpr + cy; bx = bx * f.dpr + cx; by = by * f.dpr + cy;
        if (std::max(ax, bx) < 0 || std::max(ay, by) < 0 || std::min(ax, bx) > f.w || std::min(ay, by) > f.h) continue;
        const double len = std::hypot(bx - ax, by - ay);
        const int steps = depthTest ? std::clamp(int(len / 3) + 1, 1, 400) : 1;
        double t0 = -1;
        for (int s = 0; s <= steps; ++s) {
            double t = double(s) / steps;
            bool vis = s < steps && visible(ax + (bx - ax) * (t + 0.5 / steps), ay + (by - ay) * (t + 0.5 / steps), az + (bz - az) * (t + 0.5 / steps));
            if (vis && t0 < 0) t0 = t;
            if (!vis && t0 >= 0) { lines.append(QLineF(ax + (bx - ax) * t0, ay + (by - ay) * t0, ax + (bx - ax) * t, ay + (by - ay) * t)); t0 = -1; }
        }
    }
    p.drawLines(lines);
}

void View3D::drawGrid(QPainter& p, Frame& f, const Camera& cam) const {
    const Palette& pal = Theme::current();
    md::Bbox b = sceneBbox();
    double extent = b.valid ? std::max({b.size().x, b.size().y, 100.0}) * 1.5 : 1000;
    double step = std::pow(10.0, std::floor(std::log10(extent / 10)));
    int n = int(std::ceil(extent / step));
    if (n > 40) { step *= 2.5; n = int(std::ceil(extent / step)); }
    if (n > 40) { step *= 2; n = int(std::ceil(extent / step)); }
    double ox = b.valid ? std::round(b.center().x / step) * step : 0, oy = b.valid ? std::round(b.center().y / step) * step : 0;
    std::vector<Vec3> minor, major;
    for (int i = -n; i <= n; ++i) {
        auto& dst = (i % 5 == 0) ? major : minor;
        dst.push_back({ox + i * step, oy - n * step, 0}); dst.push_back({ox + i * step, oy + n * step, 0});
        dst.push_back({ox - n * step, oy + i * step, 0}); dst.push_back({ox + n * step, oy + i * step, 0});
    }
    drawSegments(p, f, cam, minor, pal.grid, 0.8, true);
    drawSegments(p, f, cam, major, pal.gridMajor, 1.0, true);
    QColor xc(0xC2, 0x5B, 0x4A), yc(0x5B, 0x8C, 0x5A);
    drawSegments(p, f, cam, {{ox - n * step, oy, 0}, {ox + n * step, oy, 0}}, xc, 1.2, true);
    drawSegments(p, f, cam, {{ox, oy - n * step, 0}, {ox, oy + n * step, 0}}, yc, 1.2, true);
}

void View3D::drawTriad(QPainter& p, const Camera& cam, int w, int h) const {
    const Palette& pal = Theme::current();
    const QPointF o(44, h - 44);
    const double L = 28;
    struct Ax { Vec3 d; QColor c; const char* n; } axes[] = {{{1, 0, 0}, QColor(0xC2, 0x5B, 0x4A), "X"}, {{0, 1, 0}, QColor(0x5B, 0x8C, 0x5A), "Y"}, {{0, 0, 1}, QColor(0x4A, 0x6E, 0xB5), "Z"}};
    p.setRenderHint(QPainter::Antialiasing);
    QFont fnt = p.font(); fnt.setPointSizeF(9); p.setFont(fnt);
    for (const Ax& a : axes) {
        QPointF e = o + QPointF(a.d.dot(cam.right), -a.d.dot(cam.up)) * L;
        p.setPen(QPen(a.c, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(o, e);
        p.setPen(a.c);
        p.drawText(QRectF(e.x() - 8, e.y() - 8, 16, 16), Qt::AlignCenter, QString::fromLatin1(a.n));
    }
    Q_UNUSED(w); Q_UNUSED(pal);
}

void View3D::renderFrame(Frame& f, const QSize& size, qreal dpr) const {
    const Palette& pal = Theme::current();
    f.w = std::max(1, int(size.width() * dpr)); f.h = std::max(1, int(size.height() * dpr)); f.dpr = dpr;
    f.color = QImage(f.w, f.h, QImage::Format_RGB32);
    f.color.fill(pal.canvas);
    f.depth.assign(size_t(f.w) * f.h, std::numeric_limits<float>::infinity());
    f.ids.assign(size_t(f.w) * f.h, 0);
    Camera cam = camera(size.width(), size.height());
    rasterTriangles(f, cam);
    QPainter p(&f.color);
    p.setRenderHint(QPainter::Antialiasing);
    if (grid_) drawGrid(p, f, cam);
    if (edges_) {
        QColor ec = Theme::isDark() ? QColor(0xE8, 0xE6, 0xE1) : QColor(0x2B, 0x2A, 0x28);
        for (const View3DItem& it : items_) {
            std::vector<Vec3> seg; seg.reserve(it.mesh.segmentCount() * 2);
            for (size_t i = 0; i + 5 < it.mesh.edges.size(); i += 6) { seg.push_back({it.mesh.edges[i], it.mesh.edges[i + 1], it.mesh.edges[i + 2]}); seg.push_back({it.mesh.edges[i + 3], it.mesh.edges[i + 4], it.mesh.edges[i + 5]}); }
            drawSegments(p, f, cam, seg, ec, 1.0, true);
        }
    }
    f.color.setDevicePixelRatio(dpr);
}

// Подсветка выбора и наведения поверх кадра: грань — заливка акцентом по буферу идентификаторов,
// ребро — толстая линия, вершина — точка, тело — контур всех рёбер.
void View3D::drawOverlay(QPainter& p, const Frame& f, const Camera& cam) const {
    const Palette& pal = Theme::current();
    auto itemIndex = [&](quint64 id) -> int { for (size_t i = 0; i < items_.size(); ++i) if (items_[i].id == id) return int(i); return -1; };
    auto paintFace = [&](const Pick& pk, const QColor& color) {
        int ii = itemIndex(pk.item);
        if (ii < 0 || pk.face <= 0) return;
        const uint32_t want = (uint32_t(ii + 1) << kItemShift) | uint32_t(pk.face);
        QImage mask(f.w, f.h, QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);
        QRgb c = qPremultiply(color.rgba());
        bool any = false;
        for (int y = 0; y < f.h; ++y) { QRgb* row = reinterpret_cast<QRgb*>(mask.scanLine(y)); const uint32_t* ids = &f.ids[size_t(y) * f.w]; for (int x = 0; x < f.w; ++x) if (ids[x] == want) { row[x] = c; any = true; } }
        if (!any) return;
        mask.setDevicePixelRatio(f.dpr);
        p.drawImage(QPointF(0, 0), mask);
    };
    auto edgeSegments = [&](const Pick& pk, bool all) {
        std::vector<Vec3> seg;
        int ii = itemIndex(pk.item);
        if (ii < 0) return seg;
        const md::Mesh& m = items_[size_t(ii)].mesh;
        for (size_t s = 0; s < m.segmentCount(); ++s) {
            if (!all && (s >= m.edgeOfSegment.size() || int(m.edgeOfSegment[s]) != pk.edge)) continue;
            seg.push_back({m.edges[s * 6], m.edges[s * 6 + 1], m.edges[s * 6 + 2]});
            seg.push_back({m.edges[s * 6 + 3], m.edges[s * 6 + 4], m.edges[s * 6 + 5]});
        }
        return seg;
    };
    auto draw = [&](const Pick& pk, const QColor& fill, const QColor& line, double w) {
        if (!pk.valid()) return;
        if (pk.face > 0 && pk.edge == 0) paintFace(pk, fill);
        else if (pk.edge > 0) drawSegments(p, f, cam, edgeSegments(pk, false), line, w, false);
        else if (pk.face == 0 && pk.edge == 0 && pickMode_ == PickMode::Vertex) {
            QPointF s = project(pk.point) * f.dpr;
            p.setPen(QPen(line, 1.5 * f.dpr)); p.setBrush(fill);
            p.drawEllipse(s, 4.5 * f.dpr, 4.5 * f.dpr);
        } else drawSegments(p, f, cam, edgeSegments(pk, true), line, w, true);
    };
    QColor hoverFill = pal.accent; hoverFill.setAlpha(70);
    QColor selFill = pal.accent; selFill.setAlpha(140);
    if (hover_.valid() && !(hover_ == selection_)) draw(hover_, hoverFill, pal.accent, 2.0);
    draw(selection_, selFill, pal.accent, 2.6);
}

void View3D::ensureFrame() {
    const qreal dpr = devicePixelRatioF();
    if (!dirty_ && frame_.w == int(width() * dpr) && frame_.h == int(height() * dpr)) return;
    renderFrame(frame_, size(), dpr);
    dirty_ = false;
}

QImage View3D::renderToImage(const QSize& size, qreal dpr) {
    Frame f;
    renderFrame(f, size, dpr);
    QImage out = f.color.copy();
    out.setDevicePixelRatio(dpr);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    drawOverlay(p, f, camera(size.width(), size.height()));
    drawTriad(p, camera(size.width(), size.height()), size.width(), size.height());
    return out;
}

void View3D::paintEvent(QPaintEvent*) {
    ensureFrame();
    QPainter p(this);
    p.drawImage(QPointF(0, 0), frame_.color);
    p.setRenderHint(QPainter::Antialiasing);
    Camera cam = camera(width(), height());
    {
        // оверлей рисуем в координатах устройства, поэтому временно снимаем масштаб dpr
        p.save();
        p.scale(1.0 / frame_.dpr, 1.0 / frame_.dpr);
        drawOverlay(p, frame_, cam);
        p.restore();
    }
    drawTriad(p, cam, width(), height());
    if (items_.empty()) {
        const Palette& pal = Theme::current();
        p.setPen(pal.muted);
        QFont f(Theme::serifFamily()); f.setPointSizeF(15); p.setFont(f);
        p.drawText(rect().adjusted(0, 0, 0, -40), Qt::AlignCenter, QStringLiteral("Создайте эскиз и выдавите его — тело появится здесь"));
    }
}

// ---------------------------------------------------------------- выбор
View3D::Pick View3D::pickIn(const Frame& f, QPointF px) const {
    Pick pk;
    if (f.w == 0) return pk;
    const Camera cam = camera(int(f.w / f.dpr), int(f.h / f.dpr));
    const QPointF dp = px * f.dpr;
    const int x = int(dp.x()), y = int(dp.y());
    auto itemAt = [&](int xx, int yy) -> uint32_t { if (xx < 0 || yy < 0 || xx >= f.w || yy >= f.h) return 0; return f.ids[size_t(yy) * f.w + xx]; };
    auto unproject = [&](int xx, int yy) -> Vec3 {
        float z = f.depth[size_t(yy) * f.w + xx];
        double sx = (xx + 0.5 - f.w / 2.0) / f.dpr, sy = (yy + 0.5 - f.h / 2.0) / f.dpr;
        if (perspective_) return cam.eye + (cam.right * (sx / cam.focal * z) + cam.up * (-sy / cam.focal * z) + cam.forward * z);
        const double s = dist_ / cam.focal;
        return cam.eye + cam.right * (sx * s) + cam.up * (-sy * s) + cam.forward * z;
    };
    if (pickMode_ == PickMode::Edge || pickMode_ == PickMode::Vertex) {
        const double radius = (pickMode_ == PickMode::Edge ? 6.0 : 8.0) * f.dpr;
        double best = radius;
        for (size_t ii = 0; ii < items_.size(); ++ii) {
            const md::Mesh& m = items_[ii].mesh;
            for (size_t s = 0; s < m.segmentCount(); ++s) {
                Vec3 a{m.edges[s * 6], m.edges[s * 6 + 1], m.edges[s * 6 + 2]}, b{m.edges[s * 6 + 3], m.edges[s * 6 + 4], m.edges[s * 6 + 5]};
                double ax, ay, az, bx, by, bz;
                if (!toScreen(cam, a, ax, ay, az) || !toScreen(cam, b, bx, by, bz)) continue;
                ax = ax * f.dpr + f.w / 2.0; ay = ay * f.dpr + f.h / 2.0; bx = bx * f.dpr + f.w / 2.0; by = by * f.dpr + f.h / 2.0;
                auto visibleAt = [&](double sx, double sy, double z) {
                    int xx = int(sx), yy = int(sy);
                    if (xx < 0 || yy < 0 || xx >= f.w || yy >= f.h) return false;
                    double bias = perspective_ ? std::max(0.004 * z, 0.1) : std::max(0.003 * dist_, 0.1);
                    return z <= f.depth[size_t(yy) * f.w + xx] + bias;
                };
                if (pickMode_ == PickMode::Vertex) {
                    for (int e = 0; e < 2; ++e) {
                        double sx = e ? bx : ax, sy = e ? by : ay, z = e ? bz : az;
                        double d = std::hypot(sx - dp.x(), sy - dp.y());
                        if (d < best && visibleAt(sx, sy, z)) { best = d; pk.item = items_[ii].id; pk.point = e ? b : a; pk.face = 0; pk.edge = 0; }
                    }
                } else {
                    double vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
                    double t = l2 > 1e-12 ? std::clamp(((dp.x() - ax) * vx + (dp.y() - ay) * vy) / l2, 0.0, 1.0) : 0;
                    double qx = ax + vx * t, qy = ay + vy * t, qz = az + (bz - az) * t;
                    double d = std::hypot(qx - dp.x(), qy - dp.y());
                    if (d < best && visibleAt(qx, qy, qz)) { best = d; pk.item = items_[ii].id; pk.edge = s < m.edgeOfSegment.size() ? int(m.edgeOfSegment[s]) : 1; pk.point = a + (b - a) * t; }
                }
            }
        }
        return pk;
    }
    uint32_t id = itemAt(x, y);
    if (!id) {   // небольшой допуск
        for (int r = 1; r <= 2 && !id; ++r) for (int dy = -r; dy <= r && !id; ++dy) for (int dx = -r; dx <= r && !id; ++dx) id = itemAt(x + dx, y + dy);
    }
    if (!id) return pk;
    const uint32_t ii = (id >> kItemShift) - 1;
    if (ii >= items_.size()) return pk;
    pk.item = items_[ii].id;
    pk.face = pickMode_ == PickMode::Face ? int(id & ((1u << kItemShift) - 1)) : 0;
    int xx = std::clamp(x, 0, f.w - 1), yy = std::clamp(y, 0, f.h - 1);
    pk.point = unproject(xx, yy);
    // нормаль: средняя по треугольникам этой грани (достаточно для направления отверстия)
    const md::Mesh& m = items_[ii].mesh;
    Vec3 n;
    const uint32_t face = id & ((1u << kItemShift) - 1);
    for (size_t t = 0; t < m.triangleCount() && t < m.faceOfTriangle.size(); ++t) {
        if (m.faceOfTriangle[t] != face) continue;
        for (int k = 0; k < 3; ++k) { uint32_t v = m.indices[t * 3 + k]; if (v * 3 + 2 < m.normals.size()) n = n + Vec3{m.normals[v * 3], m.normals[v * 3 + 1], m.normals[v * 3 + 2]}; }
    }
    pk.normal = n.length() > 1e-9 ? n.normalized() : Vec3{0, 0, 1};
    return pk;
}

View3D::Pick View3D::pickAt(QPoint px) const {
    if (frame_.w == 0 || dirty_) { Frame f; renderFrame(f, size(), devicePixelRatioF()); return pickIn(f, px); }
    return pickIn(frame_, px);
}

// ---------------------------------------------------------------- мышь
void View3D::mousePressEvent(QMouseEvent* e) {
    lastMouse_ = e->pos();
    moved_ = false;
    if (e->button() == Qt::LeftButton) { dragging_ = true; panning_ = e->modifiers() & Qt::ShiftModifier; }
    else if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) { dragging_ = true; panning_ = !(e->modifiers() & Qt::ControlModifier) || e->button() == Qt::RightButton; }
    setFocus();
}

void View3D::mouseMoveEvent(QMouseEvent* e) {
    QPoint d = e->pos() - lastMouse_;
    if (dragging_) {
        if (d.manhattanLength() > 2) moved_ = true;
        if (moved_) {
            if (panning_) pan(d.x(), d.y());
            else orbit(-d.x() * 0.5, d.y() * 0.5);
            lastMouse_ = e->pos();
        }
        return;
    }
    Pick h = pickAt(e->pos());
    if (!(h == hover_)) { hover_ = h; update(); Q_EMIT hoverChanged(h); }
}

void View3D::mouseReleaseEvent(QMouseEvent* e) {
    bool wasDrag = dragging_ && moved_;
    dragging_ = false;
    if (!wasDrag && e->button() == Qt::LeftButton) {
        Pick p = pickAt(e->pos());
        setSelection(p);
        Q_EMIT picked(p);
    }
}

void View3D::mouseDoubleClickEvent(QMouseEvent* e) { if (e->button() == Qt::LeftButton) Q_EMIT doubleClicked(pickAt(e->pos())); }

void View3D::wheelEvent(QWheelEvent* e) {
    double steps = e->angleDelta().y() / 120.0;
    if (steps == 0) steps = e->pixelDelta().y() / 60.0;
    if (steps == 0) return;
    zoomBy(std::pow(1.15, steps));
}

void View3D::resizeEvent(QResizeEvent*) { dirty_ = true; }
void View3D::leaveEvent(QEvent*) { if (hover_.valid()) { hover_ = {}; update(); Q_EMIT hoverChanged(hover_); } }

} // namespace psi
