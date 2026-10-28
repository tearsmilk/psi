#pragma once
#include "modules/sketcher/sketch.h"
#include "ui/canvas.h"
#include <optional>

namespace psi::sk {

// Холст эскиза: рисование примитивов, выбор, перетаскивание с перерешением, значки ограничений и размерные
// подписи (двойной клик — изменить значение). Раскраска по степеням свободы: закреплённое — чёрное, свободное — акцент.
class SketchView : public CanvasWidget {
    Q_OBJECT
public:
    enum class Tool { Select, Point, Line, Polyline, Rectangle, Circle, Arc };
    explicit SketchView(QWidget* parent = nullptr);

    void setSketch(Sketch* s);                 // не владеет
    Sketch* sketch() const { return sketch_; }
    void setTool(Tool t);
    Tool tool() const { return tool_; }
    static QString toolName(Tool t);
    bool autoConstrain() const { return auto_; }
    void setAutoConstrain(bool on) { auto_ = on; }
    bool gridSnap() const { return gridSnap_; }
    void setGridSnap(bool on) { gridSnap_ = on; }

    const std::vector<Ref>& selection() const { return sel_; }
    std::vector<quint64> selectedEntities() const;
    void select(const std::vector<Ref>& refs);
    void clearSelection();
    void selectConstraint(quint64 id) { selCon_ = id; update(); }

    // ограничение на текущий выбор; возвращает id или 0 (с сообщением в prompt)
    quint64 addConstraint(CType t);
    void deleteSelection();
    void resolve();                            // решить, обновить DOF, перерисовать
    const SolveResult& lastResult() const { return last_; }
    QString prompt() const { return prompt_; }
    void zoomExtents();
    void escape();
    bool editConstraintValue(quint64 id);      // диалог значения; true если изменено

Q_SIGNALS:
    void aboutToChange(const QString& what);   // перед изменением — для снимка отмены
    void changed();
    void selectionChanged();
    void promptChanged(const QString& text);
    void solved();
    void toolChanged();

protected:
    void paintContent(QPainter& p) override;
    void canvasPress(QMouseEvent*) override;
    void canvasMove(QMouseEvent*) override;
    void canvasRelease(QMouseEvent*) override;
    void canvasDoubleClick(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    struct Hit { std::optional<Ref> ref; };
    Point snap(Point raw, bool* snapped = nullptr) const;
    std::optional<Ref> hitPoint(Point w) const;
    std::optional<quint64> hitEntity(Point w) const;
    std::optional<quint64> hitLabel(QPointF s) const;
    QPointF labelPos(const Constraint& c) const;
    void finishEntity(SketchEntity e);
    void setPrompt(const QString& t);
    void drawEntity(QPainter& p, const SketchEntity& e, bool selected, bool free);
    void drawConstraints(QPainter& p);
    bool isSelected(const Ref& r) const;

    Sketch* sketch_ = nullptr;
    Tool tool_ = Tool::Select;
    bool auto_ = true, gridSnap_ = true;
    std::vector<Ref> sel_;
    quint64 selCon_ = 0;
    std::vector<Point> pts_;                   // точки текущего построения
    Point cursor_; bool hasCursor_ = false, cursorSnapped_ = false;
    std::optional<Ref> drag_; bool dragging_ = false; Point dragStart_;
    std::optional<quint64> dragLabel_; Point dragLabelStart_; Point labelOrig_;
    SolveResult last_;
    QString prompt_;
};

} // namespace psi::sk
