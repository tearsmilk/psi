#pragma once
#include "core/document/layer.h"
#include "core/document/settings.h"
#include "core/document/spatial_index.h"
#include "core/geometry/entity.h"
#include "modules/drawing/sheet.h"
#include <QJsonObject>
#include <QObject>
#include <unordered_map>
#include <vector>

namespace psi {

// The 2D drawing document: entities, layers, sheets, settings. All mutations go through the command stack.
class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject* parent = nullptr);

    void reset();                                   // empty document with layer «0» and one A4 sheet

    // ids
    quint64 reserveId() { return nextId_++; }
    quint64 nextIdHint() const { return nextId_; }

    // entities
    quint64 addEntity(Entity e);                    // assigns an id when e.id == 0
    bool removeEntity(quint64 id);
    bool replaceEntity(const Entity& e);            // by e.id
    const Entity* entity(quint64 id) const;
    std::vector<quint64> entityIds() const { return order_; }
    size_t entityCount() const { return entities_.size(); }
    std::vector<quint64> query(const Rect& r) const { return index_.query(r); }
    Rect extents() const;
    EntityLookup lookup() const { return [this](quint64 id) { return entity(id); }; }

    // layers
    const std::vector<Layer>& layers() const { return layers_; }
    const Layer* layer(quint32 id) const;
    Layer* layerMut(quint32 id);
    quint32 addLayer(Layer l);                      // assigns an id when l.id == 0
    bool removeLayer(quint32 id);                   // fails for «0» and layers in use
    bool updateLayer(const Layer& l);
    quint32 currentLayer() const { return currentLayer_; }
    void setCurrentLayer(quint32 id);
    bool layerInUse(quint32 id) const;

    // sheets
    const std::vector<Sheet>& sheets() const { return sheets_; }
    const Sheet* sheet(quint64 id) const;
    quint64 addSheet(Sheet s);
    bool removeSheet(quint64 id);
    bool updateSheet(const Sheet& s);

    // settings
    const DocumentSettings& settings() const { return settings_; }
    void setSettings(const DocumentSettings& s);

    // effective style of an entity (resolving ByLayer)
    int effectiveColor(const Entity& e) const;
    double effectiveLineWeight(const Entity& e) const;
    LineType effectiveLineType(const Entity& e) const;
    bool entityVisible(const Entity& e) const;

    // whole-document (de)serialization used by the .psi format
    QJsonObject drawingToJson() const;              // entities + layers
    void drawingFromJson(const QJsonObject& o);
    QJsonObject sheetsToJson() const;
    void sheetsFromJson(const QJsonObject& o);

    quint64 revision() const { return revision_; }

Q_SIGNALS:
    void entitiesChanged(const std::vector<quint64>& ids);
    void layersChanged();
    void sheetsChanged();
    void settingsChanged();
    void documentReset();

private:
    void touch() { ++revision_; }

    std::unordered_map<quint64, Entity> entities_;
    std::vector<quint64> order_;
    SpatialIndex index_;
    std::vector<Layer> layers_;
    std::vector<Sheet> sheets_;
    DocumentSettings settings_;
    quint32 currentLayer_ = 0;
    quint32 nextLayerId_ = 1;
    quint64 nextId_ = 1;
    quint64 revision_ = 0;
};

} // namespace psi
