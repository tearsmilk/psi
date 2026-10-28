#include "io/stl/stl.h"
#include "modules/model/kernel_occt.h"

#include <OSD_Path.hxx>
#include <Poly_Triangulation.hxx>
#include <RWStl.hxx>
#include <Standard_Failure.hxx>

#include <QFile>
#include <QFileInfo>
#include <QtEndian>
#include <cstring>

namespace psi {

QString StlReport::summary() const {
    QStringList l;
    l << QStringLiteral("Треугольников: %1, вершин: %2, формат: %3").arg(triangles).arg(vertices).arg(binary ? QStringLiteral("двоичный") : QStringLiteral("текстовый"));
    l << QStringLiteral("Единицы: %1").arg(unit);
    for (const QString& w : warnings) l << QStringLiteral("⚠ %1").arg(w);
    if (cancelled) l << QStringLiteral("Операция отменена");
    return l.join(QLatin1Char('\n'));
}

bool Stl::write(const QString& path, const std::vector<md::Shape>& bodies, const StlOptions& opt, QString* error, StlReport* report) {
    StlReport rep; rep.binary = opt.binary;
    std::vector<float> pos; std::vector<uint32_t> idx;
    for (const md::Shape& s : bodies) {
        if (s.isNull()) continue;
        md::Mesh m = s.mesh(opt.deflection, opt.angle);
        const uint32_t base = uint32_t(pos.size() / 3);
        pos.insert(pos.end(), m.positions.begin(), m.positions.end());
        for (uint32_t i : m.indices) idx.push_back(base + i);
    }
    if (idx.empty()) { if (error) *error = QStringLiteral("Нет треугольников для записи в STL"); if (report) *report = rep; return false; }
    try {
        const int nn = int(pos.size() / 3), nt = int(idx.size() / 3);
        Handle(Poly_Triangulation) tri = new Poly_Triangulation(nn, nt, false);
        for (int i = 0; i < nn; ++i) tri->SetNode(i + 1, gp_Pnt(pos[size_t(i) * 3], pos[size_t(i) * 3 + 1], pos[size_t(i) * 3 + 2]));
        for (int i = 0; i < nt; ++i) tri->SetTriangle(i + 1, Poly_Triangle(int(idx[size_t(i) * 3]) + 1, int(idx[size_t(i) * 3 + 1]) + 1, int(idx[size_t(i) * 3 + 2]) + 1));
        OSD_Path osd(path.toUtf8().constData());
        bool ok = opt.binary ? RWStl::WriteBinary(tri, osd) : RWStl::WriteAscii(tri, osd);
        if (!ok) { if (error) *error = QStringLiteral("Не удалось записать %1").arg(path); if (report) *report = rep; return false; }
        rep.triangles = nt; rep.vertices = nn;
    } catch (const Standard_Failure& f) { if (error) *error = QStringLiteral("STL: %1").arg(QString::fromUtf8(f.GetMessageString())); if (report) *report = rep; return false; }
    if (report) *report = rep;
    return true;
}

StlMesh Stl::read(const QString& path, StlReport* report, QString* error, const std::function<bool()>& cancelled) {
    StlMesh out; StlReport rep;
    if (!QFileInfo::exists(path)) { if (error) *error = QStringLiteral("Файл не найден: %1").arg(path); if (report) *report = rep; return out; }
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) { QByteArray head = f.read(80); rep.binary = !head.trimmed().startsWith("solid") || f.size() < 84 ? true : false; }
    }
    if (cancelled && cancelled()) { rep.cancelled = true; if (report) *report = rep; return out; }
    try {
        Handle(Poly_Triangulation) tri = RWStl::ReadFile(path.toUtf8().constData());
        if (tri.IsNull() || tri->NbTriangles() == 0) { if (error) *error = QStringLiteral("STL пуст или повреждён"); if (report) *report = rep; return out; }
        out.positions.reserve(size_t(tri->NbNodes()) * 3);
        for (int i = 1; i <= tri->NbNodes(); ++i) { gp_Pnt p = tri->Node(i); out.positions.insert(out.positions.end(), {float(p.X()), float(p.Y()), float(p.Z())}); }
        for (int i = 1; i <= tri->NbTriangles(); ++i) { int a, b, c; tri->Triangle(i).Get(a, b, c); out.indices.insert(out.indices.end(), {uint32_t(a - 1), uint32_t(b - 1), uint32_t(c - 1)}); }
        rep.triangles = tri->NbTriangles(); rep.vertices = tri->NbNodes();
        rep.warnings << QStringLiteral("Импортировано как тело-сетка: без параметров и булевых операций");
    } catch (const Standard_Failure& f) { if (error) *error = QStringLiteral("STL: %1").arg(QString::fromUtf8(f.GetMessageString())); }
    if (report) *report = rep;
    return out;
}

md::Shape Stl::toMeshBody(const StlMesh& mesh, QString* error) {
    md::Result r = md::Kernel::fromMesh(mesh.positions, mesh.indices);
    if (!r.ok() && error) *error = r.error;
    return r.shape;
}

QByteArray Stl::encodeMesh(const StlMesh& mesh) {
    const quint32 n = quint32(mesh.positions.size() / 3), t = quint32(mesh.indices.size() / 3);
    QByteArray out(int(8 + n * 12 + t * 12), Qt::Uninitialized);
    uchar* p = reinterpret_cast<uchar*>(out.data());
    qToLittleEndian<quint32>(n, p); qToLittleEndian<quint32>(t, p + 4);
    for (quint32 i = 0; i < n * 3; ++i) { quint32 u; memcpy(&u, &mesh.positions[i], 4); qToLittleEndian<quint32>(u, p + 8 + i * 4); }
    for (quint32 i = 0; i < t * 3; ++i) qToLittleEndian<quint32>(mesh.indices[i], p + 8 + n * 12 + i * 4);
    return out;
}

} // namespace psi
