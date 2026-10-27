# Архитектура Пси (этап 00–01)

```
apps/psi-desktop ──► psi_ui (rendering, modules/draft, io/pdf, ui) ──► psi_core (core, modules/drawing, io/native, io/dxf)
                                                                           │
                                                                           └── только Qt Core + zlib: без GUI, тестируется headless
```

## Слои

- **core/geometry** — `Point`/`Vec`, `Rect`, аффинный `Transform`, единая структура `Entity` с `EntityType`
  (Line, Polyline с bulge, Circle, Arc, Ellipse, Spline (Catmull-Rom), Hatch, Text, Leader, Dimension). Все операции —
  свободные функции: `flatten`, `segments`, `hitDistance`, `snapPoints`, `intersections`, `offsetEntity`, `trimEntity`,
  `extendEntity`, `filletLines`, `chamferLines`, `explode`, `dimensionValue` / `dimensionGeometry` (ассоциативные размеры
  через `refs`), JSON-сериализация.
- **core/units** — модель в мм; `Units::format/parse` для мм/см/м/дюймов, дробей, футов.
- **core/attributes** — типизированные атрибуты (`AttrKind`), хранятся на объектах и в основной надписи.
- **core/document** — `Document`: объекты в `unordered_map` + порядок вставки, `SpatialIndex` (равномерная сетка 100 мм,
  «большие» объекты отдельно), слои, листы, настройки; сигналы об изменениях с id.
- **core/command-system** — `CommandStack` с `EntityCommand` (add/remove/modify, стабильные id при redo), команды слоёв,
  листов, настроек, макросы, лимиты по числу и памяти, индекс «чистого» состояния.
- **core/project** — `Project`: документ + стек + метаданные + «чужие» части архива; автосохранение по таймеру, восстановление,
  недавние файлы (QSettings).
- **modules/drawing** — листы: форматы A0–A6, масштабы, `SheetViewport` (окно модели ↔ рамка на листе), `TitleBlock`.
- **modules/draft** — `Tool` (конечный автомат: `click/move/enter/escape/input/draw`), 30 инструментов, `SnapEngine`
  (конец, середина, центр, квадрант, пересечение, перпендикуляр, касательная, сетка; орто/полярное), `InputParser`
  (AutoCAD-подобный ввод), `Demo` (сценарий §22).
- **rendering** — `Renderer` (QPainter, косметические перья по толщине в мм × px/mm, штриховка, текст без зеркалирования,
  листы с рамкой и основной надписью), `Viewport` (кеш статического слоя в QPixmap, пакетирование линий по перу,
  оверлей: выделение с ручками, призрак инструмента, рамка выбора, маркер привязки, перекрестие; пан/зум; выбор рамкой
  внутрь / пересечением).
- **io/native** — `.psi` (см. formats/psi.md). **io/dxf** — ASCII DXF AC1015 (см. formats/dxf.md). **io/pdf** —
  `QPdfWriter` 1200 dpi, страница точного размера, MediaBox переписывается в точные пункты.
- **ui** — `Theme` (палитра светлая/тёмная, акцент, QSS), `Icons` (все глифы рисуются QPainter'ом, без ресурсов).
- **apps/psi-desktop** — `MainWindow` (меню, панель рабочих областей, доки Инструменты / Дерево / Свойства / Слои,
  командная строка, строка состояния, горячие клавиши из QSettings, drag&drop), `SheetsPage`, диалоги, `WelcomePage`,
  `--selftest`.

## Потоки

Разбор DXF выполняется в рабочем `QThread` с `QProgressDialog` и отменой; результат применяется в главном потоке одной
командой (отменяемо). PDF-экспорт идёт в главном потоке с прогрессом и `processEvents` (QPainter на QPdfWriter).

## Производительность

- 50 000 линий: полный рендер < 300 мс на 2015 MacBook (в тестах бюджет 1500 мс для CI), окно-запросы к индексу ~мкс.
- Перерисовка при наведении/выделении не трогает кеш: только оверлей.
- `EntityCommand` хранит копии объектов «до/после»; стек ограничен 200 командами / 256 МБ.

## Что дальше (этапы 02–06)

Параметрический эскиз, 3D на OCCT (`PSI_WITH_OCCT`), сборки, интерьер, чертежи с модели. Рабочие области уже заведены
в панели и отключены.
