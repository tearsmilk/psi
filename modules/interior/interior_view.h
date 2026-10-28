#pragma once
#include "modules/interior/building.h"
#include "ui/canvas.h"
#include <optional>

namespace psi::in {

// План этажа: стены со штриховкой, проёмы (дверь с дугой открывания, окно тройной линией), помещения с площадью,
// мебель условными знаками. Инструменты: стена, дверь, окно, помещение, мебель, выбор/перенос, измерение.
class InteriorView : public CanvasWidget {
    Q_OBJECT
public:
    enum class Tool { Select, Wall, Door, Window, Room, Furniture, Measure };
    struct Selection {
        enum Kind { None, Wall, Opening, Furniture, Room } kind = None;
        quint64 id = 0;
        QString roomKey;
        bool operator==(const Selection& o) const { return kind == o.kind && id == o.id && roomKey == o.roomKey; }
    };
    explicit InteriorView(QWidget* parent = nullptr);

    void setBuilding(Building* b);             // не владеет
    Building* building() const { return building_; }
    void setFloor(size_t index);
    size_t floorIndex() const { return floor_; }
    Floor* floor() const;
    void setTool(Tool t);
    Tool tool() const { return tool_; }
    static QString toolName(Tool t);

    // параметры новых объектов
    double wallThickness = 200, wallHeight = 2700;
    QString wallMaterial = QStringLiteral("Кирпич");
    double openingWidth = 900, openingHeight = 2100, windowSill = 900;
    void setPlacing(const Furniture& f) { placing_ = f; setTool(Tool::Furniture); }
    const Furniture& placing() const { return placing_; }

    const Selection& selection() const { return sel_; }
    void select(const Selection& s);
    void clearSelection() { select({}); }
    void deleteSelection();
    void rotateSelection(double deg = 90);
    void refresh();                            // пересчитать помещения и коллизии, перерисовать
    const std::vector<Room>& rooms() const { return rooms_; }
    const std::vector<Collision>& collisions() const { return collisions_; }
    QString prompt() const { return prompt_; }
    void zoomExtents();
    void escape();

Q_SIGNALS:
    void aboutToChange(const QString& what);
    void changed();
    void selectionChanged();
    void promptChanged(const QString& text);
    void toolChanged();
    void floorChanged();

protected:
    void paintContent(QPainter& p) override;
    void canvasPress(QMouseEvent*) override;
    void canvasMove(QMouseEvent*) override;
    void canvasRelease(QMouseEvent*) override;
    void canvasDoubleClick(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    Point snap(Point raw, bool* snapped = nullptr) const;
    Selection hit(Point w) const;
    const Room* roomAt(Point w) const;
    const Wall* nearestWall(Point w, double* offset, double maxDist) const;
    void finishWall();
    void setPrompt(const QString& t);
    void drawWalls(QPainter& p);
    void drawOpening(QPainter& p, const Opening& o, bool selected, bool preview);
    void drawFurniture(QPainter& p, const Furniture& m, bool selected, bool preview, bool collides);
    void drawRooms(QPainter& p);
    QPolygonF poly(const std::vector<Point>& pts) const;

    Building* building_ = nullptr;
    size_t floor_ = 0;
    Tool tool_ = Tool::Select;
    Furniture placing_;
    Selection sel_;
    std::vector<Point> pts_;                   // стена / измерение
    Point cursor_; bool hasCursor_ = false, cursorSnapped_ = false;
    bool dragging_ = false, dragArmed_ = false; Point dragStart_, dragLast_;
    std::vector<Room> rooms_;
    std::vector<Collision> collisions_;
    QString prompt_;
};

} // namespace psi::in
