#pragma once
#include <QColor>
#include <QString>

class QApplication;

namespace psi {

enum class ThemeMode { System, Light, Dark };

struct Palette {
    QColor bg, panel, card, border, text, muted, faint, accent, onAccent, selected, hover, input, canvas, grid, gridMajor, crosshair;
};

struct Theme {
    static Palette palette(bool dark, const QColor& accent);
    static void apply(QApplication& app, ThemeMode mode, const QColor& accent);
    static const Palette& current();
    static bool isDark();
    static QString serifFamily();
    static QString sansFamily();
    static QColor defaultAccent() { return QColor(0xD9, 0x77, 0x57); }
};

} // namespace psi
