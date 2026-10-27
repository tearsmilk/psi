#include "core/document/spatial_index.h"
#include <algorithm>
#include <cmath>

namespace psi {

static constexpr int kMaxCells = 4096;

void SpatialIndex::cells(const Rect& r, int& x0, int& y0, int& x1, int& y1) const {
    x0 = static_cast<int>(std::floor(r.x0 / cell_)); y0 = static_cast<int>(std::floor(r.y0 / cell_));
    x1 = static_cast<int>(std::floor(r.x1 / cell_)); y1 = static_cast<int>(std::floor(r.y1 / cell_));
}

void SpatialIndex::insert(quint64 id, const Rect& r) {
    if (!r.valid() || !std::isfinite(r.x0) || !std::isfinite(r.x1) || !std::isfinite(r.y0) || !std::isfinite(r.y1)) {
        big_.insert(id); boxes_[id] = r; return;
    }
    boxes_[id] = r;
    int x0, y0, x1, y1;
    cells(r, x0, y0, x1, y1);
    long long n = static_cast<long long>(x1 - x0 + 1) * (y1 - y0 + 1);
    if (n > kMaxCells) { big_.insert(id); return; }
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y) grid_[key(x, y)].push_back(id);
}

void SpatialIndex::remove(quint64 id) {
    auto it = boxes_.find(id);
    if (it == boxes_.end()) return;
    if (big_.erase(id)) { boxes_.erase(it); return; }
    const Rect r = it->second;
    if (r.valid()) {
        int x0, y0, x1, y1;
        cells(r, x0, y0, x1, y1);
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y) {
                auto g = grid_.find(key(x, y));
                if (g == grid_.end()) continue;
                auto& v = g.value();
                v.erase(std::remove(v.begin(), v.end(), id), v.end());
                if (v.empty()) grid_.erase(g);
            }
    }
    boxes_.erase(it);
}

void SpatialIndex::clear() { grid_.clear(); big_.clear(); boxes_.clear(); }

std::vector<quint64> SpatialIndex::query(const Rect& r) const {
    std::vector<quint64> out;
    if (!r.valid()) return out;
    std::unordered_set<quint64> seen;
    int x0, y0, x1, y1;
    cells(r, x0, y0, x1, y1);
    long long n = static_cast<long long>(x1 - x0 + 1) * (y1 - y0 + 1);
    if (n > 65536) {   // huge query: scan everything instead of millions of cells
        for (const auto& [id, b] : boxes_) if (b.intersects(r)) out.push_back(id);
        return out;
    }
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y) {
            auto g = grid_.constFind(key(x, y));
            if (g == grid_.constEnd()) continue;
            for (quint64 id : g.value())
                if (seen.insert(id).second && boxes_.at(id).intersects(r)) out.push_back(id);
        }
    for (quint64 id : big_) { auto b = boxes_.find(id); if (b != boxes_.end() && (!b->second.valid() || b->second.intersects(r))) out.push_back(id); }
    return out;
}

std::vector<quint64> SpatialIndex::all() const {
    std::vector<quint64> out;
    out.reserve(boxes_.size());
    for (const auto& [id, b] : boxes_) out.push_back(id);
    return out;
}

} // namespace psi
