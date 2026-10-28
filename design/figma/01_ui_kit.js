// Пси (PSI) — UI Kit page (Figma Plugin API, run via use_figma in the file «Пси — UI», team Lotus). Re-runnable: rebuilds the page.
let page = figma.root.children.find(p => p.name === "UI Kit");
if (!page) { page = figma.createPage(); page.name = "UI Kit"; }
await figma.setCurrentPageAsync(page);
for (const c of await figma.variables.getLocalVariableCollectionsAsync()) if (c.name.startsWith("Psi")) c.remove();
for (const n of [...page.children]) n.remove();

const fonts = await figma.listAvailableFontsAsync();
const has = (fam, style) => fonts.some(f => f.fontName.family === fam && f.fontName.style === style);
const SANS = "Inter";
const SERIF = ["Source Serif 4", "Lora", "Merriweather", "Playfair Display"].find(f => has(f, "Regular")) || "Inter";
const SERIF_I = has(SERIF, "Italic") ? "Italic" : "Regular";
const MONO = ["JetBrains Mono", "Roboto Mono", "Source Code Pro"].find(f => has(f, "Regular")) || "Inter";
for (const fn of [{family: SANS, style: "Regular"}, {family: SANS, style: "Medium"}, {family: SANS, style: "Semi Bold"},
                  {family: SERIF, style: "Regular"}, {family: SERIF, style: SERIF_I}, {family: MONO, style: "Regular"}]) await figma.loadFontAsync(fn);

const hex = h => { h = h.replace("#", ""); return {r: parseInt(h.slice(0,2),16)/255, g: parseInt(h.slice(2,4),16)/255, b: parseInt(h.slice(4,6),16)/255}; };
// mirrors ui/theme.cpp Theme::palette()
const T = {
  "bg": ["#FAF9F5", "#262624"], "panel": ["#F0EEE6", "#1F1E1D"], "card": ["#FFFFFF", "#30302E"], "border": ["#E3DFD3", "#3E3D39"],
  "text": ["#141413", "#F4F2EC"], "muted": ["#6F6E69", "#A6A39A"], "faint": ["#A8A59C", "#6E6C66"], "accent": ["#D97757", "#D97757"],
  "on-accent": ["#FFFFFF", "#FFFFFF"], "selected": ["#E6E2D6", "#3A3936"], "hover": ["#E9E6DC", "#34332F"], "input": ["#FFFFFF", "#262624"],
  "canvas": ["#FDFCF9", "#1C1C1A"], "grid": ["#EEEBE2", "#262624"], "grid-major": ["#DDD9CC", "#33322F"], "crosshair": ["#8A8780", "#8A8780"],
  "ink": ["#141413", "#F4F2EC"], "paper": ["#FFFFFF", "#FFFFFF"], "white": ["#FFFFFF", "#FFFFFF"], "snap": ["#3B6FB6", "#7FA7E0"],
};
const V = {Light: {}, Dark: {}};
const colIds = {};
for (const [mi, modeName] of ["Light", "Dark"].entries()) {
  const col = figma.variables.createVariableCollection("Psi — " + modeName);  // Starter plan: 1 mode per collection
  col.renameMode(col.modes[0].modeId, modeName);
  colIds[modeName] = col.id;
  for (const [name, vals] of Object.entries(T)) {
    const v = figma.variables.createVariable("color/" + name, col, "COLOR");
    v.setValueForMode(col.modes[0].modeId, hex(vals[mi]));
    V[modeName][name] = v;
  }
}
let MODE = "Light";
const fill = (name, opacity) => { const p = figma.variables.setBoundVariableForPaint({type: "SOLID", color: {r: 0, g: 0, b: 0}}, "color", V[MODE][name]); if (opacity !== undefined) p.opacity = opacity; return p; };
function txt(chars, o = {}) {
  const t = figma.createText();
  t.fontName = {family: o.family || SANS, style: o.style || "Regular"};
  t.fontSize = o.size || 13; t.characters = chars; t.fills = [fill(o.color || "text")];
  if (o.w) { t.textAutoResize = "HEIGHT"; t.resize(o.w, t.height); }
  return t;
}
function al(dir, o = {}) {
  const f = figma.createAutoLayout(dir, {itemSpacing: o.gap ?? 8});
  f.name = o.name || "Frame";
  f.fills = o.fill ? [fill(o.fill, o.opacity)] : [];
  if (o.pad !== undefined) { const [t, r, b, l] = Array.isArray(o.pad) ? o.pad : [o.pad, o.pad, o.pad, o.pad]; f.paddingTop = t; f.paddingRight = r; f.paddingBottom = b; f.paddingLeft = l; }
  if (o.radius) f.cornerRadius = o.radius;
  if (o.stroke) { f.strokes = [fill(o.stroke)]; f.strokeWeight = 1; }
  if (o.align) f.counterAxisAlignItems = o.align;
  return f;
}
function comp(name, dir, o = {}) {
  const c = figma.createComponent(); c.name = name; c.layoutMode = dir;
  c.primaryAxisSizingMode = "AUTO"; c.counterAxisSizingMode = "AUTO"; c.itemSpacing = o.gap ?? 8;
  const [t, r, b, l] = Array.isArray(o.pad) ? o.pad : [o.pad ?? 0, o.pad ?? 0, o.pad ?? 0, o.pad ?? 0];
  c.paddingTop = t; c.paddingRight = r; c.paddingBottom = b; c.paddingLeft = l;
  c.fills = o.fill ? [fill(o.fill, o.opacity)] : []; if (o.radius) c.cornerRadius = o.radius;
  if (o.stroke) { c.strokes = [fill(o.stroke)]; c.strokeWeight = 1; }
  c.counterAxisAlignItems = o.align || "CENTER";
  return c;
}
function fixed(f, w, h) { f.resize(w, h); f.primaryAxisSizingMode = "FIXED"; f.counterAxisSizingMode = "FIXED"; return f; }
function line(x1, y1, x2, y2, color, weight) { const l = figma.createLine(); const dx = x2 - x1, dy = y2 - y1; l.resize(Math.hypot(dx, dy), 0); l.x = x1; l.y = y1; l.rotation = -Math.atan2(dy, dx) * 180 / Math.PI; l.strokes = [fill(color || "ink")]; l.strokeWeight = weight || 1; return l; }
function toolBtn(glyph, active, label) { const b = al("HORIZONTAL", {name: "Tool " + (label || glyph), pad: 0, radius: 7, fill: active ? "selected" : undefined}); fixed(b, 34, 34); b.primaryAxisAlignItems = "CENTER"; b.counterAxisAlignItems = "CENTER"; b.appendChild(txt(glyph, {size: 15, color: active ? "accent" : "text"})); return b; }
function field(label, value, w = 180) { const r = al("HORIZONTAL", {name: "Field " + label, gap: 10, align: "CENTER"}); const l = txt(label, {color: "muted", size: 12}); l.textAlignHorizontal = "RIGHT"; l.resize(90, l.height); r.appendChild(l); const box = al("HORIZONTAL", {pad: [5, 9, 5, 9], radius: 7, fill: "input", stroke: "border"}); box.resize(w, 28); box.primaryAxisSizingMode = "FIXED"; box.appendChild(txt(value, {size: 12})); r.appendChild(box); return r; }

const root = al("VERTICAL", {name: "Пси — UI Kit", gap: 40, pad: 56, fill: "bg"});
root.resize(1600, 100); root.primaryAxisSizingMode = "AUTO"; root.counterAxisSizingMode = "FIXED";
const headR = al("HORIZONTAL", {name: "Header", gap: 16, align: "CENTER"}); root.appendChild(headR);
const icon = al("HORIZONTAL", {name: "App icon", radius: 40, fill: "accent", pad: [10, 34, 22, 34]}); headR.appendChild(icon);
icon.appendChild(txt("Ψ", {family: SERIF, size: 96, color: "on-accent"}));
const hc = al("VERTICAL", {name: "Title", gap: 4}); headR.appendChild(hc);
hc.appendChild(txt("Пси — UI Kit", {family: SERIF, size: 40}));
hc.appendChild(txt("Универсальная CAD · этап 00–01: черчение, листы, PDF, DXF · коллекции «Psi — Light» / «Psi — Dark» (зеркало ui/theme.cpp)", {color: "muted", size: 15}));

// --- Typography & tokens
const typo = al("HORIZONTAL", {name: "Typography", gap: 32, align: "MIN"}); root.appendChild(typo);
const tcol = al("VERTICAL", {name: "Scale", gap: 6});
tcol.appendChild(txt("Заголовок · Serif 26", {family: SERIF, size: 26})); tcol.appendChild(txt("Подзаголовок · Serif 17", {family: SERIF, size: 17}));
tcol.appendChild(txt("Текст · Inter 13", {size: 13})); tcol.appendChild(txt("Подсказка · Inter 12 muted", {size: 12, color: "muted"})); tcol.appendChild(txt("Команда · Mono 12  @80<30", {family: MONO, size: 12}));
typo.appendChild(tcol);
const sw = al("HORIZONTAL", {name: "Tokens", gap: 10}); sw.layoutWrap = "WRAP"; sw.counterAxisSpacing = 10; sw.resize(900, 10); sw.primaryAxisSizingMode = "FIXED";
for (const n of Object.keys(T)) { const c = al("VERTICAL", {name: n, gap: 4, align: "CENTER"}); const r = figma.createRectangle(); r.resize(56, 36); r.cornerRadius = 8; r.fills = [fill(n)]; r.strokes = [fill("border")]; c.appendChild(r); c.appendChild(txt(n, {size: 10, color: "muted"})); sw.appendChild(c); }
typo.appendChild(sw);

const grid = al("HORIZONTAL", {name: "Components", gap: 24}); grid.layoutWrap = "WRAP"; grid.counterAxisSpacing = 24;
grid.resize(1488, 100); grid.primaryAxisSizingMode = "FIXED"; root.appendChild(grid);

// --- Buttons
const bAcc = comp("Button / Accent", "HORIZONTAL", {pad: [8, 18, 8, 18], radius: 8, fill: "accent"}); bAcc.appendChild(txt("Экспорт PDF…", {style: "Semi Bold", color: "on-accent"})); grid.appendChild(bAcc);
const bDef = comp("Button / Default", "HORIZONTAL", {pad: [8, 18, 8, 18], radius: 8, fill: "card", stroke: "border"}); bDef.appendChild(txt("Открыть…")); grid.appendChild(bDef);
const bDis = comp("Button / Disabled", "HORIZONTAL", {pad: [8, 18, 8, 18], radius: 8, fill: "panel", stroke: "border"}); bDis.appendChild(txt("Модель", {color: "faint"})); grid.appendChild(bDis);

// --- Workspace bar (top): Ψ, рабочие области, right-side actions
const ws = comp("Workspace bar", "HORIZONTAL", {pad: [6, 12, 6, 12], gap: 4, fill: "panel", stroke: "border"}); fixed(ws, 1240, 44);
ws.appendChild(txt("Ψ", {family: SERIF, size: 20, color: "accent"})); const wsSp0 = al("HORIZONTAL"); wsSp0.resize(8, 4); ws.appendChild(wsSp0);
for (const [t, on, en] of [["Черчение", true, true], ["Листы", false, true], ["Эскиз", false, false], ["Модель", false, false], ["Сборка", false, false], ["Интерьер", false, false], ["Чертёж 3D", false, false]]) {
  const b = al("HORIZONTAL", {name: "WS " + t, pad: [5, 11, 5, 11], radius: 7, fill: on ? "selected" : undefined}); b.appendChild(txt(t, {style: on ? "Medium" : "Regular", color: en ? "text" : "faint"})); ws.appendChild(b);
}
const wsSp = al("HORIZONTAL"); wsSp.layoutGrow = 1; ws.appendChild(wsSp);
for (const g of ["↶", "↷", "⛶", "▦", "◎", "⟂", "✧", "PDF"]) { const b = al("HORIZONTAL", {pad: [5, 8, 5, 8], radius: 6}); b.appendChild(txt(g, {size: 12, color: g === "◎" || g === "✧" ? "accent" : "text"})); ws.appendChild(b); }
grid.appendChild(ws);

// --- Tool palette (dock «Инструменты»): search, collapsible groups, «Избранное», 36 px buttons, compact mode
const TOOL_GROUPS = [["Избранное", [["○", "Окружность"], ["╱", "Линия"], ["⊔", "Объединить"]]],
  ["Рисование", [["↖", "Выбор"], ["╱", "Линия"], ["⌇", "Полилиния"], ["▭", "Прямоугольник"], ["○", "Окружность"], ["◠", "Дуга"], ["⬭", "Эллипс"], ["∿", "Сплайн"]]],
  ["Текст и размеры", [["T", "Текст"], ["↗", "Выноска"], ["↔", "Линейный размер"], ["⟷", "Горизонтальный"], ["↕", "Вертикальный"], ["R", "Радиус"], ["Ø", "Диаметр"], ["∠", "Угловой"]]],
  ["Правка", [["✥", "Переместить"], ["⧉", "Копировать"], ["↻", "Повернуть"], ["⇋", "Зеркало"], ["⤢", "Масштаб"], ["≡", "Смещение"], ["✂", "Обрезать"], ["⟶", "Удлинить"], ["◜", "Скругление"], ["◿", "Фаска"], ["⋮⋮", "Массив"], ["✸", "Расчленить"], ["✕", "Удалить"]]],
  ["Объединение", [["⟆", "Соединить"], ["⊔", "Объединить"], ["⊟", "Вычесть"], ["⊓", "Пересечение"], ["⊥", "Разрезать"], ["✁", "Разрезать объектом"]]],
  ["Штриховка", [["▨", "Штриховка"]]]];
// Palette: search box + compact toggle, collapsible group headers, 36 px buttons (icon + label, 2 columns; icons only in compact mode)
function paletteBtn(glyph, label, active, compact) {
  const b = al("HORIZONTAL", {name: "Tool " + label, gap: 6, pad: compact ? 0 : [0, 8, 0, 6], radius: 7, fill: active ? "selected" : undefined, align: "CENTER"});
  if (compact) { fixed(b, 36, 36); b.primaryAxisAlignItems = "CENTER"; } else { fixed(b, 117, 36); }
  b.appendChild(txt(glyph, {size: 14, color: active ? "accent" : "text"}));
  if (!compact) { const l = txt(label, {size: 11, color: "text"}); l.textTruncation = "ENDING"; l.resize(80, l.height); b.appendChild(l); }
  return b;
}
function palette(o = {}) {
  const w = o.width || 260, compact = !!o.compact;
  const d = al("VERTICAL", {name: "Инструменты", pad: 8, gap: 6, fill: "panel", align: "MIN"}); fixed(d, w, o.height || 830); d.strokes = [fill("border")]; d.strokeWeight = 1;
  const top = al("HORIZONTAL", {gap: 4, align: "CENTER"}); top.resize(w - 16, 28); top.primaryAxisSizingMode = "FIXED";
  const sb = al("HORIZONTAL", {gap: 6, pad: [4, 8, 4, 8], radius: 7, fill: "input", stroke: o.query ? "accent" : "border", align: "CENTER"}); sb.layoutGrow = 1; sb.resize(100, 28);
  sb.appendChild(txt("⌕", {size: 12, color: "muted"})); sb.appendChild(txt(o.query || "Поиск инструмента…", {size: 12, color: o.query ? "text" : "faint"})); top.appendChild(sb);
  const cb = al("HORIZONTAL", {pad: 0, radius: 6, fill: compact ? "selected" : undefined}); fixed(cb, 28, 28); cb.primaryAxisAlignItems = "CENTER"; cb.counterAxisAlignItems = "CENTER"; cb.appendChild(txt("▦", {size: 12, color: compact ? "accent" : "muted"})); top.appendChild(cb);
  d.appendChild(top);
  for (const [title, tools] of TOOL_GROUPS) {
    const shown = o.query ? tools.filter(([, l]) => l.toLowerCase().includes(o.query.toLowerCase())) : tools;
    if (!shown.length || (o.query && title === "Избранное")) continue;
    const collapsed = (o.collapsed || []).includes(title);
    const h = al("HORIZONTAL", {gap: 6, pad: [3, 6, 3, 6], radius: 6, fill: "hover", align: "CENTER"}); h.resize(w - 16, 22); h.primaryAxisSizingMode = "FIXED";
    h.appendChild(txt(collapsed ? "▸" : "▾", {size: 10, color: "muted"})); h.appendChild(txt(title, {size: 12, style: "Semi Bold"})); d.appendChild(h);
    if (collapsed) continue;
    const g = al("HORIZONTAL", {gap: 3}); g.layoutWrap = "WRAP"; g.counterAxisSpacing = 3; g.resize(w - 16, 10); g.primaryAxisSizingMode = "FIXED";
    for (const [gl, l] of shown) g.appendChild(paletteBtn(gl, l, l === o.active, compact));
    d.appendChild(g);
  }
  return d;
}
const tools = comp("Dock / Инструменты", "VERTICAL", {pad: 0, gap: 0, fill: "panel", align: "MIN"}); fixed(tools, 260, 760); tools.appendChild(palette({width: 260, height: 760, active: "Линия"})); grid.appendChild(tools);
const toolsC = comp("Dock / Инструменты / компактный", "VERTICAL", {pad: 0, gap: 0, fill: "panel", align: "MIN"}); fixed(toolsC, 200, 520); toolsC.appendChild(palette({width: 200, height: 520, compact: true, active: "Линия"})); grid.appendChild(toolsC);
const toolsS = comp("Dock / Инструменты / поиск", "VERTICAL", {pad: 0, gap: 0, fill: "panel", align: "MIN"}); fixed(toolsS, 260, 260); toolsS.appendChild(palette({width: 260, height: 260, query: "раз"})); grid.appendChild(toolsS);

// --- Layers dock
const layers = comp("Dock / Слои", "VERTICAL", {pad: 0, gap: 0, fill: "card", stroke: "border", align: "MIN"}); fixed(layers, 300, 190);
const lh = al("HORIZONTAL", {name: "Head", gap: 8, pad: [6, 10, 6, 10], fill: "panel"}); fixed(lh, 300, 28); for (const [t, w] of [["", 18], ["", 18], ["Слой", 120], ["Цвет", 70], ["Толщина", 50]]) { const c = txt(t, {size: 11, color: "muted"}); c.resize(w, c.height); lh.appendChild(c); } layers.appendChild(lh);
for (const [name, color, w, cur, vis, lock] of [["0", "#141413", "0.25 мм", false, true, false], ["Стены", "#141413", "0.50 мм", true, true, false], ["Проёмы", "#8A6D5A", "0.25 мм", false, true, false], ["Размеры", "#D97757", "0.18 мм", false, true, false], ["Штриховка", "#9A9A96", "0.13 мм", false, false, true]]) {
  const r = al("HORIZONTAL", {name: "Layer " + name, gap: 8, pad: [5, 10, 5, 10], align: "CENTER", fill: cur ? "selected" : undefined}); fixed(r, 300, 28);
  const eye = txt("👁", {size: 11, color: vis ? "text" : "faint"}); eye.resize(18, eye.height); r.appendChild(eye);
  const lk = txt("🔒", {size: 11, color: lock ? "accent" : "faint"}); lk.resize(18, lk.height); r.appendChild(lk);
  const nm = txt((cur ? "● " : "") + name, {size: 12, style: cur ? "Semi Bold" : "Regular"}); nm.resize(120, nm.height); r.appendChild(nm);
  const cw = al("HORIZONTAL", {gap: 6, align: "CENTER"}); const sq = figma.createRectangle(); sq.resize(12, 12); sq.cornerRadius = 3; sq.fills = [{type: "SOLID", color: hex(color)}]; cw.appendChild(sq); cw.appendChild(txt(color.toUpperCase(), {size: 10, color: "muted"})); cw.resize(70, 14); cw.primaryAxisSizingMode = "FIXED"; r.appendChild(cw);
  r.appendChild(txt(w, {size: 11})); layers.appendChild(r);
}
const lf = al("HORIZONTAL", {gap: 6, pad: [4, 8, 4, 8], align: "CENTER"}); fixed(lf, 300, 30); for (const g of ["+", "−", "⚙"]) lf.appendChild(toolBtn(g)); const lfs = al("HORIZONTAL"); lfs.layoutGrow = 1; lf.appendChild(lfs); lf.appendChild(txt("Двойной щелчок — текущий слой", {size: 10, color: "faint"})); layers.appendChild(lf);
grid.appendChild(layers);

// --- Properties dock
const props = comp("Dock / Свойства", "VERTICAL", {pad: 10, gap: 8, fill: "panel", stroke: "border", align: "MIN"}); fixed(props, 300, 330);
props.appendChild(txt("Окружность  ·  #12", {family: SERIF, size: 15}));
for (const [l, v] of [["Слой", "Стены"], ["Цвет", "По слою"], ["Толщина", "По слою"], ["Тип линии", "Сплошная"], ["Габариты", "1800 × 1800 мм"], ["Радиус", "900.000"], ["Центр", "2000, 200"]]) props.appendChild(field(l, v));
props.appendChild(txt("Атрибуты", {family: SERIF, size: 14}));
const at = al("VERTICAL", {gap: 0, fill: "card", stroke: "border", radius: 6}); fixed(at, 280, 56);
for (const [k, v] of [["material", "Сталь 45"], ["note", "Дверь 900"]]) { const r = al("HORIZONTAL", {gap: 8, pad: [4, 8, 4, 8]}); fixed(r, 280, 28); const kk = txt(k, {size: 11, family: MONO}); kk.resize(110, kk.height); r.appendChild(kk); r.appendChild(txt(v, {size: 11})); at.appendChild(r); }
props.appendChild(at); grid.appendChild(props);

// --- Document tree dock
const tree = comp("Dock / Дерево документа", "VERTICAL", {pad: 8, gap: 2, fill: "card", stroke: "border", align: "MIN"}); fixed(tree, 260, 230);
for (const [ind, t, g] of [[0, "План комнаты.psi", "⌂"], [1, "Чертёж  (27)", "╱"], [2, "Стены  (10)", "▤"], [3, "Линия  #1", ""], [3, "Линия  #2", ""], [2, "Проёмы  (7)", "▤"], [2, "Размеры  (5)", "▤"], [1, "Листы  (1)", "▱"], [2, "План — A3 альбомный", ""]]) {
  const r = al("HORIZONTAL", {gap: 6, pad: [3, 6, 3, 6 + ind * 16], align: "CENTER"}); if (g) r.appendChild(txt(g, {size: 11, color: ind === 0 ? "accent" : "muted"})); r.appendChild(txt(t, {size: 12})); tree.appendChild(r);
}
grid.appendChild(tree);

// --- Command bar + prompt
const cmd = comp("Command bar", "HORIZONTAL", {pad: [6, 12, 6, 12], gap: 10, fill: "panel", stroke: "border", align: "CENTER"}); fixed(cmd, 900, 38);
cmd.appendChild(txt("Следующая точка (Enter — завершить, «c» — замкнуть):", {size: 12, color: "muted"}));
const cb = al("HORIZONTAL", {pad: [5, 10, 5, 10], radius: 7, fill: "input", stroke: "border"}); cb.layoutGrow = 1; cb.resize(300, 28); cb.appendChild(txt("@1200<90", {family: MONO, size: 12})); cmd.appendChild(cb); grid.appendChild(cmd);

// --- Status bar
const st = comp("Status bar", "HORIZONTAL", {pad: [4, 12, 4, 12], gap: 24, fill: "panel", stroke: "border", align: "CENTER"}); fixed(st, 900, 26);
st.appendChild(txt("X 3540.00   Y 1210.50", {family: MONO, size: 11, color: "muted"})); st.appendChild(txt("Линия", {size: 11, color: "muted"})); const ssp = al("HORIZONTAL"); ssp.layoutGrow = 1; st.appendChild(ssp);
const mg = al("HORIZONTAL", {name: "Magnet toggle", gap: 4, pad: [2, 8, 2, 8], radius: 6, fill: "selected", stroke: "accent", align: "CENTER"}); mg.appendChild(txt("⌁ Сетка 1 мм", {size: 11, color: "accent"})); mg.appendChild(txt("▾", {size: 9, color: "muted"})); st.appendChild(mg);
st.appendChild(txt("Конечная точка", {size: 11, color: "snap"})); st.appendChild(txt("Миллиметры  ·  сетка 100 мм", {size: 11, color: "muted"})); grid.appendChild(st);

// --- Inline editors (double-click): dimension value, line length / angle, rectangle width × height, circle, text
const edRow = comp("Inline editor", "HORIZONTAL", {gap: 24, pad: 16, fill: "canvas", stroke: "border", align: "MIN"});
function inlineEd(fields) {
  const e = al("VERTICAL", {gap: 4, pad: [6, 8, 6, 8], radius: 8, fill: "card", stroke: "accent"});
  for (const [l, v, focus] of fields) { const r = al("HORIZONTAL", {gap: 6, align: "CENTER"}); if (fields.length > 1) { const lt = txt(l, {size: 11, color: "muted"}); lt.resize(52, lt.height); r.appendChild(lt); }
    const box = al("HORIZONTAL", {pad: [2, 6, 2, 6], radius: 5, fill: "input", stroke: focus ? "accent" : "border"}); box.resize(90, 24); box.primaryAxisSizingMode = "FIXED"; box.appendChild(txt(v, {family: MONO, size: 12})); r.appendChild(box); e.appendChild(r); }
  if (fields.length > 1) e.appendChild(txt("Enter — применить · Esc — отмена", {size: 9, color: "faint"}));
  return e;
}
for (const f of [[["Размер", "1000", true]], [["Длина", "2600", true], ["Угол", "90°"]], [["Ширина", "4000", true], ["Высота", "3000"]], [["Радиус", "450", true], ["Диаметр", "900"]], [["Текст", "Кухня", true]]]) edRow.appendChild(inlineEd(f));
grid.appendChild(edRow);
// --- Grid magnet marker: filled accent square at the rounded point + coordinates
const mgC = comp("Snap / Grid magnet", "VERTICAL", {pad: 0, fill: "canvas", stroke: "border"}); fixed(mgC, 200, 120); mgC.clipsContent = true;
const mgI = figma.createFrame(); mgI.resize(200, 120); mgI.fills = []; mgC.appendChild(mgI); mgI.layoutPositioning = "ABSOLUTE";
for (let x = 0; x <= 200; x += 20) mgI.appendChild(line(x, 0, x, 120, "grid", 1)); for (let y = 0; y <= 120; y += 20) mgI.appendChild(line(0, y, 200, y, "grid", 1));
mgI.appendChild(line(0, 60, 200, 60, "crosshair", 1)); mgI.appendChild(line(100, 0, 100, 120, "crosshair", 1));
const mgM = figma.createRectangle(); mgM.resize(8, 8); mgM.x = 96; mgM.y = 56; mgM.fills = [fill("accent")]; mgI.appendChild(mgM);
const mgL = al("HORIZONTAL", {pad: [2, 6, 2, 6], radius: 4, fill: "card", stroke: "border"}); mgL.x = 110; mgL.y = 34; mgL.appendChild(txt("Сетка · 3500, 2600", {size: 10, color: "snap"})); mgI.appendChild(mgL);
grid.appendChild(mgC);

// --- Canvas sample with grid, entities, selection grips, snap marker, crosshair
const cv = comp("Canvas / sample", "VERTICAL", {pad: 0, fill: "canvas", stroke: "border"}); fixed(cv, 520, 320); cv.clipsContent = true;
const cvInner = figma.createFrame(); cvInner.resize(520, 320); cvInner.fills = []; cv.appendChild(cvInner); cvInner.layoutPositioning = "ABSOLUTE";
for (let x = 0; x <= 520; x += 20) cvInner.appendChild(line(x, 0, x, 320, x % 100 === 0 ? "grid-major" : "grid", 1));
for (let y = 0; y <= 320; y += 20) cvInner.appendChild(line(0, y, 520, y, y % 100 === 0 ? "grid-major" : "grid", 1));
for (const [a, b] of [[[80, 60], [400, 60]], [[400, 60], [400, 260]], [[400, 260], [80, 260]], [[80, 260], [80, 60]]]) cvInner.appendChild(line(a[0], a[1], b[0], b[1], "ink", 2));
const sel = line(200, 60, 200, 260, "accent", 2); cvInner.appendChild(sel);
for (const [gx, gy] of [[200, 60], [200, 160], [200, 260]]) { const g = figma.createRectangle(); g.resize(7, 7); g.x = gx - 3.5; g.y = gy - 3.5; g.fills = [fill("accent")]; cvInner.appendChild(g); }
const snapM = figma.createRectangle(); snapM.resize(12, 12); snapM.x = 394; snapM.y = 254; snapM.fills = []; snapM.strokes = [fill("snap")]; snapM.strokeWeight = 2; cvInner.appendChild(snapM);
cvInner.appendChild(line(0, 190, 520, 190, "crosshair", 1)); cvInner.appendChild(line(300, 0, 300, 320, "crosshair", 1));
const lbl = al("HORIZONTAL", {pad: [2, 6, 2, 6], radius: 4, fill: "card", stroke: "border"}); lbl.x = 310; lbl.y = 196; lbl.appendChild(txt("Конечная точка", {size: 10, color: "snap"})); cvInner.appendChild(lbl);
const dimT = txt("3200", {size: 11, color: "accent"}); dimT.x = 228; dimT.y = 275; cvInner.appendChild(dimT); cvInner.appendChild(line(80, 290, 400, 290, "accent", 1));
grid.appendChild(cv);

// --- Sheet card (preview with title block)
const sheet = comp("Sheet / preview", "VERTICAL", {pad: 18, fill: "panel", stroke: "border"}); fixed(sheet, 440, 330);
const paper = figma.createFrame(); paper.resize(404, 286); paper.fills = [fill("paper")]; paper.strokes = [fill("border")]; paper.effects = [{type: "DROP_SHADOW", color: {r: 0, g: 0, b: 0, a: 0.18}, offset: {x: 2, y: 3}, radius: 6, visible: true, blendMode: "NORMAL"}]; sheet.appendChild(paper);
const fr = figma.createRectangle(); fr.resize(372, 268); fr.x = 20; fr.y = 6; fr.fills = []; fr.strokes = [fill("ink")]; fr.strokeWeight = 1.5; paper.appendChild(fr);
const tb = figma.createRectangle(); tb.resize(185, 55); tb.x = 207; tb.y = 219; tb.fills = []; tb.strokes = [fill("ink")]; tb.strokeWeight = 1.2; paper.appendChild(tb);
for (const [x1, y1, x2, y2] of [[207, 237, 392, 237], [207, 255, 392, 255], [272, 219, 272, 274], [337, 219, 337, 274], [337, 246, 392, 246]]) paper.appendChild(line(x1, y1, x2, y2, "ink", 0.8));
const tbt = txt("ПСИ.ДЕМО.001", {size: 9}); tbt.x = 290; tbt.y = 223; paper.appendChild(tbt); const tbn = txt("План комнаты", {size: 8}); tbn.x = 280; tbn.y = 258; paper.appendChild(tbn); const tbs = txt("1:50", {size: 8}); tbs.x = 355; tbs.y = 262; paper.appendChild(tbs);
const vpF = figma.createRectangle(); vpF.resize(300, 180); vpF.x = 40; vpF.y = 26; vpF.fills = []; vpF.strokes = [fill("accent")]; vpF.strokeWeight = 1; vpF.dashPattern = [4, 3]; paper.appendChild(vpF);
const room = figma.createRectangle(); room.resize(160, 100); room.x = 110; room.y = 66; room.fills = []; room.strokes = [fill("ink")]; room.strokeWeight = 2; paper.appendChild(room);
const vpl = txt("Вид 1 · 1:50", {size: 9, color: "accent"}); vpl.x = 44; vpl.y = 14; paper.appendChild(vpl);
grid.appendChild(sheet);

// --- Welcome card
const wc = comp("Welcome card", "VERTICAL", {pad: 32, gap: 12, fill: "card", stroke: "border", radius: 16, align: "MIN"}); fixed(wc, 520, 330);
const wi = al("HORIZONTAL", {radius: 14, fill: "accent", pad: [4, 16, 10, 16]}); wi.appendChild(txt("Ψ", {family: SERIF, size: 40, color: "on-accent"})); wc.appendChild(wi);
wc.appendChild(txt("Пси", {family: SERIF, size: 26})); wc.appendChild(txt("Универсальная CAD-система. Этап 00–01: черчение, листы, PDF, DXF.", {size: 12, color: "muted", w: 450}));
const wr = al("HORIZONTAL", {gap: 8}); wr.appendChild(bAcc.createInstance()); for (const t of ["Открыть…", "Импорт DXF…", "Демонстрация"]) { const i = bDef.createInstance(); wr.appendChild(i); } wc.appendChild(wr);
wc.appendChild(txt("Недавние", {family: SERIF, size: 15}));
for (const [n, p] of [["План комнаты.psi", "~/Documents/Пси"], ["Кронштейн.dxf", "~/Downloads"]]) { const r = al("HORIZONTAL", {gap: 10}); r.appendChild(txt("▱", {size: 11, color: "muted"})); r.appendChild(txt(n, {size: 12})); r.appendChild(txt(p, {size: 11, color: "faint"})); wc.appendChild(r); }
grid.appendChild(wc);

// --- Dialog (document settings) & toast
const dlg = comp("Dialog / Параметры документа", "VERTICAL", {pad: 20, gap: 10, fill: "bg", stroke: "border", radius: 12, align: "MIN"}); fixed(dlg, 420, 300);
dlg.appendChild(txt("Параметры документа", {family: SERIF, size: 17}));
for (const [l, v] of [["Единицы", "Миллиметры"], ["Знаков", "2"], ["Шаг сетки", "10 мм"], ["Шаг привязки", "1 мм"], ["Высота текста", "3.5 мм"], ["Автор", ""]]) dlg.appendChild(field(l, v, 240));
const dr = al("HORIZONTAL", {gap: 8}); const dsp = al("HORIZONTAL"); dsp.layoutGrow = 1; dr.appendChild(dsp); dr.resize(380, 30); dr.primaryAxisSizingMode = "FIXED"; const cancel = bDef.createInstance(); dr.appendChild(cancel); const okb = bAcc.createInstance(); dr.appendChild(okb); dlg.appendChild(dr);
grid.appendChild(dlg);
const toast = comp("Toast", "HORIZONTAL", {pad: [8, 16, 8, 16], radius: 10, fill: "text"}); toast.appendChild(txt("Нужен замкнутый контур: окружность, замкнутая полилиния, сплайн или эллипс", {color: "bg", size: 12})); grid.appendChild(toast);
const prog = comp("Progress / Импорт DXF", "VERTICAL", {pad: 16, gap: 10, fill: "bg", stroke: "border", radius: 12, align: "MIN"}); fixed(prog, 360, 96);
prog.appendChild(txt("Импорт DXF: кронштейн.dxf…", {size: 13})); const bar = al("HORIZONTAL", {fill: "selected", radius: 4}); fixed(bar, 328, 6); const barIn = al("HORIZONTAL", {fill: "accent", radius: 4}); fixed(barIn, 190, 6); bar.appendChild(barIn); prog.appendChild(bar);
const pr = al("HORIZONTAL"); pr.resize(328, 28); pr.primaryAxisSizingMode = "FIXED"; const psp = al("HORIZONTAL"); psp.layoutGrow = 1; pr.appendChild(psp); pr.appendChild(bDef.createInstance()); prog.appendChild(pr); grid.appendChild(prog);

await root.screenshot({scale: 0.5});
return {rootId: root.id, colIds, variables: Object.keys(T).length * 2};
