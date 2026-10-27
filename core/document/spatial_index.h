#pragma once
#include "core/geometry/point.h"
#include <QHash>
#include <QtGlobal>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace psi {

// Uniform grid over the model plane; entities whose bbox spans too many cells go to the «big» bucket.
class SpatialIndex {
public:
    explicit SpatialIndex(double cell = 100.0) : cell_(cell) {}
    void insert(quint64 id, const Rect& r);
    void remove(quint64 id);
    void update(quint64 id, const Rect& r) { remove(id); insert(id, r); }
    void clear();
    std::vector<quint64> query(const Rect& r) const;
    std::vector<quint64> all() const;
    size_t size() const { return boxes_.size(); }
    Rect bbox(quint64 id) const { auto it = boxes_.find(id); return it == boxes_.end() ? Rect{} : it->second; }

private:
    static quint64 key(int ix, int iy) { return (static_cast<quint64>(static_cast<quint32>(ix)) << 32) | static_cast<quint32>(iy); }
    void cells(const Rect& r, int& x0, int& y0, int& x1, int& y1) const;

    double cell_;
    QHash<quint64, std::vector<quint64>> grid_;
    std::unordered_set<quint64> big_;
    std::unordered_map<quint64, Rect> boxes_;
};

} // namespace psi
