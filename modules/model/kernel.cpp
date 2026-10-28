// Реализация фасада ядра на Open CASCADE. Единственный файл модуля, видящий заголовки OCCT.
#include "modules/model/kernel_occt.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <Poly_Triangulation.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TopAbs.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#include <QTextStream>
#include <cmath>
#include <sstream>

namespace psi::md {

// ---------------------------------------------------------------- базовые типы
double Vec3::length() const { return std::sqrt(x * x + y * y + z * z); }
Vec3 Vec3::normalized() const { double l = length(); return l > 1e-12 ? Vec3{x / l, y / l, z / l} : Vec3{0, 0, 1}; }

Plane Plane::XY(double o) { Plane p; p.origin = {0, 0, o}; p.normal = {0, 0, 1}; p.xdir = {1, 0, 0}; return p; }
Plane Plane::XZ(double o) { Plane p; p.origin = {0, -o, 0}; p.normal = {0, -1, 0}; p.xdir = {1, 0, 0}; return p; }   // Y эскиза → +Z
Plane Plane::YZ(double o) { Plane p; p.origin = {o, 0, 0}; p.normal = {1, 0, 0}; p.xdir = {0, 1, 0}; return p; }    // Y эскиза → +Z


static gp_Pnt P(const Vec3& v) { return gp_Pnt(v.x, v.y, v.z); }
static gp_Dir D(const Vec3& v) { Vec3 n = v.normalized(); return gp_Dir(n.x, n.y, n.z); }
static Vec3 V(const gp_Pnt& p) { return {p.X(), p.Y(), p.Z()}; }
static Vec3 V(const gp_Dir& d) { return {d.X(), d.Y(), d.Z()}; }
static Shape wrap(const TopoDS_Shape& s) { return fromOcc(s); }
static const TopoDS_Shape& raw(const Shape& s) { return occShape(s); }

Shape::Shape() : impl_(std::make_unique<Impl>()) {}
Shape::Shape(const Shape& o) : impl_(std::make_unique<Impl>(*o.impl_)) {}
Shape::Shape(Shape&& o) noexcept : impl_(std::move(o.impl_)) { if (!impl_) impl_ = std::make_unique<Impl>(); }
Shape& Shape::operator=(const Shape& o) { if (this != &o) *impl_ = *o.impl_; return *this; }
Shape& Shape::operator=(Shape&& o) noexcept { if (this != &o) { impl_ = std::move(o.impl_); if (!impl_) impl_ = std::make_unique<Impl>(); } return *this; }
Shape::~Shape() = default;

bool Shape::isNull() const { return impl_->shape.IsNull(); }
bool Shape::isValid() const { if (isNull()) return false; try { return BRepCheck_Analyzer(impl_->shape).IsValid(); } catch (const Standard_Failure&) { return false; } }

static int count(const TopoDS_Shape& s, TopAbs_ShapeEnum t) { if (s.IsNull()) return 0; TopTools_IndexedMapOfShape m; TopExp::MapShapes(s, t, m); return m.Extent(); }
int Shape::faceCount() const { return count(impl_->shape, TopAbs_FACE); }
int Shape::edgeCount() const { return count(impl_->shape, TopAbs_EDGE); }
int Shape::solidCount() const { return count(impl_->shape, TopAbs_SOLID); }

MassProps Shape::massProps() const {
    MassProps m;
    if (isNull()) return m;
    try {
        GProp_GProps vp, sp;
        if (solidCount() > 0) { BRepGProp::VolumeProperties(impl_->shape, vp); m.volume = vp.Mass(); m.centroid = V(vp.CentreOfMass()); }
        BRepGProp::SurfaceProperties(impl_->shape, sp);
        m.area = sp.Mass();
        if (solidCount() == 0 && m.area > 0) m.centroid = V(sp.CentreOfMass());
    } catch (const Standard_Failure&) {}
    return m;
}

Bbox Shape::bbox() const {
    Bbox b;
    if (isNull()) return b;
    Bnd_Box box;
    BRepBndLib::Add(impl_->shape, box, true);
    if (box.IsVoid()) return b;
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    b.min = {x0, y0, z0}; b.max = {x1, y1, z1}; b.valid = true;
    return b;
}

Mesh Shape::mesh(double deflection, double angle) const {
    Mesh m;
    if (isNull()) return m;
    try {
        BRepMesh_IncrementalMesh mesher(impl_->shape, deflection, false, angle, true);
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(impl_->shape, TopAbs_FACE, faces);
        for (int fi = 1; fi <= faces.Extent(); ++fi) {
            const TopoDS_Face face = TopoDS::Face(faces(fi));
            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
            if (tri.IsNull()) continue;
            const gp_Trsf trsf = loc.Transformation();
            const bool reversed = face.Orientation() == TopAbs_REVERSED;
            const uint32_t base = uint32_t(m.positions.size() / 3);
            BRepGProp_Face gface(face);
            const bool uv = tri->HasUVNodes() && !BRep_Tool::Surface(face).IsNull();
            std::vector<Vec3> nrm(tri->NbNodes());
            for (int i = 1; i <= tri->NbNodes(); ++i) {
                gp_Pnt p = tri->Node(i).Transformed(trsf);
                m.positions.insert(m.positions.end(), {float(p.X()), float(p.Y()), float(p.Z())});
                if (uv) { gp_Pnt q; gp_Vec n; gp_Pnt2d t = tri->UVNode(i); gface.Normal(t.X(), t.Y(), q, n); if (n.Magnitude() > 1e-12) { n.Normalize(); n.Transform(trsf); nrm[i - 1] = {n.X(), n.Y(), n.Z()}; } }
            }
            for (int i = 1; i <= tri->NbTriangles(); ++i) {
                int a, b, c; tri->Triangle(i).Get(a, b, c);
                if (reversed) std::swap(b, c);
                if (!uv) {   // нормаль треугольника
                    Vec3 pa{m.positions[(base + a - 1) * 3], m.positions[(base + a - 1) * 3 + 1], m.positions[(base + a - 1) * 3 + 2]};
                    Vec3 pb{m.positions[(base + b - 1) * 3], m.positions[(base + b - 1) * 3 + 1], m.positions[(base + b - 1) * 3 + 2]};
                    Vec3 pc{m.positions[(base + c - 1) * 3], m.positions[(base + c - 1) * 3 + 1], m.positions[(base + c - 1) * 3 + 2]};
                    Vec3 n = (pb - pa).cross(pc - pa).normalized();
                    for (int k : {a, b, c}) nrm[k - 1] = nrm[k - 1] + n;
                }
                m.indices.insert(m.indices.end(), {base + uint32_t(a - 1), base + uint32_t(b - 1), base + uint32_t(c - 1)});
                m.faceOfTriangle.push_back(uint32_t(fi));
            }
            for (const Vec3& n : nrm) { Vec3 u = n.normalized(); m.normals.insert(m.normals.end(), {float(u.x), float(u.y), float(u.z)}); }
        }
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(impl_->shape, TopAbs_EDGE, edges);
        for (int ei = 1; ei <= edges.Extent(); ++ei) {
            const TopoDS_Edge edge = TopoDS::Edge(edges(ei));
            if (BRep_Tool::Degenerated(edge)) continue;
            BRepAdaptor_Curve curve(edge);
            GCPnts_TangentialDeflection disc(curve, angle, deflection, 2);
            for (int i = 1; i < disc.NbPoints(); ++i) {
                gp_Pnt a = disc.Value(i), b = disc.Value(i + 1);
                m.edges.insert(m.edges.end(), {float(a.X()), float(a.Y()), float(a.Z()), float(b.X()), float(b.Y()), float(b.Z())});
                m.edgeOfSegment.push_back(uint32_t(ei));
            }
        }
    } catch (const Standard_Failure&) {}
    return m;
}

static TopoDS_Face faceAt(const TopoDS_Shape& s, int index) {
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(s, TopAbs_FACE, faces);
    if (index < 1 || index > faces.Extent()) return TopoDS_Face();
    return TopoDS::Face(faces(index));
}

Plane Shape::planeOfFace(int faceIndex, bool* ok) const {
    Plane pl;
    if (ok) *ok = false;
    TopoDS_Face face = faceAt(impl_->shape, faceIndex);
    if (face.IsNull()) return pl;
    try {
        BRepAdaptor_Surface surf(face);
        if (surf.GetType() != GeomAbs_Plane) return pl;
        gp_Pln p = surf.Plane();
        gp_Dir n = p.Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED) n.Reverse();
        GProp_GProps sp; BRepGProp::SurfaceProperties(face, sp);
        pl.origin = V(sp.CentreOfMass()); pl.normal = V(n);
        gp_Dir x = p.XAxis().Direction();
        pl.xdir = V(x);
        // правая тройка: X × Y = N
        if (pl.normal.cross(pl.xdir).cross(pl.normal).dot(pl.xdir) < 0) pl.xdir = pl.xdir * -1;
        if (ok) *ok = true;
    } catch (const Standard_Failure&) {}
    return pl;
}

Vec3 Shape::faceNormalAt(int faceIndex, const Vec3& near) const {
    TopoDS_Face face = faceAt(impl_->shape, faceIndex);
    if (face.IsNull()) return {0, 0, 1};
    try {
        Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
        GeomAPI_ProjectPointOnSurf proj(P(near), surf);
        if (proj.NbPoints() < 1) return {0, 0, 1};
        double u, v; proj.LowerDistanceParameters(u, v);
        gp_Pnt q; gp_Vec n; BRepGProp_Face(face).Normal(u, v, q, n);
        if (n.Magnitude() < 1e-12) return {0, 0, 1};
        n.Normalize();
        return {n.X(), n.Y(), n.Z()};
    } catch (const Standard_Failure&) { return {0, 0, 1}; }
}

QByteArray Shape::toBrep() const {
    if (isNull()) return {};
    std::ostringstream os;
    BRepTools::Write(impl_->shape, os);
    const std::string s = os.str();
    return QByteArray(s.data(), int(s.size()));
}

Shape Shape::fromBrep(const QByteArray& data, QString* error) {
    Shape s;
    if (data.isEmpty()) { if (error) *error = QStringLiteral("Пустые данные BREP"); return s; }
    try {
        std::istringstream is(std::string(data.constData(), size_t(data.size())));
        BRep_Builder b;
        BRepTools::Read(s.impl().shape, is, b);
        if (s.isNull() && error) *error = QStringLiteral("Не удалось прочитать BREP");
    } catch (const Standard_Failure& f) { if (error) *error = QString::fromUtf8(f.GetMessageString()); }
    return s;
}

QString Shape::describe() const {
    if (isNull()) return QStringLiteral("пусто");
    int solids = solidCount();
    return QStringLiteral("%1: %2 граней, %3 рёбер").arg(solids > 1 ? QStringLiteral("%1 тел").arg(solids) : solids == 1 ? QStringLiteral("тело") : QStringLiteral("поверхность")).arg(faceCount()).arg(edgeCount());
}

// ---------------------------------------------------------------- профиль → грань
static TopoDS_Wire loopWire(const ProfileLoop& loop, const Plane& plane, QString* err) {
    BRepBuilderAPI_MakeWire mk;
    for (const ProfileSegment& s : loop.segments) {
        gp_Pnt a = P(plane.map(s.x1, s.y1)), b = P(plane.map(s.x2, s.y2));
        if (a.Distance(b) < 1e-9) continue;
        if (std::abs(s.bulge) < 1e-12) {
            mk.Add(BRepBuilderAPI_MakeEdge(a, b).Edge());
        } else {
            // дуга по трём точкам: середина хорды + сагитта по нормали к хорде (слева при bulge > 0)
            double dx = s.x2 - s.x1, dy = s.y2 - s.y1, c = std::hypot(dx, dy);
            double sag = s.bulge * c / 2.0;
            double mx = (s.x1 + s.x2) / 2 - dy / c * sag, my = (s.y1 + s.y2) / 2 + dx / c * sag;
            gp_Pnt m = P(plane.map(mx, my));
            GC_MakeArcOfCircle arc(a, m, b);
            if (!arc.IsDone()) { if (err) *err = QStringLiteral("Дуга профиля не построена"); return TopoDS_Wire(); }
            mk.Add(BRepBuilderAPI_MakeEdge(arc.Value()).Edge());
        }
        if (!mk.IsDone()) { if (err) *err = QStringLiteral("Контур профиля не замкнут или самопересекается"); return TopoDS_Wire(); }
    }
    if (!mk.IsDone()) { if (err) *err = QStringLiteral("Контур профиля пуст"); return TopoDS_Wire(); }
    TopoDS_Wire w = mk.Wire();
    if (!w.Closed()) { if (err) *err = QStringLiteral("Контур профиля не замкнут"); return TopoDS_Wire(); }
    return w;
}

static TopoDS_Wire circleWire(const ProfileCircle& c, const Plane& plane) {
    gp_Ax2 ax(P(plane.map(c.cx, c.cy)), D(plane.normal), D(plane.xdir));
    TopoDS_Edge e = BRepBuilderAPI_MakeEdge(gp_Circ(ax, c.r)).Edge();
    return BRepBuilderAPI_MakeWire(e).Wire();
}

Result Kernel::makeFace(const Profile& profile, const Plane& plane) {
    if (profile.empty()) return Result::fail(QStringLiteral("Профиль пуст: нужен замкнутый контур"));
    try {
        gp_Pln pln(P(plane.origin), D(plane.normal));
        struct Item { TopoDS_Face face; double area; gp_Pnt sample; };
        std::vector<Item> items;
        QString err;
        auto add = [&](const TopoDS_Wire& w) {
            BRepBuilderAPI_MakeFace mf(pln, w, true);
            if (!mf.IsDone()) { err = QStringLiteral("Грань профиля не построена"); return; }
            TopoDS_Face f = mf.Face();
            GProp_GProps sp; BRepGProp::SurfaceProperties(f, sp);
            TopExp_Explorer ex(w, TopAbs_VERTEX);
            items.push_back({f, sp.Mass(), BRep_Tool::Pnt(TopoDS::Vertex(ex.Current()))});
        };
        for (const ProfileLoop& l : profile.loops) { TopoDS_Wire w = loopWire(l, plane, &err); if (w.IsNull()) return Result::fail(err); add(w); if (!err.isEmpty()) return Result::fail(err); }
        for (const ProfileCircle& c : profile.circles) { if (c.r <= 0) return Result::fail(QStringLiteral("Окружность профиля с нулевым радиусом")); add(circleWire(c, plane)); if (!err.isEmpty()) return Result::fail(err); }
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.area > b.area; });
        // внешние контуры и отверстия: контур внутри уже принятого внешнего — вычитается
        std::vector<TopoDS_Shape> outers;
        std::vector<std::vector<TopoDS_Face>> holes;
        for (const Item& it : items) {
            int inside = -1;
            for (size_t i = 0; i < outers.size() && inside < 0; ++i) {
                BRepClass_FaceClassifier cl(TopoDS::Face(outers[i]), it.sample, 1e-6);
                if (cl.State() == TopAbs_IN) inside = int(i);
            }
            if (inside >= 0) holes[size_t(inside)].push_back(it.face);
            else { outers.push_back(it.face); holes.emplace_back(); }
        }
        TopoDS_Compound comp; BRep_Builder b; b.MakeCompound(comp);
        for (size_t i = 0; i < outers.size(); ++i) {
            TopoDS_Shape f = outers[i];
            for (const TopoDS_Face& h : holes[i]) {
                BRepAlgoAPI_Cut cut(f, h);
                if (!cut.IsDone()) return Result::fail(QStringLiteral("Отверстие профиля не вырезано"));
                f = cut.Shape();
            }
            if (outers.size() == 1) { Result r; r.shape = wrap(f); return r; }
            b.Add(comp, f);
        }
        Result r; r.shape = wrap(comp); return r;
    } catch (const Standard_Failure& f) { return Result::fail(QStringLiteral("Профиль: %1").arg(QString::fromUtf8(f.GetMessageString()))); }
}

// ---------------------------------------------------------------- операции
static Result finish(const TopoDS_Shape& s, const QString& what) {
    if (s.IsNull()) return Result::fail(QStringLiteral("%1: пустой результат").arg(what));
    Result r; r.shape = wrap(s); return r;
}

#define PSI_OCC_TRY try {
#define PSI_OCC_CATCH(what) } catch (const Standard_Failure& f) { return Result::fail(QStringLiteral("%1: %2").arg(what, QString::fromUtf8(f.GetMessageString()))); } catch (...) { return Result::fail(QStringLiteral("%1: внутренняя ошибка ядра").arg(what)); }

Result Kernel::extrude(const Profile& profile, const Plane& plane, double distance, double distance2, bool symmetric) {
    const QString what = QStringLiteral("Выдавливание");
    if (std::abs(distance) < 1e-9) return Result::fail(what + QStringLiteral(": расстояние равно нулю"));
    Result face = makeFace(profile, plane);
    if (!face.ok()) return face;
    PSI_OCC_TRY
        gp_Vec n(D(plane.normal));
        TopoDS_Shape base = raw(face.shape);
        if (symmetric) {
            gp_Trsf t; t.SetTranslation(n * (-distance / 2));
            base = BRepBuilderAPI_Transform(base, t, true).Shape();
            distance2 = 0;
        }
        BRepPrimAPI_MakePrism prism(base, n * distance);
        if (!prism.IsDone()) return Result::fail(what + QStringLiteral(": призма не построена"));
        TopoDS_Shape s = prism.Shape();
        if (std::abs(distance2) > 1e-9) {
            BRepPrimAPI_MakePrism prism2(raw(face.shape), n * (-distance2));
            if (!prism2.IsDone()) return Result::fail(what + QStringLiteral(": вторая призма не построена"));
            BRepAlgoAPI_Fuse fuse(s, prism2.Shape());
            if (!fuse.IsDone()) return Result::fail(what + QStringLiteral(": объединение направлений"));
            ShapeUpgrade_UnifySameDomain uni(fuse.Shape(), true, true, false); uni.Build();
            s = uni.Shape();
        }
        return finish(s, what);
    PSI_OCC_CATCH(what)
}

Result Kernel::revolve(const Profile& profile, const Plane& plane, const Vec3& axisPoint, const Vec3& axisDir, double angleDeg) {
    const QString what = QStringLiteral("Вращение");
    if (std::abs(angleDeg) < 1e-9) return Result::fail(what + QStringLiteral(": угол равен нулю"));
    if (axisDir.length() < 1e-12) return Result::fail(what + QStringLiteral(": ось не задана"));
    Result face = makeFace(profile, plane);
    if (!face.ok()) return face;
    PSI_OCC_TRY
        gp_Ax1 ax(P(axisPoint), D(axisDir));
        double a = std::min(std::abs(angleDeg), 360.0) * M_PI / 180.0;
        BRepPrimAPI_MakeRevol rev(raw(face.shape), ax, a, true);
        if (!rev.IsDone()) return Result::fail(what + QStringLiteral(": тело вращения не построено (профиль пересекает ось?)"));
        return finish(rev.Shape(), what);
    PSI_OCC_CATCH(what)
}

Result Kernel::cylinder(const Vec3& base, const Vec3& dir, double radius, double height) {
    const QString what = QStringLiteral("Цилиндр");
    if (radius <= 0 || height <= 0) return Result::fail(what + QStringLiteral(": радиус и высота должны быть больше нуля"));
    PSI_OCC_TRY
        return finish(BRepPrimAPI_MakeCylinder(gp_Ax2(P(base), D(dir)), radius, height).Shape(), what);
    PSI_OCC_CATCH(what)
}

Result Kernel::cone(const Vec3& base, const Vec3& dir, double r1, double r2, double height) {
    const QString what = QStringLiteral("Конус");
    if (height <= 0 || r1 < 0 || r2 < 0 || std::abs(r1 - r2) < 1e-9) return Result::fail(what + QStringLiteral(": неверные размеры"));
    PSI_OCC_TRY
        return finish(BRepPrimAPI_MakeCone(gp_Ax2(P(base), D(dir)), r1, r2, height).Shape(), what);
    PSI_OCC_CATCH(what)
}

Result Kernel::box(const Vec3& corner, double dx, double dy, double dz) {
    const QString what = QStringLiteral("Параллелепипед");
    if (dx <= 0 || dy <= 0 || dz <= 0) return Result::fail(what + QStringLiteral(": размеры должны быть больше нуля"));
    PSI_OCC_TRY
        return finish(BRepPrimAPI_MakeBox(P(corner), dx, dy, dz).Shape(), what);
    PSI_OCC_CATCH(what)
}

Result Kernel::boolean(const Shape& a, const Shape& b, BoolOp op) {
    const QString what = op == BoolOp::Union ? QStringLiteral("Объединение") : op == BoolOp::Subtract ? QStringLiteral("Вычитание") : QStringLiteral("Пересечение");
    if (a.isNull()) return Result::fail(what + QStringLiteral(": первое тело пусто"));
    if (b.isNull()) return Result::fail(what + QStringLiteral(": второе тело пусто"));
    PSI_OCC_TRY
        TopoDS_Shape s;
        switch (op) {
        case BoolOp::Union: { BRepAlgoAPI_Fuse o(raw(a), raw(b)); if (!o.IsDone()) return Result::fail(what + QStringLiteral(" не выполнено")); s = o.Shape(); break; }
        case BoolOp::Subtract: { BRepAlgoAPI_Cut o(raw(a), raw(b)); if (!o.IsDone()) return Result::fail(what + QStringLiteral(" не выполнено")); s = o.Shape(); break; }
        case BoolOp::Intersect: { BRepAlgoAPI_Common o(raw(a), raw(b)); if (!o.IsDone()) return Result::fail(what + QStringLiteral(" не выполнено")); s = o.Shape(); break; }
        }
        if (s.IsNull() || count(s, TopAbs_FACE) == 0) return Result::fail(what + QStringLiteral(": тела не пересекаются"));
        ShapeUpgrade_UnifySameDomain uni(s, true, true, false); uni.Build();
        return finish(uni.Shape(), what);
    PSI_OCC_CATCH(what)
}

static bool collectEdges(const TopoDS_Shape& s, const std::vector<int>& wanted, std::vector<TopoDS_Edge>& out) {
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(s, TopAbs_EDGE, edges);
    if (wanted.empty()) { for (int i = 1; i <= edges.Extent(); ++i) { TopoDS_Edge e = TopoDS::Edge(edges(i)); if (!BRep_Tool::Degenerated(e)) out.push_back(e); } return !out.empty(); }
    for (int i : wanted) { if (i < 1 || i > edges.Extent()) return false; out.push_back(TopoDS::Edge(edges(i))); }
    return true;
}

Result Kernel::fillet(const Shape& s, const std::vector<int>& edges, double radius) {
    const QString what = QStringLiteral("Скругление");
    if (s.isNull()) return Result::fail(what + QStringLiteral(": тело пусто"));
    if (radius <= 0) return Result::fail(what + QStringLiteral(": радиус должен быть больше нуля"));
    PSI_OCC_TRY
        std::vector<TopoDS_Edge> list;
        if (!collectEdges(raw(s), edges, list)) return Result::fail(what + QStringLiteral(": ребро не найдено"));
        BRepFilletAPI_MakeFillet mk(raw(s));
        for (const TopoDS_Edge& e : list) mk.Add(radius, e);
        mk.Build();
        if (!mk.IsDone()) return Result::fail(what + QStringLiteral(": радиус %1 мм слишком велик для выбранных рёбер").arg(radius));
        TopoDS_Shape r = mk.Shape();
        if (!BRepCheck_Analyzer(r).IsValid()) return Result::fail(what + QStringLiteral(": результат некорректен (радиус %1 мм)").arg(radius));
        return finish(r, what);
    PSI_OCC_CATCH(what)
}

Result Kernel::chamfer(const Shape& s, const std::vector<int>& edges, double distance) {
    const QString what = QStringLiteral("Фаска");
    if (s.isNull()) return Result::fail(what + QStringLiteral(": тело пусто"));
    if (distance <= 0) return Result::fail(what + QStringLiteral(": размер должен быть больше нуля"));
    PSI_OCC_TRY
        std::vector<TopoDS_Edge> list;
        if (!collectEdges(raw(s), edges, list)) return Result::fail(what + QStringLiteral(": ребро не найдено"));
        BRepFilletAPI_MakeChamfer mk(raw(s));
        for (const TopoDS_Edge& e : list) mk.Add(distance, e);
        mk.Build();
        if (!mk.IsDone()) return Result::fail(what + QStringLiteral(": размер %1 мм слишком велик для выбранных рёбер").arg(distance));
        TopoDS_Shape r = mk.Shape();
        if (!BRepCheck_Analyzer(r).IsValid()) return Result::fail(what + QStringLiteral(": результат некорректен"));
        return finish(r, what);
    PSI_OCC_CATCH(what)
}

static Result transformed(const Shape& s, const gp_Trsf& t, const QString& what) {
    if (s.isNull()) return Result::fail(what + QStringLiteral(": тело пусто"));
    PSI_OCC_TRY
        return finish(BRepBuilderAPI_Transform(raw(s), t, true).Shape(), what);
    PSI_OCC_CATCH(what)
}

Result Kernel::translate(const Shape& s, const Vec3& d) { gp_Trsf t; t.SetTranslation(gp_Vec(d.x, d.y, d.z)); return transformed(s, t, QStringLiteral("Перенос")); }

Result Kernel::rotate(const Shape& s, const Vec3& axisPoint, const Vec3& axisDir, double angleDeg) {
    if (axisDir.length() < 1e-12) return Result::fail(QStringLiteral("Поворот: ось не задана"));
    gp_Trsf t; t.SetRotation(gp_Ax1(P(axisPoint), D(axisDir)), angleDeg * M_PI / 180.0);
    return transformed(s, t, QStringLiteral("Поворот"));
}

Result Kernel::mirror(const Shape& s, const Plane& plane) {
    gp_Trsf t; t.SetMirror(gp_Ax2(P(plane.origin), D(plane.normal)));
    return transformed(s, t, QStringLiteral("Зеркало"));
}

Result Kernel::compound(const std::vector<Shape>& shapes) {
    PSI_OCC_TRY
        TopoDS_Compound c; BRep_Builder b; b.MakeCompound(c);
        int n = 0;
        for (const Shape& s : shapes) if (!s.isNull()) { b.Add(c, raw(s)); ++n; }
        if (n == 0) return Result::fail(QStringLiteral("Нет тел"));
        if (n == 1) { for (const Shape& s : shapes) if (!s.isNull()) { Result r; r.shape = s; return r; } }
        return finish(c, QStringLiteral("Составное тело"));
    PSI_OCC_CATCH(QStringLiteral("Составное тело"))
}

Result Kernel::shell(const Shape& s, const std::vector<int>& openFaces, double thickness) {
    const QString what = QStringLiteral("Оболочка");
    if (s.isNull()) return Result::fail(what + QStringLiteral(": тело пусто"));
    if (std::abs(thickness) < 1e-9) return Result::fail(what + QStringLiteral(": толщина равна нулю"));
    PSI_OCC_TRY
        TopTools_ListOfShape faces;
        for (int i : openFaces) { TopoDS_Face f = faceAt(raw(s), i); if (f.IsNull()) return Result::fail(what + QStringLiteral(": грань %1 не найдена").arg(i)); faces.Append(f); }
        BRepOffsetAPI_MakeThickSolid mk;
        mk.MakeThickSolidByJoin(raw(s), faces, -std::abs(thickness), 1e-3);
        if (!mk.IsDone()) return Result::fail(what + QStringLiteral(": не построена при толщине %1 мм").arg(thickness));
        return finish(mk.Shape(), what);
    PSI_OCC_CATCH(what)
}

Result Kernel::fromMesh(const std::vector<float>& positions, const std::vector<uint32_t>& indices) {
    const QString what = QStringLiteral("Сетка");
    if (positions.size() < 9 || indices.size() < 3) return Result::fail(what + QStringLiteral(": нет треугольников"));
    PSI_OCC_TRY
        const int nn = int(positions.size() / 3), nt = int(indices.size() / 3);
        Handle(Poly_Triangulation) tri = new Poly_Triangulation(nn, nt, false);
        for (int i = 0; i < nn; ++i) tri->SetNode(i + 1, gp_Pnt(positions[size_t(i) * 3], positions[size_t(i) * 3 + 1], positions[size_t(i) * 3 + 2]));
        for (int i = 0; i < nt; ++i) {
            uint32_t a = indices[size_t(i) * 3], b = indices[size_t(i) * 3 + 1], c = indices[size_t(i) * 3 + 2];
            if (int(a) >= nn || int(b) >= nn || int(c) >= nn) return Result::fail(what + QStringLiteral(": индекс вне диапазона"));
            tri->SetTriangle(i + 1, Poly_Triangle(int(a) + 1, int(b) + 1, int(c) + 1));
        }
        TopoDS_Face f; BRep_Builder b; b.MakeFace(f, tri);
        return finish(f, what);
    PSI_OCC_CATCH(what)
}

QString Kernel::version() { return QStringLiteral("Open CASCADE ") + QStringLiteral(OCC_VERSION_COMPLETE); }

} // namespace psi::md
