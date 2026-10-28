#include "core/document/document.h"
#include <QJsonArray>
#include <algorithm>

namespace psi {

QJsonObject Layer::toJson() const {
    QJsonObject o;
    o["id"] = static_cast<qint64>(id); o["name"] = name; o["color"] = color; o["lw"] = lineWeight; o["lt"] = lineTypeKey(lineType);
    o["visible"] = visible; o["locked"] = locked; o["printable"] = printable;
    return o;
}
Layer Layer::fromJson(const QJsonObject& o) {
    Layer l;
    l.id = static_cast<quint32>(o.value("id").toInteger(0)); l.name = o.value("name").toString("0"); l.color = o.value("color").toInt(0x141413);
    l.lineWeight = o.value("lw").toDouble(0.25); l.lineType = lineTypeFromKey(o.value("lt").toString("solid"));
    l.visible = o.value("visible").toBool(true); l.locked = o.value("locked").toBool(false); l.printable = o.value("printable").toBool(true);
    return l;
}

QJsonObject DocumentSettings::toJson() const {
    QJsonObject o;
    o["unit"] = Units::key(unit); o["precision"] = precision; o["grid"] = gridSpacing; o["gridVisible"] = gridVisible;
    o["snap"] = snapEnabled; o["gridSnap"] = gridSnap; o["snapStep"] = snapStep; o["snapFlags"] = static_cast<int>(snapFlags); o["ortho"] = ortho; o["polar"] = polar; o["polarStep"] = polarStep;
    o["textHeight"] = textHeight; o["tolerance"] = tolerance; o["author"] = author;
    return o;
}
DocumentSettings DocumentSettings::fromJson(const QJsonObject& o) {
    DocumentSettings s;
    s.unit = Units::fromKey(o.value("unit").toString("mm")).value_or(Unit::Millimeter);
    s.precision = o.value("precision").toInt(2); s.gridSpacing = o.value("grid").toDouble(10); s.gridVisible = o.value("gridVisible").toBool(true);
    s.snapEnabled = o.value("snap").toBool(true); s.gridSnap = o.value("gridSnap").toBool(true); s.snapStep = o.value("snapStep").toDouble(1); s.snapFlags = static_cast<unsigned>(o.value("snapFlags").toInt(SnapAll & ~SnapGrid));
    s.ortho = o.value("ortho").toBool(false); s.polar = o.value("polar").toBool(true); s.polarStep = o.value("polarStep").toDouble(45);
    s.textHeight = o.value("textHeight").toDouble(3.5); s.tolerance = o.value("tolerance").toDouble(1e-6); s.author = o.value("author").toString();
    return s;
}

Document::Document(QObject* parent) : QObject(parent) { reset(); }

void Document::reset() {
    entities_.clear(); order_.clear(); index_.clear(); layers_.clear(); sheets_.clear();
    settings_ = DocumentSettings{};
    Layer zero; zero.id = 0; zero.name = "0";
    layers_.push_back(zero);
    nextLayerId_ = 1; currentLayer_ = 0; nextId_ = 1;
    Sheet s; s.id = reserveId(); s.title = TitleBlock::standard();
    sheets_.push_back(s);
    touch();
    Q_EMIT documentReset();
}

quint64 Document::addEntity(Entity e) {
    if (e.id == 0) e.id = reserveId();
    else if (e.id >= nextId_) nextId_ = e.id + 1;
    if (!layer(e.layer)) e.layer = 0;
    const quint64 id = e.id;
    index_.insert(id, entityBBox(e));
    auto it = entities_.find(id);
    if (it == entities_.end()) order_.push_back(id);
    entities_[id] = std::move(e);
    touch();
    Q_EMIT entitiesChanged({id});
    return id;
}

bool Document::removeEntity(quint64 id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return false;
    entities_.erase(it);
    order_.erase(std::remove(order_.begin(), order_.end(), id), order_.end());
    index_.remove(id);
    touch();
    Q_EMIT entitiesChanged({id});
    return true;
}

bool Document::replaceEntity(const Entity& e) {
    auto it = entities_.find(e.id);
    if (it == entities_.end()) return false;
    it->second = e;
    if (!layer(it->second.layer)) it->second.layer = 0;
    index_.update(e.id, entityBBox(e));
    touch();
    Q_EMIT entitiesChanged({e.id});
    return true;
}

const Entity* Document::entity(quint64 id) const {
    auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : &it->second;
}

Rect Document::extents() const {
    Rect r;
    for (const auto& [id, e] : entities_) if (entityVisible(e)) r.add(index_.bbox(id));
    return r;
}

const Layer* Document::layer(quint32 id) const {
    for (const Layer& l : layers_) if (l.id == id) return &l;
    return nullptr;
}
Layer* Document::layerMut(quint32 id) {
    for (Layer& l : layers_) if (l.id == id) return &l;
    return nullptr;
}

quint32 Document::addLayer(Layer l) {
    if (l.id == 0 || layer(l.id)) l.id = nextLayerId_++;
    else if (l.id >= nextLayerId_) nextLayerId_ = l.id + 1;
    layers_.push_back(l);
    touch();
    Q_EMIT layersChanged();
    return l.id;
}

bool Document::layerInUse(quint32 id) const {
    for (const auto& [eid, e] : entities_) if (e.layer == id) return true;
    return false;
}

bool Document::removeLayer(quint32 id) {
    if (id == 0 || layerInUse(id)) return false;
    auto it = std::find_if(layers_.begin(), layers_.end(), [&](const Layer& l) { return l.id == id; });
    if (it == layers_.end()) return false;
    layers_.erase(it);
    if (currentLayer_ == id) currentLayer_ = 0;
    touch();
    Q_EMIT layersChanged();
    return true;
}

bool Document::updateLayer(const Layer& l) {
    Layer* cur = layerMut(l.id);
    if (!cur) return false;
    *cur = l;
    touch();
    Q_EMIT layersChanged();
    return true;
}

void Document::setCurrentLayer(quint32 id) {
    if (!layer(id)) return;
    currentLayer_ = id;
    Q_EMIT layersChanged();
}

const Sheet* Document::sheet(quint64 id) const {
    for (const Sheet& s : sheets_) if (s.id == id) return &s;
    return nullptr;
}

quint64 Document::addSheet(Sheet s) {
    if (s.id == 0 || sheet(s.id)) s.id = reserveId();
    else if (s.id >= nextId_) nextId_ = s.id + 1;
    for (auto& v : s.viewports) if (v.id == 0) v.id = reserveId();
    sheets_.push_back(s);
    touch();
    Q_EMIT sheetsChanged();
    return s.id;
}

bool Document::removeSheet(quint64 id) {
    auto it = std::find_if(sheets_.begin(), sheets_.end(), [&](const Sheet& s) { return s.id == id; });
    if (it == sheets_.end()) return false;
    sheets_.erase(it);
    touch();
    Q_EMIT sheetsChanged();
    return true;
}

bool Document::updateSheet(const Sheet& s) {
    for (Sheet& cur : sheets_) {
        if (cur.id == s.id) {
            cur = s;
            for (auto& v : cur.viewports) if (v.id == 0) v.id = reserveId();
            touch();
            Q_EMIT sheetsChanged();
            return true;
        }
    }
    return false;
}

void Document::setSettings(const DocumentSettings& s) { settings_ = s; touch(); Q_EMIT settingsChanged(); }

int Document::effectiveColor(const Entity& e) const {
    if (e.color != kColorByLayer) return e.color;
    const Layer* l = layer(e.layer);
    return l ? l->color : 0x141413;
}
double Document::effectiveLineWeight(const Entity& e) const {
    if (e.lineWeight >= 0) return e.lineWeight;
    const Layer* l = layer(e.layer);
    return l ? l->lineWeight : 0.25;
}
LineType Document::effectiveLineType(const Entity& e) const {
    if (e.lineType != LineType::ByLayer) return e.lineType;
    const Layer* l = layer(e.layer);
    return l ? l->lineType : LineType::Solid;
}
bool Document::entityVisible(const Entity& e) const {
    if (!e.visible) return false;
    const Layer* l = layer(e.layer);
    return !l || l->visible;
}

QJsonObject Document::drawingToJson() const {
    QJsonObject o;
    QJsonArray layers, ents;
    for (const Layer& l : layers_) layers.push_back(l.toJson());
    for (quint64 id : order_) ents.push_back(entityToJson(entities_.at(id)));
    o["layers"] = layers;
    o["currentLayer"] = static_cast<qint64>(currentLayer_);
    o["entities"] = ents;
    o["nextId"] = QString::number(nextId_);
    return o;
}

void Document::drawingFromJson(const QJsonObject& o) {
    entities_.clear(); order_.clear(); index_.clear(); layers_.clear();
    for (const auto& l : o.value("layers").toArray()) {
        Layer lay = Layer::fromJson(l.toObject());
        layers_.push_back(lay);
        if (lay.id >= nextLayerId_) nextLayerId_ = lay.id + 1;
    }
    if (!layer(0)) { Layer zero; zero.id = 0; layers_.insert(layers_.begin(), zero); }
    currentLayer_ = static_cast<quint32>(o.value("currentLayer").toInteger(0));
    if (!layer(currentLayer_)) currentLayer_ = 0;
    for (const auto& ej : o.value("entities").toArray()) {
        Entity e = entityFromJson(ej.toObject());
        if (e.id == 0) e.id = reserveId();
        if (e.id >= nextId_) nextId_ = e.id + 1;
        if (!layer(e.layer)) e.layer = 0;
        index_.insert(e.id, entityBBox(e));
        order_.push_back(e.id);
        entities_[e.id] = std::move(e);
    }
    quint64 hint = o.value("nextId").toString().toULongLong();
    if (hint > nextId_) nextId_ = hint;
    touch();
    Q_EMIT documentReset();
}

QJsonObject Document::sheetsToJson() const {
    QJsonObject o;
    QJsonArray arr;
    for (const Sheet& s : sheets_) arr.push_back(s.toJson());
    o["sheets"] = arr;
    return o;
}

void Document::sheetsFromJson(const QJsonObject& o) {
    sheets_.clear();
    for (const auto& s : o.value("sheets").toArray()) {
        Sheet sh = Sheet::fromJson(s.toObject());
        if (sh.id == 0) sh.id = reserveId();
        if (sh.id >= nextId_) nextId_ = sh.id + 1;
        for (auto& v : sh.viewports) { if (v.id == 0) v.id = reserveId(); if (v.id >= nextId_) nextId_ = v.id + 1; }
        sheets_.push_back(sh);
    }
    if (sheets_.empty()) { Sheet s; s.id = reserveId(); s.title = TitleBlock::standard(); sheets_.push_back(s); }
    touch();
    Q_EMIT sheetsChanged();
}

} // namespace psi
