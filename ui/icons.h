#pragma once
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace psi {

// Line icons drawn with QPainter (no bitmap assets): tool, file and view glyphs.
struct Icons {
    static QIcon icon(const QString& name, const QColor& color, int size = 20);
    static QPixmap pixmap(const QString& name, const QColor& color, int size, qreal dpr = 1.0);
    static QPixmap logo(int size, const QColor& color, const QColor& bg);   // Ψ in a rounded square
    static QStringList names();
};

} // namespace psi
