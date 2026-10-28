#pragma once
// STL (двоичный и текстовый) через RWStl: экспорт тесселяции тел с заданным отклонением,
// импорт как непараметрического тела-сетки (показ, габариты, повторный экспорт; без булевых операций).
#include "modules/model/kernel.h"
#include <QString>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <vector>

namespace psi {

struct StlOptions {
    bool binary = true;
    double deflection = 0.1;        // мм, линейное отклонение тесселяции
    double angle = 0.3;             // рад, угловое отклонение
};

struct StlReport {
    int triangles = 0;
    int vertices = 0;
    bool binary = true;
    QString unit = QStringLiteral("мм (STL безразмерен, принято мм)");
    QStringList warnings;
    bool cancelled = false;
    QString summary() const;
};

struct StlMesh { std::vector<float> positions; std::vector<uint32_t> indices; };

struct Stl {
    static bool write(const QString& path, const std::vector<md::Shape>& bodies, const StlOptions& opt = {}, QString* error = nullptr, StlReport* report = nullptr);
    static StlMesh read(const QString& path, StlReport* report, QString* error = nullptr, const std::function<bool()>& cancelled = {});
    static md::Shape toMeshBody(const StlMesh& mesh, QString* error = nullptr);          // → Kernel::fromMesh
    static QByteArray encodeMesh(const StlMesh& mesh);                                    // для параметра MeshBody.mesh (без base64)
};

} // namespace psi
