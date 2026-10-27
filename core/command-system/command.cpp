#include "core/command-system/command.h"

namespace psi {

void EntityCommand::redo(Document& doc) {
    if (addedIds_.empty()) {
        for (Entity& e : adds_) { quint64 id = doc.addEntity(e); e.id = id; addedIds_.push_back(id); }
    } else {
        for (Entity& e : adds_) doc.addEntity(e);
    }
    for (const Entity& e : removes_) doc.removeEntity(e.id);
    for (auto& [before, after] : mods_) { after.id = before.id; doc.replaceEntity(after); }
}

void EntityCommand::undo(Document& doc) {
    for (auto it = mods_.rbegin(); it != mods_.rend(); ++it) doc.replaceEntity(it->first);
    for (auto it = removes_.rbegin(); it != removes_.rend(); ++it) doc.addEntity(*it);
    for (quint64 id : addedIds_) doc.removeEntity(id);
}

size_t EntityCommand::memory() const {
    size_t m = sizeof(*this);
    auto ent = [](const Entity& e) { return sizeof(Entity) + e.pts.size() * sizeof(Point) + static_cast<size_t>(e.text.size()) * 2; };
    for (auto& e : adds_) m += ent(e);
    for (auto& e : removes_) m += ent(e);
    for (auto& p : mods_) m += ent(p.first) + ent(p.second);
    return m;
}

void LayerCommand::redo(Document& doc) {
    switch (kind_) {
    case Add: { quint32 id = doc.addLayer(after_); after_.id = id; break; }
    case Remove: doc.removeLayer(before_.id); break;
    case Update: doc.updateLayer(after_); break;
    }
}
void LayerCommand::undo(Document& doc) {
    switch (kind_) {
    case Add: doc.removeLayer(after_.id); break;
    case Remove: doc.addLayer(before_); break;
    case Update: doc.updateLayer(before_); break;
    }
}

void SheetCommand::redo(Document& doc) {
    switch (kind_) {
    case Add: { quint64 id = doc.addSheet(after_); after_.id = id; break; }
    case Remove: doc.removeSheet(before_.id); break;
    case Update: doc.updateSheet(after_); break;
    }
}
void SheetCommand::undo(Document& doc) {
    switch (kind_) {
    case Add: doc.removeSheet(after_.id); break;
    case Remove: doc.addSheet(before_); break;
    case Update: doc.updateSheet(before_); break;
    }
}

void CommandStack::push(std::unique_ptr<Command> c) {
    c->redo(doc_);
    if (macro_) { macro_->append(std::move(c)); return; }
    redo_.clear();
    if (!undo_.empty() && undo_.back()->mergeWith(*c)) { Q_EMIT changed(); return; }
    undo_.push_back(std::move(c));
    trim();
    Q_EMIT changed();
}

void CommandStack::undo() {
    if (macro_ || undo_.empty()) return;
    auto c = std::move(undo_.back());
    undo_.pop_back();
    c->undo(doc_);
    redo_.push_back(std::move(c));
    Q_EMIT changed();
}

void CommandStack::redo() {
    if (macro_ || redo_.empty()) return;
    auto c = std::move(redo_.back());
    redo_.pop_back();
    c->redo(doc_);
    undo_.push_back(std::move(c));
    Q_EMIT changed();
}

void CommandStack::clear() { undo_.clear(); redo_.clear(); macro_.reset(); cleanIndex_ = 0; Q_EMIT changed(); }

void CommandStack::beginMacro(const QString& text) {
    if (macro_) return;
    macro_ = std::make_unique<MacroCommand>(text);
}

void CommandStack::endMacro() {
    if (!macro_) return;
    auto m = std::move(macro_);
    if (m->empty()) return;
    redo_.clear();
    undo_.push_back(std::move(m));
    trim();
    Q_EMIT changed();
}

void CommandStack::trim() {
    size_t mem = 0;
    for (auto& c : undo_) mem += c->memory();
    while (undo_.size() > limit_ || (mem > memLimit_ && undo_.size() > 1)) {
        mem -= undo_.front()->memory();
        undo_.pop_front();
        if (cleanIndex_ > 0) --cleanIndex_; else cleanIndex_ = -1;
    }
}

} // namespace psi
