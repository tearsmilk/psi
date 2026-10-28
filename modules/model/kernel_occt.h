#pragma once
// Внутренний заголовок psi_model: pimpl тела для файлов, которым нужен TopoDS_Shape (kernel.cpp, io/step, io/stl).
// Не подключать вне psi_model.
#include "modules/model/kernel.h"
#include <TopoDS_Shape.hxx>

namespace psi::md {
struct Shape::Impl { TopoDS_Shape shape; };
inline const TopoDS_Shape& occShape(const Shape& s) { return s.impl().shape; }
inline Shape fromOcc(const TopoDS_Shape& s) { Shape r; r.impl().shape = s; return r; }
} // namespace psi::md
