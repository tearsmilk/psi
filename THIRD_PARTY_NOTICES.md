# Сторонние компоненты

| Компонент | Версия | Лицензия | Где используется |
|---|---|---|---|
| Qt 6 (Core, Gui, Widgets, PrintSupport, Svg, Test) | 6.4+ (CI: 6.7.3) | LGPL v3 / GPL v2 / GPL v3 — используется динамически, без модификаций | весь UI, рендер, PDF |
| zlib | 1.2+ | zlib License | `io/native/zip.cpp` — deflate / inflate для `.psi` |
| Шрифты системы (Source Serif 4 / Georgia / Inter / Helvetica Neue …) | — | лицензии ОС | подбираются в рантайме, в пакет не входят |

Никакие части Autodesk DXF SDK, Open CASCADE или Live2D Cubism Core не включены. Формат DXF реализован по открытому
описанию (ASCII, AC1015). Опция `PSI_WITH_OCCT` (по умолчанию выключена) зарезервирована под Open CASCADE (LGPL 2.1 с
исключением) для этапов 02+.

Ссылки: Qt — https://www.qt.io/licensing/ · zlib — https://zlib.net/zlib_license.html
