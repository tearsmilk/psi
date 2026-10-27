# Формат `.psi` (схема 1)

ZIP-архив (deflate, zlib) с JSON внутри. Открывается любым архиватором, диффуется, расширяем.

```
manifest.json            { "format": "psi", "schema": 1, "app": "0.1.0", "parts": { "<путь>": "sha256-hex", … } }
project/metadata.json    { "name", "author", "description", "created", "modified", "appVersion" }
project/settings.json    DocumentSettings: unit, precision, grid, snap flags, ortho, polar, textHeight, tolerance, author
drawings/main.json       { "layers": [...], "current": <id>, "entities": [...], "nextId": N }
sheets/sheets.json       { "sheets": [ Sheet… ] }
<прочее>                 части неизвестных модулей сохраняются как есть («foreign parts»)
```

## Объект (`entities[]`)

```json
{"id": 12, "t": "line", "layer": 3, "color": -1, "lw": -1, "lt": "bylayer", "pts": [[0,0],[100,0]], "attrs": {"material": {"k": "material", "v": "Сталь 45"}}}
```

`t`: `line`, `polyline` (+`bulges`, `closed`), `circle` (`c`, `r`), `arc` (`c`, `r`, `a0`, `a1` в радианах), `ellipse`
(`c`, `rx`, `ry`, `rot`), `spline` (`pts`, `closed`), `hatch` (`pts`, `pattern`, `angle`, `spacing`), `text` (`pts[0]`,
`text`, `h`, `angle`), `leader` (`pts`, `text`, `h`), `dimension` (`kind`: aligned / horizontal / vertical / radius /
diameter / angular, `pts`, `refs` — id измеряемых объектов, `text` — переопределение). Цвет `-1` и толщина `-1` — «по слою».
Все координаты — миллиметры.

## Целостность и ограничения

- Каждая часть из `parts` проверяется по sha256; несовпадение — ошибка открытия с указанием части.
- Лимиты распаковки: часть ≤ 64 МБ, суммарно ≤ 512 МБ, ≤ 10 000 записей.
- `schema` больше поддерживаемой → отказ с понятным сообщением; меньше → миграция (`PsiFile::migrate`).
- Запись через `QSaveFile` (атомарно), с резервной копией `.bak` при перезаписи.

## Автосохранение

`<путь>.autosave.psi` рядом с проектом (несохранённый проект — в каталоге данных приложения). При открытии проекта, у
которого автосохранение новее файла, предлагается восстановление.
