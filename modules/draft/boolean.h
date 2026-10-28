#pragma once
// 2D-булевы операции над замкнутыми контурами через QPainterPath (объединение / вычитание / пересечение).
#include "core/geometry/entity.h"
#include <vector>

namespace psi {

enum class BoolOp { Union, Subtract, Intersect };

// Все `shapes` должны быть замкнутыми (isClosedShape). Результат — замкнутые полилинии (сплющенные с
// допуском `tol`, коллинеарные точки убраны) со стилем и слоем первого объекта. Для Subtract из первого
// вычитаются все остальные. Пусто, если результат пуст или входы не замкнуты.
std::vector<Entity> booleanShapes(const std::vector<Entity>& shapes, BoolOp op, double tol = 0.05);

} // namespace psi
