#include "core/command-system/command.h"
#include "core/document/document.h"
#include "core/document/spatial_index.h"
#include <QSignalSpy>
#include <QtTest>

using namespace psi;

class TestDocument : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void freshDocument() {
        Document d;
        QCOMPARE(d.entityCount(), size_t(0));
        QCOMPARE(d.layers().size(), size_t(1));
        QCOMPARE(d.layers()[0].name, QStringLiteral("0"));
        QCOMPARE(d.sheets().size(), size_t(1));
        QCOMPARE(d.settings().unit, Unit::Millimeter);
    }
    void addQueryRemove() {
        Document d;
        QSignalSpy spy(&d, &Document::entitiesChanged);
        quint64 a = d.addEntity(makeLine({0, 0}, {10, 0}));
        quint64 b = d.addEntity(makeCircle({100, 100}, 5));
        QVERIFY(a != b);
        QCOMPARE(d.entityCount(), size_t(2));
        QCOMPARE(spy.count(), 2);
        auto near = d.query(Rect(-1, -1, 11, 1));
        QCOMPARE(near.size(), size_t(1));
        QCOMPARE(near[0], a);
        auto far = d.query(Rect(90, 90, 110, 110));
        QCOMPARE(far.size(), size_t(1));
        QCOMPARE(far[0], b);
        Entity moved = *d.entity(a); moved.pts = {{200, 200}, {210, 200}};
        QVERIFY(d.replaceEntity(moved));
        QVERIFY(d.query(Rect(-1, -1, 11, 1)).empty());
        QCOMPARE(d.query(Rect(199, 199, 211, 201)).size(), size_t(1));
        QVERIFY(d.removeEntity(a));
        QVERIFY(!d.removeEntity(a));
        QCOMPARE(d.entityCount(), size_t(1));
        Rect ext = d.extents();
        QVERIFY(std::abs(ext.x0 - 95) < 1e-9 && std::abs(ext.x1 - 105) < 1e-9);
    }
    void spatialIndexLarge() {
        SpatialIndex idx(100);
        for (quint64 i = 1; i <= 10000; ++i) { double x = double(i % 100) * 50, y = double(i / 100) * 50; idx.insert(i, Rect(x, y, x + 10, y + 10)); }
        auto r = idx.query(Rect(0, 0, 120, 120));
        QCOMPARE(r.size(), size_t(8));   // (0,0) belongs to i = 0, which is not inserted
        idx.insert(99999, Rect(-1e6, -1e6, 1e6, 1e6));   // huge entity → big bucket
        QVERIFY(std::find(idx.query(Rect(5000, 5000, 5001, 5001)).begin(), idx.query(Rect(5000, 5000, 5001, 5001)).end(), 99999) != idx.query(Rect(5000, 5000, 5001, 5001)).end());
        idx.remove(99999);
        QVERIFY(idx.query(Rect(5000, 5000, 5001, 5001)).empty());
    }
    void layers() {
        Document d;
        Layer l; l.name = QStringLiteral("Стены"); l.color = 0xFF0000;
        quint32 id = d.addLayer(l);
        QVERIFY(id != 0);
        QCOMPARE(d.layers().size(), size_t(2));
        Entity e = makeLine({0, 0}, {1, 1}); e.layer = id;
        quint64 eid = d.addEntity(e);
        QCOMPARE(d.effectiveColor(*d.entity(eid)), 0xFF0000);
        QVERIFY(d.layerInUse(id));
        QVERIFY(!d.removeLayer(id));        // in use
        QVERIFY(!d.removeLayer(0));         // layer 0 is permanent
        Layer upd = *d.layer(id); upd.visible = false;
        QVERIFY(d.updateLayer(upd));
        QVERIFY(!d.entityVisible(*d.entity(eid)));
        d.removeEntity(eid);
        QVERIFY(d.removeLayer(id));
    }
    void commandsUndoRedo() {
        Document d;
        CommandStack stack(d);
        auto c = std::make_unique<EntityCommand>(QStringLiteral("Линии"));
        c->add(makeLine({0, 0}, {1, 0}));
        c->add(makeLine({0, 1}, {1, 1}));
        stack.push(std::move(c));
        QCOMPARE(d.entityCount(), size_t(2));
        QVERIFY(stack.canUndo());
        QCOMPARE(stack.undoText(), QStringLiteral("Линии"));
        stack.undo();
        QCOMPARE(d.entityCount(), size_t(0));
        QVERIFY(stack.canRedo());
        stack.redo();
        QCOMPARE(d.entityCount(), size_t(2));
        // ids are stable across undo/redo
        auto ids = d.entityIds();
        stack.undo(); stack.redo();
        QCOMPARE(d.entityIds(), ids);
        // modify
        const Entity* e = d.entity(ids[0]);
        Entity after = *e; after.pts[1] = {5, 0};
        auto m = std::make_unique<EntityCommand>(QStringLiteral("Растянуть"));
        m->modify(*e, after);
        stack.push(std::move(m));
        QCOMPARE(d.entity(ids[0])->pts[1].x, 5.0);
        stack.undo();
        QCOMPARE(d.entity(ids[0])->pts[1].x, 1.0);
        // clean state
        stack.setClean();
        QVERIFY(stack.isClean());
        stack.redo();
        QVERIFY(!stack.isClean());
        stack.undo();
        QVERIFY(stack.isClean());
    }
    void macroAndLimit() {
        Document d;
        CommandStack stack(d);
        stack.beginMacro(QStringLiteral("Группа"));
        for (int i = 0; i < 5; ++i) { auto c = std::make_unique<EntityCommand>("x"); c->add(makeCircle({double(i), 0}, 1)); stack.push(std::move(c)); }
        stack.endMacro();
        QCOMPARE(stack.count(), size_t(1));
        QCOMPARE(d.entityCount(), size_t(5));
        stack.undo();
        QCOMPARE(d.entityCount(), size_t(0));
        stack.redo();
        stack.setLimit(3, 1u << 30);
        for (int i = 0; i < 10; ++i) { auto c = std::make_unique<EntityCommand>("y"); c->add(makeLine({0, 0}, {1, 1})); stack.push(std::move(c)); }
        QCOMPARE(stack.count(), size_t(3));
    }
    void layerAndSheetCommands() {
        Document d;
        CommandStack stack(d);
        Layer l; l.name = QStringLiteral("A");
        stack.push(std::make_unique<LayerCommand>(LayerCommand::Add, Layer{}, l, "add"));
        QCOMPARE(d.layers().size(), size_t(2));
        stack.undo();
        QCOMPARE(d.layers().size(), size_t(1));
        stack.redo();
        QCOMPARE(d.layers().size(), size_t(2));
        Sheet s; s.name = QStringLiteral("Лист 2");
        stack.push(std::make_unique<SheetCommand>(SheetCommand::Add, Sheet{}, s, "sheet"));
        QCOMPARE(d.sheets().size(), size_t(2));
        stack.undo();
        QCOMPARE(d.sheets().size(), size_t(1));
        DocumentSettings st = d.settings(); st.unit = Unit::Meter;
        stack.push(std::make_unique<SettingsCommand>(d.settings(), st, "units"));
        QCOMPARE(d.settings().unit, Unit::Meter);
        stack.undo();
        QCOMPARE(d.settings().unit, Unit::Millimeter);
    }
    void drawingJson() {
        Document d;
        Layer l; l.name = QStringLiteral("Слой"); quint32 lid = d.addLayer(l);
        Entity e = makeText({1, 2}, QStringLiteral("Привет"), 5); e.layer = lid; e.attrs["note"] = AttrValue::string("n");
        d.addEntity(e);
        d.addEntity(makeCircle({0, 0}, 3));
        QJsonObject j = d.drawingToJson();
        Document d2;
        d2.drawingFromJson(j);
        QCOMPARE(d2.entityCount(), size_t(2));
        QCOMPARE(d2.layers().size(), size_t(2));
        for (quint64 id : d.entityIds()) QVERIFY(*d.entity(id) == *d2.entity(id));
    }
    void attributes() {
        AttributeMap m;
        m["s"] = AttrValue::string("x"); m["i"] = AttrValue::integer(-7); m["d"] = AttrValue::number(1.5); m["q"] = AttrValue::quantity(2, "кг");
        m["b"] = AttrValue::boolean(true); m["e"] = AttrValue::enumeration("a", {"a", "b"}); m["r"] = AttrValue::ref(99); m["l"] = AttrValue::list({"x", "y"});
        m["mat"] = AttrValue::material("Сталь 45"); m["date"] = AttrValue::date("2026-01-01");
        AttributeMap back = attributesFromJson(attributesToJson(m));
        QCOMPARE(back.size(), m.size());
        for (auto it = m.begin(); it != m.end(); ++it) { QCOMPARE(int(back[it.key()].kind), int(it.value().kind)); QCOMPARE(back[it.key()].display(), it.value().display()); }
        QCOMPARE(m["q"].display(), QStringLiteral("2 кг"));
        for (AttrKind k : {AttrKind::String, AttrKind::Int, AttrKind::Double, AttrKind::Quantity, AttrKind::Bool, AttrKind::Enum, AttrKind::Ref, AttrKind::List, AttrKind::Material, AttrKind::Date, AttrKind::Note})
            QCOMPARE(int(attrKindFromKey(attrKindKey(k))), int(k));
    }
};

QTEST_GUILESS_MAIN(TestDocument)
#include "test_document.moc"
