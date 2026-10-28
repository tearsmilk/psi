// Пси (PSI) — Screens page: Начальный экран (Light / Dark), Черчение (Light / Dark), Черчение — инструмент активен,
// Листы, Параметры документа, Настройки (тема / акцент / горячие клавиши), Импорт DXF (прогресс + отчёт), Экспорт PDF.
// Run after 01_ui_kit.js (uses the «Psi — Light» / «Psi — Dark» collections).
let page = figma.root.children.find(p => p.name === "Screens");
if (!page) { page = figma.createPage(); page.name = "Screens"; }
await figma.setCurrentPageAsync(page);
for (const n of [...page.children]) n.remove();

const fonts = await figma.listAvailableFontsAsync();
const has = (fam, style) => fonts.some(f => f.fontName.family === fam && f.fontName.style === style);
const SANS = "Inter";
const SERIF = ["Source Serif 4", "Lora", "Merriweather", "Playfair Display"].find(f => has(f, "Regular")) || "Inter";
const MONO = ["JetBrains Mono", "Roboto Mono", "Source Code Pro"].find(f => has(f, "Regular")) || "Inter";
for (const fn of [{family: SANS, style: "Regular"}, {family: SANS, style: "Medium"}, {family: SANS, style: "Semi Bold"}, {family: SERIF, style: "Regular"}, {family: MONO, style: "Regular"}]) await figma.loadFontAsync(fn);

const cols = await figma.variables.getLocalVariableCollectionsAsync();
const vars = await figma.variables.getLocalVariablesAsync("COLOR");
const V = {};
for (const mode of ["Light", "Dark"]) {
  const col = cols.find(c => c.name === "Psi — " + mode);
  if (!col) throw new Error("Run 01_ui_kit.js first (missing collection Psi — " + mode + ")");
  V[mode] = {};
  for (const v of vars) if (v.variableCollectionId === col.id) V[mode][v.name.replace("color/", "")] = v;
}
let MODE = "Light";
const hex = h => { h = h.replace("#", ""); return {r: parseInt(h.slice(0,2),16)/255, g: parseInt(h.slice(2,4),16)/255, b: parseInt(h.slice(4,6),16)/255}; };
const fill = (n, opacity) => { const p = figma.variables.setBoundVariableForPaint({type: "SOLID", color: {r: 0, g: 0, b: 0}}, "color", V[MODE][n]); if (opacity !== undefined) p.opacity = opacity; return p; };
function txt(chars, o = {}) { const t = figma.createText(); t.fontName = {family: o.family || SANS, style: o.style || "Regular"}; t.fontSize = o.size || 13; t.characters = chars; t.fills = [fill(o.color || "text")]; if (o.w) { t.textAutoResize = "HEIGHT"; t.resize(o.w, t.height); } return t; }
function al(dir, o = {}) {
  const f = figma.createAutoLayout(dir, {itemSpacing: o.gap ?? 8}); f.name = o.name || "Frame"; f.fills = o.fill ? [fill(o.fill, o.opacity)] : [];
  if (o.pad !== undefined) { const [t, r, b, l] = Array.isArray(o.pad) ? o.pad : [o.pad, o.pad, o.pad, o.pad]; f.paddingTop = t; f.paddingRight = r; f.paddingBottom = b; f.paddingLeft = l; }
  if (o.radius) f.cornerRadius = o.radius; if (o.stroke) { f.strokes = [fill(o.stroke)]; f.strokeWeight = 1; } if (o.align) f.counterAxisAlignItems = o.align; return f;
}
function fixed(f, w, h) { f.resize(w, h); f.primaryAxisSizingMode = "FIXED"; f.counterAxisSizingMode = "FIXED"; return f; }
function line(x1, y1, x2, y2, color, weight) { const l = figma.createLine(); const dx = x2 - x1, dy = y2 - y1; l.resize(Math.hypot(dx, dy), 0); l.x = x1; l.y = y1; l.rotation = -Math.atan2(dy, dx) * 180 / Math.PI; l.strokes = [fill(color || "ink")]; l.strokeWeight = weight || 1; return l; }
function btn(t, accent) { const b = al("HORIZONTAL", {name: "Button " + t, pad: [7, 16, 7, 16], radius: 8, fill: accent ? "accent" : "card", stroke: accent ? undefined : "border"}); b.appendChild(txt(t, {style: accent ? "Semi Bold" : "Regular", color: accent ? "on-accent" : "text"})); return b; }
function toolBtn(glyph, active) { const b = al("HORIZONTAL", {pad: 0, radius: 7, fill: active ? "selected" : undefined}); fixed(b, 34, 34); b.primaryAxisAlignItems = "CENTER"; b.counterAxisAlignItems = "CENTER"; b.appendChild(txt(glyph, {size: 15, color: active ? "accent" : "text"})); return b; }
function field(label, value, w = 180) { const r = al("HORIZONTAL", {gap: 10, align: "CENTER"}); const l = txt(label, {color: "muted", size: 12}); l.textAlignHorizontal = "RIGHT"; l.resize(96, l.height); r.appendChild(l); const box = al("HORIZONTAL", {pad: [5, 9, 5, 9], radius: 7, fill: "input", stroke: "border"}); box.resize(w, 28); box.primaryAxisSizingMode = "FIXED"; box.appendChild(txt(value, {size: 12})); r.appendChild(box); return r; }

function workspaceBar(active) {
  const ws = al("HORIZONTAL", {name: "Workspace bar", pad: [6, 12, 6, 12], gap: 4, fill: "panel", align: "CENTER"}); fixed(ws, 1440, 44); ws.strokes = [fill("border")]; ws.strokeWeight = 1;
  ws.appendChild(txt("Ψ", {family: SERIF, size: 20, color: "accent"})); const sp0 = al("HORIZONTAL"); sp0.resize(8, 4); ws.appendChild(sp0);
  for (const [t, en] of [["Черчение", true], ["Листы", true], ["Эскиз", false], ["Модель", false], ["Сборка", false], ["Интерьер", false], ["Чертёж 3D", false]]) {
    const on = t === active; const b = al("HORIZONTAL", {pad: [5, 11, 5, 11], radius: 7, fill: on ? "selected" : undefined}); b.appendChild(txt(t, {style: on ? "Medium" : "Regular", color: en ? "text" : "faint"})); ws.appendChild(b);
  }
  const sp = al("HORIZONTAL"); sp.layoutGrow = 1; ws.appendChild(sp);
  for (const g of ["↶", "↷", "⛶", "▦", "◎", "⟂", "✧", "PDF"]) { const b = al("HORIZONTAL", {pad: [5, 8, 5, 8], radius: 6}); b.appendChild(txt(g, {size: 12, color: g === "◎" || g === "✧" || g === "▦" ? "accent" : "text"})); ws.appendChild(b); }
  return ws;
}
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
function toolsDock(o = {}) { return palette(Object.assign({width: 260, height: 300}, o)); }
function treeDock() {
  const d = al("VERTICAL", {name: "Дерево документа", pad: 10, gap: 2, fill: "panel", align: "MIN"}); fixed(d, 260, 320); d.strokes = [fill("border")]; d.strokeWeight = 1;
  d.appendChild(txt("Дерево документа", {size: 11, color: "muted"}));
  for (const [ind, t, g] of [[0, "План комнаты.psi", "⌂"], [1, "Чертёж  (27)", "╱"], [2, "0  (0)", "▤"], [2, "Стены  (10)", "▤"], [3, "Линия  #1", ""], [3, "Линия  #2", ""], [3, "… ещё 8", ""], [2, "Проёмы  (7)", "▤"], [2, "Размеры  (5)", "▤"], [2, "Штриховка  (2)", "▤"], [2, "Текст  (3)", "▤"], [1, "Листы  (1)", "▱"], [2, "План — A3 альбомный", ""]]) {
    const r = al("HORIZONTAL", {gap: 6, pad: [3, 6, 3, 6 + ind * 16], align: "CENTER"}); if (g) r.appendChild(txt(g, {size: 11, color: ind === 0 ? "accent" : "muted"})); r.appendChild(txt(t, {size: 12})); d.appendChild(r);
  }
  return d;
}
function propsDock(sel) {
  const d = al("VERTICAL", {name: "Свойства", pad: 10, gap: 8, fill: "panel", align: "MIN"}); fixed(d, 300, 330); d.strokes = [fill("border")]; d.strokeWeight = 1;
  d.appendChild(txt("Свойства", {size: 11, color: "muted"}));
  if (!sel) { d.appendChild(txt("Ничего не выбрано", {family: SERIF, size: 15})); for (const l of ["Слой", "Цвет", "Толщина", "Тип линии"]) { const f = field(l, ""); f.opacity = 0.5; d.appendChild(f); } return d; }
  d.appendChild(txt("Линия  ·  #14", {family: SERIF, size: 15}));
  for (const [l, v] of [["Слой", "Стены"], ["Цвет", "По слою"], ["Толщина", "По слою"], ["Тип линии", "Сплошная"], ["Габариты", "0 × 3600 мм"], ["Длина", "3600 мм"], ["Угол", "90°"]]) d.appendChild(field(l, v));
  d.appendChild(txt("Атрибуты", {family: SERIF, size: 14}));
  const at = al("VERTICAL", {gap: 0, fill: "card", stroke: "border", radius: 6}); fixed(at, 280, 56);
  for (const [k, v] of [["material", "Кирпич"], ["note", "Перегородка"]]) { const r = al("HORIZONTAL", {gap: 8, pad: [4, 8, 4, 8]}); fixed(r, 280, 28); const kk = txt(k, {size: 11, family: MONO}); kk.resize(110, kk.height); r.appendChild(kk); r.appendChild(txt(v, {size: 11})); at.appendChild(r); }
  d.appendChild(at); return d;
}
function layersDock() {
  const d = al("VERTICAL", {name: "Слои", pad: 0, gap: 0, fill: "panel", align: "MIN"}); fixed(d, 300, 290); d.strokes = [fill("border")]; d.strokeWeight = 1;
  const t = al("HORIZONTAL", {pad: [8, 10, 4, 10]}); t.appendChild(txt("Слои", {size: 11, color: "muted"})); d.appendChild(t);
  for (const [name, color, w, cur, vis, lock] of [["0", "#141413", "0.25 мм", false, true, false], ["Стены", "#141413", "0.50 мм", true, true, false], ["Проёмы", "#8A6D5A", "0.25 мм", false, true, false], ["Размеры", "#D97757", "0.18 мм", false, true, false], ["Штриховка", "#9A9A96", "0.13 мм", false, true, false], ["Текст", "#141413", "0.25 мм", false, true, false]]) {
    const r = al("HORIZONTAL", {gap: 8, pad: [5, 10, 5, 10], align: "CENTER", fill: cur ? "selected" : "card"}); fixed(r, 300, 28);
    const eye = txt("👁", {size: 11, color: vis ? "text" : "faint"}); eye.resize(18, eye.height); r.appendChild(eye); const lk = txt("🔒", {size: 11, color: lock ? "accent" : "faint"}); lk.resize(18, lk.height); r.appendChild(lk);
    const nm = txt((cur ? "● " : "") + name, {size: 12, style: cur ? "Semi Bold" : "Regular"}); nm.resize(120, nm.height); r.appendChild(nm);
    const sq = figma.createRectangle(); sq.resize(12, 12); sq.cornerRadius = 3; sq.fills = [{type: "SOLID", color: hex(color)}]; r.appendChild(sq); const cc = txt(color.toUpperCase(), {size: 10, color: "muted"}); cc.resize(56, cc.height); r.appendChild(cc); r.appendChild(txt(w, {size: 11})); d.appendChild(r);
  }
  const lf = al("HORIZONTAL", {gap: 6, pad: [4, 8, 4, 8], align: "CENTER"}); fixed(lf, 300, 34); for (const g of ["+", "−", "⚙"]) lf.appendChild(toolBtn(g)); d.appendChild(lf); return d;
}
function canvas(w, h, o = {}) {
  const c = figma.createFrame(); c.name = "Viewport"; c.resize(w, h); c.fills = [fill("canvas")]; c.clipsContent = true;
  for (let x = 0; x <= w; x += 24) c.appendChild(line(x, 0, x, h, x % 120 === 0 ? "grid-major" : "grid", 1));
  for (let y = 0; y <= h; y += 24) c.appendChild(line(0, y, w, y, y % 120 === 0 ? "grid-major" : "grid", 1));
  // room 6000×4000 at ~0.1 px/mm → 600×400, walls 20
  const ox = (w - 600) / 2, oy = (h - 400) / 2 - 20;
  const walls = [[0, 0, 600, 0], [600, 0, 600, 400], [600, 400, 0, 400], [0, 400, 0, 0], [20, 20, 200, 20], [290, 20, 580, 20], [580, 20, 580, 120], [580, 270, 580, 380], [580, 380, 20, 380], [20, 380, 20, 20]];
  for (const [x1, y1, x2, y2] of walls) c.appendChild(line(ox + x1, oy + 400 - y1, ox + x2, oy + 400 - y2, "ink", 2));
  for (const [x1, y1, x2, y2] of [[200, 0, 200, 20], [290, 0, 290, 20], [200, 20, 200, 110], [580, 120, 600, 120], [580, 270, 600, 270], [590, 120, 590, 270]]) c.appendChild(line(ox + x1, oy + 400 - y1, ox + x2, oy + 400 - y2, "muted", 1));
  for (let i = 0; i < 20; i++) { c.appendChild(line(ox + 0, oy + 400 - i * 20, ox + 20, oy + 400 - i * 20 - 20, "faint", 1)); }
  const arc = figma.createEllipse(); arc.resize(180, 180); arc.x = ox + 200 - 90; arc.y = oy + 400 - 20 - 90; arc.fills = []; arc.strokes = [fill("muted")]; arc.strokeWeight = 1; arc.arcData = {startingAngle: -Math.PI / 2, endingAngle: 0, innerRadius: 0}; c.appendChild(arc);
  // dimensions
  c.appendChild(line(ox, oy + 460, ox + 600, oy + 460, "accent", 1)); c.appendChild(line(ox, oy + 405, ox, oy + 470, "accent", 1)); c.appendChild(line(ox + 600, oy + 405, ox + 600, oy + 470, "accent", 1));
  const d1 = txt("6000", {size: 11, color: "accent"}); d1.x = ox + 285; d1.y = oy + 443; c.appendChild(d1);
  c.appendChild(line(ox + 670, oy, ox + 670, oy + 400, "accent", 1)); const d2 = txt("4000", {size: 11, color: "accent"}); d2.x = ox + 676; d2.y = oy + 190; c.appendChild(d2);
  const t1 = txt("Комната 24 м²", {family: SERIF, size: 16}); t1.x = ox + 240; t1.y = oy + 170; c.appendChild(t1);
  const t2 = txt("Пол: ламинат", {size: 10, color: "muted"}); t2.x = ox + 240; t2.y = oy + 196; c.appendChild(t2);
  if (o.selected) { const s = line(ox + 350, oy + 20, ox + 350, oy + 380, "accent", 2.5); c.appendChild(s); for (const gy of [20, 200, 380]) { const g = figma.createRectangle(); g.resize(7, 7); g.x = ox + 350 - 3.5; g.y = oy + gy - 3.5; g.fills = [fill("accent")]; c.appendChild(g); } }
  if (o.ghost) { const gh = line(ox + 350, oy + 380, ox + 350, oy + 120, "accent", 1.5); gh.dashPattern = [6, 4]; c.appendChild(gh); const tr = line(ox + 350, oy + 120, ox + 350, oy - 40, "snap", 1); tr.dashPattern = [2, 4]; c.appendChild(tr);
    const sm = figma.createRectangle(); sm.resize(12, 12); sm.x = ox + 344; sm.y = oy + 374; sm.fills = []; sm.strokes = [fill("snap")]; sm.strokeWeight = 2; c.appendChild(sm); const lab = al("HORIZONTAL", {pad: [2, 6, 2, 6], radius: 4, fill: "card", stroke: "border"}); lab.x = ox + 362; lab.y = oy + 100; lab.appendChild(txt("Полярное 90° · 2600", {size: 10, color: "snap"})); c.appendChild(lab); }
  if (o.crosshair !== false) { c.appendChild(line(0, oy + 120, w, oy + 120, "crosshair", 1)); c.appendChild(line(ox + 350, 0, ox + 350, h, "crosshair", 1)); }
  return c;
}
function magnetToggle(on, step) {   // «⌁ Сетка 1 мм» — status-bar toggle (F9) with the step menu arrow
  const b = al("HORIZONTAL", {name: "Magnet toggle", gap: 4, pad: [2, 8, 2, 8], radius: 6, fill: on ? "selected" : undefined, stroke: on ? "accent" : undefined, align: "CENTER"});
  b.appendChild(txt("⌁ Сетка " + (step || "1 мм"), {size: 11, color: on ? "accent" : "muted"})); b.appendChild(txt("▾", {size: 9, color: "muted"})); return b;
}
function statusBar(prompt, snap, o = {}) {
  const st = al("HORIZONTAL", {pad: [4, 12, 4, 12], gap: 24, fill: "panel", align: "CENTER"}); fixed(st, 1440, 26); st.strokes = [fill("border")]; st.strokeWeight = 1;
  st.appendChild(txt(o.coords || "X 3500.00   Y 2600.00", {family: MONO, size: 11, color: "muted"})); st.appendChild(txt(prompt, {size: 11, color: "muted"})); const sp = al("HORIZONTAL"); sp.layoutGrow = 1; st.appendChild(sp);
  st.appendChild(magnetToggle(o.magnet !== false, o.step));
  st.appendChild(txt(snap, {size: 11, color: "snap"})); st.appendChild(txt("Миллиметры  ·  сетка 100 мм", {size: 11, color: "muted"})); return st;
}
function inlineEditor(fields, x0, y0) {   // double-click editor over the viewport (Enter — apply, Esc — cancel)
  const e = al("VERTICAL", {name: "Inline editor", gap: 4, pad: [6, 8, 6, 8], radius: 8, fill: "card", stroke: "accent"}); e.x = x0; e.y = y0;
  for (const [l, v, focus] of fields) { const r = al("HORIZONTAL", {gap: 6, align: "CENTER"}); if (fields.length > 1) { const lt = txt(l, {size: 11, color: "muted"}); lt.resize(52, lt.height); r.appendChild(lt); }
    const box = al("HORIZONTAL", {pad: [2, 6, 2, 6], radius: 5, fill: "input", stroke: focus ? "accent" : "border"}); box.resize(90, 24); box.primaryAxisSizingMode = "FIXED"; box.appendChild(txt(v, {family: MONO, size: 12, color: "text"})); r.appendChild(box); e.appendChild(r); }
  if (fields.length > 1) e.appendChild(txt("Enter — применить · Esc — отмена", {size: 9, color: "faint"}));
  return e;
}
function commandBar(prompt, value) {
  const cmd = al("HORIZONTAL", {pad: [6, 12, 6, 12], gap: 10, fill: "panel", align: "CENTER"}); fixed(cmd, 880, 38); cmd.strokes = [fill("border")]; cmd.strokeWeight = 1;
  const p = txt(prompt, {size: 12, color: "muted"}); p.resize(300, p.height); cmd.appendChild(p);
  const cb = al("HORIZONTAL", {pad: [5, 10, 5, 10], radius: 7, fill: "input", stroke: "border"}); cb.layoutGrow = 1; cb.resize(300, 28); cb.appendChild(txt(value, {family: MONO, size: 12, color: value.startsWith("Команда") ? "faint" : "text"})); cmd.appendChild(cb); return cmd;
}
function draftScreen(name, o = {}) {
  const f = al("VERTICAL", {name, gap: 0, fill: "bg"}); fixed(f, 1440, 900);
  f.appendChild(workspaceBar("Черчение"));
  const body = al("HORIZONTAL", {gap: 0}); fixed(body, 1440, 830);
  const left = al("VERTICAL", {gap: 0}); fixed(left, 260, 830); left.appendChild(toolsDock({active: o.activeTool, compact: o.compact, query: o.query, collapsed: o.collapsed})); const td = treeDock(); td.resize(260, 530); left.appendChild(td); body.appendChild(left);
  const mid = al("VERTICAL", {gap: 0}); fixed(mid, 880, 830); const cv = canvas(880, 792, o);
  if (o.editor) { const ed = inlineEditor(o.editor.fields, o.editor.x, o.editor.y); cv.appendChild(ed); ed.layoutPositioning = "ABSOLUTE"; }
  if (o.magnetMarker) { const m = figma.createRectangle(); m.resize(8, 8); m.x = o.magnetMarker[0] - 4; m.y = o.magnetMarker[1] - 4; m.fills = [fill("accent")]; cv.appendChild(m); m.layoutPositioning = "ABSOLUTE";
    const lab = al("HORIZONTAL", {pad: [2, 6, 2, 6], radius: 4, fill: "card", stroke: "border"}); lab.x = o.magnetMarker[0] + 12; lab.y = o.magnetMarker[1] - 24; lab.appendChild(txt("Сетка · 3500, 2600", {size: 10, color: "snap"})); cv.appendChild(lab); lab.layoutPositioning = "ABSOLUTE"; }
  mid.appendChild(cv); mid.appendChild(commandBar(o.prompt || "Выберите инструмент", o.cmd || "Команда или координаты: 100,50 · @40<30 · 250 · l / c / r …")); body.appendChild(mid);
  const right = al("VERTICAL", {gap: 0}); fixed(right, 300, 830); right.appendChild(propsDock(o.selected)); const ld = layersDock(); ld.resize(300, 500); right.appendChild(ld); body.appendChild(right);
  f.appendChild(body); f.appendChild(statusBar(o.status || "Выбор", o.snap || "привязка")); return f;
}
function welcomeScreen(name) {
  const f = al("VERTICAL", {name, gap: 0, fill: "bg"}); fixed(f, 1440, 900); f.appendChild(workspaceBar(""));
  const body = al("HORIZONTAL", {gap: 0}); fixed(body, 1440, 830); body.primaryAxisAlignItems = "CENTER"; body.counterAxisAlignItems = "CENTER";
  const wc = al("VERTICAL", {pad: 40, gap: 14, fill: "card", stroke: "border", radius: 16, align: "MIN"}); fixed(wc, 560, 520);
  const wi = al("HORIZONTAL", {radius: 18, fill: "accent", pad: [6, 20, 12, 20]}); wi.appendChild(txt("Ψ", {family: SERIF, size: 52, color: "on-accent"})); wc.appendChild(wi);
  wc.appendChild(txt("Пси", {family: SERIF, size: 30})); wc.appendChild(txt("Универсальная CAD-система. Этап 00–01: черчение, листы, PDF, DXF.", {size: 13, color: "muted", w: 480}));
  const wr = al("HORIZONTAL", {gap: 10}); wr.appendChild(btn("Новый проект", true)); for (const t of ["Открыть…", "Импорт DXF…", "Демонстрация"]) wr.appendChild(btn(t, false)); wc.appendChild(wr);
  const sp = al("HORIZONTAL"); sp.resize(10, 8); wc.appendChild(sp); wc.appendChild(txt("Недавние", {family: SERIF, size: 17}));
  for (const [n, p] of [["План комнаты.psi", "~/Documents/Пси"], ["Кронштейн.dxf", "~/Downloads"], ["Шкаф — развёртка.psi", "~/Documents/Пси/Мебель"]]) { const r = al("HORIZONTAL", {gap: 10, pad: [6, 4, 6, 4]}); r.appendChild(txt("▱", {size: 12, color: "muted"})); r.appendChild(txt(n, {size: 13})); r.appendChild(txt(p, {size: 12, color: "faint"})); wc.appendChild(r); }
  const sp2 = al("VERTICAL"); sp2.layoutGrow = 1; wc.appendChild(sp2);
  wc.appendChild(txt("Файлы .psi и .dxf можно перетащить в окно. Ввод координат: «100,50», «@40<30», длина вдоль курсора.", {size: 11, color: "faint", w: 480}));
  body.appendChild(wc); f.appendChild(body); f.appendChild(statusBar("", "")); return f;
}
function sheetsScreen(name) {
  const f = al("VERTICAL", {name, gap: 0, fill: "bg"}); fixed(f, 1440, 900); f.appendChild(workspaceBar("Листы"));
  const body = al("HORIZONTAL", {gap: 0}); fixed(body, 1440, 830);
  const left = al("VERTICAL", {pad: 10, gap: 6, fill: "panel", align: "MIN"}); fixed(left, 220, 830); left.strokes = [fill("border")]; left.strokeWeight = 1;
  left.appendChild(txt("Листы", {family: SERIF, size: 15}));
  for (const [n, p, on] of [["План", "A3 · альбомный", true], ["Разрез 1-1", "A4", false]]) { const r = al("VERTICAL", {gap: 2, pad: [6, 8, 6, 8], radius: 7, fill: on ? "selected" : undefined}); fixed(r, 200, 44); r.appendChild(txt(n, {size: 13})); r.appendChild(txt(p, {size: 11, color: "muted"})); left.appendChild(r); }
  const lsp = al("VERTICAL"); lsp.layoutGrow = 1; left.appendChild(lsp); const lb = al("HORIZONTAL", {gap: 4}); for (const g of ["+", "−", "⧉"]) lb.appendChild(toolBtn(g)); left.appendChild(lb);
  left.appendChild(btn("Экспорт PDF…", true)); left.appendChild(btn("Только этот лист…", false)); body.appendChild(left);
  const mid = figma.createFrame(); mid.resize(890, 830); mid.fills = [fill("bg")]; mid.clipsContent = true;
  const paper = figma.createFrame(); paper.resize(760, 537); paper.x = 65; paper.y = 146; paper.fills = [fill("paper")]; paper.effects = [{type: "DROP_SHADOW", color: {r: 0, g: 0, b: 0, a: 0.18}, offset: {x: 3, y: 3}, radius: 8, visible: true, blendMode: "NORMAL"}]; mid.appendChild(paper);
  const k = 760 / 420;
  const fr = figma.createRectangle(); fr.resize((420 - 25) * k, (297 - 10) * k); fr.x = 20 * k; fr.y = 5 * k; fr.fills = []; fr.strokes = [fill("ink")]; fr.strokeWeight = 1.6; paper.appendChild(fr);
  const tb = figma.createRectangle(); tb.resize(185 * k, 55 * k); tb.x = (420 - 5 - 185) * k; tb.y = (297 - 5 - 55) * k; tb.fills = []; tb.strokes = [fill("ink")]; tb.strokeWeight = 1.2; paper.appendChild(tb);
  const tx0 = (420 - 5 - 185) * k, ty0 = (297 - 5 - 55) * k;
  for (const [x1, y1, x2, y2] of [[0, 15, 185, 15], [0, 30, 185, 30], [0, 40, 65, 40], [0, 45, 65, 45], [0, 50, 65, 50], [65, 0, 65, 55], [135, 0, 135, 55], [135, 20, 185, 20], [135, 35, 185, 35], [160, 35, 160, 55]]) paper.appendChild(line(tx0 + x1 * k, ty0 + y1 * k, tx0 + x2 * k, ty0 + y2 * k, "ink", 0.8));
  const tbt = txt("ПСИ.ДЕМО.001", {size: 12}); tbt.x = tx0 + 95 * k; tbt.y = ty0 + 3 * k; paper.appendChild(tbt); const tbn = txt("План комнаты", {size: 11}); tbn.x = tx0 + 80 * k; tbn.y = ty0 + 20 * k; paper.appendChild(tbn); const tbs = txt("1:50", {size: 10}); tbs.x = tx0 + 165 * k; tbs.y = ty0 + 25 * k; paper.appendChild(tbs);
  const vpF = figma.createRectangle(); vpF.resize(370 * k, 215 * k); vpF.x = 30 * k; vpF.y = (297 - 285) * k; vpF.fills = []; vpF.strokes = [fill("accent")]; vpF.strokeWeight = 1; vpF.dashPattern = [5, 4]; paper.appendChild(vpF);
  const vpl = txt("Вид 1 · 1:50", {size: 10, color: "accent"}); vpl.x = 32 * k; vpl.y = (297 - 285) * k - 16; paper.appendChild(vpl);
  const rx = 215 * k - 60, ry = 150 * k - 40;
  for (const [x1, y1, x2, y2] of [[0, 0, 120, 0], [120, 0, 120, 80], [120, 80, 0, 80], [0, 80, 0, 0], [4, 4, 40, 4], [58, 4, 116, 4], [116, 4, 116, 24], [116, 54, 116, 76], [116, 76, 4, 76], [4, 76, 4, 4]]) paper.appendChild(line(rx + x1 * k, ry + (80 - y1) * k, rx + x2 * k, ry + (80 - y2) * k, "ink", 1.6));
  paper.appendChild(line(rx, ry + 80 * k + 20, rx + 120 * k, ry + 80 * k + 20, "accent", 0.8)); const dd = txt("6000", {size: 9, color: "accent"}); dd.x = rx + 60 * k - 10; dd.y = ry + 80 * k + 6; paper.appendChild(dd);
  const rt = txt("Комната 24 м²", {family: SERIF, size: 9}); rt.x = rx + 40 * k; rt.y = ry + 30 * k; paper.appendChild(rt);
  body.appendChild(mid);
  const right = al("VERTICAL", {pad: 12, gap: 8, fill: "panel", align: "MIN"}); fixed(right, 330, 830); right.strokes = [fill("border")]; right.strokeWeight = 1;
  right.appendChild(txt("Лист", {family: SERIF, size: 15}));
  for (const [l, v] of [["Название", "План"], ["Формат", "A3 — 297×420 мм"], ["Поля Л/В/П/Н", "20 · 5 · 5 · 5"]]) right.appendChild(field(l, v, 190));
  for (const [t, on] of [["Альбомная ориентация", true], ["Рамка", true], ["Основная надпись (ГОСТ 185×55)", true]]) { const r = al("HORIZONTAL", {gap: 6, align: "CENTER"}); const b = figma.createRectangle(); b.resize(14, 14); b.cornerRadius = 4; b.fills = [fill(on ? "accent" : "input")]; b.strokes = [fill("border")]; r.appendChild(b); r.appendChild(txt(t, {size: 12})); right.appendChild(r); }
  for (const [l, v] of [["Наименование", "План комнаты"], ["Обозначение", "ПСИ.ДЕМО.001"], ["Материал", ""], ["Масштаб", "1:50"], ["Лист", "1"], ["Листов", "1"], ["Разработал", "Пси"], ["Дата", "2026-10-27"]]) right.appendChild(field(l, v, 190));
  right.appendChild(txt("Виды", {family: SERIF, size: 15}));
  const vr = al("HORIZONTAL", {gap: 6, align: "CENTER"}); const vbox = al("HORIZONTAL", {pad: [5, 9, 5, 9], radius: 7, fill: "input", stroke: "border"}); vbox.resize(220, 28); vbox.primaryAxisSizingMode = "FIXED"; vbox.appendChild(txt("Вид 1 · 1:50", {size: 12})); vr.appendChild(vbox); vr.appendChild(toolBtn("+")); vr.appendChild(toolBtn("−")); right.appendChild(vr);
  for (const [l, v] of [["Положение X/Y", "30 · 70"], ["Размер Ш/В", "370 · 215"], ["Масштаб", "1:50"], ["Центр модели", "3200 · 1700"]]) right.appendChild(field(l, v, 190));
  right.appendChild(btn("Вписать весь чертёж", false)); right.appendChild(txt("Содержимое помещается в вид.", {size: 11, color: "muted"}));
  body.appendChild(right); f.appendChild(body); f.appendChild(statusBar("Листы", "")); return f;
}
function dialog(name, title, rows, w = 460, extra) {
  const f = al("VERTICAL", {name, pad: 24, gap: 10, fill: "bg", stroke: "border", radius: 14, align: "MIN"}); f.resize(w, 10); f.counterAxisSizingMode = "FIXED";
  f.appendChild(txt(title, {family: SERIF, size: 18})); for (const [l, v] of rows) f.appendChild(field(l, v, 240)); if (extra) extra(f);
  const r = al("HORIZONTAL", {gap: 8}); r.resize(w - 48, 32); r.primaryAxisSizingMode = "FIXED"; const sp = al("HORIZONTAL"); sp.layoutGrow = 1; r.appendChild(sp); r.appendChild(btn("Отмена", false)); r.appendChild(btn("Готово", true)); f.appendChild(r); return f;
}

const sheetsList = [];
let x = 0, y = 0;
function place(node, w) { node.x = x; node.y = y; x += (w || node.width) + 120; sheetsList.push(node); }
function row() { x = 0; y += 1020; }

MODE = "Light";
place(welcomeScreen("Начальный экран — Light"));
MODE = "Dark"; place(welcomeScreen("Начальный экран — Dark")); MODE = "Light";
row();
place(draftScreen("Черчение — Light", {selected: false}));
MODE = "Dark"; place(draftScreen("Черчение — Dark", {selected: false})); MODE = "Light";
row();
place(draftScreen("Черчение — объект выбран", {selected: true, status: "Выбрано: 1"}));
place(draftScreen("Черчение — инструмент «Линия»", {activeTool: "Линия", ghost: true, prompt: "Следующая точка (Enter — завершить, «c» — замкнуть):", cmd: "@2600<90", status: "Линия", snap: "Полярное 90°"}));
row();
place(draftScreen("Черчение — магнит к сетке", {activeTool: "Линия", crosshair: true, magnetMarker: [350 + 260 - 260, 120], snap: "Сетка", status: "Линия", coords: "X 3500.00   Y 2600.00", step: "10 мм", prompt: "Первая точка:"}));
place(draftScreen("Черчение — правка по двойному щелчку", {selected: true, status: "Выбрано: 1", editor: {x: 300, y: 405, fields: [["Размер", "1000", true]]}}));
row();
place(draftScreen("Черчение — палитра: поиск", {query: "раз", status: "Выбор"}));
place(draftScreen("Черчение — палитра: компактный режим", {compact: true, collapsed: ["Штриховка"], activeTool: "Объединить", status: "Объединить", prompt: "Укажите второй объект (Shift — оставить исходные):"}));
row();
place(sheetsScreen("Листы — Light"));
MODE = "Dark"; place(sheetsScreen("Листы — Dark")); MODE = "Light";
row();
const dlgRow = al("HORIZONTAL", {name: "Диалоги", gap: 60, align: "MIN"}); dlgRow.x = 0; dlgRow.y = y; sheetsList.push(dlgRow);
dlgRow.appendChild(dialog("Параметры документа", "Параметры документа", [["Единицы отображения", "Миллиметры"], ["Знаков после запятой", "2"], ["Шаг сетки", "10 мм"], ["Шаг привязки, мм", "1 ▾"], ["Шаг полярного угла", "45 °"], ["Высота текста", "3.5 мм"], ["Автор", ""]], 460, f => {
  const g = al("HORIZONTAL", {gap: 8}); g.layoutWrap = "WRAP"; g.counterAxisSpacing = 6; g.resize(400, 10); g.primaryAxisSizingMode = "FIXED";
  for (const [t, on] of [["Конечная точка", true], ["Середина", true], ["Центр", true], ["Квадрант", true], ["Пересечение", true], ["Перпендикуляр", true], ["Касательная", true], ["Сетка", false], ["Магнит к круглым координатам (F9)", true]]) { const r = al("HORIZONTAL", {gap: 6, align: "CENTER"}); const b = figma.createRectangle(); b.resize(14, 14); b.cornerRadius = 4; b.fills = [fill(on ? "accent" : "input")]; b.strokes = [fill("border")]; r.appendChild(b); r.appendChild(txt(t, {size: 12})); g.appendChild(r); }
  f.appendChild(txt("Режимы привязки", {size: 12, color: "muted"})); f.appendChild(g);
}));
dlgRow.appendChild(dialog("Настройки", "Настройки", [["Тема", "Как в системе"], ["Акцент", "#D97757  Терракота"], ["Автосохранение", "5 мин"]], 560, f => {
  f.appendChild(txt("Горячие клавиши", {family: SERIF, size: 15}));
  const t = al("VERTICAL", {gap: 0, fill: "card", stroke: "border", radius: 8}); fixed(t, 512, 200);
  for (const [a, k] of [["Линия", "L"], ["Окружность", "C"], ["Прямоугольник", "R"], ["Размер", "D"], ["Переместить", "M"], ["Показать всё", "⌘⇧E"], ["Экспорт PDF…", "⌘P"]]) { const r = al("HORIZONTAL", {gap: 8, pad: [5, 10, 5, 10]}); fixed(r, 512, 28); const n = txt(a, {size: 12}); n.layoutGrow = 1; r.appendChild(n); const kb = al("HORIZONTAL", {pad: [2, 8, 2, 8], radius: 5, fill: "input", stroke: "border"}); kb.appendChild(txt(k, {family: MONO, size: 11})); r.appendChild(kb); t.appendChild(r); }
  f.appendChild(t); f.appendChild(txt("Тема и акцент применяются после перезапуска. Пустое поле — без сочетания.", {size: 11, color: "faint"}));
}));
dlgRow.appendChild(dialog("Слой", "Слой", [["Имя", "Стены"], ["Цвет", "#141413"], ["Толщина", "0.50 мм"], ["Тип линии", "Сплошная"]], 380, f => { for (const [t, on] of [["Видимый", true], ["Заблокирован", false], ["Печатается", true]]) { const r = al("HORIZONTAL", {gap: 6, align: "CENTER"}); const b = figma.createRectangle(); b.resize(14, 14); b.cornerRadius = 4; b.fills = [fill(on ? "accent" : "input")]; b.strokes = [fill("border")]; r.appendChild(b); r.appendChild(txt(t, {size: 12})); f.appendChild(r); } }));
const imp = al("VERTICAL", {name: "Импорт DXF — отчёт", pad: 24, gap: 10, fill: "bg", stroke: "border", radius: 14, align: "MIN"}); imp.resize(460, 10); imp.counterAxisSizingMode = "FIXED";
imp.appendChild(txt("Импорт DXF", {family: SERIF, size: 18}));
imp.appendChild(txt("Импортировано 128 объектов на 6 слоёв.\nЕдиницы: миллиметры (из файла).\nLINE 84 · LWPOLYLINE 22 · CIRCLE 9 · ARC 7 · TEXT 6\nПропущено: INSERT (3), 3DFACE (1) — блоки и 3D не поддерживаются на этапе 01.", {size: 12, w: 412}));
const ir = al("HORIZONTAL", {gap: 8}); ir.resize(412, 32); ir.primaryAxisSizingMode = "FIXED"; const isp = al("HORIZONTAL"); isp.layoutGrow = 1; ir.appendChild(isp); ir.appendChild(btn("Закрыть", true)); imp.appendChild(ir); dlgRow.appendChild(imp);
const pdfD = al("VERTICAL", {name: "Экспорт PDF — предупреждение", pad: 24, gap: 10, fill: "bg", stroke: "border", radius: 14, align: "MIN"}); pdfD.resize(460, 10); pdfD.counterAxisSizingMode = "FIXED";
pdfD.appendChild(txt("Экспорт PDF", {family: SERIF, size: 18})); pdfD.appendChild(txt("Часть содержимого будет обрезана:\nРазрез 1-1: чертёж не помещается в видовой экран 1 (масштаб 1:20) — часть будет обрезана\n\nПродолжить?", {size: 12, w: 412}));
const pr = al("HORIZONTAL", {gap: 8}); pr.resize(412, 32); pr.primaryAxisSizingMode = "FIXED"; const psp = al("HORIZONTAL"); psp.layoutGrow = 1; pr.appendChild(psp); pr.appendChild(btn("Нет", false)); pr.appendChild(btn("Да", true)); pdfD.appendChild(pr); dlgRow.appendChild(pdfD);

for (const s of sheetsList) if (s.parent !== page) page.appendChild(s);
await figma.viewport.scrollAndZoomIntoView(sheetsList);
return {screens: sheetsList.map(s => s.name)};
