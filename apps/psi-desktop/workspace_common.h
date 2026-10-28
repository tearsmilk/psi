#pragma once
#include <QByteArray>
#include <QString>
#include <vector>

class QWidget;
class QLabel;
class QVBoxLayout;

namespace psi {

// Отмена «снимками»: рабочие пространства «Эскиз» и «Интерьер» держат модель вне Document,
// поэтому каждое изменение запоминается как JSON-снимок всей модели (они небольшие).
class SnapshotStack {
public:
    void push(const QString& text, QByteArray before) { undo_.push_back({text, std::move(before)}); redo_.clear(); if (undo_.size() > 200) undo_.erase(undo_.begin()); }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    QString undoText() const { return undo_.empty() ? QString() : undo_.back().text; }
    QString redoText() const { return redo_.empty() ? QString() : redo_.back().text; }
    // возвращает снимок для восстановления; current — состояние до отмены (уходит в redo)
    QByteArray undo(const QByteArray& current) { auto e = undo_.back(); undo_.pop_back(); redo_.push_back({e.text, current}); return e.data; }
    QByteArray redo(const QByteArray& current) { auto e = redo_.back(); redo_.pop_back(); undo_.push_back({e.text, current}); return e.data; }
    void clear() { undo_.clear(); redo_.clear(); }
private:
    struct Entry { QString text; QByteArray data; };
    std::vector<Entry> undo_, redo_;
};

// Секция боковой панели: заголовок H2 + содержимое (аналог дока внутри страницы).
QWidget* makeSection(const QString& title, QWidget* content, QWidget* parent = nullptr);
QWidget* makeSideColumn(const std::vector<QWidget*>& sections, int width);

} // namespace psi
