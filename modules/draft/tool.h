#pragma once
#include "core/command-system/command.h"
#include "core/project/project.h"
#include "rendering/renderer.h"
#include <QSet>
#include <QString>
#include <functional>
#include <memory>
#include <optional>

namespace psi {

class Viewport;

struct ToolContext {
    Project* project = nullptr;
    Viewport* view = nullptr;
    QSet<quint64>* selection = nullptr;
    std::function<void(const QString&)> setPrompt;                                        // «Укажите вторую точку:»
    std::function<void(const QString&)> message;                                          // status message
    std::function<std::optional<QString>(const QString&, const QString&, const QString&)> askText;   // dialog: title, label, default
    std::function<void()> finished;                                                       // tool wants to end → back to Select

    Document& doc() const { return project->document(); }
    void commit(std::unique_ptr<Command> c) const { project->commands().push(std::move(c)); }
    Entity styled(Entity e) const;                                                        // current layer / text height
};

// A tool is a small state machine fed by the viewport: snapped points, Enter/Esc, typed text.
class Tool {
public:
    explicit Tool(ToolContext ctx) : ctx_(std::move(ctx)) {}
    virtual ~Tool() = default;
    virtual QString id() const = 0;
    virtual QString title() const = 0;
    virtual void begin() {}
    virtual void click(Point p, bool shift) = 0;
    virtual void move(Point p) { cursor_ = p; }
    virtual void enter() {}                      // Enter / right click: finish the current step
    virtual void escape() { cancel(); }
    virtual bool input(const QString& text);     // typed in the command bar; default: parse a point and click
    virtual void draw(QPainter& p, const RenderOptions& o) { (void)p; (void)o; }
    virtual std::optional<Point> lastPoint() const { return std::nullopt; }
    virtual bool selecting() const { return false; }   // the viewport does selection; Enter → selectionDone()
    virtual void selectionDone() {}
    virtual void cancel() { if (ctx_.finished) ctx_.finished(); }
    virtual bool keepAfterFinish() const { return true; }  // draw tools restart; modify tools end
    QString prompt() const { return prompt_; }

protected:
    void setPrompt(const QString& s) { prompt_ = s; if (ctx_.setPrompt) ctx_.setPrompt(s); }
    void finish() { if (ctx_.finished) ctx_.finished(); }
    void ghost(QPainter& p, const RenderOptions& o, const Entity& e) const;   // preview in accent colour
    ToolContext ctx_;
    QString prompt_;
    std::optional<Point> cursor_;
};

std::unique_ptr<Tool> createTool(const QString& id, const ToolContext& ctx);
QStringList toolIds();
QString toolTitle(const QString& id);

} // namespace psi
