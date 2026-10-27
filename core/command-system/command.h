#pragma once
#include "core/document/document.h"
#include <QObject>
#include <QString>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

namespace psi {

class Command {
public:
    virtual ~Command() = default;
    virtual void redo(Document& doc) = 0;
    virtual void undo(Document& doc) = 0;
    virtual bool mergeWith(const Command&) { return false; }
    virtual size_t memory() const { return sizeof(*this); }
    QString text;
};

// Generic entity edit: adds, removes and modifications in one undoable step.
class EntityCommand : public Command {
public:
    explicit EntityCommand(QString t) { text = std::move(t); }
    void add(Entity e) { adds_.push_back(std::move(e)); }
    void remove(const Entity& e) { removes_.push_back(e); }
    void modify(const Entity& before, Entity after) { mods_.push_back({before, std::move(after)}); }
    bool empty() const { return adds_.empty() && removes_.empty() && mods_.empty(); }
    const std::vector<quint64>& addedIds() const { return addedIds_; }
    void redo(Document& doc) override;
    void undo(Document& doc) override;
    size_t memory() const override;

private:
    std::vector<Entity> adds_, removes_;
    std::vector<std::pair<Entity, Entity>> mods_;
    std::vector<quint64> addedIds_;
};

class LayerCommand : public Command {
public:
    enum Kind { Add, Remove, Update };
    LayerCommand(Kind k, Layer before, Layer after, QString t) : kind_(k), before_(std::move(before)), after_(std::move(after)) { text = std::move(t); }
    void redo(Document& doc) override;
    void undo(Document& doc) override;
    quint32 layerId() const { return after_.id; }
private:
    Kind kind_;
    Layer before_, after_;
};

class SheetCommand : public Command {
public:
    enum Kind { Add, Remove, Update };
    SheetCommand(Kind k, Sheet before, Sheet after, QString t) : kind_(k), before_(std::move(before)), after_(std::move(after)) { text = std::move(t); }
    void redo(Document& doc) override;
    void undo(Document& doc) override;
    quint64 sheetId() const { return after_.id; }
private:
    Kind kind_;
    Sheet before_, after_;
};

class SettingsCommand : public Command {
public:
    SettingsCommand(DocumentSettings before, DocumentSettings after, QString t) : before_(std::move(before)), after_(std::move(after)) { text = std::move(t); }
    void redo(Document& doc) override { doc.setSettings(after_); }
    void undo(Document& doc) override { doc.setSettings(before_); }
private:
    DocumentSettings before_, after_;
};

class MacroCommand : public Command {
public:
    explicit MacroCommand(QString t) { text = std::move(t); }
    void append(std::unique_ptr<Command> c) { cmds_.push_back(std::move(c)); }
    bool empty() const { return cmds_.empty(); }
    void redo(Document& doc) override { for (auto& c : cmds_) c->redo(doc); }
    void undo(Document& doc) override { for (auto it = cmds_.rbegin(); it != cmds_.rend(); ++it) (*it)->undo(doc); }
    size_t memory() const override { size_t m = sizeof(*this); for (auto& c : cmds_) m += c->memory(); return m; }
private:
    std::vector<std::unique_ptr<Command>> cmds_;
};

class CommandStack : public QObject {
    Q_OBJECT
public:
    explicit CommandStack(Document& doc, QObject* parent = nullptr) : QObject(parent), doc_(doc) {}
    void push(std::unique_ptr<Command> c);          // executes redo() and records it
    void undo();
    void redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    QString undoText() const { return undo_.empty() ? QString() : undo_.back()->text; }
    QString redoText() const { return redo_.empty() ? QString() : redo_.back()->text; }
    void clear();
    void beginMacro(const QString& text);
    void endMacro();
    bool inMacro() const { return macro_ != nullptr; }
    void setLimit(size_t count, size_t bytes) { limit_ = count; memLimit_ = bytes; trim(); }
    size_t count() const { return undo_.size(); }
    int cleanIndex() const { return cleanIndex_; }
    void setClean() { cleanIndex_ = static_cast<int>(undo_.size()); Q_EMIT changed(); }
    bool isClean() const { return cleanIndex_ == static_cast<int>(undo_.size()); }

Q_SIGNALS:
    void changed();

private:
    void trim();
    Document& doc_;
    std::deque<std::unique_ptr<Command>> undo_, redo_;
    std::unique_ptr<MacroCommand> macro_;
    size_t limit_ = 200, memLimit_ = 256u * 1024 * 1024;
    int cleanIndex_ = 0;
};

} // namespace psi
